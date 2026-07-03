#ifndef HAL_X86_64_PMAP_H
#define HAL_X86_64_PMAP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>

enum x86_64_pmap_flags {
	X86_64_PMAP_WRITE = BIT(0),
};

uint64_t x86_64_pmap_active_root_phys(void);

/*
 * Testable root helpers. Mutating helpers require a PMM-owned page-table
 * root because they may allocate or free page-table pages. They do not
 * invalidate TLB entries.
 */
bool x86_64_pmap_clone_kernel_page_tables(uint64_t source_pml4_phys,
					  uint64_t *new_pml4_phys);
bool x86_64_pmap_map_page_in_owned_root(uint64_t root_pml4_phys,
					 uint64_t vaddr,
					 uint64_t phys_addr,
					 uint32_t flags);
bool x86_64_pmap_unmap_page_in_owned_root(uint64_t root_pml4_phys,
					   uint64_t vaddr);
bool x86_64_pmap_translate_in_root(uint64_t root_pml4_phys,
				   uint64_t vaddr,
				   uint64_t *phys_addr);

/*
 * Active kernel wrappers. They use the current CR3 root and invalidate the
 * local TLB entry after a successful map or unmap.
 */
bool x86_64_pmap_map_kernel_page(uint64_t vaddr,
				 uint64_t phys_addr,
				 uint32_t flags);
bool x86_64_pmap_unmap_kernel_page(uint64_t vaddr);
bool x86_64_pmap_translate_kernel_page(uint64_t vaddr, uint64_t *phys_addr);
bool x86_64_pmap_protect_kernel_page(uint64_t vaddr, uint32_t flags);

#endif /* HAL_X86_64_PMAP_H */
