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
 * The page metadata API is an early XNU-like foundation: each managed
 * physical page has a small struct plane_page, but full VM page queues,
 * objects, coloring, and SMP locking are intentionally not here yet.
 * Wire/unwire operations use struct plane_page as the page identity; callers
 * that start with a physical address should look up the page metadata first.
 */

struct plane_page;

enum plane_page_state {
	PLANE_PAGE_INVALID = 0,
	PLANE_PAGE_FREE,
	PLANE_PAGE_ALLOCATED,
	PLANE_PAGE_METADATA,
};

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
bool plane_pmm_alloc_page(struct plane_page **page);
bool plane_pmm_alloc_page_flags(uint32_t flags, struct plane_page **page);
bool plane_pmm_free_page(struct plane_page *page);
bool plane_pmm_wire_page(struct plane_page *page);
bool plane_pmm_unwire_page(struct plane_page *page);
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
struct plane_page *plane_pmm_phys_to_page(uint64_t phys_addr);
uint64_t plane_page_phys(const struct plane_page *page);
enum plane_page_state plane_page_state(const struct plane_page *page);
struct plane_pmm_stats plane_pmm_get_stats(void);
void plane_pmm_log_stats(void);

#endif /* PLANE_PMM_H */
