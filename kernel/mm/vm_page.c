#include <stddef.h>

#include <plane/mm.h>
#include <plane/overflow.h>
#include <plane/pmm.h>
#include <plane/printk.h>
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

static bool vm_page_pointer_index(const struct plane_page *page,
				  uint64_t *index);

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

static bool guard_page_known(const struct plane_page *page)
{
	return plane_vm_zone_contains(&guard_page_zone, page);
}

static bool active_guard_page(const struct plane_page *page)
{
	return guard_page_known(page) &&
	       page->state == PLANE_VM_PAGE_GUARD;
}

static bool vm_page_known(const struct plane_page *page)
{
	return vm_page_pointer_index(page, NULL) ||
	       active_guard_page(page);
}

static void reset_queue_links(struct plane_page *page)
{
	page->queue_prev = NULL;
	page->queue_next = NULL;
	page->queue_state = PLANE_VM_PAGE_QUEUE_NONE;
}

static bool resident_state_valid(enum plane_vm_page_state state)
{
	return state == PLANE_VM_PAGE_ALLOCATED ||
	       state == PLANE_VM_PAGE_GUARD;
}

static bool grab_flags_valid(uint32_t flags)
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

void plane_vm_page_reset_resident_links(struct plane_page *page)
{
	page->vm_object = NULL;
	page->vm_object_offset = 0;
	page->object_prev = NULL;
	page->object_next = NULL;
	page->object_hash_next = NULL;
	page->object_tabled = false;
	page->object_hashed = false;
}

void plane_vm_page_reset_runtime(void)
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

