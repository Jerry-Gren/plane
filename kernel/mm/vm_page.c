#include <stddef.h>

#include <plane/mm.h>
#include <plane/overflow.h>
#include <plane/pmm.h>
#include <plane/printk.h>
#include <plane/spinlock.h>
#include <plane/vm_page.h>

#include "vm_object_internal.h"
#include "vm_page_internal.h"
#include "vm_zone_internal.h"

#define PLANE_VM_GUARD_PAGE_BOOTSTRAP_POOL_SIZE 64

static struct plane_page *page_pool;
static uint64_t tracked_page_count;
static const struct plane_vm_page_managed_range *managed_ranges;
static uint64_t managed_range_count;
static struct plane_page
	bootstrap_guard_page_pool[PLANE_VM_GUARD_PAGE_BOOTSTRAP_POOL_SIZE];
static struct plane_vm_zone_segment bootstrap_guard_page_segment;
static struct plane_vm_zone guard_page_zone;
static struct plane_spinlock vm_page_lock = PLANE_SPINLOCK_INIT;

static bool page_pool_index(const struct plane_page *page, uint64_t *index);
static enum plane_vm_page_queue_state queue_state_locked(
	const struct plane_page *page);

static plane_irq_state_t lock_page_metadata(void)
{
	return plane_spin_lock_irqsave(&vm_page_lock);
}

static void unlock_page_metadata(plane_irq_state_t state)
{
	plane_spin_unlock_irqrestore(&vm_page_lock, state);
}

static bool ensure_guard_page_zone(void)
{
	if (guard_page_zone.initialized) {
		return true;
	}

	return plane_vm_zone_init(&guard_page_zone,
				  sizeof(bootstrap_guard_page_pool[0]),
				  bootstrap_guard_page_pool,
				  PLANE_VM_GUARD_PAGE_BOOTSTRAP_POOL_SIZE,
				  &bootstrap_guard_page_segment);
}

static bool guard_page_known_to_zone(const struct plane_page *page)
{
	return plane_vm_zone_contains(&guard_page_zone, page);
}

static bool guard_page_is_active(const struct plane_page *page)
{
	return guard_page_known_to_zone(page) &&
	       page->state == PLANE_VM_PAGE_GUARD;
}

static bool page_metadata_is_known(const struct plane_page *page)
{
	return page_pool_index(page, NULL) ||
	       guard_page_is_active(page);
}

static void reset_page_queue_links(struct plane_page *page)
{
	page->queue_prev = NULL;
	page->queue_next = NULL;
	page->queue_state = PLANE_VM_PAGE_QUEUE_NONE;
}

static bool page_resident_state_is_valid(enum plane_vm_page_state state)
{
	return state == PLANE_VM_PAGE_ALLOCATED ||
	       state == PLANE_VM_PAGE_GUARD;
}

static bool grab_flags_are_valid(uint32_t flags)
{
	return (flags & ~PLANE_VM_PAGE_GRAB_ZERO) == 0;
}

static uint32_t grab_to_pmm_flags(uint32_t flags)
{
	uint32_t pmm_flags = 0;

	if ((flags & PLANE_VM_PAGE_GRAB_ZERO) != 0) {
		pmm_flags |= PLANE_PMM_ALLOC_ZERO;
	}

	return pmm_flags;
}

static void reset_page_resident_links_locked(struct plane_page *page)
{
	page->vm_object = NULL;
	page->vm_object_offset = 0;
	page->object_prev = NULL;
	page->object_next = NULL;
	page->object_hash_next = NULL;
	page->object_tabled = false;
	page->object_hashed = false;
}

void plane_vm_page_reset_resident_links(struct plane_page *page)
{
	plane_irq_state_t state;

	state = lock_page_metadata();
	reset_page_resident_links_locked(page);
	unlock_page_metadata(state);
}

static void reset_runtime_locked(void)
{
	page_pool = NULL;
	tracked_page_count = 0;
	managed_ranges = NULL;
	managed_range_count = 0;
	guard_page_zone = (struct plane_vm_zone){0};
	bootstrap_guard_page_segment = (struct plane_vm_zone_segment){0};
	BUG_ON_MSG(!ensure_guard_page_zone(),
		   "failed to reset guard page zone");
}

