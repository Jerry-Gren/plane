#ifndef HAL_MMU_H
#define HAL_MMU_H

#include <stdint.h>

void hal_mmu_invalidate_tlb(uintptr_t vaddr);

void hal_mmu_reload_cr3(void);

#endif /* HAL_MMU_H */