bool plane_vm_page_install_pool(
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

static bool vm_page_pointer_index(const struct plane_page *page,
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

void plane_vm_page_init(struct plane_page *page,
			plane_paddr_t phys_addr,
			enum plane_vm_page_state state)
{
	page->phys_addr = plane_paddr_raw(phys_addr);
	page->wire_count = 0;
	plane_vm_page_reset_resident_links(page);
	page->state = state;
	reset_queue_links(page);
}

bool plane_vm_page_set_state(struct plane_page *page,
			     enum plane_vm_page_state state)
{
	if (!vm_page_pointer_index(page, NULL)) {
		return false;
	}

	page->state = state;
	return true;
}

bool plane_vm_page_allocated_unwired_no_object(const struct plane_page *page)
{
	return vm_page_pointer_index(page, NULL) &&
	       page->state == PLANE_VM_PAGE_ALLOCATED &&
	       page->wire_count == 0 &&
	       page->vm_object == NULL;
}

static bool allocated_releasable(const struct plane_page *page)
{
	return vm_page_pointer_index(page, NULL) &&
	       page->state == PLANE_VM_PAGE_ALLOCATED &&
	       page->wire_count == 0 &&
	       page->vm_object == NULL &&
	       page->object_prev == NULL &&
	       page->object_next == NULL &&
	       page->object_hash_next == NULL &&
	       !page->object_tabled &&
	       !page->object_hashed &&
	       plane_vm_page_queue_state(page) == PLANE_VM_PAGE_QUEUE_NONE;
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

static bool queue_contains(const struct plane_vm_page_queue *queue,
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

bool plane_vm_page_queue_insert_ordered(struct plane_vm_page_queue *queue,
					struct plane_page *page)
{
	struct plane_page *next;

	if (queue == NULL || queue->state == PLANE_VM_PAGE_QUEUE_NONE ||
	    !vm_page_known(page) ||
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

bool plane_vm_page_queue_remove(struct plane_vm_page_queue *queue,
				struct plane_page *page)
{
	if (queue == NULL || queue->state == PLANE_VM_PAGE_QUEUE_NONE ||
	    !vm_page_known(page) ||
	    page->queue_state != queue->state ||
	    !queue_contains(queue, page)) {
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

	reset_queue_links(page);
	queue->count--;
	return true;
}

struct plane_page *plane_vm_page_queue_pop_head(
	struct plane_vm_page_queue *queue)
{
	struct plane_page *page;

	if (queue == NULL) {
		return NULL;
	}

	page = queue->head;
	if (page == NULL || !plane_vm_page_queue_remove(queue, page)) {
		return NULL;
	}

	return page;
}

uint64_t plane_vm_page_queue_count(const struct plane_vm_page_queue *queue)
{
	return queue != NULL ? queue->count : 0;
}

enum plane_vm_page_queue_state plane_vm_page_queue_state(
	const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_known(page)) {
		return page->queue_state;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return PLANE_VM_PAGE_QUEUE_NONE;
	}

	return page_pool[index].queue_state;
}

struct plane_page *plane_vm_page_create_guard(void)
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
	plane_vm_page_reset_resident_links(page);
	page->state = PLANE_VM_PAGE_GUARD;
	reset_queue_links(page);
	return page;
}

bool plane_vm_page_release_guard(struct plane_page *page)
{
	if (!active_guard_page(page) ||
	    page->wire_count != 0 ||
	    page->vm_object != NULL ||
	    page->object_prev != NULL ||
	    page->object_next != NULL ||
	    page->object_hash_next != NULL ||
	    page->object_tabled ||
	    page->object_hashed) {
		return false;
	}

	plane_vm_page_reset_resident_links(page);
	page->phys_addr = PLANE_VM_PAGE_NO_PHYS_RAW;
	reset_queue_links(page);
	page->state = PLANE_VM_PAGE_INVALID;
	return plane_vm_zone_free(&guard_page_zone, page);
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
	return ensure_guard_page_zone() &&
	       plane_vm_zone_add_storage(&guard_page_zone, storage, count,
					 segment);
}

struct plane_page *plane_vm_page_from_phys(plane_paddr_t phys_addr)
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

plane_paddr_t plane_vm_page_phys(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page(page)) {
		return PLANE_VM_PAGE_GUARD_PHYS;
	}
	if (guard_page_known(page)) {
		return PLANE_VM_PAGE_NO_PHYS;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return PLANE_VM_PAGE_NO_PHYS;
	}

	return plane_paddr_make(page_pool[index].phys_addr);
}

enum plane_vm_page_state plane_vm_page_state(const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_known(page)) {
		return page->state;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return PLANE_VM_PAGE_INVALID;
	}

	return page_pool[index].state;
}

bool plane_vm_page_wire_count(const struct plane_page *page,
			      uint64_t *wire_count)
{
	uint64_t index;

	if (wire_count == NULL) {
		return false;
	}
	if (active_guard_page(page)) {
		*wire_count = page->wire_count;
		return true;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return false;
	}

	*wire_count = page_pool[index].wire_count;
	return true;
}

bool plane_vm_page_is_guard(const struct plane_page *page)
{
	return plane_vm_page_state(page) == PLANE_VM_PAGE_GUARD;
}

struct plane_vm_object *plane_vm_page_object(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page(page)) {
		return page->vm_object;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].vm_object;
}

bool plane_vm_page_object_offset(const struct plane_page *page,
				 uint64_t *offset)
{
	uint64_t index;

	if (offset == NULL) {
		return false;
	}
	if (active_guard_page(page)) {
		if (page->vm_object == NULL) {
			return false;
		}
		*offset = page->vm_object_offset;
		return true;
	}
	if (!vm_page_pointer_index(page, &index) ||
	    page_pool[index].vm_object == NULL) {
		return false;
	}

	*offset = page_pool[index].vm_object_offset;
	return true;
}

bool plane_vm_page_attach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	uint64_t index;

	if (object == NULL) {
		return false;
	}
	if (active_guard_page(page)) {
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
	if (!vm_page_pointer_index(page, &index) ||
	    !resident_state_valid(page_pool[index].state) ||
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

bool plane_vm_page_detach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	uint64_t index;

	if (object == NULL) {
		return false;
	}
	if (active_guard_page(page)) {
		if (page->vm_object != object ||
		    page->vm_object_offset != offset) {
			return false;
		}

		plane_vm_page_reset_resident_links(page);
		return true;
	}
	if (!vm_page_pointer_index(page, &index) ||
	    page_pool[index].state != PLANE_VM_PAGE_ALLOCATED ||
	    page_pool[index].vm_object != object ||
	    page_pool[index].vm_object_offset != offset) {
		return false;
	}

	plane_vm_page_reset_resident_links(&page_pool[index]);
	return true;
}

struct plane_page *plane_vm_page_object_prev(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page(page)) {
		return page->object_prev;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].object_prev;
}

struct plane_page *plane_vm_page_object_next(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page(page)) {
		return page->object_next;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].object_next;
}

struct plane_page *plane_vm_page_object_hash_next(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page(page)) {
		return page->object_hash_next;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].object_hash_next;
}

