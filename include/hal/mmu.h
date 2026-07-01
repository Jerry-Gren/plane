#ifndef HAL_MMU_H
#define HAL_MMU_H

#ifndef __ASSEMBLER__
#include <stdint.h>

void hal_mmu_invalidate_tlb(uintptr_t vaddr);
void hal_mmu_flush_tlb_all(void);

#endif /* !__ASSEMBLER__ */
#endif /* HAL_MMU_H */
