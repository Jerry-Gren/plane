#include <stddef.h>

#include <hal/mmu.h>

#include <plane/kmem.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/util.h>

#define PLANE_KMEM_MAX_FREE_RANGES 64
#define PLANE_KMEM_MAX_ALLOCATIONS 128

struct kmem_free_range {
	uint64_t base;
	uint64_t page_count;
};

struct kmem_allocation {
	uint64_t base;
	uint64_t page_count;
	bool used;
};

static struct kmem_free_range free_ranges[PLANE_KMEM_MAX_FREE_RANGES];
static uint64_t free_range_count;
static struct kmem_allocation allocations[PLANE_KMEM_MAX_ALLOCATIONS];
static uint64_t allocation_count;
static struct plane_kmem_stats kmem_stats;
static bool kmem_initialized;

static bool is_page_aligned(uint64_t value)
{
	return (value & (PAGE_SIZE - 1)) == 0;
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

static bool checked_page_offset(uint64_t page_index, uint64_t *offset)
{
	return checked_mul_u64(page_index, PAGE_SIZE, offset);
}

static bool kmem_flags_valid(uint32_t flags)
{
	return (flags & ~PLANE_KMEM_ALLOC_ZERO) == 0;
}

static uint32_t kmem_to_pmm_flags(uint32_t flags)
{
	uint32_t pmm_flags = 0;

	if ((flags & PLANE_KMEM_ALLOC_ZERO) != 0) {
		pmm_flags |= PLANE_PMM_ALLOC_ZERO;
	}

	return pmm_flags;
}

static void remove_free_range(uint64_t index)
{
	for (uint64_t i = index + 1; i < free_range_count; i++) {
		free_ranges[i - 1] = free_ranges[i];
	}
	free_range_count--;
}

static bool reserve_vaddr_range(uint64_t page_count, uint64_t *vaddr)
{
	uint64_t size;

	if (!checked_mul_u64(page_count, PAGE_SIZE, &size)) {
		return false;
	}

	for (uint64_t i = 0; i < free_range_count; i++) {
		struct kmem_free_range *range = &free_ranges[i];
		uint64_t base = range->base;

		if (range->page_count < page_count) {
			continue;
		}

		if (!checked_add_u64(range->base, size, &range->base)) {
			return false;
		}
		range->page_count -= page_count;
		if (range->page_count == 0) {
			remove_free_range(i);
		}

		kmem_stats.free_pages -= page_count;
		*vaddr = base;
		return true;
	}

	return false;
}

static bool free_ranges_can_merge(uint64_t lhs, uint64_t rhs)
{
	uint64_t end;
	uint64_t size;

	if (!checked_mul_u64(free_ranges[lhs].page_count, PAGE_SIZE, &size) ||
	    !checked_add_u64(free_ranges[lhs].base, size, &end)) {
		return false;
	}

	return end == free_ranges[rhs].base;
}

static bool merge_free_neighbors(uint64_t index)
{
	if (index > 0 && free_ranges_can_merge(index - 1, index)) {
		free_ranges[index - 1].page_count += free_ranges[index].page_count;
		remove_free_range(index);
		index--;
	}

	if (index + 1 < free_range_count &&
	    free_ranges_can_merge(index, index + 1)) {
		free_ranges[index].page_count += free_ranges[index + 1].page_count;
		remove_free_range(index + 1);
	}

	return true;
}

static bool release_vaddr_range(uint64_t vaddr, uint64_t page_count)
{
	uint64_t size;
	uint64_t index = 0;
	bool merge_prev = false;
	bool merge_next = false;

	while (index < free_range_count && free_ranges[index].base < vaddr) {
		index++;
	}

	if (index < free_range_count &&
	    free_ranges[index].base == vaddr) {
		return false;
	}

	if (!checked_mul_u64(page_count, PAGE_SIZE, &size)) {
		return false;
	}

	if (index > 0) {
		uint64_t prev_end;
		uint64_t prev_size;

		if (!checked_mul_u64(free_ranges[index - 1].page_count,
				     PAGE_SIZE, &prev_size) ||
		    !checked_add_u64(free_ranges[index - 1].base, prev_size,
				     &prev_end)) {
			return false;
		}
		merge_prev = prev_end == vaddr;
	}

	if (index < free_range_count) {
		uint64_t end;

		if (!checked_add_u64(vaddr, size, &end)) {
			return false;
		}
		merge_next = end == free_ranges[index].base;
	}

	if (merge_prev) {
		free_ranges[index - 1].page_count += page_count;
		kmem_stats.free_pages += page_count;
		if (merge_next) {
			free_ranges[index - 1].page_count +=
				free_ranges[index].page_count;
			remove_free_range(index);
		}
		return true;
	}

	if (merge_next) {
		free_ranges[index].base = vaddr;
		free_ranges[index].page_count += page_count;
		kmem_stats.free_pages += page_count;
		return true;
	}

	if (free_range_count >= PLANE_KMEM_MAX_FREE_RANGES) {
		return false;
	}

	for (uint64_t i = free_range_count; i > index; i--) {
		free_ranges[i] = free_ranges[i - 1];
	}

	free_ranges[index].base = vaddr;
	free_ranges[index].page_count = page_count;
	free_range_count++;
	kmem_stats.free_pages += page_count;
	return merge_free_neighbors(index);
}

static int64_t find_free_allocation_record(void)
{
	for (uint64_t i = 0; i < ARRAY_SIZE(allocations); i++) {
		if (!allocations[i].used) {
			return (int64_t)i;
		}
	}

	return -1;
}

static int64_t find_allocation_record(uint64_t vaddr, uint64_t page_count)
{
	for (uint64_t i = 0; i < ARRAY_SIZE(allocations); i++) {
		if (allocations[i].used &&
		    allocations[i].base == vaddr &&
		    allocations[i].page_count == page_count) {
			return (int64_t)i;
		}
	}

	return -1;
}

static void add_allocation_record(uint64_t index,
				  uint64_t vaddr,
				  uint64_t page_count)
{
	allocations[index].base = vaddr;
	allocations[index].page_count = page_count;
	allocations[index].used = true;
	allocation_count++;
}

static void remove_allocation_record(uint64_t index)
{
	allocations[index] = (struct kmem_allocation){0};
	allocation_count--;
}

static bool rollback_mapped_pages(uint64_t vaddr, uint64_t page_count)
{
	for (uint64_t i = page_count; i > 0; i--) {
		uint64_t page_vaddr;
		uint64_t phys_addr;
		uint64_t offset;

		if (!checked_page_offset(i - 1, &offset) ||
		    !checked_add_u64(vaddr, offset, &page_vaddr) ||
		    !hal_mmu_translate_kernel_page(page_vaddr, &phys_addr) ||
		    !hal_mmu_unmap_kernel_page(page_vaddr) ||
		    !plane_pmm_free_page_phys(phys_addr)) {
			return false;
		}
	}

	return true;
}

static bool rollback_allocated_page(uint64_t phys_addr)
{
	return plane_pmm_free_page_phys(phys_addr);
}

static bool map_allocated_pages(uint64_t vaddr,
				uint64_t page_count,
				uint32_t flags)
{
	uint32_t pmm_flags = kmem_to_pmm_flags(flags);
	uint64_t mapped_pages = 0;

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_vaddr;
		uint64_t phys_addr;
		uint64_t offset;

		if (!checked_page_offset(i, &offset) ||
		    !checked_add_u64(vaddr, offset, &page_vaddr)) {
			if (!rollback_mapped_pages(vaddr, mapped_pages)) {
				return false;
			}
			return false;
		}

		if (!plane_pmm_alloc_page_flags(pmm_flags, &page)) {
			if (!rollback_mapped_pages(vaddr, mapped_pages)) {
				return false;
			}
			return false;
		}

		phys_addr = plane_page_phys(page);
		if (phys_addr == UINT64_MAX ||
		    !hal_mmu_map_kernel_page(page_vaddr, phys_addr,
					     HAL_MMU_MAP_WRITE)) {
			if (!rollback_allocated_page(phys_addr) ||
			    !rollback_mapped_pages(vaddr, mapped_pages)) {
				return false;
			}
			return false;
		}

		mapped_pages++;
	}

	return true;
}

