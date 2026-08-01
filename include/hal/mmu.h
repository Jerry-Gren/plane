#ifndef HAL_MMU_H
#define HAL_MMU_H

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/bits.h>

struct plane_mem_info;

enum hal_mmu_map_flags {
	HAL_MMU_MAP_WRITE = BIT(0),
};

#define HAL_MMU_INVALID_PHYS UINT64_MAX

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
void hal_mmu_set_direct_map_base(plane_vaddr_t base);
bool hal_mmu_enable_direct_map(const struct plane_mem_info *mem);

/*
 * Take ownership of the active kernel page-table tree after the direct map
 * and PMM are available. This preserves existing mappings, but moves the
 * mutable page-table pages under kernel allocation.
 */
bool hal_mmu_take_kernel_page_table_ownership(void);
plane_vaddr_t hal_mmu_direct_phys_to_virt(plane_paddr_t phys_addr);
plane_vaddr_t hal_mmu_direct_phys_range_to_virt(plane_paddr_t phys_addr,
						uint64_t size);
plane_paddr_t hal_mmu_direct_virt_to_phys(plane_vaddr_t vaddr);

/*
 * Kernel dynamic mapping window. The HAL reports the architecture-owned
 * virtual range; callers own allocation policy inside that range.
 */
bool hal_mmu_kernel_vma_range(plane_vaddr_t *base, uint64_t *size);
bool hal_mmu_map_kernel_page(plane_vaddr_t vaddr, plane_paddr_t phys_addr,
			     uint32_t flags);
bool hal_mmu_unmap_kernel_page(plane_vaddr_t vaddr);
bool hal_mmu_translate_kernel_page(plane_vaddr_t vaddr,
				   plane_paddr_t *phys_addr);
bool hal_mmu_protect_kernel_page(plane_vaddr_t vaddr, uint32_t flags);

void hal_mmu_invalidate_tlb(plane_vaddr_t vaddr);
void hal_mmu_flush_tlb_all(void);

#endif /* !__ASSEMBLER__ */
#endif /* HAL_MMU_H */
