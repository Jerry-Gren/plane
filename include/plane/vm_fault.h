#ifndef PLANE_VM_FAULT_H
#define PLANE_VM_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/vm_prot.h>

struct plane_vm_map;

/*
 * Minimal XNU-like soft fault.
 *
 * This handles a single kernel-map user page: map lookup, current protection
 * check, resident page lookup or zero-fill allocation, object insertion, and
 * pmap enter/repair. Internally this is split like a reduced vm_fault_page()
 * plus vm_fault_enter() path. The range wrapper is an XNU-like reduced
 * pre-fault path: it advances page by page and preserves successful earlier
 * faults if a later page fails. Fault wiring is the reduced vm_fault_wire()
 * layer: it wires map metadata, faults pages in, and synchronizes resident
 * page wire counts. This is also the backing path for lazy kernel object
 * allocations. Architecture page-fault dispatch can use this through kmem's
 * kernel map wrapper, but this does not implement pager, COW, pageout,
 * busy/wanted, locks, submaps, clustering, pmap pageable wiring, or user-map
 * faults.
 */
bool plane_vm_fault_page(struct plane_vm_map *map,
			 plane_vaddr_t vaddr,
			 uint32_t fault_type);
bool plane_vm_fault_pages(struct plane_vm_map *map,
			  plane_vaddr_t vaddr,
			  uint64_t page_count,
			  uint32_t fault_type);
bool plane_vm_fault_wire_pages(struct plane_vm_map *map,
			       plane_vaddr_t vaddr,
			       uint64_t page_count,
			       uint32_t fault_type);
bool plane_vm_fault_unwire_pages(struct plane_vm_map *map,
				 plane_vaddr_t vaddr,
				 uint64_t page_count);

#endif /* PLANE_VM_FAULT_H */
