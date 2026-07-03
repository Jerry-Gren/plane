#ifndef HAL_X86_64_PMAP_H
#define HAL_X86_64_PMAP_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Testable bootstrap clone helper. Normal kernel code should call
 * hal_mmu_take_kernel_page_table_ownership().
 */
bool x86_64_pmap_clone_kernel_page_tables(uint64_t source_pml4_phys,
					  uint64_t *new_pml4_phys);

#endif /* HAL_X86_64_PMAP_H */
