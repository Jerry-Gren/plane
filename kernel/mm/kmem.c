#include <stddef.h>

#include <hal/mmu.h>

#include <plane/kmem.h>
#include <plane/mm.h>
#include <plane/printk.h>
#include <plane/spinlock.h>
#include <plane/util.h>
#include <plane/vm_fault.h>
#include <plane/vm_map.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include "vm_object_internal.h"
#include "vm_page_internal.h"
#include "vm_zone_internal.h"
#include "kmem_internal.h"

#define PLANE_KERNEL_MAP_MAX_ENTRIES 128
#define PLANE_KERNEL_MAP_RUNTIME_ENTRIES 256
#define PLANE_VM_OBJECT_RUNTIME_POOL_SIZE 512
#define PLANE_VM_OBJECT_RUNTIME_HASH_BUCKETS 512
#define PLANE_VM_GUARD_PAGE_RUNTIME_POOL_SIZE 256

static struct plane_vm_map_entry kernel_map_entries[PLANE_KERNEL_MAP_MAX_ENTRIES];
static struct plane_vm_map kernel_map;
static struct plane_vm_object kernel_object;
static struct plane_vm_zone_segment kernel_object_runtime_segment;
static struct plane_vm_zone_segment kernel_guard_runtime_segment;
static struct plane_spinlock kmem_state_lock = PLANE_SPINLOCK_INIT;
static bool kmem_initializing;
static bool kmem_initialized;

struct kmem_kernel_context {
	struct plane_vm_map *map;
	struct plane_vm_object *object;
};

static plane_irq_state_t kmem_lock(void)
{
	return plane_spin_lock_irqsave(&kmem_state_lock);
}

static void kmem_unlock(plane_irq_state_t state)
{
	plane_spin_unlock_irqrestore(&kmem_state_lock, state);
}

static bool kmem_claim_init(void)
{
	plane_irq_state_t state = kmem_lock();
	bool claimed = false;

	if (!kmem_initialized && !kmem_initializing) {
		kmem_initializing = true;
		claimed = true;
	}
	kmem_unlock(state);
	return claimed;
}

static void kmem_publish_init(bool initialized)
{
	plane_irq_state_t state = kmem_lock();

	kmem_initialized = initialized;
	kmem_initializing = false;
	kmem_unlock(state);
}

static bool kmem_lookup_kernel_context(struct kmem_kernel_context *context)
{
	plane_irq_state_t state;

	if (context == NULL) {
		return false;
	}

	state = kmem_lock();
	if (!kmem_initialized) {
		kmem_unlock(state);
		return false;
	}

	context->map = &kernel_map;
	context->object = &kernel_object;
	kmem_unlock(state);
	return true;
}

static bool kmem_size_to_pages(uint64_t size, uint64_t *page_count)
{
	uint64_t rounded;

	if (page_count == NULL ||
	    size == 0 ||
	    !plane_checked_add_u64(size, PAGE_SIZE - 1, &rounded)) {
		return false;
	}

	*page_count = rounded / PAGE_SIZE;
	return true;
}

static bool kmem_flags_are_valid(uint32_t flags)
{
	return (flags & ~(PLANE_KMEM_ALLOC_ZERO |
			  PLANE_KMEM_ALLOC_GUARD |
			  PLANE_KMEM_ALLOC_READONLY |
			  PLANE_KMEM_ALLOC_LAZY)) == 0;
}

static uint32_t kmem_to_vm_page_grab_flags(uint32_t flags)
{
	uint32_t grab_flags = 0;

	if ((flags & PLANE_KMEM_ALLOC_ZERO) != 0) {
		grab_flags |= PLANE_VM_PAGE_GRAB_ZERO;
	}

	return grab_flags;
}

