#include <stddef.h>

#include <plane/compiler.h>
#include <plane/mm.h>
#include <plane/util.h>
#include <plane/vm_map.h>

#define PLANE_KERNEL_MAP_MAX_ENTRIES 128
#define VM_MAP_ENTRY_NONE UINT64_MAX

struct plane_vm_map_entry {
	uint64_t start;
	uint64_t end;
	uint64_t prev;
	uint64_t next;
	bool used;
};

struct plane_vm_map {
	uint64_t base;
	uint64_t end;
	uint64_t head;
	uint64_t tail;
	uint64_t entry_count;
	bool initialized;
};

static struct plane_vm_map_entry entries[PLANE_KERNEL_MAP_MAX_ENTRIES];
static struct plane_vm_map kernel_map;

/* Weak host-test seam; production init is one-shot. */
__weak bool plane_vm_map_test_reset_enabled(void)
{
	return false;
}

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

static uint64_t page_count_from_size(uint64_t size)
{
	return size / PAGE_SIZE;
}

static void reset_entries(void)
{
	for (uint64_t i = 0; i < ARRAY_SIZE(entries); i++) {
		entries[i] = (struct plane_vm_map_entry){0};
		entries[i].prev = VM_MAP_ENTRY_NONE;
		entries[i].next = VM_MAP_ENTRY_NONE;
	}
}

static void reset_kernel_map(void)
{
	kernel_map = (struct plane_vm_map){
		.head = VM_MAP_ENTRY_NONE,
		.tail = VM_MAP_ENTRY_NONE,
	};
	reset_entries();
}

static int64_t alloc_entry_index(void)
{
	for (uint64_t i = 0; i < ARRAY_SIZE(entries); i++) {
		if (!entries[i].used) {
			return (int64_t)i;
		}
	}

	return -1;
}

static void insert_entry(uint64_t index,
			 uint64_t start,
			 uint64_t end,
			 uint64_t prev,
			 uint64_t next)
{
	entries[index].start = start;
	entries[index].end = end;
	entries[index].prev = prev;
	entries[index].next = next;
	entries[index].used = true;

	if (prev != VM_MAP_ENTRY_NONE) {
		entries[prev].next = index;
	} else {
		kernel_map.head = index;
	}

	if (next != VM_MAP_ENTRY_NONE) {
		entries[next].prev = index;
	} else {
		kernel_map.tail = index;
	}

	kernel_map.entry_count++;
}

static void remove_entry(uint64_t index)
{
	uint64_t prev = entries[index].prev;
	uint64_t next = entries[index].next;

	if (prev != VM_MAP_ENTRY_NONE) {
		entries[prev].next = next;
	} else {
		kernel_map.head = next;
	}

	if (next != VM_MAP_ENTRY_NONE) {
		entries[next].prev = prev;
	} else {
		kernel_map.tail = prev;
	}

	entries[index] = (struct plane_vm_map_entry){0};
	entries[index].prev = VM_MAP_ENTRY_NONE;
	entries[index].next = VM_MAP_ENTRY_NONE;
	kernel_map.entry_count--;
}

static bool hole_can_fit(uint64_t start, uint64_t end, uint64_t size)
{
	return end >= start && end - start >= size;
}

static bool find_first_fit(uint64_t size,
			   uint64_t *start,
			   uint64_t *prev,
			   uint64_t *next)
{
	uint64_t cursor = kernel_map.base;
	uint64_t current = kernel_map.head;
	uint64_t previous = VM_MAP_ENTRY_NONE;

	while (current != VM_MAP_ENTRY_NONE) {
		if (hole_can_fit(cursor, entries[current].start, size)) {
			*start = cursor;
			*prev = previous;
			*next = current;
			return true;
		}
		cursor = entries[current].end;
		previous = current;
		current = entries[current].next;
	}

	if (hole_can_fit(cursor, kernel_map.end, size)) {
		*start = cursor;
		*prev = previous;
		*next = VM_MAP_ENTRY_NONE;
		return true;
	}

	return false;
}