void plane_vm_page_reset_runtime(void)
{
	plane_irq_state_t state;

	plane_spin_init(&vm_page_lock);
	state = lock_page_metadata();
	reset_runtime_locked();
	unlock_page_metadata(state);
}

static bool install_pool_locked(
	struct plane_page *pool,
	uint64_t page_count,
	const struct plane_vm_page_managed_range *ranges,
	uint64_t range_count)
{
	if (pool == NULL || page_count == 0 ||
	    ranges == NULL || range_count == 0) {
		return false;
	}

	page_pool = pool;
	tracked_page_count = page_count;
	managed_ranges = ranges;
	managed_range_count = range_count;
	return true;
}

bool plane_vm_page_install_pool(
	struct plane_page *pool,
	uint64_t page_count,
	const struct plane_vm_page_managed_range *ranges,
	uint64_t range_count)
{
	plane_irq_state_t state;
	bool installed;

	state = lock_page_metadata();
	installed = install_pool_locked(pool, page_count, ranges, range_count);
	unlock_page_metadata(state);
	return installed;
}

static bool page_pool_index(const struct plane_page *page,
				  uint64_t *index)
{
	uintptr_t pool_base;
	uintptr_t page_addr;
	uintptr_t offset;

	if (page == NULL || page_pool == NULL) {
		return false;
	}

	pool_base = (uintptr_t)page_pool;
	page_addr = (uintptr_t)page;
	if (page_addr < pool_base) {
		return false;
	}

	offset = page_addr - pool_base;
	if ((offset % sizeof(page_pool[0])) != 0) {
		return false;
	}

	offset /= sizeof(page_pool[0]);
	if (offset >= tracked_page_count) {
		return false;
	}

	if (index != NULL) {
		*index = offset;
	}
	return true;
}

static void init_page_locked(struct plane_page *page,
			     plane_paddr_t phys_addr,
			     enum plane_vm_page_state state)
{
	page->phys_addr = plane_paddr_raw(phys_addr);
	page->wire_count = 0;
	reset_page_resident_links_locked(page);
	page->state = state;
	reset_page_queue_links(page);
}

void plane_vm_page_init(struct plane_page *page,
			plane_paddr_t phys_addr,
			enum plane_vm_page_state state)
{
	plane_irq_state_t lock_state;

	lock_state = lock_page_metadata();
	init_page_locked(page, phys_addr, state);
	unlock_page_metadata(lock_state);
}

static bool set_state_locked(struct plane_page *page,
			     enum plane_vm_page_state state)
{
	if (!page_pool_index(page, NULL)) {
		return false;
	}

	page->state = state;
	return true;
}

bool plane_vm_page_set_state(struct plane_page *page,
			     enum plane_vm_page_state state)
{
	plane_irq_state_t lock_state;
	bool set;

	lock_state = lock_page_metadata();
	set = set_state_locked(page, state);
	unlock_page_metadata(lock_state);
	return set;
}

static bool page_is_allocated_unwired_no_object_locked(const struct plane_page *page)
{
	return page_pool_index(page, NULL) &&
	       page->state == PLANE_VM_PAGE_ALLOCATED &&
	       page->wire_count == 0 &&
	       page->vm_object == NULL;
}

bool plane_vm_page_is_releasable_to_pmm(const struct plane_page *page)
{
	plane_irq_state_t state;
	bool allocated;

	state = lock_page_metadata();
	allocated = page_is_allocated_unwired_no_object_locked(page);
	unlock_page_metadata(state);
	return allocated;
}

static bool page_is_releasable_locked(const struct plane_page *page)
{
	return page_pool_index(page, NULL) &&
	       page->state == PLANE_VM_PAGE_ALLOCATED &&
	       page->wire_count == 0 &&
	       page->vm_object == NULL &&
	       page->object_prev == NULL &&
	       page->object_next == NULL &&
	       page->object_hash_next == NULL &&
	       !page->object_tabled &&
	       !page->object_hashed &&
	       queue_state_locked(page) == PLANE_VM_PAGE_QUEUE_NONE;
}

