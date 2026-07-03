#include <stddef.h>

#include <plane/mm.h>
#include <plane/util.h>
#include <plane/vm_map.h>

#define PLANE_KERNEL_MAP_MAX_FREE_RANGES 64
#define PLANE_KERNEL_MAP_MAX_ALLOCATIONS 128

struct kernel_map_free_range {
	uint64_t base;
	uint64_t page_count;
};

struct kernel_map_allocation {
	uint64_t base;
	uint64_t page_count;
	bool used;
};

static struct kernel_map_free_range
	free_ranges[PLANE_KERNEL_MAP_MAX_FREE_RANGES];
static uint64_t free_range_count;
static struct kernel_map_allocation
	allocations[PLANE_KERNEL_MAP_MAX_ALLOCATIONS];
static uint64_t allocation_count;
static struct plane_vm_map_stats kernel_map_stats;
static bool kernel_map_initialized;

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
		struct kernel_map_free_range *range = &free_ranges[i];
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

		kernel_map_stats.free_pages -= page_count;
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
		kernel_map_stats.free_pages += page_count;
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
		kernel_map_stats.free_pages += page_count;
		return true;
	}

	if (free_range_count >= PLANE_KERNEL_MAP_MAX_FREE_RANGES) {
		return false;
	}

	for (uint64_t i = free_range_count; i > index; i--) {
		free_ranges[i] = free_ranges[i - 1];
	}

	free_ranges[index].base = vaddr;
	free_ranges[index].page_count = page_count;
	free_range_count++;
	kernel_map_stats.free_pages += page_count;
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
	allocations[index] = (struct kernel_map_allocation){0};
	allocation_count--;
}

bool plane_kernel_map_init(uint64_t base, uint64_t size)
{
	uint64_t end;

	free_range_count = 0;
	allocation_count = 0;
	kernel_map_stats = (struct plane_vm_map_stats){0};
	kernel_map_initialized = false;

	for (uint64_t i = 0; i < ARRAY_SIZE(allocations); i++) {
		allocations[i] = (struct kernel_map_allocation){0};
	}

	if (size == 0 ||
	    !checked_add_u64(base, size, &end) ||
	    !is_page_aligned(base) ||
	    !is_page_aligned(size)) {
		return false;
	}

	free_ranges[0].base = base;
	free_ranges[0].page_count = size / PAGE_SIZE;
	free_range_count = 1;

	kernel_map_stats.total_pages = free_ranges[0].page_count;
	kernel_map_stats.free_pages = free_ranges[0].page_count;
	kernel_map_initialized = true;
	return true;
}

bool plane_kernel_map_alloc_pages(uint64_t page_count, uint64_t *vaddr)
{
	int64_t record_index;
	uint64_t base;

	if (vaddr == NULL || !kernel_map_initialized || page_count == 0) {
		return false;
	}

	record_index = find_free_allocation_record();
	if (record_index < 0 ||
	    !reserve_vaddr_range(page_count, &base)) {
		return false;
	}

	add_allocation_record((uint64_t)record_index, base, page_count);
	*vaddr = base;
	return true;
}

bool plane_kernel_map_has_allocation(uint64_t vaddr, uint64_t page_count)
{
	if (!kernel_map_initialized ||
	    page_count == 0 ||
	    !is_page_aligned(vaddr)) {
		return false;
	}

	return find_allocation_record(vaddr, page_count) >= 0;
}

bool plane_kernel_map_free_pages(uint64_t vaddr, uint64_t page_count)
{
	int64_t record_index;

	if (!kernel_map_initialized ||
	    page_count == 0 ||
	    !is_page_aligned(vaddr)) {
		return false;
	}

	record_index = find_allocation_record(vaddr, page_count);
	if (record_index < 0) {
		return false;
	}

	remove_allocation_record((uint64_t)record_index);
	return release_vaddr_range(vaddr, page_count);
}

struct plane_vm_map_stats plane_kernel_map_get_stats(void)
{
	struct plane_vm_map_stats stats = kernel_map_stats;

	stats.allocated_pages = stats.total_pages - stats.free_pages;
	stats.free_range_count = free_range_count;
	stats.allocation_count = allocation_count;
	return stats;
}
