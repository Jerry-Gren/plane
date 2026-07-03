#ifndef PLANE_VM_MAP_H
#define PLANE_VM_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>

/*
 * Early kernel virtual map.
 *
 * This is the small kernel_map foundation used by kmem. It only manages
 * virtual address ranges; physical backing and page-table mappings belong to
 * PMM and HAL/pmap.
 *
 * Protection and max_protection are entry attribute foundations. Current
 * users consume protection when creating or updating kernel mappings; there
 * is no fault-time enforcement here yet. READ and WRITE follow XNU-style
 * independent bitset semantics.
 */

enum plane_vm_prot {
	PLANE_VM_PROT_READ = BIT(0),
	PLANE_VM_PROT_WRITE = BIT(1),
};

struct plane_vm_map_stats {
	uint64_t total_pages;
	uint64_t free_pages;
	uint64_t reserved_pages;
	uint64_t user_pages;
	uint64_t free_range_count;
	uint64_t allocation_count;
};

struct plane_kernel_map_allocation_info {
	uint64_t reserved_start;
	uint64_t reserved_pages;
	uint64_t user_start;
	uint64_t user_pages;
	uint32_t prot;
	uint32_t max_prot;
};

bool plane_kernel_map_init(uint64_t base, uint64_t size);
bool plane_kernel_map_alloc_pages(uint64_t page_count, uint64_t *vaddr);
bool plane_kernel_map_alloc_pages_protected(uint64_t page_count,
					    uint64_t guard_pages,
					    uint32_t prot,
					    uint64_t *vaddr);
/*
 * Reserves a virtual range with guard_pages before and after the user range.
 * The returned address is the user range start. Allocation lookup and free use
 * an exact user range match, and free removes the whole reserved entry.
 */
bool plane_kernel_map_alloc_pages_guarded(uint64_t page_count,
					  uint64_t guard_pages,
					  uint64_t *vaddr);
bool plane_kernel_map_has_allocation(uint64_t vaddr, uint64_t page_count);
bool plane_kernel_map_lookup_allocation(
	uint64_t vaddr,
	uint64_t page_count,
	struct plane_kernel_map_allocation_info *info);
/* Updates protection metadata for an exact user allocation range. */
bool plane_kernel_map_protect_pages(uint64_t vaddr,
				    uint64_t page_count,
				    uint32_t prot);
bool plane_kernel_map_free_pages(uint64_t vaddr, uint64_t page_count);
struct plane_vm_map_stats plane_kernel_map_get_stats(void);

#endif /* PLANE_VM_MAP_H */