bool plane_kmem_init(void)
{
	uint64_t base;
	uint64_t size;
	uint64_t end;

	free_range_count = 0;
	allocation_count = 0;
	kmem_stats = (struct plane_kmem_stats){0};
	kmem_initialized = false;

	for (uint64_t i = 0; i < ARRAY_SIZE(allocations); i++) {
		allocations[i] = (struct kmem_allocation){0};
	}

	if (!hal_mmu_kernel_vma_range(&base, &size) ||
	    size == 0 ||
	    !checked_add_u64(base, size, &end) ||
	    !is_page_aligned(base) ||
	    !is_page_aligned(size)) {
		return false;
	}

	free_ranges[0].base = base;
	free_ranges[0].page_count = size / PAGE_SIZE;
	free_range_count = 1;

	kmem_stats.total_pages = free_ranges[0].page_count;
	kmem_stats.free_pages = free_ranges[0].page_count;
	kmem_initialized = true;
	return true;
}

bool plane_kmem_alloc_pages(uint64_t page_count, uint32_t flags, void **vaddr)
{
	int64_t record_index;
	uint64_t base;

	if (vaddr == NULL ||
	    !kmem_initialized ||
	    page_count == 0 ||
	    !kmem_flags_valid(flags)) {
		return false;
	}

	record_index = find_free_allocation_record();
	if (record_index < 0 ||
	    !reserve_vaddr_range(page_count, &base)) {
		return false;
	}

	if (!map_allocated_pages(base, page_count, flags)) {
		if (!release_vaddr_range(base, page_count)) {
			return false;
		}
		return false;
	}

	add_allocation_record((uint64_t)record_index, base, page_count);
	*vaddr = (void *)(uintptr_t)base;
	return true;
}

bool plane_kmem_free_pages(void *vaddr, uint64_t page_count)
{
	uint64_t addr = (uint64_t)(uintptr_t)vaddr;
	int64_t record_index;

	if (!kmem_initialized ||
	    vaddr == NULL ||
	    page_count == 0 ||
	    !is_page_aligned(addr)) {
		return false;
	}

	record_index = find_allocation_record(addr, page_count);
	if (record_index < 0) {
		return false;
	}

	if (!rollback_mapped_pages(addr, page_count)) {
		return false;
	}

	remove_allocation_record((uint64_t)record_index);
	return release_vaddr_range(addr, page_count);
}

struct plane_kmem_stats plane_kmem_get_stats(void)
{
	struct plane_kmem_stats stats = kmem_stats;

	stats.allocated_pages = stats.total_pages - stats.free_pages;
	stats.free_range_count = free_range_count;
	stats.allocation_count = allocation_count;
	return stats;
}
