#include <stddef.h>

#include <plane/mm.h>
#include <plane/util.h>
#include <plane/vm_map.h>
#include <plane/vm_object.h>

#define VM_MAP_ENTRY_NONE UINT64_MAX

struct vm_map_delete_plan {
	uint64_t prev;
	uint64_t first;
	uint64_t stop;
	uint64_t count;
};

struct vm_map_zap {
	uint64_t head;
	uint64_t tail;
	uint64_t reusable;
	uint64_t count;
};

struct vm_map_range_plan {
	uint64_t first;
	uint64_t split_count;
};

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

static bool enter_flags_valid(uint32_t flags)
{
	bool anywhere = (flags & PLANE_VM_MAP_ENTER_ANYWHERE) != 0;
	bool fixed = (flags & PLANE_VM_MAP_ENTER_FIXED) != 0;
	bool overwrite = (flags & PLANE_VM_MAP_ENTER_OVERWRITE) != 0;

	if ((flags & ~(PLANE_VM_MAP_ENTER_ANYWHERE |
		       PLANE_VM_MAP_ENTER_FIXED |
		       PLANE_VM_MAP_ENTER_OVERWRITE)) != 0) {
		return false;
	}

	return anywhere != fixed && (!overwrite || fixed);
}

static bool object_range_valid(const struct plane_vm_object *object,
			       uint64_t object_offset,
			       uint64_t page_count)
{
	uint64_t offset_limit = plane_vm_object_offset_limit(object);
	uint64_t size;
	uint64_t end;

	if (offset_limit == 0 ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(object_offset, size, &end)) {
		return false;
	}

	return end <= offset_limit;
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

static uint64_t available_entry_count(const struct plane_vm_map *map)
{
	return map->entry_capacity - map->entry_count;
}

static bool object_can_reference_count(struct plane_vm_object *object,
				       uint64_t count)
{
	if (object == NULL || count == 0) {
		return true;
	}

	return object->initialized &&
	       object->alive &&
	       object->ref_count <= UINT64_MAX - count;
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

static bool range_overlaps_entry(const struct plane_vm_map_entry *entry,
				 uint64_t start,
				 uint64_t end)
{
	return entry->start < end && start < entry->end;
}

static bool entry_has_guards(const struct plane_vm_map_entry *entry)
{
	return entry->start != entry->user_start ||
	       entry->end != entry->user_end;
}

static bool entry_contains_user_addr(const struct plane_vm_map_entry *entry,
				     uint64_t addr)
{
	return entry->user_start <= addr && addr < entry->user_end;
}

static int64_t find_user_entry(struct plane_vm_map *map, uint64_t addr)
{
	uint64_t current = map->head;

	while (current != VM_MAP_ENTRY_NONE) {
		if (entry_contains_user_addr(&map->entries[current], addr)) {
			return (int64_t)current;
		}
		current = map->entries[current].next;
	}

	return -1;
}

static bool account_split_ref(struct plane_vm_map_entry *entry,
			      uint64_t *split_count)
{
	if (*split_count == UINT64_MAX) {
		return false;
	}

	(*split_count)++;
	return object_can_reference_count(entry->object, *split_count);
}

static bool clip_entry_start(struct plane_vm_map *map,
			     uint64_t index,
			     uint64_t start)
{
	struct plane_vm_map_entry *entry = &map->entries[index];
	struct plane_vm_map_entry *new_entry;
	int64_t new_index;
	uint64_t old_user_start;

	if (start <= entry->user_start) {
		return start == entry->user_start;
	}
	if (start >= entry->user_end) {
		return false;
	}

	new_index = alloc_entry_index(map);
	if (new_index < 0) {
		return false;
	}
	if (entry->object != NULL && !plane_vm_object_reference(entry->object)) {
		return false;
	}

	old_user_start = entry->user_start;
	new_entry = &map->entries[new_index];
	*new_entry = *entry;
	new_entry->end = start;
	new_entry->user_end = start;
	new_entry->prev = entry->prev;
	new_entry->next = index;
	new_entry->used = true;

	if (entry->prev != VM_MAP_ENTRY_NONE) {
		map->entries[entry->prev].next = (uint64_t)new_index;
	} else {
		map->head = (uint64_t)new_index;
	}

	entry->prev = (uint64_t)new_index;
	entry->start = start;
	entry->user_start = start;
	entry->object_offset += start - old_user_start;
	map->entry_count++;
	return true;
}

static bool clip_entry_end(struct plane_vm_map *map,
			   uint64_t index,
			   uint64_t end)
{
	struct plane_vm_map_entry *entry = &map->entries[index];
	struct plane_vm_map_entry *new_entry;
	int64_t new_index;
	uint64_t old_user_start;

	if (end >= entry->user_end) {
		return end == entry->user_end;
	}
	if (end <= entry->user_start) {
		return false;
	}

	new_index = alloc_entry_index(map);
	if (new_index < 0) {
		return false;
	}
	if (entry->object != NULL && !plane_vm_object_reference(entry->object)) {
		return false;
	}

	old_user_start = entry->user_start;
	new_entry = &map->entries[new_index];
	*new_entry = *entry;
	new_entry->start = end;
	new_entry->user_start = end;
	new_entry->object_offset += end - old_user_start;
	new_entry->prev = index;
	new_entry->next = entry->next;
	new_entry->used = true;

	if (entry->next != VM_MAP_ENTRY_NONE) {
		map->entries[entry->next].prev = (uint64_t)new_index;
	} else {
		map->tail = (uint64_t)new_index;
	}

	entry->next = (uint64_t)new_index;
	entry->end = end;
	entry->user_end = end;
	map->entry_count++;
	return true;
}

static bool plan_protect_user_range(struct plane_vm_map *map,
				    uint64_t start,
				    uint64_t end,
				    uint32_t prot,
				    struct vm_map_range_plan *plan)
{
	uint64_t cursor = start;
	uint64_t split_count = 0;

	*plan = (struct vm_map_range_plan){
		.first = VM_MAP_ENTRY_NONE,
		.split_count = 0,
	};

	while (cursor < end) {
		int64_t entry_index = find_user_entry(map, cursor);
		struct plane_vm_map_entry *entry;
		uint64_t segment_end;

		if (entry_index < 0) {
			return false;
		}

		entry = &map->entries[entry_index];
		if (cursor < entry->user_start ||
		    (prot & ~entry->max_prot) != 0) {
			return false;
		}

		if (plan->first == VM_MAP_ENTRY_NONE) {
			plan->first = (uint64_t)entry_index;
		}

		if (cursor > entry->user_start &&
		    !account_split_ref(entry, &split_count)) {
			return false;
		}

		segment_end = end < entry->user_end ? end : entry->user_end;
		if (segment_end < entry->user_end &&
		    !account_split_ref(entry, &split_count)) {
			return false;
		}

		cursor = segment_end;
	}

	if (split_count > available_entry_count(map)) {
		return false;
	}

	plan->split_count = split_count;
	return true;
}

static bool plan_delete_reserved_range(struct plane_vm_map *map,
				       uint64_t start,
				       uint64_t end,
				       struct vm_map_range_plan *plan)
{
	uint64_t current = map->head;
	uint64_t split_count = 0;

	*plan = (struct vm_map_range_plan){
		.first = VM_MAP_ENTRY_NONE,
		.split_count = 0,
	};

	while (current != VM_MAP_ENTRY_NONE &&
	       map->entries[current].end <= start) {
		current = map->entries[current].next;
	}

	while (current != VM_MAP_ENTRY_NONE &&
	       map->entries[current].start < end) {
		struct plane_vm_map_entry *entry = &map->entries[current];
		bool split_start = start > entry->start && start < entry->end;
		bool split_end = end > entry->start && end < entry->end;
		bool partial = split_start || split_end;

		if (entry->wired_count != 0) {
			return false;
		}
		if (entry_has_guards(entry) && partial) {
			return false;
		}
		if (split_start &&
		    !account_split_ref(entry, &split_count)) {
			return false;
		}
		if (split_end &&
		    !account_split_ref(entry, &split_count)) {
			return false;
		}
		if (!partial &&
		    entry->object != NULL &&
		    !plane_vm_object_can_deallocate(entry->object)) {
			return false;
		}

		if (plan->first == VM_MAP_ENTRY_NONE) {
			plan->first = current;
		}
		current = entry->next;
	}

	if (split_count > available_entry_count(map)) {
		return false;
	}

	plan->split_count = split_count;
	return true;
}

static bool find_delete_range(struct plane_vm_map *map,
			      uint64_t start,
			      uint64_t end,
			      struct vm_map_delete_plan *plan)
{
	uint64_t previous = VM_MAP_ENTRY_NONE;
	uint64_t current = map->head;
	uint64_t count = 0;

	while (current != VM_MAP_ENTRY_NONE &&
	       map->entries[current].end <= start) {
		previous = current;
		current = map->entries[current].next;
	}

	*plan = (struct vm_map_delete_plan){
		.prev = previous,
		.first = VM_MAP_ENTRY_NONE,
		.stop = current,
		.count = 0,
	};

	while (current != VM_MAP_ENTRY_NONE &&
	       map->entries[current].start < end) {
		const struct plane_vm_map_entry *entry = &map->entries[current];

		if (entry->start < start ||
		    entry->end > end ||
		    entry->wired_count != 0 ||
		    (entry->object != NULL &&
		     !plane_vm_object_can_deallocate(entry->object))) {
			return false;
		}

		if (count == 0) {
			plan->first = current;
		}
		count++;
		current = entry->next;
	}

	plan->stop = current;
	plan->count = count;
	return true;
}

static bool apply_protect_user_range(struct plane_vm_map *map,
				     uint64_t start,
				     uint64_t end,
				     const struct vm_map_range_plan *plan,
				     uint32_t prot,
				     bool set_max)
{
	uint64_t current = plan->first;

	while (current != VM_MAP_ENTRY_NONE) {
		uint64_t next;
		struct plane_vm_map_entry *entry = &map->entries[current];

		if (start > entry->user_start && start < entry->user_end &&
		    !clip_entry_start(map, current, start)) {
			return false;
		}

		entry = &map->entries[current];
		if (end > entry->user_start && end < entry->user_end &&
		    !clip_entry_end(map, current, end)) {
			return false;
		}

		entry = &map->entries[current];
		if (entry->user_start >= start && entry->user_end <= end) {
			if (set_max) {
				entry->max_prot = prot;
				entry->prot &= prot;
			} else {
				entry->prot = prot;
			}
		}
		if (entry->user_end >= end) {
			break;
		}
		next = entry->next;
		current = next;
	}

	return true;
}

static bool find_fixed_position(struct plane_vm_map *map,
				uint64_t start,
				uint64_t *prev,
				uint64_t *next)
{
	uint64_t previous = VM_MAP_ENTRY_NONE;
	uint64_t current = map->head;

	while (current != VM_MAP_ENTRY_NONE &&
	       map->entries[current].end <= start) {
		previous = current;
		current = map->entries[current].next;
	}

	*prev = previous;
	*next = current;
	return true;
}

static bool map_range_contains(struct plane_vm_map *map,
			       uint64_t start,
			       uint64_t end)
{
	return start >= map->base && end >= start && end <= map->end;
}

static void zap_init(struct vm_map_zap *zap)
{
	*zap = (struct vm_map_zap){
		.head = VM_MAP_ENTRY_NONE,
		.tail = VM_MAP_ENTRY_NONE,
		.reusable = VM_MAP_ENTRY_NONE,
		.count = 0,
	};
}

static void zap_append(struct plane_vm_map *map,
		       struct vm_map_zap *zap,
		       uint64_t index)
{
	map->entries[index].prev = VM_MAP_ENTRY_NONE;
	map->entries[index].next = VM_MAP_ENTRY_NONE;

	if (zap->head == VM_MAP_ENTRY_NONE) {
		zap->head = index;
		zap->reusable = index;
	} else {
		map->entries[zap->tail].next = index;
	}
	zap->tail = index;
	zap->count++;
}

static void zap_detach_entry(struct plane_vm_map *map,
			     struct vm_map_zap *zap,
			     uint64_t index)
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

	map->entry_count--;
	zap_append(map, zap, index);
}

static void zap_detach_range(struct plane_vm_map *map,
			     const struct vm_map_delete_plan *plan,
			     struct vm_map_zap *zap)
{
	uint64_t current = plan->first;

	zap_init(zap);
	if (plan->count == 0) {
		return;
	}
	while (current != plan->stop) {
		uint64_t next = map->entries[current].next;

		zap_detach_entry(map, zap, current);
		current = next;
	}
}

static void zap_dispose(struct plane_vm_map *map, struct vm_map_zap *zap)
{
	uint64_t current = zap->head;

	/* XNU-like zap disposal: release entry object refs after map unlink. */
	while (current != VM_MAP_ENTRY_NONE) {
		uint64_t next = map->entries[current].next;

		if (map->entries[current].object != NULL) {
			plane_vm_object_deallocate(map->entries[current].object);
		}
		map->entries[current] = (struct plane_vm_map_entry){0};
		map->entries[current].prev = VM_MAP_ENTRY_NONE;
		map->entries[current].next = VM_MAP_ENTRY_NONE;
		current = next;
	}
	zap_init(zap);
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

bool plane_vm_map_enter(struct plane_vm_map *map,
			const struct plane_vm_map_enter_options *options,
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
	uint64_t entry_object_offset;
	struct vm_map_delete_plan delete_plan = {0};
	struct vm_map_zap zap;
	struct plane_vm_object *entry_object;
	bool fixed;
	bool overwrite;

	if (vaddr == NULL ||
	    map == NULL ||
	    options == NULL ||
	    !map->initialized ||
	    options->page_count == 0 ||
	    !enter_flags_valid(options->flags) ||
	    (options->object == NULL && options->object_offset != 0) ||
	    (options->object != NULL &&
	     options->object_offset != PLANE_VM_MAP_OBJECT_OFFSET_AUTO &&
	     !plane_is_page_aligned(options->object_offset)) ||
	    !prot_allowed(options->prot, options->max_prot)) {
		return false;
	}

	fixed = (options->flags & PLANE_VM_MAP_ENTER_FIXED) != 0;
	overwrite = (options->flags & PLANE_VM_MAP_ENTER_OVERWRITE) != 0;
	entry_object = options->object;
	entry_object_offset = options->object_offset;
	zap_init(&zap);

	if (!plane_checked_mul_u64(options->guard_pages, 2, &guard_total) ||
	    !plane_checked_add_u64(options->page_count, guard_total,
				   &total_pages) ||
	    !plane_checked_page_offset(total_pages, &size) ||
	    !plane_checked_page_offset(options->guard_pages, &guard_size) ||
	    !plane_checked_page_offset(options->page_count, &user_size)) {
		return false;
	}

	if (fixed) {
		if (!plane_is_page_aligned(options->address) ||
		    options->address < guard_size) {
			return false;
		}
		user_start = options->address;
		start = user_start - guard_size;
		if (!plane_checked_add_u64(start, size, &end) ||
		    !plane_checked_add_u64(user_start, user_size, &user_end) ||
		    !map_range_contains(map, start, end)) {
			return false;
		}
		if (overwrite) {
			if (!find_delete_range(map, start, end, &delete_plan)) {
				return false;
			}
			prev = delete_plan.prev;
			next = delete_plan.stop;
		} else {
			if (!find_fixed_position(map, start, &prev, &next) ||
			    (next != VM_MAP_ENTRY_NONE &&
			     range_overlaps_entry(&map->entries[next],
						  start, end))) {
				return false;
			}
		}
	} else {
		if (!find_first_fit(map, size, &start, &prev, &next) ||
		    !plane_checked_add_u64(start, size, &end) ||
		    !plane_checked_add_u64(start, guard_size, &user_start) ||
		    !plane_checked_add_u64(user_start, user_size, &user_end)) {
			return false;
		}
	}

	if (entry_object != NULL &&
	    options->object_offset == PLANE_VM_MAP_OBJECT_OFFSET_AUTO) {
		entry_object_offset = user_start;
	}

	if (entry_object != NULL &&
	    !object_range_valid(entry_object, entry_object_offset,
				options->page_count)) {
		return false;
	}

	if (entry_object != NULL &&
	    !plane_vm_object_reference(entry_object)) {
		return false;
	}
	if (entry_object == NULL) {
		if (!plane_vm_object_allocate(user_size, &entry_object)) {
			return false;
		}
		entry_object_offset = 0;
	}

	entry_index = alloc_entry_index(map);
	if ((!overwrite || delete_plan.count == 0) && entry_index < 0) {
		plane_vm_object_deallocate(entry_object);
		return false;
	}

	if (overwrite && delete_plan.count != 0) {
		zap_detach_range(map, &delete_plan, &zap);
		entry_index = (int64_t)zap.reusable;
		zap_dispose(map, &zap);
		if (entry_index < 0) {
			plane_vm_object_deallocate(entry_object);
			return false;
		}
	}

	insert_entry(map, (uint64_t)entry_index, start, end, user_start, user_end,
		     entry_object, entry_object_offset, options->prot,
		     options->max_prot, prev, next);
	*vaddr = user_start;
	return true;
}

bool plane_vm_map_delete_range(struct plane_vm_map *map,
			       uint64_t start,
			       uint64_t page_count)
{
	struct vm_map_delete_plan plan;
	struct vm_map_range_plan range_plan;
	struct vm_map_zap zap;
	uint64_t size;
	uint64_t end;

	if (map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    !plane_is_page_aligned(start) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(start, size, &end) ||
	    !map_range_contains(map, start, end) ||
	    !plan_delete_reserved_range(map, start, end, &range_plan)) {
		return false;
	}

	if (range_plan.split_count != 0) {
		uint64_t current = range_plan.first;

		while (current != VM_MAP_ENTRY_NONE &&
		       map->entries[current].start < end) {
			uint64_t next;

			if (start > map->entries[current].start &&
			    start < map->entries[current].end &&
			    !clip_entry_start(map, current, start)) {
				return false;
			}
			if (end > map->entries[current].start &&
			    end < map->entries[current].end &&
			    !clip_entry_end(map, current, end)) {
				return false;
			}
			next = map->entries[current].next;
			current = next;
		}
	}

	if (!find_delete_range(map, start, end, &plan)) {
		return false;
	}
	if (plan.count == 0) {
		return true;
	}

	zap_detach_range(map, &plan, &zap);
	zap_dispose(map, &zap);
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
	struct vm_map_range_plan plan;
	uint64_t size;
	uint64_t end;

	if (map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    !plane_is_page_aligned(vaddr) ||
	    !prot_valid(prot) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(vaddr, size, &end) ||
	    !map_range_contains(map, vaddr, end) ||
	    !plan_protect_user_range(map, vaddr, end, prot, &plan)) {
		return false;
	}

	return apply_protect_user_range(map, vaddr, end, &plan, prot, false);
}

bool plane_vm_map_protect_max_pages(struct plane_vm_map *map,
				    uint64_t vaddr,
				    uint64_t page_count,
				    uint32_t max_prot)
{
	struct vm_map_range_plan plan;
	uint64_t size;
	uint64_t end;

	if (map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    !plane_is_page_aligned(vaddr) ||
	    !prot_valid(max_prot) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(vaddr, size, &end) ||
	    !map_range_contains(map, vaddr, end) ||
	    !plan_protect_user_range(map, vaddr, end, max_prot, &plan)) {
		return false;
	}

	return apply_protect_user_range(map, vaddr, end, &plan, max_prot, true);
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
	int64_t first_index;
	uint64_t reserved_start;
	uint64_t reserved_end;
	uint64_t user_end;
	uint64_t size;
	uint64_t current;
	bool guarded = false;

	if (map == NULL ||
	    !map->initialized ||
	    page_count == 0 ||
	    !plane_is_page_aligned(vaddr) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(vaddr, size, &user_end)) {
		return false;
	}

	first_index = find_user_entry(map, vaddr);
	if (first_index < 0) {
		return false;
	}

	current = (uint64_t)first_index;
	reserved_start = map->entries[current].start;
	reserved_end = map->entries[current].end;
	while (current != VM_MAP_ENTRY_NONE &&
	       map->entries[current].user_start < user_end) {
		struct plane_vm_map_entry *entry = &map->entries[current];

		if (vaddr > entry->user_start ||
		    user_end < entry->user_end) {
			if (entry_has_guards(entry)) {
				return false;
			}
		}
		if (entry->user_start > vaddr &&
		    entry->prev != VM_MAP_ENTRY_NONE &&
		    map->entries[entry->prev].user_end != entry->user_start) {
			return false;
		}
		if (entry_has_guards(entry)) {
			guarded = true;
		}
		reserved_end = entry->end;
		if (entry->user_end >= user_end) {
			break;
		}
		current = entry->next;
	}
	if (current == VM_MAP_ENTRY_NONE ||
	    map->entries[current].user_end < user_end) {
		return false;
	}

	if (guarded) {
		if (vaddr != map->entries[first_index].user_start ||
		    user_end != map->entries[current].user_end) {
			return false;
		}
		return plane_vm_map_delete_range(
			map, reserved_start,
			page_count_from_size(reserved_end - reserved_start));
	}

	return plane_vm_map_delete_range(map, vaddr, page_count);
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
