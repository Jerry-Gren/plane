#ifndef HAL_MMU_H
#define HAL_MMU_H

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>

struct plane_mem_info;

/*
 * Kernel direct map.
 *
 * This is the long-lived kernel phys-to-virt window, not a per-call mapper
 * and not a boot-protocol handoff window. The mapping must already exist
 * before this API is enabled.
 *
 * Setting the base disables the conversion helpers until the direct map is
 * enabled again. Enabling validates the current direct-map coverage, but does
 * not create or modify page table mappings.
 */
void hal_mmu_set_direct_map_base(uint64_t base);
bool hal_mmu_enable_direct_map(const struct plane_mem_info *mem);
void *hal_mmu_direct_phys_to_virt(uint64_t phys_addr);
uint64_t hal_mmu_direct_virt_to_phys(const void *vaddr);

void hal_mmu_invalidate_tlb(uintptr_t vaddr);
void hal_mmu_flush_tlb_all(void);

#endif /* !__ASSEMBLER__ */
#endif /* HAL_MMU_H */
