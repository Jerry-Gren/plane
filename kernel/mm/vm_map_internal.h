#ifndef PLANE_VM_MAP_INTERNAL_H
#define PLANE_VM_MAP_INTERNAL_H

#include <stdbool.h>

#include <plane/vm_map.h>

/*
 * Fault-facing lookup result.
 *
 * The embedded info owns a temporary object reference when has_object_ref is true.
 * Release with plane_vm_map_release_page_ref() on every success/failure path.
 */
struct plane_vm_map_page_ref {
	struct plane_vm_map_page_info info;
	bool has_object_ref;
};

bool plane_vm_map_lookup_page_ref(struct plane_vm_map *map,
				  plane_vaddr_t vaddr,
				  struct plane_vm_map_page_ref *ref);
void plane_vm_map_release_page_ref(struct plane_vm_map_page_ref *ref);

#endif /* PLANE_VM_MAP_INTERNAL_H */
