#include <stddef.h>

#include <hal/mmu.h>

#include <plane/kmem.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/vm_map.h>

static bool kmem_initialized;

static bool checked_add_u64(uint64_t lhs, uint64_t rhs, uint64_t *out)
{
	if (rhs > UINT64_MAX - lhs) {
		return false;
	}

	*out = lhs + rhs;
	return true;
}

static bool checked_mul_u64(uint64_t lhs, uint64_t rhs, uint64_t *out)
{
	if (lhs != 0 && rhs > UINT64_MAX / lhs) {
		return false;
	}

	*out = lhs * rhs;
	return true;
}

static bool checked_page_offset(uint64_t page_index, uint64_t *offset)
{
	return checked_mul_u64(page_index, PAGE_SIZE, offset);
}

static bool is_page_aligned(uint64_t value)
{
	return (value & (PAGE_SIZE - 1)) == 0;
}

static bool kmem_flags_valid(uint32_t flags)
{
	return (flags & ~PLANE_KMEM_ALLOC_ZERO) == 0;
}

static uint32_t kmem_to_pmm_flags(uint32_t flags)
{
	uint32_t pmm_flags = 0;

	if ((flags & PLANE_KMEM_ALLOC_ZERO) != 0) {
		pmm_flags |= PLANE_PMM_ALLOC_ZERO;
	}

	return pmm_flags;
}

static bool rollback_mapped_pages(uint64_t vaddr, uint64_t page_count)
{
	for (uint64_t i = page_count; i > 0; i--) {
		uint64_t page_vaddr;
		uint64_t phys_addr;
		uint64_t offset;

		if (!checked_page_offset(i - 1, &offset) ||
		    !checked_add_u64(vaddr, offset, &page_vaddr) ||
		    !hal_mmu_translate_kernel_page(page_vaddr, &phys_addr) ||
		    !hal_mmu_unmap_kernel_page(page_vaddr) ||
		    !plane_pmm_free_page_phys(phys_addr)) {
			return false;
		}
	}

	return true;
}

static bool rollback_allocated_page(uint64_t phys_addr)
{
	return plane_pmm_free_page_phys(phys_addr);
}

static bool map_allocated_pages(uint64_t vaddr,
				uint64_t page_count,
				uint32_t flags)
{
	uint32_t pmm_flags = kmem_to_pmm_flags(flags);
	uint64_t mapped_pages = 0;

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_vaddr;
		uint64_t phys_addr;
		uint64_t offset;

		if (!checked_page_offset(i, &offset) ||
		    !checked_add_u64(vaddr, offset, &page_vaddr)) {
			if (!rollback_mapped_pages(vaddr, mapped_pages)) {
				return false;
			}
			return false;
		}

		if (!plane_pmm_alloc_page_flags(pmm_flags, &page)) {
			if (!rollback_mapped_pages(vaddr, mapped_pages)) {
				return false;
			}
			return false;
		}

		phys_addr = plane_page_phys(page);
		if (phys_addr == UINT64_MAX ||
		    !hal_mmu_map_kernel_page(page_vaddr, phys_addr,
					     HAL_MMU_MAP_WRITE)) {
			if (!rollback_allocated_page(phys_addr) ||
			    !rollback_mapped_pages(vaddr, mapped_pages)) {
				return false;
			}
			return false;
		}

		mapped_pages++;
	}

	return true;
}

bool plane_kmem_init(void)
{
	uint64_t base;
	uint64_t size;

	kmem_initialized = false;

	if (!hal_mmu_kernel_vma_range(&base, &size) ||
	    !plane_kernel_map_init(base, size)) {
		return false;
	}

	kmem_initialized = true;
	return true;
}

bool plane_kmem_alloc_pages(uint64_t page_count, uint32_t flags, void **vaddr)
{
	uint64_t base;

	if (vaddr == NULL ||
	    !kmem_initialized ||
	    page_count == 0 ||
	    !kmem_flags_valid(flags)) {
		return false;
	}

	if (!plane_kernel_map_alloc_pages(page_count, &base)) {
		return false;
	}

	if (!map_allocated_pages(base, page_count, flags)) {
		if (!plane_kernel_map_free_pages(base, page_count)) {
			return false;
		}
		return false;
	}

	*vaddr = (void *)(uintptr_t)base;
	return true;
}

bool plane_kmem_free_pages(void *vaddr, uint64_t page_count)
{
	uint64_t addr = (uint64_t)(uintptr_t)vaddr;

	if (!kmem_initialized ||
	    vaddr == NULL ||
	    page_count == 0 ||
	    !is_page_aligned(addr)) {
		return false;
	}

	if (!plane_kernel_map_has_allocation(addr, page_count)) {
		return false;
	}

	if (!rollback_mapped_pages(addr, page_count)) {
		return false;
	}

	return plane_kernel_map_free_pages(addr, page_count);
}