bool plane_vm_page_queue_init(struct plane_vm_page_queue *queue,
			      enum plane_vm_page_queue_state state)
{
	if (queue == NULL || state == PLANE_VM_PAGE_QUEUE_NONE) {
		return false;
	}

	*queue = (struct plane_vm_page_queue){
		.state = state,
	};
	return true;
}

static bool queue_contains_locked(const struct plane_vm_page_queue *queue,
				  const struct plane_page *page)
{
	const struct plane_page *current;

	if (queue == NULL || page == NULL || queue->count == 0) {
		return false;
	}

	current = queue->head;
	for (uint64_t i = 0; i < queue->count; i++) {
		if (current == NULL) {
			return false;
		}
		if (current == page) {
			return true;
		}
		current = current->queue_next;
	}

	return false;
}

static bool queue_insert_ordered_locked(struct plane_vm_page_queue *queue,
					struct plane_page *page)
{
	struct plane_page *next;

	if (queue == NULL || queue->state == PLANE_VM_PAGE_QUEUE_NONE ||
	    !page_metadata_is_known(page) ||
	    page->queue_state != PLANE_VM_PAGE_QUEUE_NONE ||
	    page->queue_prev != NULL ||
	    page->queue_next != NULL) {
		return false;
	}

	if (queue->tail == NULL || queue->tail->phys_addr < page->phys_addr) {
		page->queue_prev = queue->tail;
		page->queue_next = NULL;
		if (queue->tail != NULL) {
			queue->tail->queue_next = page;
		} else {
			queue->head = page;
		}
		queue->tail = page;
		page->queue_state = queue->state;
		queue->count++;
		return true;
	}

	next = queue->head;
	while (next != NULL && next->phys_addr < page->phys_addr) {
		next = next->queue_next;
	}

	page->queue_next = next;
	if (next != NULL) {
		page->queue_prev = next->queue_prev;
		next->queue_prev = page;
	} else {
		page->queue_prev = queue->tail;
		queue->tail = page;
	}

	if (page->queue_prev != NULL) {
		page->queue_prev->queue_next = page;
	} else {
		queue->head = page;
	}

	page->queue_state = queue->state;
	queue->count++;
	return true;
}

bool plane_vm_page_queue_insert_ordered(struct plane_vm_page_queue *queue,
					struct plane_page *page)
{
	plane_irq_state_t state;
	bool inserted;

	state = lock_page_metadata();
	inserted = queue_insert_ordered_locked(queue, page);
	unlock_page_metadata(state);
	return inserted;
}

static bool queue_remove_locked(struct plane_vm_page_queue *queue,
				struct plane_page *page)
{
	if (queue == NULL || queue->state == PLANE_VM_PAGE_QUEUE_NONE ||
	    !page_metadata_is_known(page) ||
	    page->queue_state != queue->state ||
	    !queue_contains_locked(queue, page)) {
		return false;
	}

	if (page->queue_prev != NULL) {
		page->queue_prev->queue_next = page->queue_next;
	} else {
		queue->head = page->queue_next;
	}

	if (page->queue_next != NULL) {
		page->queue_next->queue_prev = page->queue_prev;
	} else {
		queue->tail = page->queue_prev;
	}

	reset_page_queue_links(page);
	queue->count--;
	return true;
}

bool plane_vm_page_queue_remove(struct plane_vm_page_queue *queue,
				struct plane_page *page)
{
	plane_irq_state_t state;
	bool removed;

	state = lock_page_metadata();
	removed = queue_remove_locked(queue, page);
	unlock_page_metadata(state);
	return removed;
}

static struct plane_page *queue_pop_head_locked(
	struct plane_vm_page_queue *queue)
{
	struct plane_page *page;

	if (queue == NULL) {
		return NULL;
	}

	page = queue->head;
	if (page == NULL || !queue_remove_locked(queue, page)) {
		return NULL;
	}

	return page;
}

struct plane_page *plane_vm_page_queue_pop_head(
	struct plane_vm_page_queue *queue)
{
	plane_irq_state_t state;
	struct plane_page *page;

	state = lock_page_metadata();
	page = queue_pop_head_locked(queue);
	unlock_page_metadata(state);
	return page;
}

