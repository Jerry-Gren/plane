#ifndef HAL_X86_64_MMU_INTERNAL_H
#define HAL_X86_64_MMU_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>

/*
 * x86_64 direct-map runtime state shared by the address conversion helpers and
 * the pmap ownership handoff. Boot-protocol direct maps are temporary bridges;
 * after ownership, pmap commits the Plane-owned physmap base here.
 */
struct x86_64_mmu_direct_map_runtime {
	plane_vaddr_t boot_base;
	uint64_t boot_bridge_size;
	uint64_t required_size;
	uint64_t owned_window_size;
	uint64_t owned_pml4_count;
};

bool x86_64_mmu_direct_map_runtime(
	struct x86_64_mmu_direct_map_runtime *runtime);
void x86_64_mmu_commit_owned_direct_map(void);

#endif /* HAL_X86_64_MMU_INTERNAL_H */
