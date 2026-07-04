#include <stddef.h>

#include <hal/mmu.h>

#include <plane/kmem.h>
#include <plane/mm.h>
#include <plane/printk.h>
#include <plane/pmm.h>
#include <plane/util.h>
#include <plane/vm_map.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#define PLANE_KERNEL_MAP_MAX_ENTRIES 128
#define PLANE_KERNEL_OBJECT_MAX_PAGES 4096

static struct plane_vm_map_entry kernel_map_entries[PLANE_KERNEL_MAP_MAX_ENTRIES];
static struct plane_vm_map kernel_map;
static struct plane_vm_object_page kernel_object_pages[PLANE_KERNEL_OBJECT_MAX_PAGES];
static struct plane_vm_object kernel_object;
static bool kmem_initialized;

static bool checked_add_u64(uint64_t lhs, uint64_t rhs, uint64_t *out)
{
	if (rhs > UINT64_MAX - lhs) {
		return false;
	}

	*out = lhs + rhs;
	return true;
}

static bool checked_mul_u64(uint64_t lhs, uint64_t rhs, uint64_t *out)
{
	if (lhs != 0 && rhs > UINT64_MAX / lhs) {
		return false;
	}

	*out = lhs * rhs;
	return true;
}

static bool checked_page_offset(uint64_t page_index, uint64_t *offset)
{
	return checked_mul_u64(page_index, PAGE_SIZE, offset);
}

static bool kmem_size_to_pages(uint64_t size, uint64_t *page_count)
{
	uint64_t rounded;

	if (page_count == NULL ||
	    size == 0 ||
	    !checked_add_u64(size, PAGE_SIZE - 1, &rounded)) {
		return false;
	}

	*page_count = rounded / PAGE_SIZE;
	return true;
}

static bool is_page_aligned(uint64_t value)
{
	return (value & (PAGE_SIZE - 1)) == 0;
}

static bool kmem_flags_valid(uint32_t flags)
{
	return (flags & ~(PLANE_KMEM_ALLOC_ZERO |
			  PLANE_KMEM_ALLOC_GUARD |
			  PLANE_KMEM_ALLOC_READONLY)) == 0;
}

static bool kmem_prot_valid(uint32_t prot)
{
	return prot != PLANE_VM_PROT_NONE &&
	       (prot & ~PLANE_VM_PROT_ALL) == 0;
}

static uint32_t kmem_to_pmm_flags(uint32_t flags)
{
	uint32_t pmm_flags = 0;

	if ((flags & PLANE_KMEM_ALLOC_ZERO) != 0) {
		pmm_flags |= PLANE_PMM_ALLOC_ZERO;
	}

	return pmm_flags;
}

static bool reserve_kmem_vaddr(struct plane_vm_map *map,
			       struct plane_vm_object *object,
			       uint64_t page_count,
			       uint32_t flags,
			       uint64_t *base)
{
	uint64_t guard_pages = 0;
	uint32_t prot = PLANE_VM_PROT_READ;

	if ((flags & PLANE_KMEM_ALLOC_READONLY) == 0) {
		prot |= PLANE_VM_PROT_WRITE;
	}
	if ((flags & PLANE_KMEM_ALLOC_GUARD) != 0) {
		guard_pages = 1;
	}

	return plane_vm_map_alloc_pages_object(
		map, page_count, guard_pages, object,
		PLANE_VM_MAP_OBJECT_OFFSET_AUTO, prot, PLANE_VM_PROT_ALL, base);
}

static uint32_t kmem_prot_to_map_flags(uint32_t prot)
{
	uint32_t map_flags = 0;

	if ((prot & PLANE_VM_PROT_WRITE) != 0) {
		map_flags |= HAL_MMU_MAP_WRITE;
	}

	return map_flags;
}