static uint64_t queue_count_locked(const struct plane_vm_page_queue *queue)
{
	return queue != NULL ? queue->count : 0;
}

uint64_t plane_vm_page_queue_count(const struct plane_vm_page_queue *queue)
{
	plane_irq_state_t state;
	uint64_t count;

	state = lock_page_metadata();
	count = queue_count_locked(queue);
	unlock_page_metadata(state);
	return count;
}

static enum plane_vm_page_queue_state queue_state_locked(
	const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_known_to_zone(page)) {
		return page->queue_state;
	}
	if (!page_pool_index(page, &index)) {
		return PLANE_VM_PAGE_QUEUE_NONE;
	}

	return page_pool[index].queue_state;
}

enum plane_vm_page_queue_state plane_vm_page_queue_state(
	const struct plane_page *page)
{
	plane_irq_state_t state;
	enum plane_vm_page_queue_state queue_state;

	state = lock_page_metadata();
	queue_state = queue_state_locked(page);
	unlock_page_metadata(state);
	return queue_state;
}

static struct plane_page *create_guard_page_locked(void)
{
	struct plane_page *page;

	if (!ensure_guard_page_zone()) {
		return NULL;
	}

	page = plane_vm_zone_alloc(&guard_page_zone);
	if (page == NULL) {
		return NULL;
	}

	page->phys_addr = PLANE_VM_PAGE_GUARD_PHYS_RAW;
	page->wire_count = 0;
	reset_page_resident_links_locked(page);
	page->state = PLANE_VM_PAGE_GUARD;
	reset_page_queue_links(page);
	return page;
}

struct plane_page *plane_vm_page_create_guard(void)
{
	plane_irq_state_t state;
	struct plane_page *page;

	state = lock_page_metadata();
	page = create_guard_page_locked();
	unlock_page_metadata(state);
	return page;
}

static bool release_guard_page_locked(struct plane_page *page)
{
	if (!guard_page_is_active(page) ||
	    page->wire_count != 0 ||
	    page->vm_object != NULL ||
	    page->object_prev != NULL ||
	    page->object_next != NULL ||
	    page->object_hash_next != NULL ||
	    page->object_tabled ||
	    page->object_hashed) {
		return false;
	}

	reset_page_resident_links_locked(page);
	page->phys_addr = PLANE_VM_PAGE_NO_PHYS_RAW;
	reset_page_queue_links(page);
	page->state = PLANE_VM_PAGE_INVALID;
	return plane_vm_zone_free(&guard_page_zone, page);
}

bool plane_vm_page_release_guard(struct plane_page *page)
{
	plane_irq_state_t state;
	bool released;

	state = lock_page_metadata();
	released = release_guard_page_locked(page);
	unlock_page_metadata(state);
	return released;
}

bool plane_vm_page_guard_storage_size(uint64_t count, uint64_t *size)
{
	if (size == NULL || count == 0) {
		return false;
	}

	return plane_checked_mul_u64(count, sizeof(struct plane_page), size);
}

bool plane_vm_page_add_guard_storage(void *storage,
				     uint64_t count,
				     struct plane_vm_zone_segment *segment)
{
	plane_irq_state_t state;
	bool added;

	state = lock_page_metadata();
	added = ensure_guard_page_zone() &&
		plane_vm_zone_add_storage(&guard_page_zone, storage, count,
					  segment);
	unlock_page_metadata(state);
	return added;
}

static struct plane_page *page_from_phys_locked(plane_paddr_t phys_addr)
{
	uint64_t raw_phys = plane_paddr_raw(phys_addr);

	if (page_pool == NULL || !plane_paddr_is_page_aligned(phys_addr)) {
		return NULL;
	}

	for (uint64_t i = 0; i < managed_range_count; i++) {
		uint64_t managed_end;

		if (!plane_checked_page_range_end(managed_ranges[i].base,
					    managed_ranges[i].page_count,
					    &managed_end)) {
			return NULL;
		}

		if (raw_phys >= managed_ranges[i].base &&
		    raw_phys < managed_end) {
			uint64_t page_offset =
				(raw_phys - managed_ranges[i].base) / PAGE_SIZE;

			return &page_pool[managed_ranges[i].page_index +
					  page_offset];
		}
	}

