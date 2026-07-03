#ifndef PLANE_VM_MAP_H
#define PLANE_VM_MAP_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Early kernel virtual map.
 *
 * This is the small kernel_map foundation used by kmem. It only manages
 * virtual address ranges; physical backing and page-table mappings belong to
 * PMM and HAL/pmap.
 */

struct plane_vm_map_stats {
	uint64_t total_pages;
	uint64_t free_pages;
	uint64_t allocated_pages;
	uint64_t free_range_count;
	uint64_t allocation_count;
};

bool plane_kernel_map_init(uint64_t base, uint64_t size);
bool plane_kernel_map_alloc_pages(uint64_t page_count, uint64_t *vaddr);
bool plane_kernel_map_has_allocation(uint64_t vaddr, uint64_t page_count);
bool plane_kernel_map_free_pages(uint64_t vaddr, uint64_t page_count);
struct plane_vm_map_stats plane_kernel_map_get_stats(void);

#endif /* PLANE_VM_MAP_H */
