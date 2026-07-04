#include <stddef.h>

#include <plane/mm.h>
#include <plane/util.h>
#include <plane/vm_map.h>

#define VM_MAP_ENTRY_NONE UINT64_MAX

static uint64_t page_count_from_size(uint64_t size)
{
	return size / PAGE_SIZE;
}

static bool prot_valid(uint32_t prot)
{
	return prot != PLANE_VM_PROT_NONE &&
	       (prot & ~PLANE_VM_PROT_ALL) == 0;
}

static bool prot_allowed(uint32_t prot, uint32_t max_prot)
{
	return prot_valid(prot) && prot_valid(max_prot) &&
	       (prot & ~max_prot) == 0;
}

static void reset_entries(struct plane_vm_map_entry *entries,
			  uint64_t entry_capacity)
{
	for (uint64_t i = 0; i < entry_capacity; i++) {
		entries[i] = (struct plane_vm_map_entry){0};
		entries[i].prev = VM_MAP_ENTRY_NONE;
		entries[i].next = VM_MAP_ENTRY_NONE;
	}
}

static void reset_map(struct plane_vm_map *map,
		      struct plane_vm_map_entry *entries,
		      uint64_t entry_capacity)
{
	*map = (struct plane_vm_map){
		.head = VM_MAP_ENTRY_NONE,
		.tail = VM_MAP_ENTRY_NONE,
		.entry_capacity = entry_capacity,
		.entries = entries,
	};
	reset_entries(entries, entry_capacity);
}

static int64_t alloc_entry_index(struct plane_vm_map *map)
{
	for (uint64_t i = 0; i < map->entry_capacity; i++) {
		if (!map->entries[i].used) {
			return (int64_t)i;
		}
	}

	return -1;
}

static void insert_entry(struct plane_vm_map *map,
			 uint64_t index,
			 uint64_t start,
			 uint64_t end,
			 uint64_t user_start,
			 uint64_t user_end,
			 struct plane_vm_object *object,
			 uint64_t object_offset,
			 uint32_t prot,
			 uint32_t max_prot,
			 uint64_t prev,
			 uint64_t next)
{
	map->entries[index].start = start;
	map->entries[index].end = end;
	map->entries[index].user_start = user_start;
	map->entries[index].user_end = user_end;
	map->entries[index].object = object;
	map->entries[index].object_offset = object_offset;
	map->entries[index].prot = prot;
	map->entries[index].max_prot = max_prot;
	map->entries[index].prev = prev;
	map->entries[index].next = next;
	map->entries[index].used = true;

	if (prev != VM_MAP_ENTRY_NONE) {
		map->entries[prev].next = index;
	} else {
		map->head = index;
	}

	if (next != VM_MAP_ENTRY_NONE) {
		map->entries[next].prev = index;
	} else {
		map->tail = index;
	}

	map->entry_count++;
}

static void remove_entry(struct plane_vm_map *map, uint64_t index)
{
	uint64_t prev = map->entries[index].prev;
	uint64_t next = map->entries[index].next;

	if (prev != VM_MAP_ENTRY_NONE) {
		map->entries[prev].next = next;
	} else {
		map->head = next;
	}

	if (next != VM_MAP_ENTRY_NONE) {
		map->entries[next].prev = prev;
	} else {
		map->tail = prev;
	}

	map->entries[index] = (struct plane_vm_map_entry){0};
	map->entries[index].prev = VM_MAP_ENTRY_NONE;
	map->entries[index].next = VM_MAP_ENTRY_NONE;
	map->entry_count--;
}

static bool hole_can_fit(uint64_t start, uint64_t end, uint64_t size)
{
	return end >= start && end - start >= size;
}

static bool find_first_fit(struct plane_vm_map *map,
			   uint64_t size,
			   uint64_t *start,
			   uint64_t *prev,
			   uint64_t *next)
{
	uint64_t cursor = map->base;
	uint64_t current = map->head;
	uint64_t previous = VM_MAP_ENTRY_NONE;

	while (current != VM_MAP_ENTRY_NONE) {
		if (hole_can_fit(cursor, map->entries[current].start, size)) {
			*start = cursor;
			*prev = previous;
			*next = current;
			return true;
		}
		cursor = map->entries[current].end;
		previous = current;
		current = map->entries[current].next;
	}

	if (hole_can_fit(cursor, map->end, size)) {
		*start = cursor;
		*prev = previous;
		*next = VM_MAP_ENTRY_NONE;
		return true;
	}

	return false;
}

