#ifndef HAL_MMU_H
#define HAL_MMU_H

#ifndef __ASSEMBLER__
#include <stdint.h>

void hal_mmu_invalidate_tlb(uintptr_t vaddr);
void hal_mmu_flush_tlb_all(void);

void *hal_mmu_map_early_framebuffer(uint64_t phys_addr, uint64_t size);
void hal_mmu_remove_identity_mapping(void);

void *hal_mmu_phys_to_virt(uintptr_t phys_addr);
uintptr_t hal_mmu_virt_to_phys(void *virt_addr);

#endif /* !__ASSEMBLER__ */
#endif /* HAL_MMU_H */