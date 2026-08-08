#ifndef HAL_X86_64_PHYSMAP_INTERNAL_H
#define HAL_X86_64_PHYSMAP_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>

/*
 * x86_64 physmap runtime state shared by the generic conversion wrappers and
 * the pmap ownership handoff. Bootstrap physmap mappings are temporary
 * handoff mappings; after ownership, pmap commits the Plane-owned physmap
 * base here.
 */
struct x86_64_physmap_runtime {
	plane_vaddr_t bootstrap_base;
	uint64_t bootstrap_size;
	uint64_t required_size;
	uint64_t owned_window_size;
	uint64_t owned_pml4_count;
};

uint64_t x86_64_physmap_window_size(void);
void x86_64_physmap_set_bootstrap(plane_vaddr_t base, uint64_t size);
bool x86_64_physmap_get_runtime(struct x86_64_physmap_runtime *runtime);
void x86_64_physmap_commit_owned(void);

#endif /* HAL_X86_64_PHYSMAP_INTERNAL_H */
