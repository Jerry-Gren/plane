#ifndef PLANE_VM_FAULT_H
#define PLANE_VM_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/vm_prot.h>

struct plane_vm_map;

/*
 * Early XNU-like soft fault core.
 *
 * This handles a single kernel-map user page: map lookup, current protection
 * check, resident page lookup or zero-fill allocation, object insertion, and
 * pmap enter/repair. It is not wired to hardware #PF yet and does not
 * implement pager, COW, pageout, busy/wanted, locks, or submaps.
 */
bool plane_vm_fault_page(struct plane_vm_map *map,
			 uint64_t vaddr,
			 uint32_t fault_type);

#endif /* PLANE_VM_FAULT_H */