static int64_t find_exact_entry(struct plane_vm_map *map,
				uint64_t vaddr,
				uint64_t page_count)
{
	uint64_t size;
	uint64_t end;
	uint64_t current;

	if (!plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(vaddr, size, &end)) {
		return -1;
	}

	current = map->head;
	while (current != VM_MAP_ENTRY_NONE) {
		if (map->entries[current].user_start == vaddr &&
		    map->entries[current].user_end == end) {
			return (int64_t)current;
		}
		current = map->entries[current].next;
	}

	return -1;
}

static uint64_t reserved_pages(struct plane_vm_map *map)
{
	uint64_t pages = 0;
	uint64_t current = map->head;

	while (current != VM_MAP_ENTRY_NONE) {
		pages += page_count_from_size(map->entries[current].end -
					      map->entries[current].start);
		current = map->entries[current].next;
	}

	return pages;
}

static uint64_t user_pages(struct plane_vm_map *map)
{
	uint64_t pages = 0;
	uint64_t current = map->head;

	while (current != VM_MAP_ENTRY_NONE) {
		pages += page_count_from_size(map->entries[current].user_end -
					      map->entries[current].user_start);
		current = map->entries[current].next;
	}

	return pages;
}

static uint64_t free_range_count(struct plane_vm_map *map)
{
	uint64_t count = 0;
	uint64_t cursor = map->base;
	uint64_t current = map->head;

	while (current != VM_MAP_ENTRY_NONE) {
		if (map->entries[current].start > cursor) {
			count++;
		}
		cursor = map->entries[current].end;
		current = map->entries[current].next;
	}

	if (map->end > cursor) {
		count++;
	}

	return count;
}

bool plane_vm_map_init(struct plane_vm_map *map,
		       struct plane_vm_map_entry *entries,
		       uint64_t entry_capacity,
		       uint64_t base,
		       uint64_t size)
{
	uint64_t end;

	if (map == NULL ||
	    entries == NULL ||
	    entry_capacity == 0 ||
	    map->initialized ||
	    size == 0 ||
	    !plane_checked_add_u64(base, size, &end) ||
	    !plane_is_page_aligned(base) ||
	    !plane_is_page_aligned(size)) {
		return false;
	}

	reset_map(map, entries, entry_capacity);
	map->base = base;
	map->end = end;
	map->initialized = true;
	return true;
}

bool plane_vm_map_alloc_pages(struct plane_vm_map *map,
			      uint64_t page_count,
			      uint64_t *vaddr)
{
	return plane_vm_map_alloc_pages_protected(
		map, page_count, 0, PLANE_VM_PROT_DEFAULT, vaddr);
}

bool plane_vm_map_alloc_pages_protected(struct plane_vm_map *map,
					uint64_t page_count,
					uint64_t guard_pages,
					uint32_t prot,
					uint64_t *vaddr)
{
	return plane_vm_map_alloc_pages_protected_max(
		map, page_count, guard_pages, prot, PLANE_VM_PROT_ALL, vaddr);
}

bool plane_vm_map_alloc_pages_protected_max(struct plane_vm_map *map,
					    uint64_t page_count,
					    uint64_t guard_pages,
					    uint32_t prot,
					    uint32_t max_prot,
					    uint64_t *vaddr)
{
	return plane_vm_map_alloc_pages_object(
		map, page_count, guard_pages, NULL, 0, prot, max_prot, vaddr);
}

bool plane_vm_map_alloc_pages_object(struct plane_vm_map *map,
				     uint64_t page_count,
				     uint64_t guard_pages,
				     struct plane_vm_object *object,
				     uint64_t object_offset,
				     uint32_t prot,
				     uint32_t max_prot,
				     uint64_t *vaddr)
{
	int64_t entry_index;
	uint64_t guard_total;
	uint64_t total_pages;
	uint64_t size;
	uint64_t start;
	uint64_t prev;
	uint64_t next;
	uint64_t end;
	uint64_t guard_size;
	uint64_t user_start;
	uint64_t user_size;
	uint64_t user_end;
	uint64_t entry_object_offset = object_offset;

	if (vaddr == NULL ||
	    map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    (object == NULL && object_offset != 0) ||
	    (object != NULL &&
	     object_offset != PLANE_VM_MAP_OBJECT_OFFSET_AUTO &&
	     !plane_is_page_aligned(object_offset)) ||
	    !prot_allowed(prot, max_prot)) {
		return false;
	}

	entry_index = alloc_entry_index(map);
	if (!plane_checked_mul_u64(guard_pages, 2, &guard_total) ||
	    !plane_checked_add_u64(page_count, guard_total, &total_pages)) {
		return false;
	}

	if (entry_index < 0 ||
	    !plane_checked_page_offset(total_pages, &size) ||
	    !find_first_fit(map, size, &start, &prev, &next) ||
	    !plane_checked_add_u64(start, size, &end) ||
	    !plane_checked_page_offset(guard_pages, &guard_size) ||
	    !plane_checked_add_u64(start, guard_size, &user_start) ||
	    !plane_checked_page_offset(page_count, &user_size) ||
	    !plane_checked_add_u64(user_start, user_size, &user_end)) {
		return false;
	}

	if (object != NULL &&
	    object_offset == PLANE_VM_MAP_OBJECT_OFFSET_AUTO) {
		entry_object_offset = user_start;
	}

	insert_entry(map, (uint64_t)entry_index, start, end, user_start, user_end,
		     object, entry_object_offset, prot, max_prot, prev, next);
	*vaddr = user_start;
	return true;
}

