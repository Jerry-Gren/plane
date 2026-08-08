#ifndef HAL_MMU_H
#define HAL_MMU_H

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>

struct plane_mem_info;

enum hal_mmu_mapping_attr {
	HAL_MMU_MAPPING_DEFAULT,
	HAL_MMU_MAPPING_DEVICE,
	HAL_MMU_MAPPING_WRITE_COMBINE,
};

struct hal_mmu_map_options {
	uint32_t prot;
	enum hal_mmu_mapping_attr attr;
};

static inline struct hal_mmu_map_options hal_mmu_default_map_options(
	uint32_t prot)
{
	return (struct hal_mmu_map_options){
		.prot = prot,
		.attr = HAL_MMU_MAPPING_DEFAULT,
	};
}

#define HAL_MMU_INVALID_PHYS UINT64_MAX

/*
 * Kernel physmap.
 *
 * This is the long-lived kernel phys-to-virt window, not a per-call mapper
 * and not a boot-protocol handoff window. The mapping must already exist
 * before this API is enabled.
 *
 * Enabling derives the runtime RAM-like coverage required by the memory map.
 * Architecture code owns bootstrap physmap setup and later rebuilds the
 * Plane-owned physmap subtree during page-table ownership handoff.
 */
bool hal_mmu_enable_physmap(const struct plane_mem_info *mem);

/*
 * Take ownership of the active kernel page-table tree after the physmap
 * and PMM are available. This preserves ordinary kernel mappings, skips
 * bootstrap physmap mappings, and installs Plane's owned physmap.
 */
bool hal_mmu_take_kernel_page_table_ownership(void);
plane_vaddr_t hal_mmu_physmap_phys_to_virt(plane_paddr_t phys_addr);
plane_vaddr_t hal_mmu_physmap_phys_range_to_virt(plane_paddr_t phys_addr,
						uint64_t size);
plane_paddr_t hal_mmu_physmap_virt_to_phys(plane_vaddr_t vaddr);

/*
 * Kernel dynamic mapping window. The HAL reports the architecture-owned
 * virtual range; callers own allocation policy inside that range.
 */
bool hal_mmu_kernel_vma_range(plane_vaddr_t *base, uint64_t *size);
bool hal_mmu_map_kernel_page(plane_vaddr_t vaddr, plane_paddr_t phys_addr,
			     struct hal_mmu_map_options options);
bool hal_mmu_unmap_kernel_page(plane_vaddr_t vaddr);
bool hal_mmu_translate_kernel_page(plane_vaddr_t vaddr,
				   plane_paddr_t *phys_addr);
bool hal_mmu_protect_kernel_page(plane_vaddr_t vaddr, uint32_t prot);

void hal_mmu_invalidate_tlb(plane_vaddr_t vaddr);
void hal_mmu_flush_tlb_all(void);

#endif /* !__ASSEMBLER__ */
#endif /* HAL_MMU_H */
