#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <plane/kmem.h>
#include <plane/compiler.h>
#include <plane/mm.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include "support/test.h"
#include "../kernel/mm/vm_page_internal.h"
#include "../kernel/mm/vm_zone_internal.h"

#define TEST_KMEM_PAGES 192
#define TEST_KMEM_SIZE (TEST_KMEM_PAGES * PAGE_SIZE)
#define TEST_PAGE_COUNT 192
#define TEST_GUARD_PAGE_COUNT 8
#define TEST_MAP_COUNT 192
#define TEST_SMALL_ALLOC_COUNT 130

struct plane_page {
	uint64_t phys_addr;
	uint64_t wire_count;
	struct plane_vm_object *object;
	uint64_t object_offset;
	struct plane_page *object_prev;
	struct plane_page *object_next;
	struct plane_page *object_hash_next;
	bool object_tabled;
	bool object_hashed;
	bool allocated;
	bool guard;
};

struct test_mapping {
	uint64_t vaddr;
	uint64_t phys_addr;
	uint32_t flags;
	bool used;
};

static struct plane_page test_pages[TEST_PAGE_COUNT];
static struct plane_page test_guard_pages[TEST_GUARD_PAGE_COUNT];
static struct plane_page *runtime_guard_pages;
static uint64_t runtime_guard_page_count;
static struct test_mapping test_mappings[TEST_MAP_COUNT];
static uint8_t test_kmem_storage[TEST_KMEM_SIZE] __aligned(PAGE_SIZE);

static bool is_test_page(const struct plane_page *page)
{
	return page != NULL &&
	       page >= &test_pages[0] &&
	       page < &test_pages[TEST_PAGE_COUNT];
}

static bool is_test_guard_page(const struct plane_page *page)
{
	if (page == NULL) {
		return false;
	}
	if (page >= &test_guard_pages[0] &&
	    page < &test_guard_pages[TEST_GUARD_PAGE_COUNT]) {
		return true;
	}
	return runtime_guard_pages != NULL &&
	       page >= runtime_guard_pages &&
	       page < runtime_guard_pages + runtime_guard_page_count;
}

static bool is_test_active_guard_page(const struct plane_page *page)
{
	return is_test_guard_page(page) && page->guard;
}

static bool is_test_vm_page(const struct plane_page *page)
{
	return is_test_page(page) || is_test_active_guard_page(page);
}

static uint64_t allocated_page_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (test_pages[i].allocated) {
			count++;
		}
	}

	return count;
}

static uint64_t mapping_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_MAP_COUNT; i++) {
		if (test_mappings[i].used) {
			count++;
		}
	}

	return count;
}

static uint64_t wired_page_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (test_pages[i].wire_count != 0) {
			count++;
		}
	}

	return count;
}

static uint64_t guard_page_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_GUARD_PAGE_COUNT; i++) {
		if (test_guard_pages[i].guard) {
			count++;
		}
	}
	for (uint64_t i = 0; i < runtime_guard_page_count; i++) {
		if (runtime_guard_pages[i].guard) {
			count++;
		}
	}

	return count;
}

static struct test_mapping *find_mapping(uint64_t vaddr)
{
	for (uint64_t i = 0; i < TEST_MAP_COUNT; i++) {
		if (test_mappings[i].used && test_mappings[i].vaddr == vaddr) {
			return &test_mappings[i];
		}
	}

	return NULL;
}

static plane_vaddr_t test_vaddr_from_ptr(const void *ptr)
{
	return plane_vaddr_make((uint64_t)(uintptr_t)ptr);
}

static void *test_ptr_from_vaddr(plane_vaddr_t vaddr)
{
	return (void *)(uintptr_t)plane_vaddr_raw(vaddr);
}

static bool test_kmem_alloc_pages(uint64_t page_count,
				  uint32_t flags,
				  void **addr)
{
	plane_vaddr_t vaddr;
	bool ok;

	if (addr == NULL) {
		return false;
	}

	ok = plane_kmem_alloc_pages(page_count, flags, &vaddr);
	if (ok) {
		*addr = test_ptr_from_vaddr(vaddr);
	}
	return ok;
}

static bool test_kmem_free_pages(void *addr, uint64_t page_count)
{
	return plane_kmem_free_pages(test_vaddr_from_ptr(addr), page_count);
}