	return NULL;
}

struct plane_page *plane_vm_page_from_phys(plane_paddr_t phys_addr)
{
	plane_irq_state_t state;
	struct plane_page *page;

	state = lock_page_metadata();
	page = page_from_phys_locked(phys_addr);
	unlock_page_metadata(state);
	return page;
}

static plane_paddr_t page_phys_locked(const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_is_active(page)) {
		return PLANE_VM_PAGE_GUARD_PHYS;
	}
	if (guard_page_known_to_zone(page)) {
		return PLANE_VM_PAGE_NO_PHYS;
	}
	if (!page_pool_index(page, &index)) {
		return PLANE_VM_PAGE_NO_PHYS;
	}

	return plane_paddr_make(page_pool[index].phys_addr);
}

plane_paddr_t plane_vm_page_phys(const struct plane_page *page)
{
	plane_irq_state_t state;
	plane_paddr_t phys_addr;

	state = lock_page_metadata();
	phys_addr = page_phys_locked(page);
	unlock_page_metadata(state);
	return phys_addr;
}

static enum plane_vm_page_state page_state_locked(const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_known_to_zone(page)) {
		return page->state;
	}
	if (!page_pool_index(page, &index)) {
		return PLANE_VM_PAGE_INVALID;
	}

	return page_pool[index].state;
}

enum plane_vm_page_state plane_vm_page_state(const struct plane_page *page)
{
	plane_irq_state_t lock_state;
	enum plane_vm_page_state page_state;

	lock_state = lock_page_metadata();
	page_state = page_state_locked(page);
	unlock_page_metadata(lock_state);
	return page_state;
}

static bool page_wire_count_locked(const struct plane_page *page,
			      uint64_t *wire_count)
{
	uint64_t index;

	if (wire_count == NULL) {
		return false;
	}
	if (guard_page_is_active(page)) {
		*wire_count = page->wire_count;
		return true;
	}
	if (!page_pool_index(page, &index)) {
		return false;
	}

	*wire_count = page_pool[index].wire_count;
	return true;
}

bool plane_vm_page_wire_count(const struct plane_page *page,
			      uint64_t *wire_count)
{
	plane_irq_state_t state;
	bool queried;

	state = lock_page_metadata();
	queried = page_wire_count_locked(page, wire_count);
	unlock_page_metadata(state);
	return queried;
}

bool plane_vm_page_is_guard(const struct plane_page *page)
{
	plane_irq_state_t state;
	bool is_guard;

	state = lock_page_metadata();
	is_guard = page_state_locked(page) == PLANE_VM_PAGE_GUARD;
	unlock_page_metadata(state);
	return is_guard;
}

static struct plane_vm_object *page_object_locked(const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_is_active(page)) {
		return page->vm_object;
	}
	if (!page_pool_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].vm_object;
}

struct plane_vm_object *plane_vm_page_object(const struct plane_page *page)
{
	plane_irq_state_t state;
	struct plane_vm_object *object;

	state = lock_page_metadata();
	object = page_object_locked(page);
	unlock_page_metadata(state);
	return object;
}

static bool page_object_offset_locked(const struct plane_page *page,
				 uint64_t *offset)
{
	uint64_t index;

	if (offset == NULL) {
		return false;
	}
	if (guard_page_is_active(page)) {
		if (page->vm_object == NULL) {
			return false;
		}
		*offset = page->vm_object_offset;
		return true;
	}
	if (!page_pool_index(page, &index) ||
	    page_pool[index].vm_object == NULL) {
		return false;
	}

	*offset = page_pool[index].vm_object_offset;
	return true;
}

bool plane_vm_page_object_offset(const struct plane_page *page,
				 uint64_t *offset)
{
	plane_irq_state_t state;
	bool queried;

	state = lock_page_metadata();
	queried = page_object_offset_locked(page, offset);
	unlock_page_metadata(state);
	return queried;
}

