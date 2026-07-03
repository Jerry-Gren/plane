#ifndef PLANE_KMEM_H
#define PLANE_KMEM_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>
#include <plane/vm_prot.h>

/*
 * Early kernel virtual allocator.
 *
 * This owns a kernel virtual address window and backs allocations with PMM
 * pages. Byte-size allocations are rounded up to whole pages; this is a
 * small kmem/vm_map foundation, not a sub-page heap or pageable VM.
 * PLANE_KMEM_ALLOC_ZERO clears the complete backing page range.
 * PLANE_KMEM_ALLOC_GUARD reserves one unmapped guard page before and after the
 * returned allocation. Guard pages have no PMM backing.
 * PLANE_KMEM_ALLOC_READONLY records read-only entry protection in the kernel
 * map; kmem consumes that protection when creating and protecting mappings.
 * Protect APIs only support exact allocation ranges in this early layer.
 */

enum plane_kmem_alloc_flags {
	PLANE_KMEM_ALLOC_ZERO = BIT(0),
	PLANE_KMEM_ALLOC_GUARD = BIT(1),
	PLANE_KMEM_ALLOC_READONLY = BIT(2),
};

bool plane_kmem_init(void);
bool plane_kmem_alloc(uint64_t size, uint32_t flags, void **addr);
bool plane_kmem_free(void *addr, uint64_t size);
bool plane_kmem_protect(void *addr, uint64_t size, uint32_t prot);
bool plane_kmem_alloc_pages(uint64_t page_count, uint32_t flags, void **vaddr);
bool plane_kmem_free_pages(void *vaddr, uint64_t page_count);
bool plane_kmem_protect_pages(void *vaddr, uint64_t page_count, uint32_t prot);

#endif /* PLANE_KMEM_H */
