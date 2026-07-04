#ifndef PLANE_PMM_H
#define PLANE_PMM_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>
#include <plane/memmap.h>

/*
 * Early physical page allocator.
 *
 * The returned addresses are physical addresses, not directly
 * dereferenceable virtual addresses. Pages are not zeroed unless
 * PLANE_PMM_ALLOC_ZERO is requested.
 * Each managed physical page has a small struct plane_page. Page metadata
 * queries and resident-page operations live in the VM page layer.
 */

enum plane_pmm_alloc_flags {
	PLANE_PMM_ALLOC_ZERO = BIT(0),
};

struct plane_pmm_allocator_stats {
	uint64_t managed_pages;
	uint64_t metadata_pages;
	uint64_t metadata_bytes;
	uint64_t free_pages;
	uint64_t wired_pages;
	uint64_t free_run_count;
};

struct plane_pmm_memtype_stats {
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
};

struct plane_pmm_stats {
	struct plane_pmm_allocator_stats allocator;
	struct plane_pmm_memtype_stats memtype;
};

bool plane_pmm_init(const struct plane_mem_info *mem);
bool plane_pmm_alloc_page_phys(uint64_t *phys_addr);
bool plane_pmm_alloc_pages_phys(uint64_t page_count,
				uint64_t alignment_pages,
				uint64_t *phys_addr);
bool plane_pmm_alloc_pages_phys_flags(uint64_t page_count,
				      uint64_t alignment_pages,
				      uint32_t flags,
				      uint64_t *phys_addr);
bool plane_pmm_free_page_phys(uint64_t phys_addr);
bool plane_pmm_free_pages_phys(uint64_t phys_addr, uint64_t page_count);
struct plane_pmm_stats plane_pmm_get_stats(void);
void plane_pmm_log_stats(void);

#endif /* PLANE_PMM_H */