static bool kmem_reserve_vaddr(struct plane_vm_map *map,
			       struct plane_vm_object *object,
			       uint64_t page_count,
			       uint32_t flags,
			       plane_vaddr_t *base)
{
	uint64_t guard_pages = 0;
	uint32_t prot = PLANE_VM_PROT_READ;

	if ((flags & PLANE_KMEM_ALLOC_READONLY) == 0) {
		prot |= PLANE_VM_PROT_WRITE;
	}
	if ((flags & PLANE_KMEM_ALLOC_GUARD) != 0) {
		guard_pages = 1;
	}

	return plane_vm_map_enter(
		map,
		&(struct plane_vm_map_enter_options){
			.page_count = page_count,
			.guard_pages = guard_pages,
			.object = object,
			.object_offset = PLANE_VM_MAP_OBJECT_OFFSET_AUTO,
			.prot = prot,
			.max_prot = PLANE_VM_PROT_ALL,
			.flags = PLANE_VM_MAP_ENTER_ANYWHERE,
		},
		base);
}

static bool kmem_va_pages_are_reserved(struct plane_vm_map *map,
				       plane_vaddr_t vaddr,
				       uint64_t page_count)
{
	struct plane_vm_map_allocation_info info;

	if (map == NULL ||
	    plane_vaddr_is_null(vaddr) ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr)) {
		return false;
	}

	if (!plane_vm_map_lookup_allocation(map, vaddr, page_count, &info)) {
		return false;
	}

	return info.object == NULL;
}

bool plane_kmem_reserve_va_pages(uint64_t page_count,
				 uint32_t prot,
				 plane_vaddr_t *vaddr)
{
	struct kmem_kernel_context context;

	if (vaddr == NULL ||
	    page_count == 0 ||
	    !plane_vm_prot_is_valid(prot) ||
	    !kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return plane_vm_map_enter(
		context.map,
		&(struct plane_vm_map_enter_options){
			.page_count = page_count,
			.prot = prot,
			.max_prot = prot,
			.flags = PLANE_VM_MAP_ENTER_ANYWHERE |
				 PLANE_VM_MAP_ENTER_VA_ONLY,
		},
		vaddr);
}

bool plane_kmem_release_va_pages(plane_vaddr_t vaddr, uint64_t page_count)
{
	struct kmem_kernel_context context;

	if (plane_vaddr_is_null(vaddr) ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr) ||
	    !kmem_lookup_kernel_context(&context) ||
	    !kmem_va_pages_are_reserved(context.map, vaddr, page_count)) {
		return false;
	}

	return plane_vm_map_free_pages(context.map, vaddr, page_count);
}

