#include <stddef.h>

#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>
#include <plane/memmap.h>

static uint64_t direct_map_base = X86_64_DIRECT_MAP_BASE;
static bool direct_map_initialized;

static bool checked_add_u64(uint64_t lhs, uint64_t rhs, uint64_t *out)
{
	if (rhs > UINT64_MAX - lhs) {
		return false;
	}

	*out = lhs + rhs;
	return true;
}

void hal_mmu_set_direct_map_base(uint64_t base)
{
	direct_map_base = base;
	direct_map_initialized = false;
}

bool hal_mmu_enable_direct_map(const struct plane_mem_info *mem)
{
	uint64_t direct_map_end;

	direct_map_initialized = false;

	if (mem == NULL || (direct_map_base & (ARCH_LARGE_PAGE_SIZE - 1)) != 0) {
		return false;
	}

	if (!checked_add_u64(direct_map_base, X86_64_DIRECT_MAP_SIZE,
			     &direct_map_end) ||
	    (KERNEL_VMA_BASE >= direct_map_base &&
	     KERNEL_VMA_BASE < direct_map_end)) {
		return false;
	}

	for (uint64_t i = 0; i < mem->entry_count; i++) {
		const struct plane_mem_region *region = &mem->map[i];
		uint64_t end;

		if (region->type != PLANE_MEM_USABLE || region->length == 0) {
			continue;
		}

		if (!checked_add_u64(region->base, region->length, &end) ||
		    end > X86_64_DIRECT_MAP_SIZE) {
			return false;
		}
	}

	direct_map_initialized = true;
	return true;
}

void *hal_mmu_direct_phys_to_virt(uint64_t phys_addr)
{
	if (!direct_map_initialized || phys_addr >= X86_64_DIRECT_MAP_SIZE) {
		return NULL;
	}

	return (void *)(direct_map_base + phys_addr);
}

uint64_t hal_mmu_direct_virt_to_phys(const void *vaddr)
{
	uint64_t addr = (uint64_t)(uintptr_t)vaddr;
	uint64_t offset;

	if (!direct_map_initialized || addr < direct_map_base) {
		return UINT64_MAX;
	}

	offset = addr - direct_map_base;
	if (offset >= X86_64_DIRECT_MAP_SIZE) {
		return UINT64_MAX;
	}

	return offset;
}

void hal_mmu_invalidate_tlb(uintptr_t vaddr)
{
	__asm__ volatile ("invlpg (%0)" : : "r" (vaddr) : "memory");
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
