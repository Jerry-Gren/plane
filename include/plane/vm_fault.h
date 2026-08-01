#ifndef PLANE_VM_FAULT_H
#define PLANE_VM_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/vm_prot.h>

struct plane_vm_map;

/*
 * Early XNU-like soft fault core.
 *
 * This handles a single kernel-map user page: map lookup, current protection
 * check, resident page lookup or zero-fill allocation, object insertion, and
 * pmap enter/repair. Internally this is split like a reduced vm_fault_page()
 * plus vm_fault_enter() path. This is also the backing path for lazy kernel
 * object allocations. x86_64 #PF dispatch can use this through kmem's kernel
 * map wrapper, but this does not implement pager, COW, pageout, busy/wanted,
 * locks, submaps, or user-map faults.
 */
bool plane_vm_fault_page(struct plane_vm_map *map,
			 plane_vaddr_t vaddr,
			 uint32_t fault_type);

#endif /* PLANE_VM_FAULT_H */