bool plane_kmem_va_pages_reserved(plane_vaddr_t vaddr, uint64_t page_count)
{
	struct kmem_kernel_context context;

	if (!kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return kmem_va_pages_are_reserved(context.map, vaddr, page_count);
}

static bool kmem_expand_metadata(void)
{
	struct plane_vm_map_entry *runtime_entries;
	struct plane_vm_object *runtime_objects;
	struct plane_page **runtime_hash;
	void *runtime_guards;
	plane_vaddr_t runtime_entries_addr;
	plane_vaddr_t runtime_objects_addr;
	plane_vaddr_t runtime_hash_addr;
	plane_vaddr_t runtime_guards_addr;
	uint64_t runtime_guard_size;

	BUILD_BUG_ON(PLANE_VM_OBJECT_RUNTIME_HASH_BUCKETS == 0);
	BUILD_BUG_ON((PLANE_VM_OBJECT_RUNTIME_HASH_BUCKETS &
		      (PLANE_VM_OBJECT_RUNTIME_HASH_BUCKETS - 1)) != 0);

	if (!plane_kmem_alloc_in_map(&kernel_map, &kernel_object,
				     sizeof(runtime_entries[0]) *
					     PLANE_KERNEL_MAP_RUNTIME_ENTRIES,
				     PLANE_KMEM_ALLOC_ZERO,
				     &runtime_entries_addr)) {
		return false;
	}
	runtime_entries = plane_vaddr_to_ptr(runtime_entries_addr);
	if (!plane_vm_map_rehome_entries(&kernel_map, runtime_entries,
					 PLANE_KERNEL_MAP_RUNTIME_ENTRIES)) {
		return false;
	}

	if (!plane_kmem_alloc_in_map(&kernel_map, &kernel_object,
				     sizeof(runtime_objects[0]) *
					     PLANE_VM_OBJECT_RUNTIME_POOL_SIZE,
				     PLANE_KMEM_ALLOC_ZERO,
				     &runtime_objects_addr)) {
		return false;
	}
	runtime_objects = plane_vaddr_to_ptr(runtime_objects_addr);
	if (!plane_vm_object_add_zone_storage(runtime_objects,
					      PLANE_VM_OBJECT_RUNTIME_POOL_SIZE,
					      &kernel_object_runtime_segment)) {
		return false;
	}

	if (!plane_kmem_alloc_in_map(&kernel_map, &kernel_object,
				     sizeof(runtime_hash[0]) *
					     PLANE_VM_OBJECT_RUNTIME_HASH_BUCKETS,
				     PLANE_KMEM_ALLOC_ZERO,
				     &runtime_hash_addr)) {
		return false;
	}
	runtime_hash = plane_vaddr_to_ptr(runtime_hash_addr);
	if (!plane_vm_object_rehome_resident_hash(
		    runtime_hash, PLANE_VM_OBJECT_RUNTIME_HASH_BUCKETS)) {
		return false;
	}

	if (!plane_vm_page_guard_storage_size(
		    PLANE_VM_GUARD_PAGE_RUNTIME_POOL_SIZE,
		    &runtime_guard_size) ||
	    !plane_kmem_alloc_in_map(&kernel_map, &kernel_object,
				     runtime_guard_size,
				     PLANE_KMEM_ALLOC_ZERO,
				     &runtime_guards_addr)) {
		return false;
	}
	runtime_guards = plane_vaddr_to_ptr(runtime_guards_addr);
	return plane_vm_page_add_guard_storage(
		runtime_guards,
		PLANE_VM_GUARD_PAGE_RUNTIME_POOL_SIZE,
		&kernel_guard_runtime_segment);
}

static bool kmem_release_resident_page(struct plane_vm_object *object,
				       uint64_t object_offset,
				       plane_vaddr_t vaddr)
{
	struct plane_page *page;
	plane_paddr_t phys_addr;
	uint64_t wire_count;
	uint64_t hold_count;

	page = plane_vm_object_lookup_and_hold_page(object, object_offset);
	if (page == NULL) {
		return true;
	}

	if (!plane_vm_page_hold_count(page, &hold_count) ||
	    hold_count != 1) {
		goto out;
	}
	if (!plane_vm_page_wire_count(page, &wire_count) ||
	    wire_count == 0) {
		goto out;
	}
	if (hal_mmu_translate_kernel_page(vaddr, &phys_addr)) {
		if (plane_vm_page_from_phys(phys_addr) != page ||
		    !hal_mmu_unmap_kernel_page(vaddr)) {
			goto out;
		}
	}
	BUG_ON_MSG(plane_vm_object_remove_held_page(object, object_offset,
						    page) != page,
		   "failed to remove kmem resident page from object");
	BUG_ON_MSG(!plane_vm_page_unwire(page),
		   "failed to unwire kmem resident page");
	BUG_ON_MSG(!plane_vm_page_unhold(page),
		   "failed to unhold kmem resident page");
	BUG_ON_MSG(!plane_vm_page_release(page),
		   "failed to release kmem resident page");
	return true;

out:
	BUG_ON_MSG(!plane_vm_page_unhold(page),
		   "failed to unhold kmem resident page");
	return false;
}

static bool kmem_release_resident_pages(plane_vaddr_t vaddr,
					struct plane_vm_object *object,
					uint64_t object_offset,
					uint64_t page_count)
{
	for (uint64_t i = page_count; i > 0; i--) {
		plane_vaddr_t page_vaddr;
		uint64_t page_object_offset;
		uint64_t offset;

		if (!plane_vaddr_add_pages(vaddr, i - 1, &page_vaddr) ||
		    !plane_checked_page_offset(i - 1, &offset) ||
		    !plane_checked_add_u64(object_offset, offset,
					   &page_object_offset) ||
		    !kmem_release_resident_page(object, page_object_offset,
						page_vaddr)) {
			return false;
		}
	}

	return true;
}

static bool kmem_rollback_object_page(struct plane_vm_object *object,
				      uint64_t object_offset,
				      struct plane_page *page)
{
	struct plane_page *removed;

	removed = plane_vm_object_remove_page(object, object_offset);
	if (removed != page) {
		return false;
	}
	if (!plane_vm_page_unwire(page)) {
		return false;
	}
	return plane_vm_page_release(page);
}

static bool kmem_map_allocated_pages(plane_vaddr_t vaddr,
				     struct plane_vm_object *object,
				     uint64_t object_offset,
				     uint64_t page_count,
				     uint32_t flags,
				     uint32_t prot)
{
	uint32_t grab_flags = kmem_to_vm_page_grab_flags(flags);
	struct hal_mmu_map_options options = hal_mmu_default_map_options(prot);
	uint64_t mapped_pages = 0;

	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_page *page;
		plane_vaddr_t page_vaddr;
		uint64_t page_object_offset;
		plane_paddr_t phys_addr;
		uint64_t offset;

		if (!plane_vaddr_add_pages(vaddr, i, &page_vaddr) ||
		    !plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(object_offset, offset,
					   &page_object_offset)) {
			BUG_ON_MSG(!kmem_release_resident_pages(
					   vaddr, object, object_offset,
					   mapped_pages),
				   "failed to rollback kmem mappings");
			return false;
		}

		if (!plane_vm_page_grab(grab_flags, &page)) {
			BUG_ON_MSG(!kmem_release_resident_pages(
					   vaddr, object, object_offset,
					   mapped_pages),
				   "failed to rollback kmem mappings");
			return false;
		}

		phys_addr = plane_vm_page_phys(page);
		if (plane_paddr_equal(phys_addr, PLANE_VM_PAGE_NO_PHYS)) {
			bool page_ok = plane_vm_page_release(page);
			bool mappings_ok = kmem_release_resident_pages(
				vaddr, object, object_offset, mapped_pages);

			BUG_ON_MSG(!page_ok, "failed to rollback kmem physical page");
			BUG_ON_MSG(!mappings_ok, "failed to rollback kmem mappings");
			return false;
		}

		if (!plane_vm_page_wire(page)) {
			bool page_ok = plane_vm_page_release(page);
			bool mappings_ok = kmem_release_resident_pages(
				vaddr, object, object_offset, mapped_pages);

			BUG_ON_MSG(!page_ok, "failed to rollback kmem physical page");
			BUG_ON_MSG(!mappings_ok, "failed to rollback kmem mappings");
			return false;
		}

		if (!plane_vm_object_insert_page(object, page_object_offset, page)) {
			bool page_ok = plane_vm_page_unwire(page) &&
				       plane_vm_page_release(page);
			bool mappings_ok = kmem_release_resident_pages(
				vaddr, object, object_offset, mapped_pages);

			BUG_ON_MSG(!page_ok, "failed to rollback kmem object page");
			BUG_ON_MSG(!mappings_ok, "failed to rollback kmem mappings");
			return false;
		}

		if (!hal_mmu_map_kernel_page(page_vaddr, phys_addr, options)) {
			bool page_ok = kmem_rollback_object_page(
				object, page_object_offset, page);
			bool mappings_ok = kmem_release_resident_pages(
				vaddr, object, object_offset, mapped_pages);

			BUG_ON_MSG(!page_ok, "failed to rollback kmem object page");
			BUG_ON_MSG(!mappings_ok, "failed to rollback kmem mappings");
			return false;
		}

		mapped_pages++;
	}

	return true;
}