static bool release_mapped_page(struct plane_vm_object *object,
				uint64_t object_offset,
				uint64_t vaddr)
{
	struct plane_page *page;
	struct plane_page *object_page;
	uint64_t phys_addr;
	uint64_t wire_count;

	if (!hal_mmu_translate_kernel_page(vaddr, &phys_addr)) {
		return false;
	}

	page = plane_pmm_phys_to_page(phys_addr);
	if (!plane_vm_page_wire_count(page, &wire_count)) {
		return false;
	}
	object_page = plane_vm_object_lookup_page(object, object_offset);
	if (object_page != page) {
		return false;
	}
	if (wire_count == 0) {
		return false;
	}
	if (!hal_mmu_unmap_kernel_page(vaddr)) {
		return false;
	}
	if (plane_vm_object_remove_page(object, object_offset) != page) {
		return false;
	}
	if (!plane_pmm_unwire_page(page)) {
		return false;
	}

	return plane_pmm_free_page_phys(phys_addr);
}

static bool rollback_mapped_pages(uint64_t vaddr,
				  struct plane_vm_object *object,
				  uint64_t object_offset,
				  uint64_t page_count)
{
	for (uint64_t i = page_count; i > 0; i--) {
		uint64_t page_vaddr;
		uint64_t page_object_offset;
		uint64_t offset;

		if (!checked_page_offset(i - 1, &offset) ||
		    !checked_add_u64(vaddr, offset, &page_vaddr) ||
		    !checked_add_u64(object_offset, offset,
				     &page_object_offset) ||
		    !release_mapped_page(object, page_object_offset,
					 page_vaddr)) {
			return false;
		}
	}

	return true;
}

static bool rollback_allocated_page(uint64_t phys_addr)
{
	return plane_pmm_free_page_phys(phys_addr);
}

static bool rollback_object_page(struct plane_vm_object *object,
				 uint64_t object_offset,
				 struct plane_page *page)
{
	struct plane_page *removed;
	uint64_t phys_addr = plane_page_phys(page);

	removed = plane_vm_object_remove_page(object, object_offset);
	if (removed != page) {
		return false;
	}
	if (!plane_pmm_unwire_page(page)) {
		return false;
	}
	return plane_pmm_free_page_phys(phys_addr);
}

static bool map_allocated_pages(uint64_t vaddr,
				struct plane_vm_object *object,
				uint64_t object_offset,
				uint64_t page_count,
				uint32_t flags,
				uint32_t prot)
{
	uint32_t pmm_flags = kmem_to_pmm_flags(flags);
	uint32_t map_flags = kmem_prot_to_map_flags(prot);
	uint64_t mapped_pages = 0;

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		uint64_t page_vaddr;
		uint64_t page_object_offset;
		uint64_t phys_addr;
		uint64_t offset;

		if (!checked_page_offset(i, &offset) ||
		    !checked_add_u64(vaddr, offset, &page_vaddr) ||
		    !checked_add_u64(object_offset, offset,
				     &page_object_offset)) {
			BUG_ON_MSG(!rollback_mapped_pages(vaddr, object,
							  object_offset,
							  mapped_pages),
				   "failed to rollback kmem mappings");
			return false;
		}

		if (!plane_pmm_alloc_page_flags(pmm_flags, &page)) {
			BUG_ON_MSG(!rollback_mapped_pages(vaddr, object,
							  object_offset,
							  mapped_pages),
				   "failed to rollback kmem mappings");
			return false;
		}

		phys_addr = plane_page_phys(page);
		if (phys_addr == UINT64_MAX) {
			bool page_ok = rollback_allocated_page(phys_addr);
			bool mappings_ok = rollback_mapped_pages(
				vaddr, object, object_offset, mapped_pages);

			BUG_ON_MSG(!page_ok, "failed to rollback kmem physical page");
			BUG_ON_MSG(!mappings_ok, "failed to rollback kmem mappings");
			return false;
		}

		if (!plane_pmm_wire_page(page)) {
			bool page_ok = rollback_allocated_page(phys_addr);
			bool mappings_ok = rollback_mapped_pages(
				vaddr, object, object_offset, mapped_pages);

			BUG_ON_MSG(!page_ok, "failed to rollback kmem physical page");
			BUG_ON_MSG(!mappings_ok, "failed to rollback kmem mappings");
			return false;
		}

		if (!plane_vm_object_insert_page(object, page_object_offset, page)) {
			bool page_ok = plane_pmm_unwire_page(page) &&
				       rollback_allocated_page(phys_addr);
			bool mappings_ok = rollback_mapped_pages(
				vaddr, object, object_offset, mapped_pages);

			BUG_ON_MSG(!page_ok, "failed to rollback kmem object page");
			BUG_ON_MSG(!mappings_ok, "failed to rollback kmem mappings");
			return false;
		}

		if (!hal_mmu_map_kernel_page(page_vaddr, phys_addr, map_flags)) {
			bool page_ok = rollback_object_page(
				object, page_object_offset, page);
			bool mappings_ok = rollback_mapped_pages(
				vaddr, object, object_offset, mapped_pages);

			BUG_ON_MSG(!page_ok, "failed to rollback kmem object page");
			BUG_ON_MSG(!mappings_ok, "failed to rollback kmem mappings");
			return false;
		}

		mapped_pages++;
	}

	return true;
}

