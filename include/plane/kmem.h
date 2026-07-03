#ifndef PLANE_KMEM_H
#define PLANE_KMEM_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>

/*
 * Early kernel virtual page allocator.
 *
 * This owns a kernel virtual address window and backs allocations with PMM
 * pages. It is a small kmem/vm_map foundation, not a heap or pageable VM.
 */

enum plane_kmem_alloc_flags {
	PLANE_KMEM_ALLOC_ZERO = BIT(0),
};

bool plane_kmem_init(void);
bool plane_kmem_alloc_pages(uint64_t page_count, uint32_t flags, void **vaddr);
bool plane_kmem_free_pages(void *vaddr, uint64_t page_count);

#endif /* PLANE_KMEM_H */