static bool kmem_protect_mapped_pages(plane_vaddr_t vaddr,
				      struct plane_vm_object *object,
				      uint64_t object_offset,
				      uint64_t page_count,
				      uint32_t prot)
{
	for (uint64_t i = 0; i < page_count; i++) {
		plane_vaddr_t page_vaddr;
		uint64_t page_object_offset;
		uint64_t offset;
		struct plane_page *page;
		plane_paddr_t phys_addr;
		bool ok = true;

		if (!plane_vaddr_add_pages(vaddr, i, &page_vaddr) ||
		    !plane_checked_page_offset(i, &offset) ||
		    !plane_checked_add_u64(object_offset, offset,
					   &page_object_offset)) {
			return false;
		}

		page = plane_vm_object_lookup_and_hold_page(object,
							page_object_offset);
		if (page == NULL ||
		    !hal_mmu_translate_kernel_page(page_vaddr, &phys_addr)) {
			goto next_page;
		}
		ok = plane_vm_page_from_phys(phys_addr) == page &&
		     hal_mmu_protect_kernel_page(page_vaddr, prot);

next_page:
		if (page != NULL) {
			BUG_ON_MSG(!plane_vm_page_unhold(page),
				   "failed to unhold kmem protect page");
		}
		if (!ok) {
			return false;
		}
	}

	return true;
}

