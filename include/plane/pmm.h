#ifndef PLANE_PMM_H
#define PLANE_PMM_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/memmap.h>

/*
 * Early physical page allocator.
 *
 * The returned addresses are physical addresses, not directly
 * dereferenceable virtual addresses. Allocated pages are not zeroed.
 * A future XNU-like VM page layer should add struct plane_page and
 * page-level APIs instead of broadening this early phys API.
 */

struct plane_pmm_stats {
	uint64_t managed_pages;
	uint64_t free_pages;
	uint64_t allocated_pages;
	uint64_t usable_pages;
	uint64_t invalid_pages;
	uint64_t reserved_pages;
	uint64_t acpi_reclaimable_pages;
	uint64_t acpi_nvs_pages;
	uint64_t bootloader_reclaimable_pages;
	uint64_t executable_and_modules_pages;
	uint64_t framebuffer_pages;
	uint64_t bad_pages;
	uint64_t reserved_mapped_pages;
	uint64_t free_range_count;
};

bool plane_pmm_init(const struct plane_mem_info *mem);
bool plane_pmm_alloc_page_phys(uint64_t *phys_addr);
bool plane_pmm_alloc_pages_phys(uint64_t page_count,
				uint64_t alignment_pages,
				uint64_t *phys_addr);
bool plane_pmm_free_page_phys(uint64_t phys_addr);
bool plane_pmm_free_pages_phys(uint64_t phys_addr, uint64_t page_count);
struct plane_pmm_stats plane_pmm_get_stats(void);

#endif /* PLANE_PMM_H */
