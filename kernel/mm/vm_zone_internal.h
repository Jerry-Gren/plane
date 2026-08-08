#ifndef PLANE_VM_ZONE_INTERNAL_H
#define PLANE_VM_ZONE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct plane_vm_zone_segment {
	void *storage;
	uint64_t count;
	struct plane_vm_zone_segment *next;
};

struct plane_vm_zone {
	size_t elem_size;
	struct plane_vm_zone_segment *segments;
	void *free_list;
	uint64_t capacity;
	uint64_t allocated;
	bool initialized;
};

/*
 * Tiny VM metadata zone allocator.
 *
 * This mirrors the ownership shape of XNU zones without implementing a
 * general-purpose allocator: callers supply storage and segment descriptors,
 * allocation returns a zeroed element, and free returns it to the zone.
 * Element size and storage must be pointer-aligned because freed elements
 * carry an intrusive next pointer.
 */
bool plane_vm_zone_init(struct plane_vm_zone *zone,
			size_t elem_size,
			void *storage,
			uint64_t count,
			struct plane_vm_zone_segment *segment);
bool plane_vm_zone_add_storage(struct plane_vm_zone *zone,
			       void *storage,
			       uint64_t count,
			       struct plane_vm_zone_segment *segment);
void *plane_vm_zone_alloc(struct plane_vm_zone *zone);
bool plane_vm_zone_free(struct plane_vm_zone *zone, void *elem);
bool plane_vm_zone_contains(const struct plane_vm_zone *zone, const void *elem);
uint64_t plane_vm_zone_capacity(const struct plane_vm_zone *zone);

#endif /* PLANE_VM_ZONE_INTERNAL_H */