bool hal_mmu_kernel_vma_range(plane_vaddr_t *base, uint64_t *size)
{
	if (base == NULL || size == NULL) {
		return false;
	}

	*base = plane_vaddr_make((uint64_t)(uintptr_t)test_kmem_storage);
	*size = TEST_KMEM_SIZE;
	return true;
}

bool hal_mmu_map_kernel_page(plane_vaddr_t vaddr,
			     plane_paddr_t phys_addr,
			     uint32_t flags)
{
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);

	if ((flags & ~HAL_MMU_MAP_WRITE) != 0 ||
	    find_mapping(raw_vaddr) != NULL) {
		return false;
	}

	for (uint64_t i = 0; i < TEST_MAP_COUNT; i++) {
		if (!test_mappings[i].used) {
			test_mappings[i].vaddr = raw_vaddr;
			test_mappings[i].phys_addr = plane_paddr_raw(phys_addr);
			test_mappings[i].flags = flags;
			test_mappings[i].used = true;
			return true;
		}
	}

	return false;
}

bool hal_mmu_unmap_kernel_page(plane_vaddr_t vaddr)
{
	struct test_mapping *mapping = find_mapping(plane_vaddr_raw(vaddr));

	if (mapping == NULL) {
		return false;
	}

	*mapping = (struct test_mapping){0};
	return true;
}

bool hal_mmu_protect_kernel_page(plane_vaddr_t vaddr, uint32_t flags)
{
	struct test_mapping *mapping = find_mapping(plane_vaddr_raw(vaddr));

	if ((flags & ~HAL_MMU_MAP_WRITE) != 0 || mapping == NULL) {
		return false;
	}

	mapping->flags = flags;
	return true;
}

bool hal_mmu_translate_kernel_page(plane_vaddr_t vaddr,
				   plane_paddr_t *phys_addr)
{
	struct test_mapping *mapping;

	if (phys_addr == NULL) {
		return false;
	}

	mapping = find_mapping(plane_vaddr_raw(vaddr));
	if (mapping == NULL) {
		return false;
	}

	*phys_addr = plane_paddr_make(mapping->phys_addr);
	return true;
}

bool plane_vm_page_grab(uint32_t flags, struct plane_page **page)
{
	if (page == NULL || (flags & ~PLANE_VM_PAGE_GRAB_ZERO) != 0) {
		return false;
	}

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (!test_pages[i].allocated) {
			test_pages[i].phys_addr = i * PAGE_SIZE;
			test_pages[i].allocated = true;
			*page = &test_pages[i];
			return true;
		}
	}

	return false;
}

bool plane_vm_page_release(struct plane_page *page)
{
	if (!is_test_page(page) ||
	    !page->allocated ||
	    page->wire_count != 0 ||
	    page->object != NULL) {
		return false;
	}

	page->allocated = false;
	page->object = NULL;
	page->object_offset = 0;
	return true;
}

bool plane_vm_page_wire(struct plane_page *page)
{
	if (!is_test_page(page) ||
	    !page->allocated ||
	    page->wire_count == UINT64_MAX) {
		return false;
	}

	page->wire_count++;
	return true;
}

bool plane_vm_page_unwire(struct plane_page *page)
{
	if (!is_test_page(page) ||
	    !page->allocated ||
	    page->wire_count == 0) {
		return false;
	}

	page->wire_count--;
	return true;
}

struct plane_page *plane_vm_page_from_phys(plane_paddr_t phys_addr)
{
	uint64_t raw_phys = plane_paddr_raw(phys_addr);
	uint64_t page = raw_phys / PAGE_SIZE;

	if (!plane_paddr_is_page_aligned(phys_addr) ||
	    page >= TEST_PAGE_COUNT) {
		return NULL;
	}

	return &test_pages[page];
}

plane_paddr_t plane_vm_page_phys(const struct plane_page *page)
{
	if (is_test_guard_page(page)) {
		return page->guard ? PLANE_VM_PAGE_GUARD_PHYS :
				     PLANE_VM_PAGE_NO_PHYS;
	}
	if (!is_test_page(page)) {
		return PLANE_VM_PAGE_NO_PHYS;
	}

	return plane_paddr_make(page->phys_addr);
}

