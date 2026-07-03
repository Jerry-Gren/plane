#ifndef PLANE_KMEM_H
#define PLANE_KMEM_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>

/*
 * Early kernel virtual allocator.
 *
 * This owns a kernel virtual address window and backs allocations with PMM
 * pages. Byte-size allocations are rounded up to whole pages; this is a
 * small kmem/vm_map foundation, not a sub-page heap or pageable VM.
 * PLANE_KMEM_ALLOC_ZERO clears the complete backing page range.
 */

enum plane_kmem_alloc_flags {
	PLANE_KMEM_ALLOC_ZERO = BIT(0),
};

bool plane_kmem_init(void);
bool plane_kmem_alloc(uint64_t size, uint32_t flags, void **addr);
bool plane_kmem_free(void *addr, uint64_t size);
bool plane_kmem_alloc_pages(uint64_t page_count, uint32_t flags, void **vaddr);
bool plane_kmem_free_pages(void *vaddr, uint64_t page_count);

#endif /* PLANE_KMEM_H */