static bool protect_mapped_pages(uint64_t vaddr,
				 uint64_t page_count,
				 uint32_t prot)
{
	uint32_t map_flags = kmem_prot_to_map_flags(prot);

	for (uint64_t i = 0; i < page_count; i++) {
		uint64_t page_vaddr;
		uint64_t offset;

		if (!checked_page_offset(i, &offset) ||
		    !checked_add_u64(vaddr, offset, &page_vaddr) ||
		    !hal_mmu_protect_kernel_page(page_vaddr, map_flags)) {
			return false;
		}
	}

	return true;
}

bool plane_kmem_init(void)
{
	uint64_t base;
	uint64_t object_size;
	uint64_t size;

	if (kmem_initialized) {
		return false;
	}

	if (!hal_mmu_kernel_vma_range(&base, &size) ||
	    size == 0 ||
	    !is_page_aligned(base) ||
	    !is_page_aligned(size) ||
	    !checked_add_u64(base, size, &object_size) ||
	    ARRAY_SIZE(kernel_map_entries) == 0 ||
	    ARRAY_SIZE(kernel_object_pages) == 0) {
		return false;
	}

	if (!plane_vm_map_init(&kernel_map, kernel_map_entries,
			       ARRAY_SIZE(kernel_map_entries), base, size)) {
		return false;
	}

	BUG_ON_MSG(!plane_vm_object_init(&kernel_object, kernel_object_pages,
					 ARRAY_SIZE(kernel_object_pages),
					 object_size),
		   "failed to initialize kernel object");

	kmem_initialized = true;
	return true;
}

bool plane_kmem_alloc(uint64_t size, uint32_t flags, void **addr)
{
	if (!kmem_initialized) {
		return false;
	}

	return plane_kmem_alloc_in_map(&kernel_map, &kernel_object, size, flags,
				       addr);
}

bool plane_kmem_alloc_in_map(struct plane_vm_map *map,
			     struct plane_vm_object *object,
			     uint64_t size,
			     uint32_t flags,
			     void **addr)
{
	uint64_t page_count;

	if (!kmem_size_to_pages(size, &page_count)) {
		return false;
	}

	return plane_kmem_alloc_pages_in_map(map, object, page_count, flags,
					     addr);
}

bool plane_kmem_free(void *addr, uint64_t size)
{
	if (!kmem_initialized) {
		return false;
	}

	return plane_kmem_free_in_map(&kernel_map, &kernel_object, addr, size);
}

bool plane_kmem_free_in_map(struct plane_vm_map *map,
			    struct plane_vm_object *object,
			    void *addr,
			    uint64_t size)
{
	uint64_t page_count;

	if (!kmem_size_to_pages(size, &page_count)) {
		return false;
	}

	return plane_kmem_free_pages_in_map(map, object, addr, page_count);
}

bool plane_kmem_protect(void *addr, uint64_t size, uint32_t prot)
{
	if (!kmem_initialized) {
		return false;
	}

	return plane_kmem_protect_in_map(&kernel_map, addr, size, prot);
}

bool plane_kmem_protect_in_map(struct plane_vm_map *map,
			       void *addr,
			       uint64_t size,
			       uint32_t prot)
{
	uint64_t page_count;

	if (!kmem_size_to_pages(size, &page_count)) {
		return false;
	}

	return plane_kmem_protect_pages_in_map(map, addr, page_count, prot);
}