enum plane_vm_page_state plane_vm_page_state(const struct plane_page *page)
{
	if (is_test_guard_page(page)) {
		return page->guard ? PLANE_VM_PAGE_GUARD : PLANE_VM_PAGE_INVALID;
	}
	if (!is_test_page(page)) {
		return PLANE_VM_PAGE_INVALID;
	}

	return page->allocated ? PLANE_VM_PAGE_ALLOCATED : PLANE_VM_PAGE_FREE;
}

bool plane_vm_page_is_guard(const struct plane_page *page)
{
	return plane_vm_page_state(page) == PLANE_VM_PAGE_GUARD;
}

struct plane_vm_object *plane_vm_page_object(const struct plane_page *page)
{
	if (!is_test_vm_page(page)) {
		return NULL;
	}

	return page->object;
}

bool plane_vm_page_object_offset(const struct plane_page *page,
				 uint64_t *offset)
{
	if (offset == NULL ||
	    !is_test_vm_page(page) ||
	    page->object == NULL) {
		return false;
	}

	*offset = page->object_offset;
	return true;
}

bool plane_vm_page_attach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	if (!is_test_vm_page(page) ||
	    object == NULL ||
	    (!page->allocated && !page->guard) ||
	    page->object != NULL) {
		return false;
	}

	page->object = object;
	page->object_offset = offset;
	return true;
}

bool plane_vm_page_detach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset)
{
	if (!is_test_vm_page(page) ||
	    object == NULL ||
	    (!page->allocated && !page->guard) ||
	    page->object != object ||
	    page->object_offset != offset) {
		return false;
	}

	page->object = NULL;
	page->object_offset = 0;
	return true;
}

struct plane_page *plane_vm_page_object_prev(const struct plane_page *page)
{
	if (!is_test_vm_page(page)) {
		return NULL;
	}

	return page->object_prev;
}

struct plane_page *plane_vm_page_object_next(const struct plane_page *page)
{
	if (!is_test_vm_page(page)) {
		return NULL;
	}

	return page->object_next;
}

struct plane_page *plane_vm_page_object_hash_next(const struct plane_page *page)
{
	if (!is_test_vm_page(page)) {
		return NULL;
	}

	return page->object_hash_next;
}

bool plane_vm_page_object_tabled(const struct plane_page *page)
{
	if (!is_test_vm_page(page)) {
		return false;
	}

	return page->object_tabled;
}

bool plane_vm_page_object_hashed(const struct plane_page *page)
{
	if (!is_test_vm_page(page)) {
		return false;
	}

	return page->object_hashed;
}

bool plane_vm_page_set_object_prev(struct plane_page *page,
				   struct plane_page *prev)
{
	if (!is_test_vm_page(page) ||
	    (prev != NULL && !is_test_vm_page(prev))) {
		return false;
	}

	page->object_prev = prev;
	return true;
}

bool plane_vm_page_set_object_next(struct plane_page *page,
				   struct plane_page *next)
{
	if (!is_test_vm_page(page) ||
	    (next != NULL && !is_test_vm_page(next))) {
		return false;
	}

	page->object_next = next;
	return true;
}

bool plane_vm_page_set_object_hash_next(struct plane_page *page,
					struct plane_page *next)
{
	if (!is_test_vm_page(page) ||
	    (next != NULL && !is_test_vm_page(next))) {
		return false;
	}

	page->object_hash_next = next;
	return true;
}

bool plane_vm_page_set_object_tabled(struct plane_page *page, bool tabled)
{
	if (!is_test_vm_page(page)) {
		return false;
	}

	page->object_tabled = tabled;
	return true;
}

bool plane_vm_page_set_object_hashed(struct plane_page *page, bool hashed)
{
	if (!is_test_vm_page(page)) {
		return false;
	}

	page->object_hashed = hashed;
	return true;
}

bool plane_vm_page_wire_count(const struct plane_page *page, uint64_t *wire_count)
{
	if (wire_count == NULL ||
	    !is_test_vm_page(page)) {
		return false;
	}

	*wire_count = page->wire_count;
	return true;
}

bool plane_vm_page_guard_storage_size(uint64_t count, uint64_t *size)
{
	if (size == NULL ||
	    count == 0 ||
	    count > UINT64_MAX / sizeof(test_guard_pages[0])) {
		return false;
	}

	*size = count * sizeof(test_guard_pages[0]);
	return true;
}

