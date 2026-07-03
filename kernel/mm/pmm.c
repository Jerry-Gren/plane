#include <stddef.h>

#include <hal/mmu.h>

#include <klib/string.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/util.h>

struct pmm_managed_range {
	uint64_t base;
	uint64_t page_count;
	uint64_t page_index;
};

struct plane_page {
	uint64_t phys_addr;
	enum plane_page_state state;
	struct plane_page *free_prev;
	struct plane_page *free_next;
	bool on_free_queue;
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

static bool is_power_of_two(uint64_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

static bool checked_add_u64(uint64_t lhs, uint64_t rhs, uint64_t *out)
{
	if (rhs > UINT64_MAX - lhs) {
		return false;
	}

	*out = lhs + rhs;
	return true;
}

static bool checked_mul_u64(uint64_t lhs, uint64_t rhs, uint64_t *out)
{
	if (lhs != 0 && rhs > UINT64_MAX / lhs) {
		return false;
	}

	*out = lhs * rhs;
	return true;
}

static bool checked_region_end(uint64_t base, uint64_t length, uint64_t *end)
{
	return checked_add_u64(base, length, end);
}

static bool checked_align_up(uint64_t value, uint64_t align, uint64_t *out)
{
	uint64_t mask;

	if (!is_power_of_two(align)) {
		return false;
	}

	mask = align - 1;
	if ((value & mask) == 0) {
		*out = value;
		return true;
	}

	if (value > UINT64_MAX - mask) {
		return false;
	}

	*out = (value + mask) & ~mask;
	return true;
}

static bool checked_page_range_end(uint64_t base,
				   uint64_t page_count,
				   uint64_t *end)
{
	uint64_t size;

	if (!checked_mul_u64(page_count, PAGE_SIZE, &size)) {
		return false;
	}

	return checked_add_u64(base, size, end);
}

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

	if (!checked_add_u64(tracked_page_count, page_count,
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
	return checked_add_u64(*stat, pages, stat);
}

static bool append_usable_region(uint64_t base, uint64_t page_count)
{
	uint64_t managed_pages;

	if (!checked_add_u64(pmm_stats.allocator.managed_pages, page_count,
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

	if (!checked_page_range_end(base, page_count, &end)) {
		return false;
	}

	for (uint64_t i = 0; i < managed_range_count; i++) {
		uint64_t managed_end;

		if (!checked_page_range_end(managed_ranges[i].base,
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

	if (page == NULL || page->state != PLANE_PAGE_FREE ||
	    page->on_free_queue) {
		return false;
	}

	if (free_queue.tail == NULL ||
	    free_queue.tail->phys_addr < page->phys_addr) {
		page->free_prev = free_queue.tail;
		page->free_next = NULL;
		if (free_queue.tail != NULL) {
			free_queue.tail->free_next = page;
		} else {
			free_queue.head = page;
		}
		free_queue.tail = page;
		page->on_free_queue = true;
		free_queue.count++;
		return true;
	}

	next = free_queue.head;
	while (next != NULL && next->phys_addr < page->phys_addr) {
		next = next->free_next;
	}

	page->free_next = next;
	if (next != NULL) {
		page->free_prev = next->free_prev;
		next->free_prev = page;
	} else {
		page->free_prev = free_queue.tail;
		free_queue.tail = page;
	}

	if (page->free_prev != NULL) {
		page->free_prev->free_next = page;
	} else {
		free_queue.head = page;
	}

	page->on_free_queue = true;
	free_queue.count++;
	return true;
}

static bool free_queue_remove(struct plane_page *page)
{
	if (page == NULL || !page->on_free_queue || free_queue.count == 0) {
		return false;
	}

	if (page->free_prev != NULL) {
		page->free_prev->free_next = page->free_next;
	} else {
		free_queue.head = page->free_next;
	}

	if (page->free_next != NULL) {
		page->free_next->free_prev = page->free_prev;
	} else {
		free_queue.tail = page->free_prev;
	}

	page->free_prev = NULL;
	page->free_next = NULL;
	page->on_free_queue = false;
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

	*index = offset / sizeof(page_pool[0]);
	return *index < tracked_page_count;
}

struct plane_page *plane_pmm_phys_to_page(uint64_t phys_addr)
{
	if (page_pool == NULL || (phys_addr & (PAGE_SIZE - 1)) != 0) {
		return NULL;
	}

	for (uint64_t i = 0; i < managed_range_count; i++) {
		uint64_t managed_end;

		if (!checked_page_range_end(managed_ranges[i].base,
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

uint64_t plane_page_phys(const struct plane_page *page)
{
	uint64_t index;

	if (!page_pointer_index(page, &index)) {
		return UINT64_MAX;
	}

	return page_pool[index].phys_addr;
}

enum plane_page_state plane_page_state(const struct plane_page *page)
{
	uint64_t index;

	if (!page_pointer_index(page, &index)) {
		return PLANE_PAGE_INVALID;
	}

	return page_pool[index].state;
}

static bool page_state_range_matches(uint64_t phys_addr,
				     uint64_t page_count,
				     enum plane_page_state expected)
{
	for (uint64_t i = 0; i < page_count; i++) {
		uint64_t offset;
		uint64_t page_phys;
		struct plane_page *page;

		if (!checked_mul_u64(i, PAGE_SIZE, &offset) ||
		    !checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_pmm_phys_to_page(page_phys);
		if (page == NULL || page->state != expected) {
			return false;
		}
	}

	return true;
}

static bool set_page_state_range(uint64_t phys_addr,
				 uint64_t page_count,
				 enum plane_page_state expected,
				 enum plane_page_state next)
{
	if (!page_state_range_matches(phys_addr, page_count, expected)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		uint64_t offset;
		uint64_t page_phys;
		struct plane_page *page;

		if (!checked_mul_u64(i, PAGE_SIZE, &offset) ||
		    !checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_pmm_phys_to_page(page_phys);

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

	if (!checked_mul_u64(pmm_stats.allocator.managed_pages, sizeof(page_pool[0]),
			     &metadata_bytes) ||
	    !checked_align_up(metadata_bytes, PAGE_SIZE, &metadata_size)) {
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

	page_pool = hal_mmu_direct_phys_to_virt(metadata_phys_base);
	if (page_pool == NULL) {
		return false;
	}

	pmm_stats.allocator.metadata_bytes = metadata_bytes;
	pmm_stats.allocator.metadata_pages = metadata_pages;

	for (uint64_t i = 0; i < managed_range_count; i++) {
		for (uint64_t j = 0; j < managed_ranges[i].page_count; j++) {
			uint64_t phys;
			uint64_t page_index;

			if (!checked_mul_u64(j, PAGE_SIZE, &phys) ||
			    !checked_add_u64(managed_ranges[i].base, phys, &phys) ||
			    !checked_add_u64(managed_ranges[i].page_index, j,
					     &page_index)) {
				return false;
			}

			page_pool[page_index].phys_addr = phys;
			page_pool[page_index].state = PLANE_PAGE_FREE;
			page_pool[page_index].free_prev = NULL;
			page_pool[page_index].free_next = NULL;
			page_pool[page_index].on_free_queue = false;
		}
	}

	if (!set_page_state_range(metadata_phys_base, metadata_pages,
				  PLANE_PAGE_FREE, PLANE_PAGE_METADATA)) {
		return false;
	}

	for (uint64_t i = 0; i < tracked_page_count; i++) {
		if (page_pool[i].state == PLANE_PAGE_FREE &&
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

		if (!checked_region_end(region->base, region->length, &end)) {
			return false;
		}

		if (region->type == PLANE_MEM_USABLE) {
			if (!checked_align_up(region->base, PAGE_SIZE, &start)) {
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
		if (!checked_align_up(end, PAGE_SIZE, &aligned_end)) {
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
	return managed_range_contains(phys_addr, page_count) &&
	       page_state_range_matches(phys_addr, page_count, PLANE_PAGE_FREE);
}

static bool alloc_flags_valid(uint32_t flags)
{
	return (flags & ~PLANE_PMM_ALLOC_ZERO) == 0;
}

static bool zero_allocated_pages(uint64_t phys_addr, uint64_t page_count)
{
	uint64_t size;
	void *vaddr;

	if (!checked_mul_u64(page_count, PAGE_SIZE, &size)) {
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
	if (!page_state_range_matches(phys_addr, page_count,
				      PLANE_PAGE_ALLOCATED)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_phys;
		uint64_t offset;

		if (!checked_mul_u64(i, PAGE_SIZE, &offset) ||
		    !checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_pmm_phys_to_page(page_phys);
		if (page == NULL || page->on_free_queue) {
			return false;
		}

		page->state = PLANE_PAGE_FREE;
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

		if (!checked_add_u64(range_base_page,
				     managed_ranges[i].page_count,
				     &range_end_page) ||
		    !checked_align_up(range_base_page, alignment_pages,
				      &candidate_page)) {
			return false;
		}

		while (candidate_page <= range_end_page) {
			uint64_t candidate_end_page;
			uint64_t candidate_phys;

			if (!checked_add_u64(candidate_page, page_count,
					     &candidate_end_page)) {
				return false;
			}
			if (candidate_end_page > range_end_page) {
				break;
			}
			if (!checked_mul_u64(candidate_page, PAGE_SIZE,
					     &candidate_phys)) {
				return false;
			}

			if (page_range_is_free(candidate_phys, page_count)) {
				*phys_addr = candidate_phys;
				return true;
			}

			if (!checked_add_u64(candidate_page, alignment_pages,
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

	page->state = PLANE_PAGE_ALLOCATED;
	*phys_addr = page->phys_addr;
	return true;
}

static bool plane_pmm_alloc_pages_phys_raw(uint64_t page_count,
					   uint64_t alignment_pages,
					   uint64_t *phys_addr)
{
	uint64_t alloc_base;

	if (phys_addr == NULL || page_count == 0 ||
	    !is_power_of_two(alignment_pages)) {
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

		if (!checked_mul_u64(i, PAGE_SIZE, &offset) ||
		    !checked_add_u64(alloc_base, offset, &page_phys)) {
			return false;
		}

		page = plane_pmm_phys_to_page(page_phys);
		if (!free_queue_remove(page)) {
			return false;
		}
		page->state = PLANE_PAGE_ALLOCATED;
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

bool plane_pmm_alloc_page_flags(uint32_t flags, struct plane_page **page)
{
	uint64_t phys_addr;

	if (page == NULL ||
	    !plane_pmm_alloc_pages_phys_flags(1, 1, flags, &phys_addr)) {
		return false;
	}

	*page = plane_pmm_phys_to_page(phys_addr);
	if (*page == NULL) {
		if (!plane_pmm_free_page_phys(phys_addr)) {
			return false;
		}
		return false;
	}

	return true;
}

bool plane_pmm_alloc_page(struct plane_page **page)
{
	return plane_pmm_alloc_page_flags(0, page);
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
	    !page_state_range_matches(phys_addr, page_count,
				      PLANE_PAGE_ALLOCATED)) {
		return false;
	}

	if (!set_page_state_range(phys_addr, page_count,
				  PLANE_PAGE_ALLOCATED,
				  PLANE_PAGE_FREE)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_phys;
		uint64_t offset;

		if (!checked_mul_u64(i, PAGE_SIZE, &offset) ||
		    !checked_add_u64(phys_addr, offset, &page_phys)) {
			return false;
		}

		page = plane_pmm_phys_to_page(page_phys);
		if (!free_queue_insert_ordered(page)) {
			return false;
		}
	}

	return true;
}

bool plane_pmm_free_page(struct plane_page *page)
{
	uint64_t phys_addr = plane_page_phys(page);

	if (phys_addr == UINT64_MAX) {
		return false;
	}

	return plane_pmm_free_page_phys(phys_addr);
}

bool plane_pmm_free_page_phys(uint64_t phys_addr)
{
	return plane_pmm_free_pages_phys(phys_addr, 1);
}

struct plane_pmm_stats plane_pmm_get_stats(void)
{
	struct plane_pmm_stats stats = pmm_stats;
	uint64_t free_run_count = 0;
	bool in_free_run = false;

	for (uint64_t i = 0; i < managed_range_count; i++) {
		for (uint64_t j = 0; j < managed_ranges[i].page_count; j++) {
			struct plane_page *page =
				&page_pool[managed_ranges[i].page_index + j];

			if (page->state == PLANE_PAGE_FREE) {
				if (!in_free_run) {
					free_run_count++;
					in_free_run = true;
				}
			} else {
				in_free_run = false;
			}
		}
		in_free_run = false;
	}

	stats.allocator.free_pages = free_queue.count;
	stats.allocator.free_run_count = free_run_count;
	return stats;
}
