#include <stddef.h>

#include <hal/mmu.h>
#include <hal/x86_64/address_space.h>
#include <plane/memmap.h>
#include <plane/overflow.h>

#include <x86_64/physmap_internal.h>

static uint64_t physmap_base = X86_64_PHYSMAP_BASE;
static uint64_t physmap_bootstrap_size = X86_64_PHYSMAP_BOOTSTRAP_SIZE;
static uint64_t physmap_required_size;
static uint64_t physmap_owned_window_size;
static bool physmap_initialized;

static bool physmap_covers_region_type(uint32_t type)
{
	return type == PLANE_MEM_USABLE ||
	       type == PLANE_MEM_BOOTLOADER_RECLAIMABLE ||
	       type == PLANE_MEM_EXECUTABLE_AND_MODULES;
}

static bool ranges_overlap(uint64_t base, uint64_t end,
			   uint64_t other_base, uint64_t other_end)
{
	return base < other_end && other_base < end;
}

uint64_t x86_64_physmap_window_size(void)
{
	return X86_64_PHYSMAP_WINDOW_SIZE;
}

void x86_64_physmap_set_bootstrap_window(plane_vaddr_t base, uint64_t size)
{
	physmap_base = plane_vaddr_raw(base);
	physmap_bootstrap_size = size;
	physmap_required_size = 0;
	physmap_owned_window_size = 0;
	physmap_initialized = false;
}

bool x86_64_physmap_install_bootstrap_window(plane_vaddr_t base)
{
	if (plane_vaddr_is_null(base)) {
		return false;
	}

	x86_64_physmap_set_bootstrap_window(base, x86_64_physmap_window_size());
	return true;
}

bool hal_mmu_enable_physmap(const struct plane_mem_info *mem)
{
	uint64_t physmap_end;
	uint64_t required_size = 0;
	uint64_t owned_window_size = 0;

	physmap_initialized = false;
	physmap_required_size = 0;
	physmap_owned_window_size = 0;

	if (mem == NULL || physmap_bootstrap_size == 0 ||
	    (physmap_base & (ARCH_LARGE_PAGE_SIZE - 1)) != 0 ||
	    (physmap_bootstrap_size & (ARCH_LARGE_PAGE_SIZE - 1)) != 0 ||
	    physmap_bootstrap_size > X86_64_PHYSMAP_WINDOW_SIZE) {
		return false;
	}

	if (!plane_checked_add_u64(physmap_base,
				   physmap_bootstrap_size,
				   &physmap_end) ||
	    (KERNEL_VMA_BASE >= physmap_base &&
	     KERNEL_VMA_BASE < physmap_end) ||
	    ranges_overlap(physmap_base, physmap_end,
			   X86_64_KERNEL_MAP_BASE, X86_64_KERNEL_MAP_END)) {
		return false;
	}

	for (uint64_t i = 0; i < mem->entry_count; i++) {
		const struct plane_mem_region *region = &mem->map[i];
		uint64_t region_base = plane_paddr_raw(region->base);
		uint64_t end;

		if (!physmap_covers_region_type(region->type) ||
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
	    required_size > physmap_bootstrap_size ||
	    !plane_checked_align_up_u64(required_size,
					X86_64_PAGING_PML4_SLOT_SIZE,
					&owned_window_size) ||
	    required_size > X86_64_PHYSMAP_WINDOW_SIZE) {
		return false;
	}

	physmap_required_size = required_size;
	physmap_owned_window_size = owned_window_size;
	physmap_initialized = true;
	return true;
}

bool x86_64_physmap_get_runtime(struct x86_64_physmap_runtime *runtime)
{
	if (runtime == NULL || !physmap_initialized) {
		return false;
	}

	runtime->bootstrap_base = plane_vaddr_make(physmap_base);
	runtime->bootstrap_size = physmap_bootstrap_size;
	runtime->required_size = physmap_required_size;
	runtime->owned_window_size = physmap_owned_window_size;
	runtime->owned_pml4_count =
		physmap_owned_window_size / X86_64_PAGING_PML4_SLOT_SIZE;
	return true;
}

void x86_64_physmap_commit_owned(void)
{
	physmap_base = X86_64_PHYSMAP_BASE;
}

plane_vaddr_t hal_mmu_physmap_phys_to_virt(plane_paddr_t phys_addr)
{
	return hal_mmu_physmap_phys_range_to_virt(phys_addr, 1);
}

plane_vaddr_t hal_mmu_physmap_phys_range_to_virt(plane_paddr_t phys_addr,
						uint64_t size)
{
	uint64_t raw_phys = plane_paddr_raw(phys_addr);
	uint64_t end;
	uint64_t vaddr;

	if (!physmap_initialized || size == 0 ||
	    !plane_checked_add_u64(raw_phys, size, &end) ||
	    end > physmap_required_size) {
		return plane_vaddr_make(0);
	}

	if (!plane_checked_add_u64(physmap_base, raw_phys, &vaddr)) {
		return plane_vaddr_make(0);
	}

	return plane_vaddr_make(vaddr);
}

plane_paddr_t hal_mmu_physmap_virt_to_phys(plane_vaddr_t vaddr)
{
	uint64_t addr = plane_vaddr_raw(vaddr);
	uint64_t offset;

	if (!physmap_initialized || addr < physmap_base) {
		return plane_paddr_make(HAL_MMU_INVALID_PHYS);
	}

	offset = addr - physmap_base;
	if (offset >= physmap_required_size) {
		return plane_paddr_make(HAL_MMU_INVALID_PHYS);
	}

	return plane_paddr_make(offset);
}
