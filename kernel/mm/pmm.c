#include <stddef.h>

#include <hal/mmu.h>

#include <klib/string.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/printk.h>
#include <plane/util.h>
#include <plane/vm_page.h>

#include "vm_page_internal.h"

#define PLANE_PMM_NULL_PHYS_GUARD_SIZE PAGE_SIZE

struct pmm_page_queue {
	struct plane_page *head;
	struct plane_page *tail;
	uint64_t count;
};

static struct plane_vm_page_managed_range
	managed_ranges[PLANE_MAX_MEMMAP_ENTRIES];
static uint64_t managed_range_count;
static struct plane_page *page_pool;
static uint64_t tracked_page_count;
static struct pmm_page_queue free_queue;
static struct plane_pmm_stats pmm_stats;
static uint64_t metadata_phys_base;
static uint64_t metadata_page_count;

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

static bool reserve_low_usable_pages(uint64_t *start, uint64_t aligned_end)
{
	uint64_t reserve_end;
	uint64_t pages;

	if (start == NULL || *start >= aligned_end ||
	    *start >= PLANE_PMM_NULL_PHYS_GUARD_SIZE) {
		return true;
	}

	reserve_end = aligned_end < PLANE_PMM_NULL_PHYS_GUARD_SIZE ?
		      aligned_end : PLANE_PMM_NULL_PHYS_GUARD_SIZE;
	pages = page_count_for_region(*start, reserve_end);
	if (pages != 0 &&
	    !account_unusable_region(PLANE_MEM_RESERVED, pages)) {
		return false;
	}

	*start = reserve_end;
	return true;
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

static void reset_free_queue(void)
{
	free_queue = (struct pmm_page_queue){0};
}

static bool free_queue_insert_ordered(struct plane_page *page)
{
	struct plane_page *next;

	if (page == NULL || page->state != PLANE_VM_PAGE_FREE ||
	    page->queue_state != PLANE_VM_PAGE_QUEUE_NONE) {
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
		page->queue_state = PLANE_VM_PAGE_QUEUE_FREE;
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

	page->queue_state = PLANE_VM_PAGE_QUEUE_FREE;
	free_queue.count++;
	return true;
}

static bool free_queue_remove(struct plane_page *page)
{
	if (page == NULL ||
	    page->queue_state != PLANE_VM_PAGE_QUEUE_FREE ||
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
	page->queue_state = PLANE_VM_PAGE_QUEUE_NONE;
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

static bool page_is_free_queued(const struct plane_page *page)
{
	return page != NULL &&
	       page->state == PLANE_VM_PAGE_FREE &&
	       page->queue_state == PLANE_VM_PAGE_QUEUE_FREE;
}

static bool page_is_allocated_unqueued(const struct plane_page *page)
{
	return plane_vm_page_allocated_unwired_no_object(page) &&
	       page->queue_state == PLANE_VM_PAGE_QUEUE_NONE;
}

static bool page_state_range_matches(plane_paddr_t phys_addr,
				     uint64_t page_count,
				     enum plane_vm_page_state expected)
{
	for (uint64_t i = 0; i < page_count; i++) {
		plane_paddr_t page_phys;
		struct plane_page *page;

		if (!plane_paddr_add_pages(phys_addr, i, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (page == NULL || page->state != expected) {
			return false;
		}
	}

	return true;
}

static bool set_page_state_range(plane_paddr_t phys_addr,
				 uint64_t page_count,
				 enum plane_vm_page_state expected,
				 enum plane_vm_page_state next)
{
	if (!page_state_range_matches(phys_addr, page_count, expected)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		plane_paddr_t page_phys;
		struct plane_page *page;

		if (!plane_paddr_add_pages(phys_addr, i, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (!plane_vm_page_set_state(page, next)) {
			return false;
		}
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

	plane_vaddr_t metadata_vaddr = hal_mmu_physmap_phys_range_to_virt(
		plane_paddr_make(metadata_phys_base), metadata_size);
	if (plane_vaddr_is_null(metadata_vaddr)) {
		return false;
	}
	page_pool = plane_vaddr_to_ptr(metadata_vaddr);

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

			plane_vm_page_init(&page_pool[page_index],
					   plane_paddr_make(phys),
					   PLANE_VM_PAGE_FREE);
		}
	}

	if (!plane_vm_page_install_pool(page_pool, tracked_page_count,
					managed_ranges, managed_range_count)) {
		return false;
	}

	if (!set_page_state_range(plane_paddr_make(metadata_phys_base),
				  metadata_pages,
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
	reset_free_queue();
	plane_vm_page_reset_runtime();
	metadata_phys_base = 0;
	metadata_page_count = 0;
	pmm_stats = (struct plane_pmm_stats){0};

	if (mem == NULL) {
		return false;
	}

	for (uint64_t i = 0; i < mem->entry_count; i++) {
		const struct plane_mem_region *region = &mem->map[i];
		uint64_t region_base = plane_paddr_raw(region->base);
		uint64_t end;
		uint64_t start;
		uint64_t aligned_end;
		uint64_t pages;

		if (region->length == 0) {
			continue;
		}

		if (!plane_checked_add_u64(region_base, region->length, &end)) {
			return false;
		}

		if (region->type == PLANE_MEM_USABLE) {
			if (!plane_checked_align_up_u64(region_base, PAGE_SIZE,
						       &start)) {
				return false;
			}
			aligned_end = ALIGN_DOWN(end, PAGE_SIZE);
			if (start >= aligned_end) {
				continue;
			}
			/*
			 * Firmware memory maps are input, not Plane's final
			 * allocation policy. Keep physical page zero reserved so
			 * null physical addresses never become page tables,
			 * metadata storage, or VM backing pages.
			 */
			if (!reserve_low_usable_pages(&start, aligned_end)) {
				return false;
			}
			if (start >= aligned_end) {
				continue;
			}

			pages = page_count_for_region(start, aligned_end);
			if (!append_usable_region(start, pages)) {
				return false;
			}
			continue;
		}

		start = ALIGN_DOWN(region_base, PAGE_SIZE);
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

static bool page_range_is_free(plane_paddr_t phys_addr, uint64_t page_count)
{
	if (!managed_range_contains(plane_paddr_raw(phys_addr), page_count)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		plane_paddr_t page_phys;

		if (!plane_paddr_add_pages(phys_addr, i, &page_phys)) {
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

static bool page_range_is_allocated_unwired(plane_paddr_t phys_addr,
					    uint64_t page_count)
{
	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		plane_paddr_t page_phys;

		if (!plane_paddr_add_pages(phys_addr, i, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (!plane_vm_page_allocated_unwired_no_object(page)) {
			return false;
		}
	}

	return true;
}

static bool zero_allocated_pages(plane_paddr_t phys_addr, uint64_t page_count)
{
	plane_vaddr_t mapped_vaddr;
	uint64_t size;
	void *vaddr;

	if (!plane_checked_page_offset(page_count, &size)) {
		return false;
	}

	mapped_vaddr = hal_mmu_physmap_phys_range_to_virt(phys_addr, size);
	if (plane_vaddr_is_null(mapped_vaddr)) {
		return false;
	}
	vaddr = plane_vaddr_to_ptr(mapped_vaddr);

	memset(vaddr, 0, size);
	return true;
}

static bool rollback_allocated_page_run(plane_paddr_t phys_addr,
					uint64_t page_count)
{
	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		plane_paddr_t page_phys;

		if (!plane_paddr_add_pages(phys_addr, i, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (!page_is_allocated_unqueued(page)) {
			return false;
		}
	}

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		plane_paddr_t page_phys;

		if (!plane_paddr_add_pages(phys_addr, i, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (!plane_vm_page_set_state(page, PLANE_VM_PAGE_FREE)) {
			return false;
		}
		plane_vm_page_reset_resident_links(page);
		if (!free_queue_insert_ordered(page)) {
			return false;
		}
	}

	return true;
}

static bool find_free_page_run(uint64_t page_count,
			       uint64_t alignment_pages,
			       plane_paddr_t *phys_addr)
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

			if (page_range_is_free(plane_paddr_make(candidate_phys),
					       page_count)) {
				*phys_addr = plane_paddr_make(candidate_phys);
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

static bool plane_pmm_alloc_page_phys_internal(plane_paddr_t *phys_addr)
{
	struct plane_page *page;

	if (phys_addr == NULL) {
		return false;
	}

	page = free_queue_pop_head();
	if (page == NULL) {
		return false;
	}

	if (!plane_vm_page_set_state(page, PLANE_VM_PAGE_ALLOCATED)) {
		return false;
	}
	plane_vm_page_reset_resident_links(page);
	*phys_addr = plane_paddr_make(page->phys_addr);
	return true;
}

static bool plane_pmm_alloc_pages_phys_internal(uint64_t page_count,
						uint64_t alignment_pages,
						plane_paddr_t *phys_addr)
{
	plane_paddr_t alloc_base;

	if (phys_addr == NULL || page_count == 0 ||
	    !plane_is_power_of_two_u64(alignment_pages)) {
		return false;
	}

	if (page_count == 1 && alignment_pages == 1) {
		return plane_pmm_alloc_page_phys_internal(phys_addr);
	}

	if (!find_free_page_run(page_count, alignment_pages, &alloc_base)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		plane_paddr_t page_phys;

		if (!plane_paddr_add_pages(alloc_base, i, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		if (!free_queue_remove(page)) {
			return false;
		}
		if (!plane_vm_page_set_state(page, PLANE_VM_PAGE_ALLOCATED)) {
			return false;
		}
		plane_vm_page_reset_resident_links(page);
	}

	*phys_addr = alloc_base;
	return true;
}

bool plane_pmm_alloc_pages_phys_flags(uint64_t page_count,
				      uint64_t alignment_pages,
				      uint32_t flags,
				      plane_paddr_t *phys_addr)
{
	plane_paddr_t alloc_base;

	if (phys_addr == NULL ||
	    !alloc_flags_valid(flags) ||
	    !plane_pmm_alloc_pages_phys_internal(page_count, alignment_pages,
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
				plane_paddr_t *phys_addr)
{
	return plane_pmm_alloc_pages_phys_flags(page_count, alignment_pages, 0,
					       phys_addr);
}

bool plane_pmm_alloc_page_phys(plane_paddr_t *phys_addr)
{
	return plane_pmm_alloc_pages_phys_flags(1, 1, 0, phys_addr);
}

bool plane_pmm_free_pages_phys(plane_paddr_t phys_addr, uint64_t page_count)
{
	uint64_t raw_phys = plane_paddr_raw(phys_addr);

	if (page_count == 0 || !plane_paddr_is_page_aligned(phys_addr) ||
	    page_count > allocated_page_count() ||
	    !managed_range_contains(raw_phys, page_count) ||
	    !page_range_is_allocated_unwired(phys_addr, page_count)) {
		return false;
	}

	BUG_ON_MSG(!set_page_state_range(phys_addr, page_count,
					 PLANE_VM_PAGE_ALLOCATED,
					 PLANE_VM_PAGE_FREE),
		   "failed to mark PMM pages free");

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		plane_paddr_t page_phys;

		if (!plane_paddr_add_pages(phys_addr, i, &page_phys)) {
			return false;
		}

		page = plane_vm_page_from_phys(page_phys);
		plane_vm_page_reset_resident_links(page);
		BUG_ON_MSG(!free_queue_insert_ordered(page),
			   "failed to insert PMM free page");
	}

	return true;
}

bool plane_pmm_free_page_phys(plane_paddr_t phys_addr)
{
	return plane_pmm_free_pages_phys(phys_addr, 1);
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