static int64_t find_exact_entry(uint64_t vaddr, uint64_t page_count)
{
	uint64_t size;
	uint64_t end;
	uint64_t current;

	if (!checked_mul_u64(page_count, PAGE_SIZE, &size) ||
	    !checked_add_u64(vaddr, size, &end)) {
		return -1;
	}

	current = kernel_map.head;
	while (current != VM_MAP_ENTRY_NONE) {
		if (entries[current].start == vaddr &&
		    entries[current].end == end) {
			return (int64_t)current;
		}
		current = entries[current].next;
	}

	return -1;
}

static uint64_t allocated_pages(void)
{
	uint64_t pages = 0;
	uint64_t current = kernel_map.head;

	while (current != VM_MAP_ENTRY_NONE) {
		pages += page_count_from_size(entries[current].end -
					      entries[current].start);
		current = entries[current].next;
	}

	return pages;
}

static uint64_t free_range_count(void)
{
	uint64_t count = 0;
	uint64_t cursor = kernel_map.base;
	uint64_t current = kernel_map.head;

	while (current != VM_MAP_ENTRY_NONE) {
		if (entries[current].start > cursor) {
			count++;
		}
		cursor = entries[current].end;
		current = entries[current].next;
	}

	if (kernel_map.end > cursor) {
		count++;
	}

	return count;
}

bool plane_kernel_map_init(uint64_t base, uint64_t size)
{
	uint64_t end;

	if (kernel_map.initialized && !plane_vm_map_test_reset_enabled()) {
		return false;
	}
	if (size == 0 ||
	    !checked_add_u64(base, size, &end) ||
	    !is_page_aligned(base) ||
	    !is_page_aligned(size)) {
		return false;
	}

	reset_kernel_map();
	kernel_map.base = base;
	kernel_map.end = end;
	kernel_map.initialized = true;
	return true;
}

bool plane_kernel_map_alloc_pages(uint64_t page_count, uint64_t *vaddr)
{
	int64_t entry_index;
	uint64_t size;
	uint64_t start;
	uint64_t prev;
	uint64_t next;
	uint64_t end;

	if (vaddr == NULL || !kernel_map.initialized || page_count == 0) {
		return false;
	}

	entry_index = alloc_entry_index();
	if (entry_index < 0 ||
	    !checked_mul_u64(page_count, PAGE_SIZE, &size) ||
	    !find_first_fit(size, &start, &prev, &next) ||
	    !checked_add_u64(start, size, &end)) {
		return false;
	}

	insert_entry((uint64_t)entry_index, start, end, prev, next);
	*vaddr = start;
	return true;
}

bool plane_kernel_map_has_allocation(uint64_t vaddr, uint64_t page_count)
{
	if (!kernel_map.initialized ||
	    page_count == 0 ||
	    !is_page_aligned(vaddr)) {
		return false;
	}

	return find_exact_entry(vaddr, page_count) >= 0;
}

bool plane_kernel_map_free_pages(uint64_t vaddr, uint64_t page_count)
{
	int64_t entry_index;

	if (!kernel_map.initialized ||
	    page_count == 0 ||
	    !is_page_aligned(vaddr)) {
		return false;
	}

	entry_index = find_exact_entry(vaddr, page_count);
	if (entry_index < 0) {
		return false;
	}

	remove_entry((uint64_t)entry_index);
	return true;
}

struct plane_vm_map_stats plane_kernel_map_get_stats(void)
{
	struct plane_vm_map_stats stats = {0};

	if (!kernel_map.initialized) {
		return stats;
	}

	stats.total_pages = page_count_from_size(kernel_map.end -
						kernel_map.base);
	stats.allocated_pages = allocated_pages();
	stats.free_pages = stats.total_pages - stats.allocated_pages;
	stats.free_range_count = free_range_count();
	stats.allocation_count = kernel_map.entry_count;
	return stats;
}