bool plane_kmem_init(void)
{
	plane_vaddr_t base;
	uint64_t object_size;
	uint64_t size;

	if (!kmem_claim_init()) {
		return false;
	}

	if (!hal_mmu_kernel_vma_range(&base, &size) ||
	    size == 0 ||
	    !plane_vaddr_is_page_aligned(base) ||
	    !plane_addr_is_page_aligned(size) ||
	    !plane_checked_add_u64(plane_vaddr_raw(base), size, &object_size) ||
	    ARRAY_SIZE(kernel_map_entries) == 0) {
		kmem_publish_init(false);
		return false;
	}

	if (!plane_vm_map_init(&kernel_map, kernel_map_entries,
			       ARRAY_SIZE(kernel_map_entries), base, size)) {
		kmem_publish_init(false);
		return false;
	}

	BUG_ON_MSG(!plane_vm_object_init(&kernel_object, object_size),
		   "failed to initialize kernel object");

	if (!kmem_expand_metadata()) {
		kmem_publish_init(false);
		return false;
	}
	kmem_publish_init(true);
	return true;
}

bool plane_kmem_alloc(uint64_t size, uint32_t flags, plane_vaddr_t *addr)
{
	struct kmem_kernel_context context;

	if (!kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return plane_kmem_alloc_in_map(context.map, context.object, size, flags,
				       addr);
}

bool plane_kmem_alloc_in_map(struct plane_vm_map *map,
			     struct plane_vm_object *object,
			     uint64_t size,
			     uint32_t flags,
			     plane_vaddr_t *addr)
{
	uint64_t page_count;

	if (addr == NULL ||
	    !kmem_size_to_pages(size, &page_count)) {
		return false;
	}

	return plane_kmem_alloc_pages_in_map(map, object, page_count, flags,
					     addr);
}

bool plane_kmem_free(plane_vaddr_t addr, uint64_t size)
{
	struct kmem_kernel_context context;

	if (!kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return plane_kmem_free_in_map(context.map, context.object, addr, size);
}

bool plane_kmem_free_in_map(struct plane_vm_map *map,
			    struct plane_vm_object *object,
			    plane_vaddr_t addr,
			    uint64_t size)
{
	uint64_t page_count;

	if (!kmem_size_to_pages(size, &page_count)) {
		return false;
	}

	return plane_kmem_free_pages_in_map(map, object, addr, page_count);
}

bool plane_kmem_protect(plane_vaddr_t addr, uint64_t size, uint32_t prot)
{
	struct kmem_kernel_context context;

	if (!kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return plane_kmem_protect_in_map(context.map, addr, size, prot);
}

bool plane_kmem_protect_in_map(struct plane_vm_map *map,
			       plane_vaddr_t addr,
			       uint64_t size,
			       uint32_t prot)
{
	uint64_t page_count;

	if (!kmem_size_to_pages(size, &page_count)) {
		return false;
	}

	return plane_kmem_protect_pages_in_map(map, addr, page_count, prot);
}

bool plane_kmem_alloc_pages(uint64_t page_count,
			    uint32_t flags,
			    plane_vaddr_t *vaddr)
{
	struct kmem_kernel_context context;

	if (!kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return plane_kmem_alloc_pages_in_map(context.map, context.object,
					     page_count, flags, vaddr);
}

bool plane_kmem_alloc_pages_in_map(struct plane_vm_map *map,
				   struct plane_vm_object *object,
				   uint64_t page_count,
				   uint32_t flags,
				   plane_vaddr_t *vaddr)
{
	struct plane_vm_map_allocation_info info;
	plane_vaddr_t base;

	if (vaddr == NULL ||
	    map == NULL ||
	    object == NULL ||
	    page_count == 0 ||
	    !kmem_flags_are_valid(flags)) {
		return false;
	}

	if (!kmem_reserve_vaddr(map, object, page_count, flags, &base)) {
		return false;
	}

	BUG_ON_MSG(!plane_vm_map_lookup_allocation(map, base, page_count, &info),
		   "failed to find reserved kmem allocation");
	/*
	 * This is the XNU-like KMA_KOBJECT path: guard pages are VA-only
	 * sentinels in the map entry, not materialized resident pages.
	 */
	BUG_ON_MSG(!plane_vm_map_wire_pages(map, base, page_count),
		   "failed to wire kmem virtual reservation");

	if ((flags & PLANE_KMEM_ALLOC_LAZY) == 0 &&
	    !kmem_map_allocated_pages(base, object, info.object_offset,
				      page_count, flags, info.prot)) {
		BUG_ON_MSG(!plane_vm_map_unwire_pages(map, base, page_count),
			   "failed to unwire kmem virtual reservation");
		BUG_ON_MSG(!plane_vm_map_free_pages(map, base, page_count),
			   "failed to release kmem virtual reservation");
		return false;
	}

	*vaddr = base;
	return true;
}

bool plane_kmem_protect_pages(plane_vaddr_t vaddr,
			      uint64_t page_count,
			      uint32_t prot)
{
	struct kmem_kernel_context context;

	if (!kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return plane_kmem_protect_pages_in_map(context.map, vaddr, page_count,
					       prot);
}

bool plane_kmem_fault_page(plane_vaddr_t vaddr, uint32_t fault_type)
{
	struct kmem_kernel_context context;

	if (plane_vaddr_is_null(vaddr) ||
	    !kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return plane_vm_fault_page(context.map, vaddr, fault_type);
}

bool plane_kmem_fault_pages(plane_vaddr_t vaddr,
			    uint64_t page_count,
			    uint32_t fault_type)
{
	struct kmem_kernel_context context;

	if (!kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return plane_vm_fault_pages(context.map, vaddr, page_count, fault_type);
}

bool plane_kmem_protect_pages_in_map(struct plane_vm_map *map,
				     plane_vaddr_t vaddr,
				     uint64_t page_count,
				     uint32_t prot)
{
	struct plane_vm_map_allocation_info info;

	if (map == NULL ||
	    plane_vaddr_is_null(vaddr) ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_vm_prot_is_valid(prot)) {
		return false;
	}

	if (!plane_vm_map_lookup_allocation(map, vaddr, page_count, &info)) {
		return false;
	}
	if ((prot & ~info.max_prot) != 0) {
		return false;
	}

	BUG_ON_MSG(!plane_vm_map_protect_pages(map, vaddr, page_count, prot),
		   "failed to update kmem virtual protection");
	BUG_ON_MSG(!kmem_protect_mapped_pages(vaddr, info.object,
					      info.object_offset, page_count,
					      prot),
		   "failed to protect kmem backing pages");
	return true;
}

bool plane_kmem_free_pages(plane_vaddr_t vaddr, uint64_t page_count)
{
	struct kmem_kernel_context context;

	if (!kmem_lookup_kernel_context(&context)) {
		return false;
	}

	return plane_kmem_free_pages_in_map(context.map, context.object, vaddr,
					    page_count);
}

bool plane_kmem_free_pages_in_map(struct plane_vm_map *map,
				  struct plane_vm_object *object,
				  plane_vaddr_t vaddr,
				  uint64_t page_count)
{
	struct plane_vm_map_allocation_info info;

	if (map == NULL ||
	    object == NULL ||
	    plane_vaddr_is_null(vaddr) ||
	    page_count == 0 ||
	    !plane_vaddr_is_page_aligned(vaddr)) {
		return false;
	}

	if (!plane_vm_map_lookup_allocation(map, vaddr, page_count, &info) ||
	    info.object != object) {
		return false;
	}

	if (!kmem_release_resident_pages(vaddr, object, info.object_offset,
					 page_count)) {
		return false;
	}
	BUG_ON_MSG(!plane_vm_map_unwire_pages(map, vaddr, page_count),
		   "failed to unwire kmem virtual reservation");
	BUG_ON_MSG(!plane_vm_map_free_pages(map, vaddr, page_count),
		   "failed to release kmem virtual reservation");
	return true;
}
