#ifndef PLANE_VM_MAP_H
#define PLANE_VM_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/vm_prot.h>

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
 */
struct plane_vm_map_entry {
	uint64_t start;
	uint64_t end;
	uint64_t user_start;
	uint64_t user_end;
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
	uint32_t prot;
	uint32_t max_prot;
};

bool plane_vm_map_init(struct plane_vm_map *map,
		       struct plane_vm_map_entry *entries,
		       uint64_t entry_capacity,
		       uint64_t base,
		       uint64_t size);
bool plane_vm_map_alloc_pages(struct plane_vm_map *map,
			      uint64_t page_count,
			      uint64_t *vaddr);
bool plane_vm_map_alloc_pages_protected(struct plane_vm_map *map,
					uint64_t page_count,
					uint64_t guard_pages,
					uint32_t prot,
					uint64_t *vaddr);
bool plane_vm_map_alloc_pages_protected_max(struct plane_vm_map *map,
					    uint64_t page_count,
					    uint64_t guard_pages,
					    uint32_t prot,
					    uint32_t max_prot,
					    uint64_t *vaddr);
/*
 * Reserves a virtual range with guard_pages before and after the user range.
 * The returned address is the user range start. Allocation lookup and free use
 * an exact user range match, and free removes the whole reserved entry.
 */
bool plane_vm_map_alloc_pages_guarded(struct plane_vm_map *map,
				      uint64_t page_count,
				      uint64_t guard_pages,
				      uint64_t *vaddr);
bool plane_vm_map_has_allocation(struct plane_vm_map *map,
				 uint64_t vaddr,
				 uint64_t page_count);
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
bool plane_vm_map_free_pages(struct plane_vm_map *map,
			     uint64_t vaddr,
			     uint64_t page_count);
struct plane_vm_map_stats plane_vm_map_get_stats(struct plane_vm_map *map);

#endif /* PLANE_VM_MAP_H */