bool plane_vm_page_object_tabled(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page(page)) {
		return page->object_tabled;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return false;
	}

	return page_pool[index].object_tabled;
}

bool plane_vm_page_object_hashed(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page(page)) {
		return page->object_hashed;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return false;
	}

	return page_pool[index].object_hashed;
}

bool plane_vm_page_set_object_prev(struct plane_page *page,
				   struct plane_page *prev)
{
	uint64_t index;

	if (prev != NULL && !vm_page_known(prev)) {
		return false;
	}
	if (active_guard_page(page)) {
		page->object_prev = prev;
		return true;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return false;
	}

	page_pool[index].object_prev = prev;
	return true;
}

bool plane_vm_page_set_object_next(struct plane_page *page,
				   struct plane_page *next)
{
	uint64_t index;

	if (next != NULL && !vm_page_known(next)) {
		return false;
	}
	if (active_guard_page(page)) {
		page->object_next = next;
		return true;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return false;
	}

	page_pool[index].object_next = next;
	return true;
}

bool plane_vm_page_set_object_hash_next(struct plane_page *page,
					struct plane_page *next)
{
	uint64_t index;

	if (next != NULL && !vm_page_known(next)) {
		return false;
	}
	if (active_guard_page(page)) {
		page->object_hash_next = next;
		return true;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return false;
	}

	page_pool[index].object_hash_next = next;
	return true;
}

bool plane_vm_page_set_object_tabled(struct plane_page *page, bool tabled)
{
	uint64_t index;

	if (active_guard_page(page)) {
		page->object_tabled = tabled;
		return true;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return false;
	}

	page_pool[index].object_tabled = tabled;
	return true;
}

bool plane_vm_page_set_object_hashed(struct plane_page *page, bool hashed)
{
	uint64_t index;

	if (active_guard_page(page)) {
		page->object_hashed = hashed;
		return true;
	}
	if (!vm_page_pointer_index(page, &index)) {
		return false;
	}

	page_pool[index].object_hashed = hashed;
	return true;
}

bool plane_vm_page_grab(uint32_t flags, struct plane_page **page)
{
	plane_paddr_t phys_addr;
	struct plane_page *grabbed_page;

	if (page == NULL ||
	    !grab_flags_valid(flags) ||
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
	plane_paddr_t phys_addr;

	if (!allocated_releasable(page)) {
		return false;
	}

	phys_addr = plane_paddr_make(page->phys_addr);
	BUG_ON_MSG(!plane_pmm_free_page_phys(phys_addr),
		   "failed to release VM page: phys=%llx",
		   (unsigned long long)plane_paddr_raw(phys_addr));
	return true;
}

bool plane_vm_page_wire(struct plane_page *page)
{
	if (!vm_page_pointer_index(page, NULL) ||
	    page->state != PLANE_VM_PAGE_ALLOCATED ||
	    page->wire_count == UINT64_MAX) {
		return false;
	}

	if (page->vm_object != NULL &&
	    page->wire_count == 0 &&
	    !plane_vm_object_page_became_wired(page->vm_object)) {
		return false;
	}
	page->wire_count++;
	return true;
}

bool plane_vm_page_unwire(struct plane_page *page)
{
	if (!vm_page_pointer_index(page, NULL) ||
	    page->state != PLANE_VM_PAGE_ALLOCATED ||
	    page->wire_count == 0) {
		return false;
	}

	if (page->vm_object != NULL &&
	    page->wire_count == 1 &&
	    !plane_vm_object_page_became_unwired(page->vm_object)) {
		return false;
	}
	page->wire_count--;
	return true;
}
