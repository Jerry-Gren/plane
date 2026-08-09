#ifndef PLANE_VM_MAP_H
#define PLANE_VM_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/bits.h>
#include <plane/spinlock.h>
#include <plane/vm_prot.h>

/* Use the returned user virtual address as the object offset. */
#define PLANE_VM_MAP_OBJECT_OFFSET_AUTO UINT64_MAX
#define PLANE_VM_MAP_ENTER_ANYWHERE BIT(0)
#define PLANE_VM_MAP_ENTER_FIXED BIT(1)
#define PLANE_VM_MAP_ENTER_OVERWRITE BIT(2)
#define PLANE_VM_MAP_ENTER_VA_ONLY BIT(3)

struct plane_vm_object;

/*
 * Kernel virtual map.
 *
 * This is the small kernel_map foundation used by kmem. It only manages
 * virtual address ranges; physical backing and page-table mappings belong to
 * PMM and HAL/pmap.
 *
 * Protection and max_protection are entry attribute foundations. Plane
 * supports XNU-like current protection updates and the shrinking subset of
 * vm_map_protect(set_maximum); soft faults consume these attributes when
 * repairing pmap state.
 */

struct plane_vm_map_stats {
	uint64_t total_pages;
	uint64_t free_pages;
	uint64_t reserved_pages;
	uint64_t user_pages;
	uint64_t map_free_range_count;
	uint64_t allocation_count;
};

/*
 * Callers provide entry storage, but treat both structs as vm_map-owned state.
 * They are exposed so pre-kmem kernel users can instantiate maps.
 *
 * A plane_vm_map object must be zero-initialized before its first init call.
 * Entry storage does not need to be zeroed by callers; init resets it.
 * VM map locking is internal to the VM map owner. Callers must not take or
 * inspect the embedded lock directly.
 */
struct plane_vm_map_entry {
	plane_vaddr_t start;
	plane_vaddr_t end;
	plane_vaddr_t user_start;
	plane_vaddr_t user_end;
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
	struct plane_spinlock lock;
	plane_vaddr_t base;
	plane_vaddr_t end;
	uint64_t head;
	uint64_t tail;
	uint64_t entry_count;
	uint64_t entry_capacity;
	struct plane_vm_map_entry *entries;
	bool initialized;
};

struct plane_vm_map_allocation_info {
	plane_vaddr_t reserved_start;
	uint64_t reserved_pages;
	plane_vaddr_t user_start;
	uint64_t user_pages;
	struct plane_vm_object *object;
	uint64_t object_offset;
	uint64_t wired_count;
	uint32_t prot;
	uint32_t max_prot;
};

struct plane_vm_map_page_info {
	plane_vaddr_t page_vaddr;
	struct plane_vm_object *object;
	uint64_t object_offset;
	uint64_t wired_count;
	uint32_t prot;
	uint32_t max_prot;
};

struct plane_vm_map_enter_options {
	plane_vaddr_t address;
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
		       plane_vaddr_t base,
		       uint64_t size);
/*
 * Move map-owned entry metadata into caller-provided storage. Existing entry
 * indices are preserved; the old storage remains caller-owned.
 */
bool plane_vm_map_rehome_entries(struct plane_vm_map *map,
				 struct plane_vm_map_entry *entries,
				 uint64_t entry_capacity);
/*
 * Enter stores one object reference in the map entry. A NULL object normally
 * creates an anonymous internal object and transfers its initial reference to
 * the entry. PLANE_VM_MAP_ENTER_VA_ONLY opts out for non-faultable device-style
 * reservations; such entries are lookup-allocation visible but lookup-page
 * invisible.
 */
bool plane_vm_map_enter(struct plane_vm_map *map,
			const struct plane_vm_map_enter_options *options,
			plane_vaddr_t *vaddr);
/*
 * Deletes complete entries in a reserved map range. This does not unmap pmap
 * state or release resident pages; callers that own backing state must tear it
 * down first.
 */
bool plane_vm_map_delete_range(struct plane_vm_map *map,
			       plane_vaddr_t start,
			       uint64_t page_count);
/* Lookup and free use an exact user range match. */
bool plane_vm_map_lookup_allocation(
	struct plane_vm_map *map,
	plane_vaddr_t vaddr,
	uint64_t page_count,
	struct plane_vm_map_allocation_info *info);
/* Looks up the single user page containing vaddr; guard pages are holes. */
bool plane_vm_map_lookup_page(struct plane_vm_map *map,
			      plane_vaddr_t vaddr,
			      struct plane_vm_map_page_info *info);
/* Updates current protection metadata for a continuous user range. */
bool plane_vm_map_protect_pages(struct plane_vm_map *map,
				plane_vaddr_t vaddr,
				uint64_t page_count,
				uint32_t prot);
/* Shrinks max protection metadata for a continuous user range. */
bool plane_vm_map_protect_max_pages(struct plane_vm_map *map,
				    plane_vaddr_t vaddr,
				    uint64_t page_count,
				    uint32_t max_prot);
/* Wire/unwire only updates entry metadata; resident page wiring is in fault. */
bool plane_vm_map_wire_pages(struct plane_vm_map *map,
			     plane_vaddr_t vaddr,
			     uint64_t page_count);
bool plane_vm_map_unwire_pages(struct plane_vm_map *map,
			       plane_vaddr_t vaddr,
			       uint64_t page_count);
bool plane_vm_map_free_pages(struct plane_vm_map *map,
			     plane_vaddr_t vaddr,
			     uint64_t page_count);
struct plane_vm_map_stats plane_vm_map_get_stats(struct plane_vm_map *map);

#endif /* PLANE_VM_MAP_H */
