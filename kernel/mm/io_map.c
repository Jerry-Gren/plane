#include <hal/mmu.h>
#include <hal/page.h>

#include <plane/io_map.h>
#include <plane/mm.h>
#include <plane/overflow.h>
#include <plane/printk.h>
#include <plane/util.h>

#include "kmem_internal.h"

static bool io_map_initialized;

static bool io_map_cache_is_valid(enum plane_io_map_cache cache)
{
	return cache == PLANE_IO_MAP_CACHE_DEVICE ||
	       cache == PLANE_IO_MAP_CACHE_WRITE_COMBINE;
}

static struct hal_mmu_map_options io_map_options(uint32_t prot,
						 enum plane_io_map_cache cache)
{
	return (struct hal_mmu_map_options){
		.prot = prot,
		.attr = cache == PLANE_IO_MAP_CACHE_DEVICE ?
			HAL_MMU_MAPPING_DEVICE :
			HAL_MMU_MAPPING_WRITE_COMBINE,
	};
}

static bool io_map_page_range_from_phys(plane_paddr_t phys_addr,
			  uint64_t size,
			  plane_paddr_t *phys_base,
			  uint64_t *page_offset,
			  uint64_t *page_count)
{
	uint64_t raw_phys = plane_paddr_raw(phys_addr);
	uint64_t base = ALIGN_DOWN(raw_phys, PAGE_SIZE);
	uint64_t offset = raw_phys - base;
	uint64_t size_with_offset;
	uint64_t aligned_size;
	uint64_t last_byte;

	if (phys_base == NULL ||
	    page_offset == NULL ||
	    page_count == NULL ||
	    size == 0 ||
	    !plane_checked_add_u64(size, offset, &size_with_offset) ||
	    !plane_checked_align_up_u64(size_with_offset, PAGE_SIZE,
					&aligned_size) ||
	    !plane_checked_add_u64(base, aligned_size - 1, &last_byte)) {
		return false;
	}

	*phys_base = plane_paddr_make(base);
	*page_offset = offset;
	*page_count = aligned_size / PAGE_SIZE;
	return true;
}

static bool io_map_unmap_pages(plane_vaddr_t base, uint64_t page_count)
{
	for (uint64_t i = page_count; i > 0; i--) {
		plane_vaddr_t page_vaddr;

		if (!plane_vaddr_add_pages(base, i - 1, &page_vaddr) ||
		    !hal_mmu_unmap_kernel_page(page_vaddr)) {
			return false;
		}
	}

	return true;
}

bool plane_io_map_init(void)
{
	if (io_map_initialized) {
		return false;
	}

	io_map_initialized = true;
	return true;
}

bool plane_io_map(plane_paddr_t phys_addr,
		  uint64_t size,
		  enum plane_io_map_cache cache,
		  uint32_t prot,
		  plane_vaddr_t *vaddr)
{
	plane_paddr_t phys_base;
	plane_vaddr_t va_base;
	uint64_t page_offset;
	uint64_t page_count;
	uint64_t mapped_pages = 0;

	if (!io_map_initialized ||
	    vaddr == NULL ||
	    !io_map_cache_is_valid(cache) ||
	    !plane_vm_prot_is_valid(prot) ||
	    !io_map_page_range_from_phys(phys_addr, size, &phys_base, &page_offset,
			   &page_count) ||
	    !plane_kmem_reserve_va_pages(page_count, prot, &va_base)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		plane_vaddr_t page_vaddr;
		plane_paddr_t page_phys;
		uint64_t phys_offset;

		if (!plane_vaddr_add_pages(va_base, i, &page_vaddr) ||
		    !plane_checked_page_offset(i, &phys_offset)) {
			break;
		}

		page_phys = plane_paddr_make(plane_paddr_raw(phys_base) +
					     phys_offset);
		if (!hal_mmu_map_kernel_page(page_vaddr, page_phys,
					     io_map_options(prot, cache))) {
			break;
		}
		mapped_pages++;
	}

	if (mapped_pages != page_count) {
		BUG_ON_MSG(!io_map_unmap_pages(va_base, mapped_pages),
			   "failed to rollback IO map pages");
		BUG_ON_MSG(!plane_kmem_release_va_pages(va_base, page_count),
			   "failed to release IO map VA reservation");
		return false;
	}

	*vaddr = plane_vaddr_make(plane_vaddr_raw(va_base) + page_offset);
	return true;
}

bool plane_io_unmap(plane_vaddr_t vaddr, uint64_t size)
{
	plane_vaddr_t va_base;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t page_offset = raw_vaddr & (PAGE_SIZE - 1);
	uint64_t size_with_offset;
	uint64_t aligned_size;
	uint64_t page_count;

	if (!io_map_initialized ||
	    plane_vaddr_is_null(vaddr) ||
	    size == 0 ||
	    !plane_checked_add_u64(size, page_offset, &size_with_offset) ||
	    !plane_checked_align_up_u64(size_with_offset, PAGE_SIZE,
					&aligned_size)) {
		return false;
	}

	va_base = plane_vaddr_make(ALIGN_DOWN(raw_vaddr, PAGE_SIZE));
	page_count = aligned_size / PAGE_SIZE;
	if (!plane_kmem_va_pages_reserved(va_base, page_count)) {
		return false;
	}
	if (!io_map_unmap_pages(va_base, page_count)) {
		return false;
	}

	return plane_kmem_release_va_pages(va_base, page_count);
}