static bool attach_page_object_locked(struct plane_page *page,
				      struct plane_vm_object *object,
				      uint64_t offset)
{
	uint64_t index;

	if (object == NULL) {
		return false;
	}
	if (guard_page_is_active(page)) {
		if (page->vm_object != NULL) {
			return false;
		}

		page->vm_object = object;
		page->vm_object_offset = offset;
		page->object_prev = NULL;
		page->object_next = NULL;
		page->object_hash_next = NULL;
		page->object_tabled = false;
		page->object_hashed = false;
		return true;
	}
	if (!page_pool_index(page, &index) ||
	    !page_resident_state_is_valid(page_pool[index].state) ||
	    page_pool[index].vm_object != NULL) {
		return false;
	}

	page_pool[index].vm_object = object;
	page_pool[index].vm_object_offset = offset;
	page_pool[index].object_prev = NULL;
	page_pool[index].object_next = NULL;
	page_pool[index].object_hash_next = NULL;
	page_pool[index].object_tabled = false;
	page_pool[index].object_hashed = false;
	return true;
}

bool plane_vm_page_attach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	plane_irq_state_t state;
	bool attached;

	state = lock_page_metadata();
	attached = attach_page_object_locked(page, object, offset);
	unlock_page_metadata(state);
	return attached;
}

static bool detach_page_object_locked(struct plane_page *page,
				      struct plane_vm_object *object,
				      uint64_t offset)
{
	uint64_t index;

	if (object == NULL) {
		return false;
	}
	if (guard_page_is_active(page)) {
		if (page->vm_object != object ||
		    page->vm_object_offset != offset) {
			return false;
		}

		reset_page_resident_links_locked(page);
		return true;
	}
	if (!page_pool_index(page, &index) ||
	    page_pool[index].state != PLANE_VM_PAGE_ALLOCATED ||
	    page_pool[index].vm_object != object ||
	    page_pool[index].vm_object_offset != offset) {
		return false;
	}

	reset_page_resident_links_locked(&page_pool[index]);
	return true;
}

bool plane_vm_page_detach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	plane_irq_state_t state;
	bool detached;

	state = lock_page_metadata();
	detached = detach_page_object_locked(page, object, offset);
	unlock_page_metadata(state);
	return detached;
}

static struct plane_page *page_object_prev_locked(const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_is_active(page)) {
		return page->object_prev;
	}
	if (!page_pool_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].object_prev;
}

struct plane_page *plane_vm_page_object_prev(const struct plane_page *page)
{
	plane_irq_state_t state;
	struct plane_page *prev;

	state = lock_page_metadata();
	prev = page_object_prev_locked(page);
	unlock_page_metadata(state);
	return prev;
}

static struct plane_page *page_object_next_locked(const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_is_active(page)) {
		return page->object_next;
	}
	if (!page_pool_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].object_next;
}

struct plane_page *plane_vm_page_object_next(const struct plane_page *page)
{
	plane_irq_state_t state;
	struct plane_page *next;

	state = lock_page_metadata();
	next = page_object_next_locked(page);
	unlock_page_metadata(state);
	return next;
}

static struct plane_page *page_object_hash_next_locked(
	const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_is_active(page)) {
		return page->object_hash_next;
	}
	if (!page_pool_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].object_hash_next;
}

struct plane_page *plane_vm_page_object_hash_next(const struct plane_page *page)
{
	plane_irq_state_t state;
	struct plane_page *next;

	state = lock_page_metadata();
	next = page_object_hash_next_locked(page);
	unlock_page_metadata(state);
	return next;
}

static bool page_object_tabled_locked(const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_is_active(page)) {
		return page->object_tabled;
	}
	if (!page_pool_index(page, &index)) {
		return false;
	}

	return page_pool[index].object_tabled;
}

bool plane_vm_page_object_is_tabled(const struct plane_page *page)
{
	plane_irq_state_t state;
	bool tabled;

	state = lock_page_metadata();
	tabled = page_object_tabled_locked(page);
	unlock_page_metadata(state);
	return tabled;
}

static bool page_object_hashed_locked(const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_is_active(page)) {
		return page->object_hashed;
	}
	if (!page_pool_index(page, &index)) {
		return false;
	}

	return page_pool[index].object_hashed;
}

