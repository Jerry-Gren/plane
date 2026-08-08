#ifndef HAL_X86_64_PMAP_H
#define HAL_X86_64_PMAP_H

#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>

#include <plane/address.h>

struct x86_64_pmap_skip_range {
	plane_vaddr_t base;
	uint64_t size;
};

plane_paddr_t x86_64_pmap_active_root_phys(void);

/*
 * Testable root helpers. Mutating helpers require a PMM-owned page-table
 * root because they may allocate or free page-table pages. They do not
 * invalidate TLB entries.
 */
bool x86_64_pmap_clone_kernel_page_tables(plane_paddr_t source_pml4_phys,
					  const struct x86_64_pmap_skip_range *skip,
					  uint64_t skip_count,
					  plane_paddr_t *new_pml4_phys);
bool x86_64_pmap_build_direct_map_in_owned_root(plane_paddr_t root_pml4_phys,
						plane_vaddr_t base,
						uint64_t required_size,
						uint64_t window_size);
bool x86_64_pmap_map_page_in_owned_root(plane_paddr_t root_pml4_phys,
					 plane_vaddr_t vaddr,
					 plane_paddr_t phys_addr,
					 struct hal_mmu_map_options options);
bool x86_64_pmap_unmap_page_in_owned_root(plane_paddr_t root_pml4_phys,
					   plane_vaddr_t vaddr);
bool x86_64_pmap_translate_in_root(plane_paddr_t root_pml4_phys,
				   plane_vaddr_t vaddr,
				   plane_paddr_t *phys_addr);

/*
 * Active kernel wrappers. They use the current CR3 root and invalidate the
 * local TLB entry after a successful map or unmap.
 */
bool x86_64_pmap_map_kernel_page(plane_vaddr_t vaddr,
				 plane_paddr_t phys_addr,
				 struct hal_mmu_map_options options);
bool x86_64_pmap_unmap_kernel_page(plane_vaddr_t vaddr);
bool x86_64_pmap_translate_kernel_page(plane_vaddr_t vaddr,
				       plane_paddr_t *phys_addr);
bool x86_64_pmap_protect_kernel_page(plane_vaddr_t vaddr, uint32_t prot);

#endif /* HAL_X86_64_PMAP_H */