bool plane_kmem_alloc_pages(uint64_t page_count, uint32_t flags, void **vaddr)
{
	if (!kmem_initialized) {
		return false;
	}

	return plane_kmem_alloc_pages_in_map(&kernel_map, &kernel_object,
					     page_count, flags, vaddr);
}

bool plane_kmem_alloc_pages_in_map(struct plane_vm_map *map,
				   struct plane_vm_object *object,
				   uint64_t page_count,
				   uint32_t flags,
				   void **vaddr)
{
	struct plane_vm_map_allocation_info info;
	uint64_t base;

	if (vaddr == NULL ||
	    map == NULL ||
	    object == NULL ||
	    page_count == 0 ||
	    !kmem_flags_valid(flags)) {
		return false;
	}

	if (!reserve_kmem_vaddr(map, object, page_count, flags, &base)) {
		return false;
	}

	BUG_ON_MSG(!plane_vm_map_lookup_allocation(map, base, page_count, &info),
		   "failed to find reserved kmem allocation");
	BUG_ON_MSG(!plane_vm_map_wire_pages(map, base, page_count),
		   "failed to wire kmem virtual reservation");

	if (!map_allocated_pages(base, object, info.object_offset, page_count,
				 flags, info.prot)) {
		BUG_ON_MSG(!plane_vm_map_unwire_pages(map, base, page_count),
			   "failed to unwire kmem virtual reservation");
		BUG_ON_MSG(!plane_vm_map_free_pages(map, base, page_count),
			   "failed to release kmem virtual reservation");
		return false;
	}

	*vaddr = (void *)(uintptr_t)base;
	return true;
}

bool plane_kmem_protect_pages(void *vaddr, uint64_t page_count, uint32_t prot)
{
	if (!kmem_initialized) {
		return false;
	}

	return plane_kmem_protect_pages_in_map(&kernel_map, vaddr, page_count, prot);
}

bool plane_kmem_protect_pages_in_map(struct plane_vm_map *map,
				     void *vaddr,
				     uint64_t page_count,
				     uint32_t prot)
{
	struct plane_vm_map_allocation_info info;
	uint64_t addr = (uint64_t)(uintptr_t)vaddr;

	if (map == NULL ||
	    vaddr == NULL ||
	    page_count == 0 ||
	    !is_page_aligned(addr) ||
	    !kmem_prot_valid(prot)) {
		return false;
	}

	if (!plane_vm_map_lookup_allocation(map, addr, page_count, &info)) {
		return false;
	}
	if ((prot & ~info.max_prot) != 0) {
		return false;
	}

	BUG_ON_MSG(!protect_mapped_pages(addr, page_count, prot),
		   "failed to protect kmem backing pages");
	BUG_ON_MSG(!plane_vm_map_protect_pages(map, addr, page_count, prot),
		   "failed to update kmem virtual protection");
	return true;
}

bool plane_kmem_free_pages(void *vaddr, uint64_t page_count)
{
	if (!kmem_initialized) {
		return false;
	}

	return plane_kmem_free_pages_in_map(&kernel_map, &kernel_object, vaddr,
					    page_count);
}

bool plane_kmem_free_pages_in_map(struct plane_vm_map *map,
				  struct plane_vm_object *object,
				  void *vaddr,
				  uint64_t page_count)
{
	struct plane_vm_map_allocation_info info;
	uint64_t addr = (uint64_t)(uintptr_t)vaddr;

	if (map == NULL ||
	    object == NULL ||
	    vaddr == NULL ||
	    page_count == 0 ||
	    !is_page_aligned(addr)) {
		return false;
	}

	if (!plane_vm_map_lookup_allocation(map, addr, page_count, &info) ||
	    info.object != object) {
		return false;
	}

	BUG_ON_MSG(!rollback_mapped_pages(addr, object, info.object_offset,
					  page_count),
		   "failed to release kmem backing pages");
	BUG_ON_MSG(!plane_vm_map_unwire_pages(map, addr, page_count),
		   "failed to unwire kmem virtual reservation");
	BUG_ON_MSG(!plane_vm_map_free_pages(map, addr, page_count),
		   "failed to release kmem virtual reservation");
	return true;
}