bool plane_vm_page_add_guard_storage(void *storage,
				     uint64_t count,
				     struct plane_vm_zone_segment *segment)
{
	if (storage == NULL ||
	    count == 0 ||
	    segment == NULL ||
	    runtime_guard_pages != NULL) {
		return false;
	}

	runtime_guard_pages = storage;
	runtime_guard_page_count = count;
	*segment = (struct plane_vm_zone_segment){
		.storage = storage,
		.count = count,
	};
	return true;
}

struct plane_page *plane_vm_page_create_guard(void)
{
	for (uint64_t i = 0; i < TEST_GUARD_PAGE_COUNT; i++) {
		if (!test_guard_pages[i].guard) {
			test_guard_pages[i] = (struct plane_page){0};
			test_guard_pages[i].phys_addr =
				PLANE_VM_PAGE_GUARD_PHYS_RAW;
			test_guard_pages[i].guard = true;
			return &test_guard_pages[i];
		}
	}
	for (uint64_t i = 0; i < runtime_guard_page_count; i++) {
		if (!runtime_guard_pages[i].guard) {
			runtime_guard_pages[i] = (struct plane_page){0};
			runtime_guard_pages[i].phys_addr =
				PLANE_VM_PAGE_GUARD_PHYS_RAW;
			runtime_guard_pages[i].guard = true;
			return &runtime_guard_pages[i];
		}
	}

	return NULL;
}

bool plane_vm_page_release_guard(struct plane_page *page)
{
	if (!is_test_guard_page(page) ||
	    !page->guard ||
	    page->wire_count != 0 ||
	    page->object != NULL ||
	    page->object_prev != NULL ||
	    page->object_next != NULL ||
	    page->object_hash_next != NULL ||
	    page->object_tabled ||
	    page->object_hashed) {
		return false;
	}

	page->guard = false;
	page->phys_addr = PLANE_VM_PAGE_NO_PHYS_RAW;
	return true;
}

