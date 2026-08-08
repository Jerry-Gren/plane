#ifndef HAL_X86_64_PMAP_H
#define HAL_X86_64_PMAP_H

#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>

#include <plane/address.h>

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