bool plane_vm_page_object_is_hashed(const struct plane_page *page)
{
	plane_irq_state_t state;
	bool hashed;

	state = lock_page_metadata();
	hashed = page_object_hashed_locked(page);
	unlock_page_metadata(state);
	return hashed;
}

static bool set_page_object_prev_locked(struct plane_page *page,
					struct plane_page *prev)
{
	uint64_t index;

	if (prev != NULL && !page_metadata_is_known(prev)) {
		return false;
	}
	if (guard_page_is_active(page)) {
		page->object_prev = prev;
		return true;
	}
	if (!page_pool_index(page, &index)) {
		return false;
	}

	page_pool[index].object_prev = prev;
	return true;
}

bool plane_vm_page_set_object_prev(struct plane_page *page,
				   struct plane_page *prev)
{
	plane_irq_state_t state;
	bool set;

	state = lock_page_metadata();
	set = set_page_object_prev_locked(page, prev);
	unlock_page_metadata(state);
	return set;
}

static bool set_page_object_next_locked(struct plane_page *page,
					struct plane_page *next)
{
	uint64_t index;

	if (next != NULL && !page_metadata_is_known(next)) {
		return false;
	}
	if (guard_page_is_active(page)) {
		page->object_next = next;
		return true;
	}
	if (!page_pool_index(page, &index)) {
		return false;
	}

	page_pool[index].object_next = next;
	return true;
}

bool plane_vm_page_set_object_next(struct plane_page *page,
				   struct plane_page *next)
{
	plane_irq_state_t state;
	bool set;

	state = lock_page_metadata();
	set = set_page_object_next_locked(page, next);
	unlock_page_metadata(state);
	return set;
}

static bool set_page_object_hash_next_locked(struct plane_page *page,
					     struct plane_page *next)
{
	uint64_t index;

	if (next != NULL && !page_metadata_is_known(next)) {
		return false;
	}
	if (guard_page_is_active(page)) {
		page->object_hash_next = next;
		return true;
	}
	if (!page_pool_index(page, &index)) {
		return false;
	}

	page_pool[index].object_hash_next = next;
	return true;
}

bool plane_vm_page_set_object_hash_next(struct plane_page *page,
					struct plane_page *next)
{
	plane_irq_state_t state;
	bool set;

	state = lock_page_metadata();
	set = set_page_object_hash_next_locked(page, next);
	unlock_page_metadata(state);
	return set;
}

static bool set_page_object_tabled_locked(struct plane_page *page, bool tabled)
{
	uint64_t index;

	if (guard_page_is_active(page)) {
		page->object_tabled = tabled;
		return true;
	}
	if (!page_pool_index(page, &index)) {
		return false;
	}

	page_pool[index].object_tabled = tabled;
	return true;
}

bool plane_vm_page_set_object_tabled(struct plane_page *page, bool tabled)
{
	plane_irq_state_t state;
	bool set;

	state = lock_page_metadata();
	set = set_page_object_tabled_locked(page, tabled);
	unlock_page_metadata(state);
	return set;
}

static bool set_page_object_hashed_locked(struct plane_page *page, bool hashed)
{
	uint64_t index;

	if (guard_page_is_active(page)) {
		page->object_hashed = hashed;
		return true;
	}
	if (!page_pool_index(page, &index)) {
		return false;
	}

	page_pool[index].object_hashed = hashed;
	return true;
}

bool plane_vm_page_set_object_hashed(struct plane_page *page, bool hashed)
{
	plane_irq_state_t state;
	bool set;

	state = lock_page_metadata();
	set = set_page_object_hashed_locked(page, hashed);
	unlock_page_metadata(state);
	return set;
}

bool plane_vm_page_grab(uint32_t flags, struct plane_page **page)
{
	plane_paddr_t phys_addr;
	struct plane_page *grabbed_page;

	if (page == NULL ||
	    !grab_flags_are_valid(flags) ||
	    !plane_pmm_alloc_pages_phys_flags(
			1, 1, grab_to_pmm_flags(flags), &phys_addr)) {
		return false;
	}

	grabbed_page = plane_vm_page_from_phys(phys_addr);
	BUG_ON_MSG(grabbed_page == NULL,
		   "PMM allocated page without VM metadata: phys=%llx",
		   (unsigned long long)plane_paddr_raw(phys_addr));

	*page = grabbed_page;
	return true;
}

