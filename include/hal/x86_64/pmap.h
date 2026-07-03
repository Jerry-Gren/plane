#ifndef HAL_X86_64_PMAP_H
#define HAL_X86_64_PMAP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>

enum x86_64_pmap_flags {
	X86_64_PMAP_WRITE = BIT(0),
};

/*
 * Testable bootstrap clone helper. Normal kernel code should call
 * hal_mmu_take_kernel_page_table_ownership().
 */
bool x86_64_pmap_clone_kernel_page_tables(uint64_t source_pml4_phys,
					  uint64_t *new_pml4_phys);
bool x86_64_pmap_map_page(uint64_t root_pml4_phys,
			  uint64_t vaddr,
			  uint64_t phys_addr,
			  uint32_t flags);
bool x86_64_pmap_unmap_page(uint64_t root_pml4_phys, uint64_t vaddr);
bool x86_64_pmap_translate(uint64_t root_pml4_phys,
			   uint64_t vaddr,
			   uint64_t *phys_addr);

#endif /* HAL_X86_64_PMAP_H */
