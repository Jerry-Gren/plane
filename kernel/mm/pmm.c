#include <stddef.h>

#include <hal/mmu.h>

#include <klib/string.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/printk.h>
#include <plane/util.h>
#include <plane/vm_page.h>

#include "vm_object_internal.h"
#include "vm_page_internal.h"

struct pmm_managed_range {
	uint64_t base;
	uint64_t page_count;
	uint64_t page_index;
};

enum pmm_page_queue_state {
	PMM_PAGE_QUEUE_NONE = 0,
	PMM_PAGE_QUEUE_FREE,
};

#define PLANE_VM_GUARD_PAGE_POOL_SIZE 64

struct plane_page {
	uint64_t phys_addr;
	uint64_t wire_count;
	struct plane_vm_object *vm_object;
	uint64_t vm_object_offset;
	struct plane_page *object_prev;
	struct plane_page *object_next;
	struct plane_page *object_hash_next;
	bool object_tabled;
	bool object_hashed;
	enum plane_vm_page_state state;
	struct plane_page *queue_prev;
	struct plane_page *queue_next;
	enum pmm_page_queue_state queue_state;
};

struct pmm_page_queue {
	struct plane_page *head;
	struct plane_page *tail;
	uint64_t count;
};

static struct pmm_managed_range managed_ranges[PLANE_MAX_MEMMAP_ENTRIES];
static uint64_t managed_range_count;
static struct plane_page *page_pool;
static uint64_t tracked_page_count;
static struct pmm_page_queue free_queue;
static struct plane_pmm_stats pmm_stats;
static uint64_t metadata_phys_base;
static uint64_t metadata_page_count;
static struct plane_page guard_page_pool[PLANE_VM_GUARD_PAGE_POOL_SIZE];

static uint64_t page_count_for_region(uint64_t start, uint64_t end)
{
	return (end - start) / PAGE_SIZE;
}

static bool append_managed_range(uint64_t base, uint64_t page_count)
{
	uint64_t new_tracked_count;

	if (managed_range_count >= PLANE_MAX_MEMMAP_ENTRIES) {
		return false;
	}

	if (!plane_checked_add_u64(tracked_page_count, page_count,
			     &new_tracked_count)) {
		return false;
	}

	managed_ranges[managed_range_count].base = base;
	managed_ranges[managed_range_count].page_count = page_count;
	managed_ranges[managed_range_count].page_index = tracked_page_count;
	managed_range_count++;
	tracked_page_count = new_tracked_count;
	return true;
}

static uint64_t allocated_page_count(void)
{
	return pmm_stats.allocator.managed_pages - free_queue.count;
}

static bool add_pages_to_stat(uint64_t *stat, uint64_t pages)
{
	return plane_checked_add_u64(*stat, pages, stat);
}

static bool append_usable_region(uint64_t base, uint64_t page_count)
{
	uint64_t managed_pages;

	if (!plane_checked_add_u64(pmm_stats.allocator.managed_pages, page_count,
			     &managed_pages) ||
	    !append_managed_range(base, page_count) ||
	    !add_pages_to_stat(&pmm_stats.memtype.usable_pages, page_count)) {
		return false;
	}

	pmm_stats.allocator.managed_pages = managed_pages;
	return true;
}

static bool account_unusable_region(uint32_t type, uint64_t pages)
{
	switch (type) {
	case PLANE_MEM_ACPI_RECLAIMABLE:
		return add_pages_to_stat(&pmm_stats.memtype.acpi_reclaimable_pages,
					 pages);
	case PLANE_MEM_ACPI_NVS:
		return add_pages_to_stat(&pmm_stats.memtype.acpi_nvs_pages, pages);
	case PLANE_MEM_BAD_MEMORY:
		return add_pages_to_stat(&pmm_stats.memtype.bad_pages, pages);
	case PLANE_MEM_BOOTLOADER_RECLAIMABLE:
		return add_pages_to_stat(&pmm_stats.memtype.bootloader_reclaimable_pages,
					 pages);
	case PLANE_MEM_EXECUTABLE_AND_MODULES:
		return add_pages_to_stat(&pmm_stats.memtype.executable_and_modules_pages,
					 pages);
	case PLANE_MEM_FRAMEBUFFER:
		return add_pages_to_stat(&pmm_stats.memtype.framebuffer_pages, pages);
	case PLANE_MEM_INVALID:
		return add_pages_to_stat(&pmm_stats.memtype.invalid_pages, pages);
	case PLANE_MEM_RESERVED:
		return add_pages_to_stat(&pmm_stats.memtype.reserved_pages, pages);
	case PLANE_MEM_RESERVED_MAPPED:
		return add_pages_to_stat(&pmm_stats.memtype.reserved_mapped_pages,
					 pages);
	default:
		return add_pages_to_stat(&pmm_stats.memtype.invalid_pages, pages);
	}
}

