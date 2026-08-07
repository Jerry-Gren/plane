#ifndef KERNEL_MM_KMEM_INTERNAL_H
#define KERNEL_MM_KMEM_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>

bool plane_kmem_reserve_va_pages(uint64_t page_count,
				 uint32_t prot,
				 plane_vaddr_t *vaddr);
bool plane_kmem_va_pages_reserved(plane_vaddr_t vaddr, uint64_t page_count);
bool plane_kmem_release_va_pages(plane_vaddr_t vaddr, uint64_t page_count);

#endif /* KERNEL_MM_KMEM_INTERNAL_H */
