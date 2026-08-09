#include <stddef.h>

#include <plane/mm.h>
#include <plane/spinlock.h>
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

static plane_irq_state_t lock_map(struct plane_vm_map *map)
{
	return plane_spin_lock_irqsave(&map->lock);
}

static void unlock_map(struct plane_vm_map *map, plane_irq_state_t state)
{
	plane_spin_unlock_irqrestore(&map->lock, state);
}

static void entry_set_range(struct plane_vm_map_entry *entry,
			    uint64_t start,
			    uint64_t end,
			    uint64_t user_start,
			    uint64_t user_end)
{
	entry->start = plane_vaddr_make(start);
	entry->end = plane_vaddr_make(end);
	entry->user_start = plane_vaddr_make(user_start);
	entry->user_end = plane_vaddr_make(user_end);
}

static bool enter_flags_valid(uint32_t flags)
{
	bool anywhere = (flags & PLANE_VM_MAP_ENTER_ANYWHERE) != 0;
	bool fixed = (flags & PLANE_VM_MAP_ENTER_FIXED) != 0;
	bool overwrite = (flags & PLANE_VM_MAP_ENTER_OVERWRITE) != 0;

	if ((flags & ~(PLANE_VM_MAP_ENTER_ANYWHERE |
		       PLANE_VM_MAP_ENTER_FIXED |
		       PLANE_VM_MAP_ENTER_OVERWRITE |
		       PLANE_VM_MAP_ENTER_VA_ONLY)) != 0) {
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

static bool entry_storage_range(const struct plane_vm_map_entry *entries,
				uint64_t entry_capacity,
				uintptr_t *start,
				uintptr_t *end)
{
	uint64_t bytes;
	uintptr_t base = (uintptr_t)entries;

	if (entries == NULL ||
	    start == NULL ||
	    end == NULL ||
	    !plane_checked_mul_u64((uint64_t)sizeof(entries[0]),
				   entry_capacity, &bytes) ||
	    bytes > UINTPTR_MAX - base) {
		return false;
	}

	*start = base;
	*end = base + (uintptr_t)bytes;
	return true;
}

static bool entry_storage_overlaps(const struct plane_vm_map_entry *first,
				   uint64_t first_capacity,
				   const struct plane_vm_map_entry *second,
				   uint64_t second_capacity)
{
	uintptr_t first_start;
	uintptr_t first_end;
	uintptr_t second_start;
	uintptr_t second_end;

	if (!entry_storage_range(first, first_capacity,
				 &first_start, &first_end) ||
	    !entry_storage_range(second, second_capacity,
				 &second_start, &second_end)) {
		return true;
	}

	return first_start < second_end && second_start < first_end;
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
	uint64_t ref_count;

	if (object == NULL || count == 0) {
		return true;
	}

	ref_count = plane_vm_object_ref_count(object);
	return plane_vm_object_is_alive(object) &&
	       ref_count != 0 &&
	       ref_count <= UINT64_MAX - count;
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
	entry_set_range(&map->entries[index], start, end, user_start, user_end);
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
	uint64_t cursor = plane_vaddr_raw(map->base);
	uint64_t current = map->head;
	uint64_t previous = VM_MAP_ENTRY_NONE;

	while (current != VM_MAP_ENTRY_NONE) {
		if (hole_can_fit(cursor,
				 plane_vaddr_raw(map->entries[current].start),
				 size)) {
			*start = cursor;
			*prev = previous;
			*next = current;
			return true;
		}
		cursor = plane_vaddr_raw(map->entries[current].end);
		previous = current;
		current = map->entries[current].next;
	}

	if (hole_can_fit(cursor, plane_vaddr_raw(map->end), size)) {
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
	uint64_t entry_start = plane_vaddr_raw(entry->start);
	uint64_t entry_end = plane_vaddr_raw(entry->end);

	return entry_start < end && start < entry_end;
}

static bool entry_has_guards(const struct plane_vm_map_entry *entry)
{
	uint64_t entry_start = plane_vaddr_raw(entry->start);
	uint64_t entry_end = plane_vaddr_raw(entry->end);
	uint64_t user_start = plane_vaddr_raw(entry->user_start);
	uint64_t user_end = plane_vaddr_raw(entry->user_end);

	return entry_start != user_start || entry_end != user_end;
}

static bool entry_contains_user_addr(const struct plane_vm_map_entry *entry,
				     uint64_t addr)
{
	uint64_t user_start = plane_vaddr_raw(entry->user_start);
	uint64_t user_end = plane_vaddr_raw(entry->user_end);

	return user_start <= addr && addr < user_end;
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
	uint64_t entry_user_start = plane_vaddr_raw(entry->user_start);
	uint64_t entry_user_end = plane_vaddr_raw(entry->user_end);

	if (start <= entry_user_start) {
		return start == entry_user_start;
	}
	if (start >= entry_user_end) {
		return false;
	}

	new_index = alloc_entry_index(map);
	if (new_index < 0) {
		return false;
	}
	if (entry->object != NULL && !plane_vm_object_reference(entry->object)) {
		return false;
	}

	old_user_start = entry_user_start;
	new_entry = &map->entries[new_index];
	*new_entry = *entry;
	new_entry->end = plane_vaddr_make(start);
	new_entry->user_end = plane_vaddr_make(start);
	new_entry->prev = entry->prev;
	new_entry->next = index;
	new_entry->used = true;

	if (entry->prev != VM_MAP_ENTRY_NONE) {
		map->entries[entry->prev].next = (uint64_t)new_index;
	} else {
		map->head = (uint64_t)new_index;
	}

	entry->prev = (uint64_t)new_index;
	entry->start = plane_vaddr_make(start);
	entry->user_start = plane_vaddr_make(start);
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
	uint64_t entry_user_start = plane_vaddr_raw(entry->user_start);
	uint64_t entry_user_end = plane_vaddr_raw(entry->user_end);

	if (end >= entry_user_end) {
		return end == entry_user_end;
	}
	if (end <= entry_user_start) {
		return false;
	}

	new_index = alloc_entry_index(map);
	if (new_index < 0) {
		return false;
	}
	if (entry->object != NULL && !plane_vm_object_reference(entry->object)) {
		return false;
	}

	old_user_start = entry_user_start;
	new_entry = &map->entries[new_index];
	*new_entry = *entry;
	new_entry->start = plane_vaddr_make(end);
	new_entry->user_start = plane_vaddr_make(end);
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
	entry->end = plane_vaddr_make(end);
	entry->user_end = plane_vaddr_make(end);
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
		uint64_t entry_user_start = plane_vaddr_raw(entry->user_start);
		uint64_t entry_user_end = plane_vaddr_raw(entry->user_end);

		if (cursor < entry_user_start ||
		    (prot & ~entry->max_prot) != 0) {
			return false;
		}

		if (plan->first == VM_MAP_ENTRY_NONE) {
			plan->first = (uint64_t)entry_index;
		}

		if (cursor > entry_user_start &&
		    !account_split_ref(entry, &split_count)) {
			return false;
		}

		segment_end = end < entry_user_end ? end : entry_user_end;
		if (segment_end < entry_user_end &&
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

static bool plan_wire_user_range(struct plane_vm_map *map,
				 uint64_t start,
				 uint64_t end,
				 bool wire,
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
		uint64_t entry_user_start = plane_vaddr_raw(entry->user_start);
		uint64_t entry_user_end = plane_vaddr_raw(entry->user_end);

		if (cursor < entry_user_start ||
		    (wire && entry->wired_count == UINT64_MAX) ||
		    (!wire && entry->wired_count == 0)) {
			return false;
		}

		if (plan->first == VM_MAP_ENTRY_NONE) {
			plan->first = (uint64_t)entry_index;
		}

		if (cursor > entry_user_start &&
		    !account_split_ref(entry, &split_count)) {
			return false;
		}

		segment_end = end < entry_user_end ? end : entry_user_end;
		if (segment_end < entry_user_end &&
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
	       plane_vaddr_raw(map->entries[current].end) <= start) {
		current = map->entries[current].next;
	}

	while (current != VM_MAP_ENTRY_NONE &&
	       plane_vaddr_raw(map->entries[current].start) < end) {
		struct plane_vm_map_entry *entry = &map->entries[current];
		uint64_t entry_start = plane_vaddr_raw(entry->start);
		uint64_t entry_end = plane_vaddr_raw(entry->end);
		bool split_start = start > entry_start && start < entry_end;
		bool split_end = end > entry_start && end < entry_end;
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
	       plane_vaddr_raw(map->entries[current].end) <= start) {
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
	       plane_vaddr_raw(map->entries[current].start) < end) {
		const struct plane_vm_map_entry *entry = &map->entries[current];
		uint64_t entry_start = plane_vaddr_raw(entry->start);
		uint64_t entry_end = plane_vaddr_raw(entry->end);

		if (entry_start < start ||
		    entry_end > end ||
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
		uint64_t entry_user_start = plane_vaddr_raw(entry->user_start);
		uint64_t entry_user_end = plane_vaddr_raw(entry->user_end);

		if (start > entry_user_start &&
		    start < entry_user_end &&
		    !clip_entry_start(map, current, start)) {
			return false;
		}

		entry = &map->entries[current];
		entry_user_start = plane_vaddr_raw(entry->user_start);
		entry_user_end = plane_vaddr_raw(entry->user_end);
		if (end > entry_user_start &&
		    end < entry_user_end &&
		    !clip_entry_end(map, current, end)) {
			return false;
		}

		entry = &map->entries[current];
		entry_user_start = plane_vaddr_raw(entry->user_start);
		entry_user_end = plane_vaddr_raw(entry->user_end);
		if (entry_user_start >= start && entry_user_end <= end) {
			if (set_max) {
				entry->max_prot = prot;
				entry->prot &= prot;
			} else {
				entry->prot = prot;
			}
		}
		if (entry_user_end >= end) {
			break;
		}
		next = entry->next;
		current = next;
	}

	return true;
}

static bool apply_wire_user_range(struct plane_vm_map *map,
				  uint64_t start,
				  uint64_t end,
				  const struct vm_map_range_plan *plan,
				  bool wire)
{
	uint64_t current = plan->first;

	while (current != VM_MAP_ENTRY_NONE) {
		uint64_t next;
		struct plane_vm_map_entry *entry = &map->entries[current];
		uint64_t entry_user_start = plane_vaddr_raw(entry->user_start);
		uint64_t entry_user_end = plane_vaddr_raw(entry->user_end);

		if (start > entry_user_start &&
		    start < entry_user_end &&
		    !clip_entry_start(map, current, start)) {
			return false;
		}

		entry = &map->entries[current];
		entry_user_start = plane_vaddr_raw(entry->user_start);
		entry_user_end = plane_vaddr_raw(entry->user_end);
		if (end > entry_user_start &&
		    end < entry_user_end &&
		    !clip_entry_end(map, current, end)) {
			return false;
		}

		entry = &map->entries[current];
		entry_user_start = plane_vaddr_raw(entry->user_start);
		entry_user_end = plane_vaddr_raw(entry->user_end);
		if (entry_user_start >= start && entry_user_end <= end) {
			if (wire) {
				entry->wired_count++;
			} else {
				entry->wired_count--;
			}
		}
		if (entry_user_end >= end) {
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
	       plane_vaddr_raw(map->entries[current].end) <= start) {
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
	return start >= plane_vaddr_raw(map->base) && end >= start &&
	       end <= plane_vaddr_raw(map->end);
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
		if (plane_vaddr_raw(map->entries[current].user_start) == vaddr &&
		    plane_vaddr_raw(map->entries[current].user_end) == end) {
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
		pages += page_count_from_size(
			plane_vaddr_raw(map->entries[current].end) -
			plane_vaddr_raw(map->entries[current].start));
		current = map->entries[current].next;
	}

	return pages;
}

static uint64_t user_pages(struct plane_vm_map *map)
{
	uint64_t pages = 0;
	uint64_t current = map->head;

	while (current != VM_MAP_ENTRY_NONE) {
		pages += page_count_from_size(
			plane_vaddr_raw(map->entries[current].user_end) -
			plane_vaddr_raw(map->entries[current].user_start));
		current = map->entries[current].next;
	}

	return pages;
}

static uint64_t free_range_count(struct plane_vm_map *map)
{
	uint64_t count = 0;
	uint64_t cursor = plane_vaddr_raw(map->base);
	uint64_t current = map->head;

	while (current != VM_MAP_ENTRY_NONE) {
		if (plane_vaddr_raw(map->entries[current].start) > cursor) {
			count++;
		}
		cursor = plane_vaddr_raw(map->entries[current].end);
		current = map->entries[current].next;
	}

	if (plane_vaddr_raw(map->end) > cursor) {
		count++;
	}

	return count;
}

static bool rehome_entries_locked(struct plane_vm_map *map,
				  struct plane_vm_map_entry *entries,
				  uint64_t entry_capacity)
{
	if (!map->initialized ||
	    entries == NULL ||
	    entry_capacity < map->entry_capacity) {
		return false;
	}
	if (entries == map->entries) {
		return entry_capacity == map->entry_capacity;
	}
	if (entry_storage_overlaps(map->entries, map->entry_capacity,
				   entries, entry_capacity)) {
		return false;
	}

	reset_entries(entries, entry_capacity);
	for (uint64_t i = 0; i < map->entry_capacity; i++) {
		if (map->entries[i].used) {
			entries[i] = map->entries[i];
		}
	}

	map->entries = entries;
	map->entry_capacity = entry_capacity;
	return true;
}

bool plane_vm_map_init(struct plane_vm_map *map,
		       struct plane_vm_map_entry *entries,
		       uint64_t entry_capacity,
		       plane_vaddr_t base,
		       uint64_t size)
{
	uint64_t raw_base = plane_vaddr_raw(base);
	uint64_t end;

	if (map == NULL ||
	    entries == NULL ||
	    entry_capacity == 0 ||
	    map->initialized ||
	    size == 0 ||
	    !plane_checked_add_u64(raw_base, size, &end) ||
	    !plane_vaddr_is_page_aligned(base) ||
	    !plane_is_page_aligned(size)) {
		return false;
	}

	reset_map(map, entries, entry_capacity);
	plane_spin_init(&map->lock);
	map->base = plane_vaddr_make(raw_base);
	map->end = plane_vaddr_make(end);
	map->initialized = true;
	return true;
}

bool plane_vm_map_rehome_entries(struct plane_vm_map *map,
				 struct plane_vm_map_entry *entries,
				 uint64_t entry_capacity)
{
	plane_irq_state_t state;
	bool rehomed;

	if (map == NULL) {
		return false;
	}

	state = lock_map(map);
	rehomed = rehome_entries_locked(map, entries, entry_capacity);
	unlock_map(map, state);
	return rehomed;
}

static bool enter_locked(struct plane_vm_map *map,
			 const struct plane_vm_map_enter_options *options,
			 plane_vaddr_t *vaddr)
{
	int64_t entry_index;
	uint64_t raw_address;
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
	bool va_only;

	if (vaddr == NULL ||
	    !map->initialized ||
	    options->page_count == 0 ||
	    !enter_flags_valid(options->flags) ||
	    (options->object == NULL && options->object_offset != 0) ||
	    (options->object != NULL &&
	     options->object_offset != PLANE_VM_MAP_OBJECT_OFFSET_AUTO &&
	     !plane_is_page_aligned(options->object_offset)) ||
	    !plane_vm_prot_allowed(options->prot, options->max_prot)) {
		return false;
	}

	fixed = (options->flags & PLANE_VM_MAP_ENTER_FIXED) != 0;
	overwrite = (options->flags & PLANE_VM_MAP_ENTER_OVERWRITE) != 0;
	va_only = (options->flags & PLANE_VM_MAP_ENTER_VA_ONLY) != 0;
	entry_object = options->object;
	entry_object_offset = options->object_offset;
	raw_address = plane_vaddr_raw(options->address);
	zap_init(&zap);

	if (va_only && entry_object != NULL) {
		return false;
	}

	if (!plane_checked_mul_u64(options->guard_pages, 2, &guard_total) ||
	    !plane_checked_add_u64(options->page_count, guard_total,
				   &total_pages) ||
	    !plane_checked_page_offset(total_pages, &size) ||
	    !plane_checked_page_offset(options->guard_pages, &guard_size) ||
	    !plane_checked_page_offset(options->page_count, &user_size)) {
		return false;
	}

	if (fixed) {
		if (!plane_vaddr_is_page_aligned(options->address) ||
		    raw_address < guard_size) {
			return false;
		}
		user_start = raw_address;
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
	if (entry_object == NULL && !va_only) {
		if (!plane_vm_object_allocate(user_size, &entry_object)) {
			return false;
		}
		entry_object_offset = 0;
	}

	entry_index = alloc_entry_index(map);
	if ((!overwrite || delete_plan.count == 0) && entry_index < 0) {
		if (entry_object != NULL) {
			plane_vm_object_deallocate(entry_object);
		}
		return false;
	}

	if (overwrite && delete_plan.count != 0) {
		zap_detach_range(map, &delete_plan, &zap);
		entry_index = (int64_t)zap.reusable;
		zap_dispose(map, &zap);
		if (entry_index < 0) {
			if (entry_object != NULL) {
				plane_vm_object_deallocate(entry_object);
			}
			return false;
		}
	}

	insert_entry(map, (uint64_t)entry_index, start, end, user_start, user_end,
		     entry_object, entry_object_offset, options->prot,
		     options->max_prot, prev, next);
	*vaddr = plane_vaddr_make(user_start);
	return true;
}

bool plane_vm_map_enter(struct plane_vm_map *map,
			const struct plane_vm_map_enter_options *options,
			plane_vaddr_t *vaddr)
{
	plane_irq_state_t state;
	bool entered;

	if (map == NULL || options == NULL || vaddr == NULL) {
		return false;
	}

	state = lock_map(map);
	entered = enter_locked(map, options, vaddr);
	unlock_map(map, state);
	return entered;
}

static bool delete_range_locked(struct plane_vm_map *map,
				plane_vaddr_t start,
				uint64_t page_count)
{
	struct vm_map_delete_plan plan;
	struct vm_map_range_plan range_plan;
	struct vm_map_zap zap;
	uint64_t raw_start = plane_vaddr_raw(start);
	uint64_t size;
	uint64_t end;

	if (!map->initialized ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(start) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(raw_start, size, &end) ||
	    !map_range_contains(map, raw_start, end) ||
	    !plan_delete_reserved_range(map, raw_start, end, &range_plan)) {
		return false;
	}

	if (range_plan.split_count != 0) {
		uint64_t current = range_plan.first;

		while (current != VM_MAP_ENTRY_NONE &&
		       plane_vaddr_raw(map->entries[current].start) < end) {
			struct plane_vm_map_entry *entry = &map->entries[current];
			uint64_t entry_start = plane_vaddr_raw(entry->start);
			uint64_t entry_end = plane_vaddr_raw(entry->end);
			uint64_t next;

			if (raw_start > entry_start &&
			    raw_start < entry_end &&
			    !clip_entry_start(map, current, raw_start)) {
				return false;
			}
			entry = &map->entries[current];
			entry_start = plane_vaddr_raw(entry->start);
			entry_end = plane_vaddr_raw(entry->end);
			if (end > entry_start &&
			    end < entry_end &&
			    !clip_entry_end(map, current, end)) {
				return false;
			}
			next = map->entries[current].next;
			current = next;
		}
	}

	if (!find_delete_range(map, raw_start, end, &plan)) {
		return false;
	}
	if (plan.count == 0) {
		return true;
	}

	zap_detach_range(map, &plan, &zap);
	zap_dispose(map, &zap);
	return true;
}

bool plane_vm_map_delete_range(struct plane_vm_map *map,
			       plane_vaddr_t start,
			       uint64_t page_count)
{
	plane_irq_state_t state;
	bool deleted;

	if (map == NULL) {
		return false;
	}

	state = lock_map(map);
	deleted = delete_range_locked(map, start, page_count);
	unlock_map(map, state);
	return deleted;
}

static bool lookup_allocation_locked(
	struct plane_vm_map *map,
	plane_vaddr_t vaddr,
	uint64_t page_count,
	struct plane_vm_map_allocation_info *info)
{
	int64_t entry_index;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);

	if (!map->initialized ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr)) {
		return false;
	}

	entry_index = find_exact_entry(map, raw_vaddr, page_count);
	if (entry_index < 0) {
		return false;
	}

	if (info != NULL) {
		struct plane_vm_map_entry *entry = &map->entries[entry_index];
		uint64_t reserved_start = plane_vaddr_raw(entry->start);
		uint64_t reserved_end = plane_vaddr_raw(entry->end);
		uint64_t user_start = plane_vaddr_raw(entry->user_start);
		uint64_t user_end = plane_vaddr_raw(entry->user_end);

		info->reserved_start = entry->start;
		info->reserved_pages = page_count_from_size(reserved_end -
							    reserved_start);
		info->user_start = entry->user_start;
		info->user_pages = page_count_from_size(user_end - user_start);
		info->object = entry->object;
		info->object_offset = entry->object_offset;
		info->wired_count = entry->wired_count;
		info->prot = entry->prot;
		info->max_prot = entry->max_prot;
	}

	return true;
}

bool plane_vm_map_lookup_allocation(
	struct plane_vm_map *map,
	plane_vaddr_t vaddr,
	uint64_t page_count,
	struct plane_vm_map_allocation_info *info)
{
	plane_irq_state_t state;
	bool found;

	if (map == NULL) {
		return false;
	}

	state = lock_map(map);
	found = lookup_allocation_locked(map, vaddr, page_count, info);
	unlock_map(map, state);
	return found;
}

static bool lookup_page_locked(struct plane_vm_map *map,
			       plane_vaddr_t vaddr,
			       struct plane_vm_map_page_info *info)
{
	struct plane_vm_map_entry *entry;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t page_vaddr = raw_vaddr & PAGE_MASK;
	uint64_t object_delta;
	uint64_t object_offset;
	int64_t entry_index;

	if (!map->initialized) {
		return false;
	}

	entry_index = find_user_entry(map, page_vaddr);
	if (entry_index < 0) {
		return false;
	}

	entry = &map->entries[entry_index];
	object_delta = page_vaddr - plane_vaddr_raw(entry->user_start);
	if (entry->object == NULL ||
	    !plane_checked_add_u64(entry->object_offset, object_delta,
				   &object_offset)) {
		return false;
	}

	if (info != NULL) {
		info->page_vaddr = plane_vaddr_make(page_vaddr);
		info->object = entry->object;
		info->object_offset = object_offset;
		info->wired_count = entry->wired_count;
		info->prot = entry->prot;
		info->max_prot = entry->max_prot;
	}

	return true;
}

bool plane_vm_map_lookup_page(struct plane_vm_map *map,
			      plane_vaddr_t vaddr,
			      struct plane_vm_map_page_info *info)
{
	plane_irq_state_t state;
	bool found;

	if (map == NULL) {
		return false;
	}

	state = lock_map(map);
	found = lookup_page_locked(map, vaddr, info);
	unlock_map(map, state);
	return found;
}

static bool protect_pages_locked(struct plane_vm_map *map,
				 plane_vaddr_t vaddr,
				 uint64_t page_count,
				 uint32_t prot)
{
	struct vm_map_range_plan plan;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t size;
	uint64_t end;

	if (!map->initialized ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_vm_prot_valid(prot) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(raw_vaddr, size, &end) ||
	    !map_range_contains(map, raw_vaddr, end) ||
	    !plan_protect_user_range(map, raw_vaddr, end, prot, &plan)) {
		return false;
	}

	return apply_protect_user_range(map, raw_vaddr, end, &plan, prot, false);
}

bool plane_vm_map_protect_pages(struct plane_vm_map *map,
				plane_vaddr_t vaddr,
				uint64_t page_count,
				uint32_t prot)
{
	plane_irq_state_t state;
	bool protected;

	if (map == NULL) {
		return false;
	}

	state = lock_map(map);
	protected = protect_pages_locked(map, vaddr, page_count, prot);
	unlock_map(map, state);
	return protected;
}

static bool protect_max_pages_locked(struct plane_vm_map *map,
				     plane_vaddr_t vaddr,
				     uint64_t page_count,
				     uint32_t max_prot)
{
	struct vm_map_range_plan plan;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t size;
	uint64_t end;

	if (!map->initialized ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_vm_prot_valid(max_prot) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(raw_vaddr, size, &end) ||
	    !map_range_contains(map, raw_vaddr, end) ||
	    !plan_protect_user_range(map, raw_vaddr, end, max_prot, &plan)) {
		return false;
	}

	return apply_protect_user_range(map, raw_vaddr, end, &plan, max_prot, true);
}

bool plane_vm_map_protect_max_pages(struct plane_vm_map *map,
				    plane_vaddr_t vaddr,
				    uint64_t page_count,
				    uint32_t max_prot)
{
	plane_irq_state_t state;
	bool protected;

	if (map == NULL) {
		return false;
	}

	state = lock_map(map);
	protected = protect_max_pages_locked(map, vaddr, page_count, max_prot);
	unlock_map(map, state);
	return protected;
}

static bool wire_pages_locked(struct plane_vm_map *map,
			      plane_vaddr_t vaddr,
			      uint64_t page_count)
{
	struct vm_map_range_plan plan;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t size;
	uint64_t end;

	if (!map->initialized ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(raw_vaddr, size, &end) ||
	    !map_range_contains(map, raw_vaddr, end) ||
	    !plan_wire_user_range(map, raw_vaddr, end, true, &plan)) {
		return false;
	}

	return apply_wire_user_range(map, raw_vaddr, end, &plan, true);
}

bool plane_vm_map_wire_pages(struct plane_vm_map *map,
			     plane_vaddr_t vaddr,
			     uint64_t page_count)
{
	plane_irq_state_t state;
	bool wired;

	if (map == NULL) {
		return false;
	}

	state = lock_map(map);
	wired = wire_pages_locked(map, vaddr, page_count);
	unlock_map(map, state);
	return wired;
}

static bool unwire_pages_locked(struct plane_vm_map *map,
				plane_vaddr_t vaddr,
				uint64_t page_count)
{
	struct vm_map_range_plan plan;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t size;
	uint64_t end;

	if (!map->initialized ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(raw_vaddr, size, &end) ||
	    !map_range_contains(map, raw_vaddr, end) ||
	    !plan_wire_user_range(map, raw_vaddr, end, false, &plan)) {
		return false;
	}

	return apply_wire_user_range(map, raw_vaddr, end, &plan, false);
}

bool plane_vm_map_unwire_pages(struct plane_vm_map *map,
			       plane_vaddr_t vaddr,
			       uint64_t page_count)
{
	plane_irq_state_t state;
	bool unwired;

	if (map == NULL) {
		return false;
	}

	state = lock_map(map);
	unwired = unwire_pages_locked(map, vaddr, page_count);
	unlock_map(map, state);
	return unwired;
}

static bool free_pages_locked(struct plane_vm_map *map,
			      plane_vaddr_t vaddr,
			      uint64_t page_count)
{
	int64_t first_index;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t reserved_start;
	uint64_t reserved_end;
	uint64_t user_end;
	uint64_t size;
	uint64_t current;
	bool guarded = false;

	if (!map->initialized ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_checked_page_offset(page_count, &size) ||
	    !plane_checked_add_u64(raw_vaddr, size, &user_end)) {
		return false;
	}

	first_index = find_user_entry(map, raw_vaddr);
	if (first_index < 0) {
		return false;
	}

	current = (uint64_t)first_index;
	reserved_start = plane_vaddr_raw(map->entries[current].start);
	reserved_end = plane_vaddr_raw(map->entries[current].end);
	while (current != VM_MAP_ENTRY_NONE &&
	       plane_vaddr_raw(map->entries[current].user_start) < user_end) {
		struct plane_vm_map_entry *entry = &map->entries[current];
		uint64_t entry_user_start = plane_vaddr_raw(entry->user_start);
		uint64_t entry_user_end = plane_vaddr_raw(entry->user_end);

		if (raw_vaddr > entry_user_start ||
		    user_end < entry_user_end) {
			if (entry_has_guards(entry)) {
				return false;
			}
		}
		if (entry_user_start > raw_vaddr &&
		    entry->prev != VM_MAP_ENTRY_NONE &&
		    plane_vaddr_raw(map->entries[entry->prev].user_end) !=
			    entry_user_start) {
			return false;
		}
		if (entry_has_guards(entry)) {
			guarded = true;
		}
		reserved_end = plane_vaddr_raw(entry->end);
		if (entry_user_end >= user_end) {
			break;
		}
		current = entry->next;
	}
	if (current == VM_MAP_ENTRY_NONE ||
	    plane_vaddr_raw(map->entries[current].user_end) < user_end) {
		return false;
	}

	if (guarded) {
		uint64_t first_user_start =
			plane_vaddr_raw(map->entries[first_index].user_start);
		uint64_t current_user_end =
			plane_vaddr_raw(map->entries[current].user_end);

		if (raw_vaddr != first_user_start ||
		    user_end != current_user_end) {
			return false;
		}
		return delete_range_locked(
			map, plane_vaddr_make(reserved_start),
			page_count_from_size(reserved_end - reserved_start));
	}

	return delete_range_locked(map, vaddr, page_count);
}

bool plane_vm_map_free_pages(struct plane_vm_map *map,
			     plane_vaddr_t vaddr,
			     uint64_t page_count)
{
	plane_irq_state_t state;
	bool freed;

	if (map == NULL) {
		return false;
	}

	state = lock_map(map);
	freed = free_pages_locked(map, vaddr, page_count);
	unlock_map(map, state);
	return freed;
}

static struct plane_vm_map_stats stats_locked(struct plane_vm_map *map)
{
	struct plane_vm_map_stats stats = {0};

	if (!map->initialized) {
		return stats;
	}

	stats.total_pages = page_count_from_size(plane_vaddr_raw(map->end) -
						plane_vaddr_raw(map->base));
	stats.reserved_pages = reserved_pages(map);
	stats.user_pages = user_pages(map);
	stats.free_pages = stats.total_pages - stats.reserved_pages;
	stats.free_range_count = free_range_count(map);
	stats.allocation_count = map->entry_count;
	return stats;
}

struct plane_vm_map_stats plane_vm_map_get_stats(struct plane_vm_map *map)
{
	plane_irq_state_t state;
	struct plane_vm_map_stats stats;

	if (map == NULL) {
		return (struct plane_vm_map_stats){0};
	}

	state = lock_map(map);
	stats = stats_locked(map);
	unlock_map(map, state);
	return stats;
}