static bool managed_range_contains(uint64_t base, uint64_t page_count)
{
	uint64_t end;

	if (!plane_checked_page_range_end(base, page_count, &end)) {
		return false;
	}

	for (uint64_t i = 0; i < managed_range_count; i++) {
		uint64_t managed_end;

		if (!plane_checked_page_range_end(managed_ranges[i].base,
					    managed_ranges[i].page_count,
					    &managed_end)) {
			return false;
		}

		if (base >= managed_ranges[i].base && end <= managed_end) {
			return true;
		}
	}

	return false;
}

static void free_queue_reset(void)
{
	free_queue = (struct pmm_page_queue){0};
}

static bool free_queue_insert_ordered(struct plane_page *page)
{
	struct plane_page *next;

	if (page == NULL || page->state != PLANE_VM_PAGE_FREE ||
	    page->queue_state != PMM_PAGE_QUEUE_NONE) {
		return false;
	}

	if (free_queue.tail == NULL ||
	    free_queue.tail->phys_addr < page->phys_addr) {
		page->queue_prev = free_queue.tail;
		page->queue_next = NULL;
		if (free_queue.tail != NULL) {
			free_queue.tail->queue_next = page;
		} else {
			free_queue.head = page;
		}
		free_queue.tail = page;
		page->queue_state = PMM_PAGE_QUEUE_FREE;
		free_queue.count++;
		return true;
	}

	next = free_queue.head;
	while (next != NULL && next->phys_addr < page->phys_addr) {
		next = next->queue_next;
	}

	page->queue_next = next;
	if (next != NULL) {
		page->queue_prev = next->queue_prev;
		next->queue_prev = page;
	} else {
		page->queue_prev = free_queue.tail;
		free_queue.tail = page;
	}

	if (page->queue_prev != NULL) {
		page->queue_prev->queue_next = page;
	} else {
		free_queue.head = page;
	}

	page->queue_state = PMM_PAGE_QUEUE_FREE;
	free_queue.count++;
	return true;
}

static bool free_queue_remove(struct plane_page *page)
{
	if (page == NULL ||
	    page->queue_state != PMM_PAGE_QUEUE_FREE ||
	    free_queue.count == 0) {
		return false;
	}

	if (page->queue_prev != NULL) {
		page->queue_prev->queue_next = page->queue_next;
	} else {
		free_queue.head = page->queue_next;
	}

	if (page->queue_next != NULL) {
		page->queue_next->queue_prev = page->queue_prev;
	} else {
		free_queue.tail = page->queue_prev;
	}

	page->queue_prev = NULL;
	page->queue_next = NULL;
	page->queue_state = PMM_PAGE_QUEUE_NONE;
	free_queue.count--;
	return true;
}

static struct plane_page *free_queue_pop_head(void)
{
	struct plane_page *page = free_queue.head;

	if (page == NULL || !free_queue_remove(page)) {
		return NULL;
	}

	return page;
}

static bool page_pointer_index(const struct plane_page *page, uint64_t *index)
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

static bool guard_page_index(const struct plane_page *page, uint64_t *index)
{
	uintptr_t pool_base;
	uintptr_t page_addr;
	uintptr_t offset;

	if (page == NULL) {
		return false;
	}

	pool_base = (uintptr_t)guard_page_pool;
	page_addr = (uintptr_t)page;
	if (page_addr < pool_base) {
		return false;
	}

	offset = page_addr - pool_base;
	if ((offset % sizeof(guard_page_pool[0])) != 0) {
		return false;
	}

	offset /= sizeof(guard_page_pool[0]);
	if (offset >= PLANE_VM_GUARD_PAGE_POOL_SIZE) {
		return false;
	}

	if (index != NULL) {
		*index = offset;
	}
	return true;
}