bool plane_vm_page_release(struct plane_page *page)
{
	plane_irq_state_t state;
	plane_paddr_t phys_addr;

	state = lock_page_metadata();
	if (!page_is_releasable_locked(page)) {
		unlock_page_metadata(state);
		return false;
	}

	phys_addr = plane_paddr_make(page->phys_addr);
	unlock_page_metadata(state);

	BUG_ON_MSG(!plane_pmm_free_page_phys(phys_addr),
		   "failed to release VM page: phys=%llx",
		   (unsigned long long)plane_paddr_raw(phys_addr));
	return true;
}

struct wire_snapshot {
	struct plane_vm_object *object;
	uint64_t wire_count;
	bool account_object;
};

static bool take_page_wire_snapshot_locked(struct plane_page *page,
					   bool wire,
					   struct wire_snapshot *snapshot)
{
	uint64_t index;

	if (snapshot == NULL ||
	    !page_pool_index(page, &index) ||
	    page_pool[index].state != PLANE_VM_PAGE_ALLOCATED) {
		return false;
	}

	*snapshot = (struct wire_snapshot){
		.object = page_pool[index].vm_object,
		.wire_count = page_pool[index].wire_count,
	};
	if (wire) {
		if (snapshot->wire_count == UINT64_MAX) {
			return false;
		}
		snapshot->account_object = snapshot->object != NULL &&
					   snapshot->wire_count == 0;
		return true;
	}

	if (snapshot->wire_count == 0) {
		return false;
	}
	snapshot->account_object = snapshot->object != NULL &&
				   snapshot->wire_count == 1;
	return true;
}

static bool commit_page_wire_snapshot_locked(struct plane_page *page,
					     const struct wire_snapshot *snapshot,
					     bool wire)
{
	uint64_t index;

	if (snapshot == NULL ||
	    !page_pool_index(page, &index) ||
	    page_pool[index].state != PLANE_VM_PAGE_ALLOCATED ||
	    page_pool[index].vm_object != snapshot->object ||
	    page_pool[index].wire_count != snapshot->wire_count) {
		return false;
	}

	if (wire) {
		if (page_pool[index].wire_count == UINT64_MAX) {
			return false;
		}
		page_pool[index].wire_count++;
	} else {
		if (page_pool[index].wire_count == 0) {
			return false;
		}
		page_pool[index].wire_count--;
	}
	return true;
}

bool plane_vm_page_wire(struct plane_page *page)
{
	plane_irq_state_t state;
	struct wire_snapshot snapshot;
	bool committed;

	state = lock_page_metadata();
	if (!take_page_wire_snapshot_locked(page, true, &snapshot)) {
		unlock_page_metadata(state);
		return false;
	}
	unlock_page_metadata(state);

	if (snapshot.account_object &&
	    !plane_vm_object_account_page_wired(snapshot.object)) {
		return false;
	}

	state = lock_page_metadata();
	committed = commit_page_wire_snapshot_locked(page, &snapshot, true);
	unlock_page_metadata(state);
	if (!committed && snapshot.account_object) {
		BUG_ON_MSG(!plane_vm_object_account_page_unwired(
				   snapshot.object),
			   "failed to rollback VM page wire accounting");
	}
	return committed;
}

bool plane_vm_page_unwire(struct plane_page *page)
{
	plane_irq_state_t state;
	struct wire_snapshot snapshot;
	bool committed;

	state = lock_page_metadata();
	if (!take_page_wire_snapshot_locked(page, false, &snapshot)) {
		unlock_page_metadata(state);
		return false;
	}
	unlock_page_metadata(state);

	if (snapshot.account_object &&
	    !plane_vm_object_account_page_unwired(snapshot.object)) {
		return false;
	}

	state = lock_page_metadata();
	committed = commit_page_wire_snapshot_locked(page, &snapshot, false);
	unlock_page_metadata(state);
	if (!committed && snapshot.account_object) {
		BUG_ON_MSG(!plane_vm_object_account_page_wired(
				   snapshot.object),
			   "failed to rollback VM page unwire accounting");
	}
	return committed;
}