static int test_global_kmem_init_is_one_shot(void)
{
	void *small_allocs[TEST_SMALL_ALLOC_COUNT];
	struct plane_page *guards[TEST_GUARD_PAGE_COUNT + 1];
	void *addr = NULL;
	void *guarded_addr = NULL;
	void *lazy_addr = NULL;
	void *lazy_range_addr = NULL;
	void *readonly_addr = NULL;
	struct test_mapping *mapping;
	uint64_t mapped_phys;
	uint64_t init_allocated;
	uint64_t init_mappings;
	uint64_t init_wired;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_SMALL_ALLOC_COUNT; i++) {
		small_allocs[i] = NULL;
	}
	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(guards); i++) {
		guards[i] = NULL;
	}

	failures += test_expect_bool("global fault before init",
				     plane_kmem_fault_page(
					     plane_vaddr_make(
						     (uint64_t)(uintptr_t)
							     test_kmem_storage),
					     PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("global range fault before init",
				     plane_kmem_fault_pages(
					     plane_vaddr_make(
						     (uint64_t)(uintptr_t)
							     test_kmem_storage),
					     1, PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("global init", plane_kmem_init(), true);
	init_allocated = allocated_page_count();
	init_mappings = mapping_count();
	init_wired = wired_page_count();
	failures += test_expect_bool("global metadata eager backing",
				     init_allocated != 0 &&
					     init_mappings != 0 &&
					     init_wired != 0,
				     true);
	failures += test_expect_bool("global guard storage expanded",
				     runtime_guard_page_count >
					     TEST_GUARD_PAGE_COUNT,
				     true);
	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(guards); i++) {
		guards[i] = plane_vm_page_create_guard();
		failures += test_expect_not_null("global expanded guard create",
						 guards[i]);
	}
	failures += test_expect_u64("global expanded guard count",
				    guard_page_count(),
				    TEST_ARRAY_SIZE(guards));
	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(guards); i++) {
		failures += test_expect_bool("global expanded guard release",
					     plane_vm_page_release_guard(
						     guards[i]),
					     true);
	}
	failures += test_expect_u64("global expanded guard count released",
				    guard_page_count(), 0);

	failures += test_expect_bool("global alloc",
				     test_kmem_alloc_pages(2, 0, &addr),
				     true);
	failures += test_expect_u64("global wired pages",
				    wired_page_count(), init_wired + 2);
	mapping = find_mapping((uint64_t)(uintptr_t)addr);
	failures += test_expect_not_null("global fault mapping", mapping);
	if (mapping != NULL) {
		mapped_phys = mapping->phys_addr;
		failures += test_expect_u32("global fault starts writable",
					    mapping->flags,
					    HAL_MMU_MAP_WRITE);
		failures += test_expect_bool("global fault unmap",
					     hal_mmu_unmap_kernel_page(
						     plane_vaddr_make(
							     (uint64_t)(uintptr_t)addr)),
					     true);
		failures += test_expect_u64("global fault mapping removed",
					    mapping_count(),
					    init_mappings + 1);
		failures += test_expect_bool("global fault repairs pmap",
					     plane_kmem_fault_page(
						     plane_vaddr_make(
							     (uint64_t)(uintptr_t)addr),
						     PLANE_VM_PROT_READ),
					     true);
		mapping = find_mapping((uint64_t)(uintptr_t)addr);
		failures += test_expect_not_null("global fault remapped",
						 mapping);
		if (mapping != NULL) {
			failures += test_expect_u64("global fault remap phys",
						    mapping->phys_addr,
						    mapped_phys);
			failures += test_expect_u32("global fault remap flags",
						    mapping->flags,
						    HAL_MMU_MAP_WRITE);
		}
	}
	failures += test_expect_u64("global fault mapped count",
				    mapping_count(), init_mappings + 2);
	failures += test_expect_u64("global fault allocated stable",
				    allocated_page_count(), init_allocated + 2);
	failures += test_expect_u64("global fault wired stable",
				    wired_page_count(), init_wired + 2);
	failures += test_expect_bool("global repeat init",
				     plane_kmem_init(), false);
	failures += test_expect_bool("global preserved free",
				     test_kmem_free_pages(addr, 2), true);
	failures += test_expect_u64("global free backing pages",
				    allocated_page_count(), init_allocated);
	failures += test_expect_u64("global free wired pages",
				    wired_page_count(), init_wired);
	failures += test_expect_u64("global free mappings",
				    mapping_count(), init_mappings);
	failures += test_expect_bool("global guarded alloc",
				     test_kmem_alloc_pages(
					     2, PLANE_KMEM_ALLOC_GUARD,
					     &guarded_addr),
				     true);
	failures += test_expect_u64("global guarded no resident guards",
				    guard_page_count(), 0);
	failures += test_expect_bool("global guarded free",
				     test_kmem_free_pages(guarded_addr, 2),
				     true);
	failures += test_expect_u64("global guarded free guards",
				    guard_page_count(), 0);

	failures += test_expect_bool("global readonly alloc",
				     test_kmem_alloc_pages(
					     1, PLANE_KMEM_ALLOC_READONLY,
					     &readonly_addr),
				     true);
	mapping = find_mapping((uint64_t)(uintptr_t)readonly_addr);
	failures += test_expect_not_null("global readonly mapping", mapping);
	if (mapping != NULL) {
		mapped_phys = mapping->phys_addr;
		failures += test_expect_u32("global readonly starts ro",
					    mapping->flags, 0);
		failures += test_expect_bool("global readonly write fault",
					     plane_kmem_fault_page(
						     plane_vaddr_make(
							     (uint64_t)(uintptr_t)
								     readonly_addr),
						     PLANE_VM_PROT_READ |
							     PLANE_VM_PROT_WRITE),
					     false);
		mapping = find_mapping((uint64_t)(uintptr_t)readonly_addr);
		failures += test_expect_not_null("global readonly unchanged",
						 mapping);
		if (mapping != NULL) {
			failures += test_expect_u64(
				"global readonly phys unchanged",
				mapping->phys_addr, mapped_phys);
			failures += test_expect_u32(
				"global readonly flags unchanged",
				mapping->flags, 0);
		}
	}
	failures += test_expect_bool("global readonly free",
				     test_kmem_free_pages(readonly_addr, 1),
				     true);
	failures += test_expect_u64("global readonly free backing pages",
				    allocated_page_count(), init_allocated);
	failures += test_expect_u64("global readonly free mappings",
				    mapping_count(), init_mappings);

	failures += test_expect_bool("global lazy alloc",
				     test_kmem_alloc_pages(
					     2, PLANE_KMEM_ALLOC_LAZY,
					     &lazy_addr),
				     true);
	failures += test_expect_u64("global lazy no eager backing",
				    allocated_page_count(), init_allocated);
	failures += test_expect_u64("global lazy no eager wiring",
				    wired_page_count(), init_wired);
	failures += test_expect_u64("global lazy no eager mapping",
				    mapping_count(), init_mappings);
	failures += test_expect_bool("global lazy fault",
				     plane_kmem_fault_page(
					     plane_vaddr_make(
						     (uint64_t)(uintptr_t)
							     lazy_addr),
					     PLANE_VM_PROT_READ),
				     true);
	mapping = find_mapping((uint64_t)(uintptr_t)lazy_addr);
	failures += test_expect_not_null("global lazy mapped", mapping);
	if (mapping != NULL) {
		failures += test_expect_u32("global lazy mapping writable",
					    mapping->flags,
					    HAL_MMU_MAP_WRITE);
	}
	failures += test_expect_u64("global lazy fault backing",
				    allocated_page_count(), init_allocated + 1);
	failures += test_expect_u64("global lazy fault wired",
				    wired_page_count(), init_wired + 1);
	failures += test_expect_u64("global lazy fault mapping count",
				    mapping_count(), init_mappings + 1);
	failures += test_expect_bool("global lazy free",
				     test_kmem_free_pages(lazy_addr, 2),
				     true);
	failures += test_expect_u64("global lazy free backing",
				    allocated_page_count(), init_allocated);
	failures += test_expect_u64("global lazy free wired",
				    wired_page_count(), init_wired);
	failures += test_expect_u64("global lazy free mappings",
				    mapping_count(), init_mappings);

	failures += test_expect_bool("global lazy range alloc",
				     test_kmem_alloc_pages(
					     2, PLANE_KMEM_ALLOC_LAZY,
					     &lazy_range_addr),
				     true);
	failures += test_expect_u64("global lazy range no eager backing",
				    allocated_page_count(), init_allocated);
	failures += test_expect_u64("global lazy range no eager wiring",
				    wired_page_count(), init_wired);
	failures += test_expect_u64("global lazy range no eager mapping",
				    mapping_count(), init_mappings);
	failures += test_expect_bool("global lazy range fault",
				     plane_kmem_fault_pages(
					     plane_vaddr_make(
						     (uint64_t)(uintptr_t)
							     lazy_range_addr),
					     2, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_u64("global lazy range backing",
				    allocated_page_count(), init_allocated + 2);
	failures += test_expect_u64("global lazy range wired",
				    wired_page_count(), init_wired + 2);
	failures += test_expect_u64("global lazy range mappings",
				    mapping_count(), init_mappings + 2);
	failures += test_expect_not_null("global lazy range first mapped",
					 find_mapping((uint64_t)(uintptr_t)
							      lazy_range_addr));
	failures += test_expect_not_null(
		"global lazy range second mapped",
		find_mapping((uint64_t)(uintptr_t)lazy_range_addr + PAGE_SIZE));
	failures += test_expect_bool("global lazy range free",
				     test_kmem_free_pages(lazy_range_addr, 2),
				     true);
	failures += test_expect_u64("global lazy range free backing",
				    allocated_page_count(), init_allocated);
	failures += test_expect_u64("global lazy range free wired",
				    wired_page_count(), init_wired);
	failures += test_expect_u64("global lazy range free mappings",
				    mapping_count(), init_mappings);

	for (uint64_t i = 0; i < TEST_SMALL_ALLOC_COUNT; i++) {
		failures += test_expect_bool("global small alloc",
					     test_kmem_alloc_pages(
						     1, 0, &small_allocs[i]),
					     true);
	}
	failures += test_expect_u64("global small allocated pages",
				    allocated_page_count(),
				    init_allocated + TEST_SMALL_ALLOC_COUNT);
	for (uint64_t i = 0; i < TEST_SMALL_ALLOC_COUNT; i++) {
		failures += test_expect_bool("global small free",
					     test_kmem_free_pages(
						     small_allocs[i], 1),
					     true);
	}
	failures += test_expect_u64("global small free backing pages",
				    allocated_page_count(), init_allocated);
	failures += test_expect_u64("global small free mappings",
				    mapping_count(), init_mappings);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_global_kmem_init_is_one_shot),
	};

	return test_run_cases("kmem_global_test", cases, TEST_ARRAY_SIZE(cases));
}
