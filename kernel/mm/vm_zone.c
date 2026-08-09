#include <stddef.h>
#include <stdint.h>

#include <klib/string.h>
#include <plane/overflow.h>

#include "vm_zone_internal.h"

static bool elem_size_is_valid(size_t elem_size)
{
	return elem_size >= sizeof(void *) &&
	       (elem_size % sizeof(void *)) == 0;
}

static void **elem_next(void *elem)
{
	return (void **)elem;
}

static uintptr_t segment_start(const struct plane_vm_zone_segment *segment)
{
	return (uintptr_t)segment->storage;
}

static bool storage_range(const struct plane_vm_zone *zone,
			  const void *storage,
			  uint64_t count,
			  uintptr_t *start,
			  uintptr_t *end)
{
	uint64_t bytes;
	uintptr_t base = (uintptr_t)storage;

	if (!plane_checked_mul_u64((uint64_t)zone->elem_size, count, &bytes) ||
	    bytes > UINTPTR_MAX - base) {
		return false;
	}

	*start = base;
	*end = base + (uintptr_t)bytes;
	return true;
}

static uintptr_t segment_end(const struct plane_vm_zone *zone,
			     const struct plane_vm_zone_segment *segment)
{
	uintptr_t start;
	uintptr_t end;

	if (!storage_range(zone, segment->storage, segment->count,
			   &start, &end)) {
		return segment_start(segment);
	}

	return end;
}

static bool elem_is_in_segment(const struct plane_vm_zone *zone,
			    const struct plane_vm_zone_segment *segment,
			    const void *elem)
{
	uintptr_t addr = (uintptr_t)elem;
	uintptr_t start = segment_start(segment);
	uintptr_t end = segment_end(zone, segment);

	return addr >= start &&
	       addr < end &&
	       ((addr - start) % zone->elem_size) == 0;
}

static bool elem_belongs_to_zone(const struct plane_vm_zone *zone,
				 const void *elem)
{
	const struct plane_vm_zone_segment *segment = zone->segments;

	while (segment != NULL) {
		if (elem_is_in_segment(zone, segment, elem)) {
			return true;
		}
		segment = segment->next;
	}

	return false;
}

static bool elem_is_on_free_list(const struct plane_vm_zone *zone,
			      const void *elem)
{
	const void *current = zone->free_list;

	while (current != NULL) {
		if (current == elem) {
			return true;
		}
		current = *elem_next((void *)current);
	}

	return false;
}

static bool ranges_overlap(uintptr_t first_start,
			   uintptr_t first_end,
			   uintptr_t second_start,
			   uintptr_t second_end)
{
	return first_start < second_end && second_start < first_end;
}

static bool storage_overlaps_existing(const struct plane_vm_zone *zone,
				      const void *storage,
				      uint64_t count)
{
	const struct plane_vm_zone_segment *segment = zone->segments;
	uintptr_t start;
	uintptr_t end;

	if (!storage_range(zone, storage, count, &start, &end)) {
		return true;
	}

	while (segment != NULL) {
		uintptr_t segment_start_addr;
		uintptr_t segment_end_addr;

		if (!storage_range(zone, segment->storage, segment->count,
				   &segment_start_addr, &segment_end_addr) ||
		    ranges_overlap(start, end,
				   segment_start_addr, segment_end_addr)) {
			return true;
		}
		segment = segment->next;
	}

	return false;
}

static bool segment_is_valid(const struct plane_vm_zone *zone,
			  const void *storage,
			  uint64_t count,
			  struct plane_vm_zone_segment *segment,
			  uint64_t *new_capacity)
{
	uint64_t bytes;

	if (zone == NULL ||
	    !zone->initialized ||
	    storage == NULL ||
	    count == 0 ||
	    segment == NULL ||
	    segment->storage != NULL ||
	    segment->count != 0 ||
	    segment->next != NULL ||
	    ((uintptr_t)storage % sizeof(void *)) != 0 ||
	    !plane_checked_mul_u64((uint64_t)zone->elem_size, count, &bytes) ||
	    storage_overlaps_existing(zone, storage, count)) {
		return false;
	}

	return plane_checked_add_u64(zone->capacity, count, new_capacity);
}

bool plane_vm_zone_add_storage(struct plane_vm_zone *zone,
			       void *storage,
			       uint64_t count,
			       struct plane_vm_zone_segment *segment)
{
	uint8_t *cursor = storage;
	uint64_t new_capacity;

	if (!segment_is_valid(zone, storage, count, segment, &new_capacity)) {
		return false;
	}

	for (uint64_t i = 0; i < count; i++) {
		void *elem = cursor + i * zone->elem_size;

		memset(elem, 0, zone->elem_size);
		*elem_next(elem) = zone->free_list;
		zone->free_list = elem;
	}

	*segment = (struct plane_vm_zone_segment){
		.storage = storage,
		.count = count,
		.next = zone->segments,
	};
	zone->segments = segment;
	zone->capacity = new_capacity;
	return true;
}

bool plane_vm_zone_init(struct plane_vm_zone *zone,
			size_t elem_size,
			void *storage,
			uint64_t count,
			struct plane_vm_zone_segment *segment)
{
	bool initialized;

	if (zone == NULL ||
	    zone->initialized ||
	    !elem_size_is_valid(elem_size) ||
	    storage == NULL ||
	    count == 0 ||
	    segment == NULL) {
		return false;
	}

	*zone = (struct plane_vm_zone){
		.elem_size = elem_size,
		.initialized = true,
	};
	initialized = plane_vm_zone_add_storage(zone, storage, count, segment);
	if (!initialized) {
		*zone = (struct plane_vm_zone){0};
	}
	return initialized;
}

void *plane_vm_zone_alloc(struct plane_vm_zone *zone)
{
	void *elem;

	if (zone == NULL || !zone->initialized || zone->free_list == NULL) {
		return NULL;
	}
	if (zone->allocated == UINT64_MAX) {
		return NULL;
	}

	elem = zone->free_list;
	zone->free_list = *elem_next(elem);
	memset(elem, 0, zone->elem_size);
	zone->allocated++;
	return elem;
}

bool plane_vm_zone_free(struct plane_vm_zone *zone, void *elem)
{
	if (zone == NULL ||
	    !zone->initialized ||
	    elem == NULL ||
	    zone->allocated == 0 ||
	    !elem_belongs_to_zone(zone, elem) ||
	    elem_is_on_free_list(zone, elem)) {
		return false;
	}

	memset(elem, 0, zone->elem_size);
	*elem_next(elem) = zone->free_list;
	zone->free_list = elem;
	zone->allocated--;
	return true;
}

uint64_t plane_vm_zone_capacity(const struct plane_vm_zone *zone)
{
	if (zone == NULL || !zone->initialized) {
		return 0;
	}

	return zone->capacity;
}

bool plane_vm_zone_contains(const struct plane_vm_zone *zone, const void *elem)
{
	if (zone == NULL || !zone->initialized || elem == NULL) {
		return false;
	}

	return elem_belongs_to_zone(zone, elem);
}