static bool active_guard_page_index(const struct plane_page *page,
				    uint64_t *index)
{
	uint64_t guard_index;

	if (!guard_page_index(page, &guard_index) ||
	    guard_page_pool[guard_index].state != PLANE_VM_PAGE_GUARD) {
		return false;
	}

	if (index != NULL) {
		*index = guard_index;
	}
	return true;
}

static bool vm_page_known(const struct plane_page *page)
{
	return page_pointer_index(page, NULL) ||
	       active_guard_page_index(page, NULL);
}

static bool vm_page_resident_state_valid(enum plane_vm_page_state state)
{
	return state == PLANE_VM_PAGE_ALLOCATED ||
	       state == PLANE_VM_PAGE_GUARD;
}

static void reset_resident_links(struct plane_page *page)
{
	page->vm_object = NULL;
	page->vm_object_offset = 0;
	page->object_prev = NULL;
	page->object_next = NULL;
	page->object_hash_next = NULL;
	page->object_tabled = false;
	page->object_hashed = false;
}

static void reset_guard_page_pool(void)
{
	for (uint64_t i = 0; i < PLANE_VM_GUARD_PAGE_POOL_SIZE; i++) {
		guard_page_pool[i] = (struct plane_page){0};
		guard_page_pool[i].phys_addr = PLANE_VM_PAGE_NO_PHYS;
		guard_page_pool[i].state = PLANE_VM_PAGE_INVALID;
		guard_page_pool[i].queue_state = PMM_PAGE_QUEUE_NONE;
	}
}

struct plane_page *plane_vm_page_create_guard(void)
{
	for (uint64_t i = 0; i < PLANE_VM_GUARD_PAGE_POOL_SIZE; i++) {
		struct plane_page *page = &guard_page_pool[i];

		if (page->state != PLANE_VM_PAGE_INVALID) {
			continue;
		}

		page->phys_addr = PLANE_VM_PAGE_GUARD_PHYS;
		page->wire_count = 0;
		reset_resident_links(page);
		page->state = PLANE_VM_PAGE_GUARD;
		page->queue_prev = NULL;
		page->queue_next = NULL;
		page->queue_state = PMM_PAGE_QUEUE_NONE;
		return page;
	}

	return NULL;
}

bool plane_vm_page_release_guard(struct plane_page *page)
{
	if (!guard_page_index(page, NULL) ||
	    page->state != PLANE_VM_PAGE_GUARD ||
	    page->wire_count != 0 ||
	    page->vm_object != NULL ||
	    page->object_prev != NULL ||
	    page->object_next != NULL ||
	    page->object_hash_next != NULL ||
	    page->object_tabled ||
	    page->object_hashed) {
		return false;
	}

	reset_resident_links(page);
	page->phys_addr = PLANE_VM_PAGE_NO_PHYS;
	page->queue_prev = NULL;
	page->queue_next = NULL;
	page->queue_state = PMM_PAGE_QUEUE_NONE;
	page->state = PLANE_VM_PAGE_INVALID;
	return true;
}

struct plane_page *plane_vm_page_from_phys(uint64_t phys_addr)
{
	if (page_pool == NULL || (phys_addr & (PAGE_SIZE - 1)) != 0) {
		return NULL;
	}

	for (uint64_t i = 0; i < managed_range_count; i++) {
		uint64_t managed_end;

		if (!plane_checked_page_range_end(managed_ranges[i].base,
					    managed_ranges[i].page_count,
					    &managed_end)) {
			return NULL;
		}

		if (phys_addr >= managed_ranges[i].base &&
		    phys_addr < managed_end) {
			uint64_t page_offset =
				(phys_addr - managed_ranges[i].base) / PAGE_SIZE;

			return &page_pool[managed_ranges[i].page_index +
					  page_offset];
		}
	}

	return NULL;
}

uint64_t plane_vm_page_phys(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page_index(page, NULL)) {
		return PLANE_VM_PAGE_GUARD_PHYS;
	}
	if (guard_page_index(page, NULL)) {
		return PLANE_VM_PAGE_NO_PHYS;
	}
	if (!page_pointer_index(page, &index)) {
		return PLANE_VM_PAGE_NO_PHYS;
	}

	return page_pool[index].phys_addr;
}

