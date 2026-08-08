#include <stddef.h>

#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>
#include <hal/x86_64/mmu_internal.h>
#include <plane/memmap.h>
#include <plane/overflow.h>

static uint64_t direct_map_base = X86_64_DIRECT_MAP_BASE;
static uint64_t direct_map_size;
static bool direct_map_initialized;

static bool direct_map_covers_region_type(uint32_t type)
{
	return type == PLANE_MEM_USABLE ||
	       type == PLANE_MEM_BOOTLOADER_RECLAIMABLE ||
	       type == PLANE_MEM_EXECUTABLE_AND_MODULES;
}

void hal_mmu_set_direct_map_base(plane_vaddr_t base)
{
	direct_map_base = plane_vaddr_raw(base);
	direct_map_size = 0;
	direct_map_initialized = false;
}

bool hal_mmu_enable_direct_map(const struct plane_mem_info *mem)
{
	uint64_t direct_map_end;
	uint64_t required_size = 0;

	direct_map_initialized = false;
	direct_map_size = 0;

	if (mem == NULL || (direct_map_base & (ARCH_LARGE_PAGE_SIZE - 1)) != 0) {
		return false;
	}

	if (!plane_checked_add_u64(direct_map_base,
				   X86_64_DIRECT_MAP_WINDOW_SIZE,
				   &direct_map_end) ||
	    (KERNEL_VMA_BASE >= direct_map_base &&
	     KERNEL_VMA_BASE < direct_map_end)) {
		return false;
	}

	for (uint64_t i = 0; i < mem->entry_count; i++) {
		const struct plane_mem_region *region = &mem->map[i];
		uint64_t region_base = plane_paddr_raw(region->base);
		uint64_t end;

		if (!direct_map_covers_region_type(region->type) ||
		    region->length == 0) {
			continue;
		}

		if (!plane_checked_add_u64(region_base, region->length, &end)) {
			return false;
		}
		if (end > required_size) {
			required_size = end;
		}
	}

	if (!plane_checked_align_up_u64(required_size, ARCH_LARGE_PAGE_SIZE,
					&required_size) ||
	    required_size > X86_64_DIRECT_MAP_WINDOW_SIZE) {
		return false;
	}

	direct_map_size = required_size;
	direct_map_initialized = true;
	return true;
}

bool x86_64_mmu_direct_map_runtime(plane_vaddr_t *base, uint64_t *size)
{
	if (base == NULL || size == NULL || !direct_map_initialized) {
		return false;
	}

	*base = plane_vaddr_make(direct_map_base);
	*size = direct_map_size;
	return true;
}

void x86_64_mmu_commit_owned_direct_map(void)
{
	direct_map_base = X86_64_DIRECT_MAP_BASE;
}

plane_vaddr_t hal_mmu_direct_phys_to_virt(plane_paddr_t phys_addr)
{
	return hal_mmu_direct_phys_range_to_virt(phys_addr, 1);
}

plane_vaddr_t hal_mmu_direct_phys_range_to_virt(plane_paddr_t phys_addr,
						uint64_t size)
{
	uint64_t raw_phys = plane_paddr_raw(phys_addr);
	uint64_t end;
	uint64_t vaddr;

	if (!direct_map_initialized || size == 0 ||
	    !plane_checked_add_u64(raw_phys, size, &end) ||
	    end > direct_map_size) {
		return plane_vaddr_make(0);
	}

	if (!plane_checked_add_u64(direct_map_base, raw_phys, &vaddr)) {
		return plane_vaddr_make(0);
	}

	return plane_vaddr_make(vaddr);
}

plane_paddr_t hal_mmu_direct_virt_to_phys(plane_vaddr_t vaddr)
{
	uint64_t addr = plane_vaddr_raw(vaddr);
	uint64_t offset;

	if (!direct_map_initialized || addr < direct_map_base) {
		return plane_paddr_make(HAL_MMU_INVALID_PHYS);
	}

	offset = addr - direct_map_base;
	if (offset >= direct_map_size) {
		return plane_paddr_make(HAL_MMU_INVALID_PHYS);
	}

	return plane_paddr_make(offset);
}

bool hal_mmu_kernel_vma_range(plane_vaddr_t *base, uint64_t *size)
{
	if (base == NULL || size == NULL) {
		return false;
	}

	*base = plane_vaddr_make(X86_64_KERNEL_MAP_BASE);
	*size = X86_64_KERNEL_MAP_SIZE;
	return true;
}

void hal_mmu_invalidate_tlb(plane_vaddr_t vaddr)
{
	/*
	 * INVLPG invalidates cached translations for one linear address on the
	 * current CPU. Cross-CPU shootdown comes with the later SMP pmap path.
	 */
	__asm__ volatile ("invlpg (%0)" : : "r" (plane_vaddr_raw(vaddr)) : "memory");
}

void hal_mmu_flush_tlb_all(void)
{
	__asm__ volatile (
		"mov %%cr3, %%rax\n\t"
		"mov %%rax, %%cr3\n\t"
		: /* no input */
		: /* no output */
		: "rax", "memory"
	);
}
