#ifndef HAL_MMU_H
#define HAL_MMU_H

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>

struct plane_mem_info;

enum hal_mmu_map_flags {
	HAL_MMU_MAP_WRITE = BIT(0),
};

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
void hal_mmu_set_direct_map_base(uint64_t base);
bool hal_mmu_enable_direct_map(const struct plane_mem_info *mem);

/*
 * Take ownership of the active kernel page-table tree after the direct map
 * and PMM are available. This preserves existing mappings, but moves the
 * mutable page-table pages under kernel allocation.
 */
bool hal_mmu_take_kernel_page_table_ownership(void);
void *hal_mmu_direct_phys_to_virt(uint64_t phys_addr);
void *hal_mmu_direct_phys_range_to_virt(uint64_t phys_addr, uint64_t size);
uint64_t hal_mmu_direct_virt_to_phys(const void *vaddr);

/*
 * Kernel dynamic mapping window. The HAL reports the architecture-owned
 * virtual range; callers own allocation policy inside that range.
 */
bool hal_mmu_kernel_vma_range(uint64_t *base, uint64_t *size);
bool hal_mmu_map_kernel_page(uint64_t vaddr, uint64_t phys_addr,
			     uint32_t flags);
bool hal_mmu_unmap_kernel_page(uint64_t vaddr);
bool hal_mmu_translate_kernel_page(uint64_t vaddr, uint64_t *phys_addr);

void hal_mmu_invalidate_tlb(uintptr_t vaddr);
void hal_mmu_flush_tlb_all(void);

#endif /* !__ASSEMBLER__ */
#endif /* HAL_MMU_H */