enum plane_vm_page_state plane_vm_page_state(const struct plane_page *page)
{
	uint64_t index;

	if (guard_page_index(page, &index)) {
		return guard_page_pool[index].state;
	}
	if (!page_pointer_index(page, &index)) {
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
	if (active_guard_page_index(page, &index)) {
		*wire_count = guard_page_pool[index].wire_count;
		return true;
	}
	if (!page_pointer_index(page, &index)) {
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

	if (active_guard_page_index(page, &index)) {
		return guard_page_pool[index].vm_object;
	}
	if (!page_pointer_index(page, &index)) {
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
	if (active_guard_page_index(page, &index)) {
		if (guard_page_pool[index].vm_object == NULL) {
			return false;
		}
		*offset = guard_page_pool[index].vm_object_offset;
		return true;
	}
	if (!page_pointer_index(page, &index) ||
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
	if (active_guard_page_index(page, &index)) {
		if (guard_page_pool[index].vm_object != NULL) {
			return false;
		}

		guard_page_pool[index].vm_object = object;
		guard_page_pool[index].vm_object_offset = offset;
		guard_page_pool[index].object_prev = NULL;
		guard_page_pool[index].object_next = NULL;
		guard_page_pool[index].object_hash_next = NULL;
		guard_page_pool[index].object_tabled = false;
		guard_page_pool[index].object_hashed = false;
		return true;
	}
	if (!page_pointer_index(page, &index) ||
	    !vm_page_resident_state_valid(page_pool[index].state) ||
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
	if (active_guard_page_index(page, &index)) {
		if (guard_page_pool[index].vm_object != object ||
		    guard_page_pool[index].vm_object_offset != offset) {
			return false;
		}

		reset_resident_links(&guard_page_pool[index]);
		return true;
	}
	if (!page_pointer_index(page, &index) ||
	    page_pool[index].state != PLANE_VM_PAGE_ALLOCATED ||
	    page_pool[index].vm_object != object ||
	    page_pool[index].vm_object_offset != offset) {
		return false;
	}

	page_pool[index].vm_object = NULL;
	page_pool[index].vm_object_offset = 0;
	page_pool[index].object_prev = NULL;
	page_pool[index].object_next = NULL;
	page_pool[index].object_hash_next = NULL;
	page_pool[index].object_tabled = false;
	page_pool[index].object_hashed = false;
	return true;
}

struct plane_page *plane_vm_page_object_prev(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page_index(page, &index)) {
		return guard_page_pool[index].object_prev;
	}
	if (!page_pointer_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].object_prev;
}

struct plane_page *plane_vm_page_object_next(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page_index(page, &index)) {
		return guard_page_pool[index].object_next;
	}
	if (!page_pointer_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].object_next;
}

struct plane_page *plane_vm_page_object_hash_next(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page_index(page, &index)) {
		return guard_page_pool[index].object_hash_next;
	}
	if (!page_pointer_index(page, &index)) {
		return NULL;
	}

	return page_pool[index].object_hash_next;
}

bool plane_vm_page_object_tabled(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page_index(page, &index)) {
		return guard_page_pool[index].object_tabled;
	}
	if (!page_pointer_index(page, &index)) {
		return false;
	}

	return page_pool[index].object_tabled;
}

bool plane_vm_page_object_hashed(const struct plane_page *page)
{
	uint64_t index;

	if (active_guard_page_index(page, &index)) {
		return guard_page_pool[index].object_hashed;
	}
	if (!page_pointer_index(page, &index)) {
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
	if (active_guard_page_index(page, &index)) {
		guard_page_pool[index].object_prev = prev;
		return true;
	}
	if (!page_pointer_index(page, &index)) {
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
	if (active_guard_page_index(page, &index)) {
		guard_page_pool[index].object_next = next;
		return true;
	}
	if (!page_pointer_index(page, &index)) {
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
	if (active_guard_page_index(page, &index)) {
		guard_page_pool[index].object_hash_next = next;
		return true;
	}
	if (!page_pointer_index(page, &index)) {
		return false;
	}

	page_pool[index].object_hash_next = next;
	return true;
}

bool plane_vm_page_set_object_tabled(struct plane_page *page, bool tabled)
{
	uint64_t index;

	if (active_guard_page_index(page, &index)) {
		guard_page_pool[index].object_tabled = tabled;
		return true;
	}
	if (!page_pointer_index(page, &index)) {
		return false;
	}

	page_pool[index].object_tabled = tabled;
	return true;
}

bool plane_vm_page_set_object_hashed(struct plane_page *page, bool hashed)
{
	uint64_t index;

	if (active_guard_page_index(page, &index)) {
		guard_page_pool[index].object_hashed = hashed;
		return true;
	}
	if (!page_pointer_index(page, &index)) {
		return false;
	}

	page_pool[index].object_hashed = hashed;
	return true;
}

static bool page_is_free_queued(const struct plane_page *page)
{
	return page != NULL &&
	       page->state == PLANE_VM_PAGE_FREE &&
	       page->queue_state == PMM_PAGE_QUEUE_FREE;
}

static bool page_is_allocated_unqueued(const struct plane_page *page)
{
	return page != NULL &&
	       page->state == PLANE_VM_PAGE_ALLOCATED &&
	       page->queue_state == PMM_PAGE_QUEUE_NONE &&
	       page->vm_object == NULL;
}

static bool page_state_range_matches(uint64_t phys_addr,
				     uint64_t page_count,
				     enum plane_vm_page_state expected)
{
	for (uint64_t i = 0; i < page_count; i++) {
		uint64_t offset;
		uint64_t page_phys;
		struct plane_page *page;

		if (!plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (page == NULL || page->state != expected) {
			return false;
		}
	}

	return true;
}

static bool set_page_state_range(uint64_t phys_addr,
				 uint64_t page_count,
				 enum plane_vm_page_state expected,
				 enum plane_vm_page_state next)
{
	if (!page_state_range_matches(phys_addr, page_count, expected)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		uint64_t offset;
		uint64_t page_phys;
		struct plane_page *page;

		if (!plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);

		page->state = next;
	}

	return true;
}

static bool init_page_metadata(void)
{
	uint64_t metadata_bytes;
	uint64_t metadata_pages;
	uint64_t metadata_size;

	if (pmm_stats.allocator.managed_pages == 0) {
		return true;
	}

	if (!plane_checked_mul_u64(pmm_stats.allocator.managed_pages,
				   sizeof(page_pool[0]), &metadata_bytes) ||
	    !plane_checked_align_up_u64(metadata_bytes, PAGE_SIZE,
					&metadata_size)) {
		return false;
	}
	metadata_pages = metadata_size / PAGE_SIZE;

	for (uint64_t i = 0; i < managed_range_count; i++) {
		if (managed_ranges[i].page_count >= metadata_pages) {
			metadata_phys_base = managed_ranges[i].base;
			metadata_page_count = metadata_pages;
			break;
		}
	}

	if (metadata_page_count == 0) {
		return false;
	}

	page_pool = hal_mmu_direct_phys_range_to_virt(metadata_phys_base,
						     metadata_size);
	if (page_pool == NULL) {
		return false;
	}

	pmm_stats.allocator.metadata_bytes = metadata_bytes;
	pmm_stats.allocator.metadata_pages = metadata_pages;

	for (uint64_t i = 0; i < managed_range_count; i++) {
		for (uint64_t j = 0; j < managed_ranges[i].page_count; j++) {
			uint64_t phys;
			uint64_t page_index;

			if (!plane_checked_page_offset(j, &phys) ||
			    !plane_checked_add_u64(managed_ranges[i].base, phys,
						   &phys) ||
			    !plane_checked_add_u64(managed_ranges[i].page_index, j,
						   &page_index)) {
				return false;
			}

			page_pool[page_index].phys_addr = phys;
			page_pool[page_index].wire_count = 0;
			page_pool[page_index].vm_object = NULL;
			page_pool[page_index].vm_object_offset = 0;
			page_pool[page_index].object_prev = NULL;
			page_pool[page_index].object_next = NULL;
			page_pool[page_index].object_hash_next = NULL;
			page_pool[page_index].object_tabled = false;
			page_pool[page_index].object_hashed = false;
			page_pool[page_index].state = PLANE_VM_PAGE_FREE;
			page_pool[page_index].queue_prev = NULL;
			page_pool[page_index].queue_next = NULL;
			page_pool[page_index].queue_state =
				PMM_PAGE_QUEUE_NONE;
		}
	}

	if (!set_page_state_range(metadata_phys_base, metadata_pages,
				  PLANE_VM_PAGE_FREE, PLANE_VM_PAGE_METADATA)) {
		return false;
	}

	for (uint64_t i = 0; i < tracked_page_count; i++) {
		if (page_pool[i].state == PLANE_VM_PAGE_FREE &&
		    !free_queue_insert_ordered(&page_pool[i])) {
			return false;
		}
	}

	return true;
}

bool plane_pmm_init(const struct plane_mem_info *mem)
{
	managed_range_count = 0;
	tracked_page_count = 0;
	page_pool = NULL;
	free_queue_reset();
	reset_guard_page_pool();
	metadata_phys_base = 0;
	metadata_page_count = 0;
	pmm_stats = (struct plane_pmm_stats){0};

	if (mem == NULL) {
		return false;
	}

	for (uint64_t i = 0; i < mem->entry_count; i++) {
		const struct plane_mem_region *region = &mem->map[i];
		uint64_t end;
		uint64_t start;
		uint64_t aligned_end;
		uint64_t pages;

		if (region->length == 0) {
			continue;
		}

		if (!plane_checked_add_u64(region->base, region->length, &end)) {
			return false;
		}

		if (region->type == PLANE_MEM_USABLE) {
			if (!plane_checked_align_up_u64(region->base, PAGE_SIZE, &start)) {
				return false;
			}
			aligned_end = ALIGN_DOWN(end, PAGE_SIZE);
			if (start >= aligned_end) {
				continue;
			}

			pages = page_count_for_region(start, aligned_end);
			if (!append_usable_region(start, pages)) {
				return false;
			}
			continue;
		}

		start = ALIGN_DOWN(region->base, PAGE_SIZE);
		if (!plane_checked_align_up_u64(end, PAGE_SIZE, &aligned_end)) {
			return false;
		}
		if (start >= aligned_end) {
			continue;
		}

		pages = page_count_for_region(start, aligned_end);
		if (!account_unusable_region(region->type, pages)) {
			return false;
		}
	}

	return init_page_metadata();
}

static bool page_range_is_free(uint64_t phys_addr, uint64_t page_count)
{
	if (!managed_range_contains(phys_addr, page_count)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_phys;
		uint64_t offset;

		if (!plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (!page_is_free_queued(page)) {
			return false;
		}
	}

	return true;
}

static bool alloc_flags_valid(uint32_t flags)
{
	return (flags & ~PLANE_PMM_ALLOC_ZERO) == 0;
}

static bool vm_page_grab_flags_valid(uint32_t flags)
{
	return (flags & ~PLANE_VM_PAGE_GRAB_ZERO) == 0;
}

static uint32_t vm_page_grab_to_pmm_flags(uint32_t flags)
{
	uint32_t pmm_flags = 0;

	if ((flags & PLANE_VM_PAGE_GRAB_ZERO) != 0) {
		pmm_flags |= PLANE_PMM_ALLOC_ZERO;
	}

	return pmm_flags;
}

static bool page_range_is_allocated_unwired(uint64_t phys_addr,
					    uint64_t page_count)
{
	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_phys;
		uint64_t offset;

		if (!plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (page == NULL ||
		    page->state != PLANE_VM_PAGE_ALLOCATED ||
		    page->wire_count != 0 ||
		    page->vm_object != NULL) {
			return false;
		}
	}

	return true;
}

static bool zero_allocated_pages(uint64_t phys_addr, uint64_t page_count)
{
	uint64_t size;
	void *vaddr;

	if (!plane_checked_page_offset(page_count, &size)) {
		return false;
	}

	vaddr = hal_mmu_direct_phys_range_to_virt(phys_addr, size);
	if (vaddr == NULL) {
		return false;
	}

	memset(vaddr, 0, size);
	return true;
}

static bool rollback_allocated_page_run(uint64_t phys_addr,
					uint64_t page_count)
{
	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_phys;
		uint64_t offset;

		if (!plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (!page_is_allocated_unqueued(page)) {
			return false;
		}
	}

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_phys;
		uint64_t offset;

		if (!plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		page->state = PLANE_VM_PAGE_FREE;
		page->vm_object = NULL;
		page->vm_object_offset = 0;
		page->object_prev = NULL;
		page->object_next = NULL;
		page->object_hash_next = NULL;
		page->object_tabled = false;
		page->object_hashed = false;
		if (!free_queue_insert_ordered(page)) {
			return false;
		}
	}

	return true;
}

static bool find_free_page_run(uint64_t page_count,
			       uint64_t alignment_pages,
			       uint64_t *phys_addr)
{
	for (uint64_t i = 0; i < managed_range_count; i++) {
		uint64_t range_base_page = managed_ranges[i].base / PAGE_SIZE;
		uint64_t range_end_page;
		uint64_t candidate_page;

		if (!plane_checked_add_u64(range_base_page,
				     managed_ranges[i].page_count,
				     &range_end_page) ||
		    !plane_checked_align_up_u64(range_base_page, alignment_pages,
				      &candidate_page)) {
			return false;
		}

		while (candidate_page <= range_end_page) {
			uint64_t candidate_end_page;
			uint64_t candidate_phys;

			if (!plane_checked_add_u64(candidate_page, page_count,
					     &candidate_end_page)) {
				return false;
			}
			if (candidate_end_page > range_end_page) {
				break;
			}
			if (!plane_checked_page_offset(candidate_page,
						       &candidate_phys)) {
				return false;
			}

			if (page_range_is_free(candidate_phys, page_count)) {
				*phys_addr = candidate_phys;
				return true;
			}

			if (!plane_checked_add_u64(candidate_page, alignment_pages,
					     &candidate_page)) {
				return false;
			}
		}
	}

	return false;
}

static bool plane_pmm_alloc_page_phys_raw(uint64_t *phys_addr)
{
	struct plane_page *page;

	if (phys_addr == NULL) {
		return false;
	}

	page = free_queue_pop_head();
	if (page == NULL) {
		return false;
	}

	page->state = PLANE_VM_PAGE_ALLOCATED;
	page->vm_object = NULL;
	page->vm_object_offset = 0;
	page->object_prev = NULL;
	page->object_next = NULL;
	page->object_hash_next = NULL;
	page->object_tabled = false;
	page->object_hashed = false;
	*phys_addr = page->phys_addr;
	return true;
}

static bool plane_pmm_alloc_pages_phys_raw(uint64_t page_count,
					   uint64_t alignment_pages,
					   uint64_t *phys_addr)
{
	uint64_t alloc_base;

	if (phys_addr == NULL || page_count == 0 ||
	    !plane_is_power_of_two_u64(alignment_pages)) {
		return false;
	}

	if (page_count == 1 && alignment_pages == 1) {
		return plane_pmm_alloc_page_phys_raw(phys_addr);
	}

	if (!find_free_page_run(page_count, alignment_pages, &alloc_base)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_phys;
		uint64_t offset;

		if (!plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(alloc_base, offset, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (!free_queue_remove(page)) {
			return false;
		}
		page->state = PLANE_VM_PAGE_ALLOCATED;
		page->vm_object = NULL;
		page->vm_object_offset = 0;
		page->object_prev = NULL;
		page->object_next = NULL;
		page->object_hash_next = NULL;
		page->object_tabled = false;
		page->object_hashed = false;
	}

	*phys_addr = alloc_base;
	return true;
}

bool plane_pmm_alloc_pages_phys_flags(uint64_t page_count,
				      uint64_t alignment_pages,
				      uint32_t flags,
				      uint64_t *phys_addr)
{
	uint64_t alloc_base;

	if (phys_addr == NULL ||
	    !alloc_flags_valid(flags) ||
	    !plane_pmm_alloc_pages_phys_raw(page_count, alignment_pages,
					    &alloc_base)) {
		return false;
	}

	if ((flags & PLANE_PMM_ALLOC_ZERO) != 0 &&
	    !zero_allocated_pages(alloc_base, page_count)) {
		if (!rollback_allocated_page_run(alloc_base, page_count)) {
			return false;
		}
		return false;
	}

	*phys_addr = alloc_base;
	return true;
}

bool plane_pmm_alloc_pages_phys(uint64_t page_count,
				uint64_t alignment_pages,
				uint64_t *phys_addr)
{
	return plane_pmm_alloc_pages_phys_flags(page_count, alignment_pages, 0,
					       phys_addr);
}

bool plane_vm_page_grab(uint32_t flags, struct plane_page **page)
{
	uint64_t phys_addr;
	struct plane_page *grabbed_page;

	if (page == NULL ||
	    !vm_page_grab_flags_valid(flags) ||
	    !plane_pmm_alloc_pages_phys_flags(
		    1, 1, vm_page_grab_to_pmm_flags(flags), &phys_addr)) {
		return false;
	}

	grabbed_page = plane_vm_page_from_phys(phys_addr);
	BUG_ON_MSG(grabbed_page == NULL,
		   "PMM allocated page without VM metadata: phys=%llx",
		   (unsigned long long)phys_addr);

	*page = grabbed_page;
	return true;
}

bool plane_pmm_alloc_page_phys(uint64_t *phys_addr)
{
	return plane_pmm_alloc_pages_phys_flags(1, 1, 0, phys_addr);
}

bool plane_pmm_free_pages_phys(uint64_t phys_addr, uint64_t page_count)
{
	if (page_count == 0 || (phys_addr & (PAGE_SIZE - 1)) != 0 ||
	    page_count > allocated_page_count() ||
	    !managed_range_contains(phys_addr, page_count) ||
	    !page_range_is_allocated_unwired(phys_addr, page_count)) {
		return false;
	}

	BUG_ON_MSG(!set_page_state_range(phys_addr, page_count,
					 PLANE_VM_PAGE_ALLOCATED,
					 PLANE_VM_PAGE_FREE),
		   "failed to mark PMM pages free");

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_phys;
		uint64_t offset;

		if (!plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		page->vm_object = NULL;
		page->vm_object_offset = 0;
		page->object_prev = NULL;
		page->object_next = NULL;
		page->object_hash_next = NULL;
		page->object_tabled = false;
		page->object_hashed = false;
		BUG_ON_MSG(!free_queue_insert_ordered(page),
			   "failed to insert PMM free page");
	}

	return true;
}

bool plane_vm_page_release(struct plane_page *page)
{
	uint64_t phys_addr;

	if (!page_pointer_index(page, NULL) ||
	    page->state != PLANE_VM_PAGE_ALLOCATED ||
	    page->wire_count != 0 ||
	    page->vm_object != NULL ||
	    page->object_prev != NULL ||
	    page->object_next != NULL ||
	    page->object_hash_next != NULL ||
	    page->object_tabled ||
	    page->object_hashed ||
	    page->queue_prev != NULL ||
	    page->queue_next != NULL ||
	    page->queue_state != PMM_PAGE_QUEUE_NONE) {
		return false;
	}

	phys_addr = page->phys_addr;
	BUG_ON_MSG(!plane_pmm_free_page_phys(phys_addr),
		   "failed to release VM page: phys=%llx",
		   (unsigned long long)phys_addr);
	return true;
}

bool plane_pmm_free_page_phys(uint64_t phys_addr)
{
	return plane_pmm_free_pages_phys(phys_addr, 1);
}

bool plane_vm_page_wire(struct plane_page *page)
{
	if (!page_pointer_index(page, NULL) ||
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
	if (!page_pointer_index(page, NULL) ||
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

struct plane_pmm_stats plane_pmm_get_stats(void)
{
	struct plane_pmm_stats stats = pmm_stats;
	uint64_t free_run_count = 0;
	uint64_t wired_pages = 0;
	bool in_free_run = false;

	for (uint64_t i = 0; i < managed_range_count; i++) {
		for (uint64_t j = 0; j < managed_ranges[i].page_count; j++) {
			struct plane_page *page =
				&page_pool[managed_ranges[i].page_index + j];

			if (page_is_free_queued(page)) {
				if (!in_free_run) {
					free_run_count++;
					in_free_run = true;
				}
			} else {
				in_free_run = false;
			}
			if (page->state == PLANE_VM_PAGE_ALLOCATED &&
			    page->wire_count != 0) {
				wired_pages++;
			}
		}
		in_free_run = false;
	}

	stats.allocator.free_pages = free_queue.count;
	stats.allocator.wired_pages = wired_pages;
	stats.allocator.free_run_count = free_run_count;
	return stats;
}