bool plane_vm_map_lookup_allocation(
	struct plane_vm_map *map,
	uint64_t vaddr,
	uint64_t page_count,
	struct plane_vm_map_allocation_info *info)
{
	int64_t entry_index;

	if (map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    !plane_is_page_aligned(vaddr)) {
		return false;
	}

	entry_index = find_exact_entry(map, vaddr, page_count);
	if (entry_index < 0) {
		return false;
	}

	if (info != NULL) {
		struct plane_vm_map_entry *entry = &map->entries[entry_index];

		info->reserved_start = entry->start;
		info->reserved_pages = page_count_from_size(entry->end -
							    entry->start);
		info->user_start = entry->user_start;
		info->user_pages = page_count_from_size(entry->user_end -
							entry->user_start);
		info->object = entry->object;
		info->object_offset = entry->object_offset;
		info->wired_count = entry->wired_count;
		info->prot = entry->prot;
		info->max_prot = entry->max_prot;
	}

	return true;
}

bool plane_vm_map_protect_pages(struct plane_vm_map *map,
				uint64_t vaddr,
				uint64_t page_count,
				uint32_t prot)
{
	int64_t entry_index;

	if (map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    !plane_is_page_aligned(vaddr) ||
	    !prot_valid(prot)) {
		return false;
	}

	entry_index = find_exact_entry(map, vaddr, page_count);
	if (entry_index < 0 ||
	    (prot & ~map->entries[entry_index].max_prot) != 0) {
		return false;
	}

	map->entries[entry_index].prot = prot;
	return true;
}

bool plane_vm_map_wire_pages(struct plane_vm_map *map,
			     uint64_t vaddr,
			     uint64_t page_count)
{
	int64_t entry_index;

	if (map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    !plane_is_page_aligned(vaddr)) {
		return false;
	}

	entry_index = find_exact_entry(map, vaddr, page_count);
	if (entry_index < 0 ||
	    map->entries[entry_index].wired_count == UINT64_MAX) {
		return false;
	}

	map->entries[entry_index].wired_count++;
	return true;
}

bool plane_vm_map_unwire_pages(struct plane_vm_map *map,
			       uint64_t vaddr,
			       uint64_t page_count)
{
	int64_t entry_index;

	if (map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    !plane_is_page_aligned(vaddr)) {
		return false;
	}

	entry_index = find_exact_entry(map, vaddr, page_count);
	if (entry_index < 0 ||
	    map->entries[entry_index].wired_count == 0) {
		return false;
	}

	map->entries[entry_index].wired_count--;
	return true;
}

bool plane_vm_map_free_pages(struct plane_vm_map *map,
			     uint64_t vaddr,
			     uint64_t page_count)
{
	int64_t entry_index;

	if (map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    !plane_is_page_aligned(vaddr)) {
		return false;
	}

	entry_index = find_exact_entry(map, vaddr, page_count);
	if (entry_index < 0 ||
	    map->entries[entry_index].wired_count != 0) {
		return false;
	}

	remove_entry(map, (uint64_t)entry_index);
	return true;
}

struct plane_vm_map_stats plane_vm_map_get_stats(struct plane_vm_map *map)
{
	struct plane_vm_map_stats stats = {0};

	if (map == NULL || !map->initialized) {
		return stats;
	}

	stats.total_pages = page_count_from_size(map->end - map->base);
	stats.reserved_pages = reserved_pages(map);
	stats.user_pages = user_pages(map);
	stats.free_pages = stats.total_pages - stats.reserved_pages;
	stats.free_range_count = free_range_count(map);
	stats.allocation_count = map->entry_count;
	return stats;
}
