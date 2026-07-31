#ifndef PLANE_VM_MAP_H
#define PLANE_VM_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>
#include <plane/vm_prot.h>

/* Use the returned user virtual address as the object offset. */
#define PLANE_VM_MAP_OBJECT_OFFSET_AUTO UINT64_MAX
#define PLANE_VM_MAP_ENTER_ANYWHERE BIT(0)
#define PLANE_VM_MAP_ENTER_FIXED BIT(1)
#define PLANE_VM_MAP_ENTER_OVERWRITE BIT(2)

struct plane_vm_object;

/*
 * Early kernel virtual map.
 *
 * This is the small kernel_map foundation used by kmem. It only manages
 * virtual address ranges; physical backing and page-table mappings belong to
 * PMM and HAL/pmap.
 *
 * Protection and max_protection are entry attribute foundations. Explicit
 * max protection is allocation-time metadata, not a complete
 * vm_map_protect(set_maximum) implementation.
 */

struct plane_vm_map_stats {
	uint64_t total_pages;
	uint64_t free_pages;
	uint64_t reserved_pages;
	uint64_t user_pages;
	uint64_t free_range_count;
	uint64_t allocation_count;
};

/*
 * Callers provide entry storage, but treat both structs as vm_map-owned state.
 * They are exposed so early kernel users can instantiate maps without kmem.
 *
 * A plane_vm_map object must be zero-initialized before its first init call.
 * Entry storage does not need to be zeroed by callers; init resets it.
 */
struct plane_vm_map_entry {
	uint64_t start;
	uint64_t end;
	uint64_t user_start;
	uint64_t user_end;
	struct plane_vm_object *object;
	uint64_t object_offset;
	uint64_t wired_count;
	uint32_t prot;
	uint32_t max_prot;
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
	uint64_t entry_capacity;
	struct plane_vm_map_entry *entries;
	bool initialized;
};

struct plane_vm_map_allocation_info {
	uint64_t reserved_start;
	uint64_t reserved_pages;
	uint64_t user_start;
	uint64_t user_pages;
	struct plane_vm_object *object;
	uint64_t object_offset;
	uint64_t wired_count;
	uint32_t prot;
	uint32_t max_prot;
};

struct plane_vm_map_enter_options {
	uint64_t address;
	uint64_t page_count;
	uint64_t guard_pages;
	struct plane_vm_object *object;
	uint64_t object_offset;
	uint32_t prot;
	uint32_t max_prot;
	uint32_t flags;
};

bool plane_vm_map_init(struct plane_vm_map *map,
		       struct plane_vm_map_entry *entries,
		       uint64_t entry_capacity,
		       uint64_t base,
		       uint64_t size);
/*
 * Enter stores one object reference in the map entry. A NULL object creates an
 * anonymous internal object and transfers its initial reference to the entry.
 */
bool plane_vm_map_enter(struct plane_vm_map *map,
			const struct plane_vm_map_enter_options *options,
			uint64_t *vaddr);
/*
 * Deletes complete entries in a reserved map range. This does not unmap pmap
 * state or release resident pages; callers that own backing state must tear it
 * down first.
 */
bool plane_vm_map_delete_range(struct plane_vm_map *map,
			       uint64_t start,
			       uint64_t page_count);
/* Lookup and free use an exact user range match. */
bool plane_vm_map_lookup_allocation(
	struct plane_vm_map *map,
	uint64_t vaddr,
	uint64_t page_count,
	struct plane_vm_map_allocation_info *info);
/* Updates protection metadata for an exact user allocation range. */
bool plane_vm_map_protect_pages(struct plane_vm_map *map,
				uint64_t vaddr,
				uint64_t page_count,
				uint32_t prot);
bool plane_vm_map_wire_pages(struct plane_vm_map *map,
			     uint64_t vaddr,
			     uint64_t page_count);
bool plane_vm_map_unwire_pages(struct plane_vm_map *map,
			       uint64_t vaddr,
			       uint64_t page_count);
bool plane_vm_map_free_pages(struct plane_vm_map *map,
			     uint64_t vaddr,
			     uint64_t page_count);
struct plane_vm_map_stats plane_vm_map_get_stats(struct plane_vm_map *map);

#endif /* PLANE_VM_MAP_H */
