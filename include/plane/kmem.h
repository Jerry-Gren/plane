#ifndef PLANE_KMEM_H
#define PLANE_KMEM_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/bits.h>
#include <plane/vm_prot.h>

struct plane_vm_map;
struct plane_vm_object;

/*
 * Minimal kernel virtual allocator.
 *
 * This owns a kernel virtual address window. Byte-size allocations are rounded
 * up to whole pages; this is a small kmem/vm_map foundation, not a sub-page
 * heap or pageable VM.
 * By default, allocations are eagerly backed with PMM pages.
 * PLANE_KMEM_ALLOC_LAZY creates a fault-backed reservation: no resident page or
 * pmap mapping is created until plane_kmem_fault_page() handles a hardware
 * fault or plane_kmem_fault_pages() explicitly populates a range.
 * PLANE_KMEM_ALLOC_ZERO clears the complete eager backing page range; with
 * PLANE_KMEM_ALLOC_LAZY, zero-fill happens at fault time.
 * PLANE_KMEM_ALLOC_GUARD reserves one unmapped guard page before and after the
 * returned allocation. This kernel-object path follows XNU's KMA_KOBJECT
 * direction: guard pages are VA-only sentinels here, with no PMM backing and
 * no resident guard page materialized in the object.
 * PLANE_KMEM_ALLOC_READONLY records read-only entry protection in the kernel
 * map; kmem consumes that protection when creating and protecting mappings.
 * Protect APIs only support exact allocation ranges in this minimal layer.
 * Page faults are routed through the private kernel map only; generic map
 * fault handling lives in the VM fault layer.
 */

enum plane_kmem_alloc_flags {
	PLANE_KMEM_ALLOC_ZERO = BIT(0),
	PLANE_KMEM_ALLOC_GUARD = BIT(1),
	PLANE_KMEM_ALLOC_READONLY = BIT(2),
	PLANE_KMEM_ALLOC_LAZY = BIT(3),
};

bool plane_kmem_init(void);
bool plane_kmem_alloc(uint64_t size, uint32_t flags, plane_vaddr_t *addr);
bool plane_kmem_free(plane_vaddr_t addr, uint64_t size);
bool plane_kmem_protect(plane_vaddr_t addr, uint64_t size, uint32_t prot);
bool plane_kmem_alloc_pages(uint64_t page_count,
			    uint32_t flags,
			    plane_vaddr_t *vaddr);
bool plane_kmem_free_pages(plane_vaddr_t vaddr, uint64_t page_count);
bool plane_kmem_protect_pages(plane_vaddr_t vaddr,
			      uint64_t page_count,
			      uint32_t prot);
bool plane_kmem_fault_page(plane_vaddr_t vaddr, uint32_t fault_type);
bool plane_kmem_fault_pages(plane_vaddr_t vaddr,
			    uint64_t page_count,
			    uint32_t fault_type);

/*
 * The in-map APIs are still kernel virtual allocation APIs. The supplied map
 * must describe a HAL kernel mapping window, or an equivalent host-test map.
 * They are not generic VM object or pageable map backing APIs.
 */
bool plane_kmem_alloc_in_map(struct plane_vm_map *map,
			     struct plane_vm_object *object,
			     uint64_t size,
			     uint32_t flags,
			     plane_vaddr_t *addr);
bool plane_kmem_free_in_map(struct plane_vm_map *map,
			    struct plane_vm_object *object,
			    plane_vaddr_t addr,
			    uint64_t size);
bool plane_kmem_protect_in_map(struct plane_vm_map *map,
			       plane_vaddr_t addr,
			       uint64_t size,
			       uint32_t prot);
bool plane_kmem_alloc_pages_in_map(struct plane_vm_map *map,
				   struct plane_vm_object *object,
				   uint64_t page_count,
				   uint32_t flags,
				   plane_vaddr_t *vaddr);
bool plane_kmem_free_pages_in_map(struct plane_vm_map *map,
				  struct plane_vm_object *object,
				  plane_vaddr_t vaddr,
				  uint64_t page_count);
bool plane_kmem_protect_pages_in_map(struct plane_vm_map *map,
				     plane_vaddr_t vaddr,
				     uint64_t page_count,
				     uint32_t prot);

#endif /* PLANE_KMEM_H */
