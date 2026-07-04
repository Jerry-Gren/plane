#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <plane/kmem.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/vm_map.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include "support/test.h"
#include "../kernel/mm/vm_page_internal.h"

#define TEST_KMEM_BASE 0xffff900000000000ull
#define TEST_KMEM_PAGES 256
#define TEST_PAGE_COUNT 256
#define TEST_MAP_COUNT 256
#define TEST_KMEM_SIZE (TEST_KMEM_PAGES * PAGE_SIZE)
#define TEST_KMEM_OBJECT_SIZE (TEST_KMEM_BASE + TEST_KMEM_SIZE)
#define TEST_ALLOCATION_RECORDS 128

static struct plane_vm_map_entry test_map_entries[TEST_ALLOCATION_RECORDS];
static struct plane_vm_map test_map;
static struct plane_vm_object_page test_object_pages[TEST_PAGE_COUNT];
static struct plane_vm_object test_object;

struct plane_page {
	uint64_t phys_addr;
	uint64_t wire_count;
	struct plane_vm_object *object;
	uint64_t object_offset;
	bool allocated;
	uint32_t flags;
};

struct test_mapping {
	uint64_t vaddr;
	uint64_t phys_addr;
	uint32_t flags;
	bool used;
};

static struct plane_page test_pages[TEST_PAGE_COUNT];
static struct test_mapping test_mappings[TEST_MAP_COUNT];
static uint64_t test_kmem_base;
static uint64_t test_kmem_size;
static uint64_t pmm_alloc_attempts;
static uint64_t pmm_fail_after;
static bool pmm_force_fail;
static uint64_t map_attempts;
static uint64_t map_fail_after;
static uint32_t last_pmm_flags;
static void reset_kmem_test(void)
{
	test_map = (struct plane_vm_map){0};
	test_object = (struct plane_vm_object){0};
	for (uint64_t i = 0; i < TEST_ALLOCATION_RECORDS; i++) {
		test_map_entries[i] = (struct plane_vm_map_entry){0};
	}
	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		test_object_pages[i] = (struct plane_vm_object_page){0};
	}

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		test_pages[i].phys_addr = i * PAGE_SIZE;
		test_pages[i].wire_count = 0;
		test_pages[i].object = NULL;
		test_pages[i].object_offset = 0;
		test_pages[i].allocated = false;
		test_pages[i].flags = 0;
	}

	for (uint64_t i = 0; i < TEST_MAP_COUNT; i++) {
		test_mappings[i] = (struct test_mapping){0};
	}

	test_kmem_base = TEST_KMEM_BASE;
	test_kmem_size = TEST_KMEM_SIZE;
	pmm_alloc_attempts = 0;
	pmm_fail_after = UINT64_MAX;
	pmm_force_fail = false;
	map_attempts = 0;
	map_fail_after = UINT64_MAX;
	last_pmm_flags = 0;

	if (!plane_vm_map_init(&test_map, test_map_entries,
			       TEST_ALLOCATION_RECORDS,
			       TEST_KMEM_BASE,
			       TEST_KMEM_SIZE)) {
		test_fail("failed to init kmem test map");
	}
	if (!plane_vm_object_init(&test_object, test_object_pages,
				  TEST_PAGE_COUNT, TEST_KMEM_OBJECT_SIZE)) {
		test_fail("failed to init kmem test object");
	}
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

static uint64_t allocated_page_count_with_flags(uint32_t flags)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (test_pages[i].allocated &&
		    test_pages[i].flags == flags) {
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

static uint64_t object_page_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (test_object_pages[i].used) {
			count++;
		}
	}

	return count;
}

static uint64_t kmem_page_vaddr(uint64_t page)
{
	return TEST_KMEM_BASE + page * PAGE_SIZE;
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

bool hal_mmu_kernel_vma_range(uint64_t *base, uint64_t *size)
{
	if (base == NULL || size == NULL) {
		return false;
	}

	*base = test_kmem_base;
	*size = test_kmem_size;
	return true;
}

bool hal_mmu_map_kernel_page(uint64_t vaddr, uint64_t phys_addr, uint32_t flags)
{
	if ((flags & ~HAL_MMU_MAP_WRITE) != 0 ||
	    find_mapping(vaddr) != NULL ||
	    map_attempts++ >= map_fail_after) {
		return false;
	}

	for (uint64_t i = 0; i < TEST_MAP_COUNT; i++) {
		if (!test_mappings[i].used) {
			test_mappings[i].vaddr = vaddr;
			test_mappings[i].phys_addr = phys_addr;
			test_mappings[i].flags = flags;
			test_mappings[i].used = true;
			return true;
		}
	}

	return false;
}

bool hal_mmu_unmap_kernel_page(uint64_t vaddr)
{
	struct test_mapping *mapping = find_mapping(vaddr);

	if (mapping == NULL) {
		return false;
	}

	*mapping = (struct test_mapping){0};
	return true;
}

bool hal_mmu_protect_kernel_page(uint64_t vaddr, uint32_t flags)
{
	struct test_mapping *mapping;

	if ((flags & ~HAL_MMU_MAP_WRITE) != 0) {
		return false;
	}

	mapping = find_mapping(vaddr);
	if (mapping == NULL) {
		return false;
	}

	mapping->flags = flags;
	return true;
}

bool hal_mmu_translate_kernel_page(uint64_t vaddr, uint64_t *phys_addr)
{
	struct test_mapping *mapping;

	if (phys_addr == NULL) {
		return false;
	}

	mapping = find_mapping(vaddr);
	if (mapping == NULL) {
		return false;
	}

	*phys_addr = mapping->phys_addr;
	return true;
}

bool plane_pmm_alloc_page_flags(uint32_t flags, struct plane_page **page)
{
	if (page == NULL || (flags & ~PLANE_PMM_ALLOC_ZERO) != 0) {
		return false;
	}

	if (pmm_force_fail || pmm_alloc_attempts++ >= pmm_fail_after) {
		return false;
	}

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (!test_pages[i].allocated) {
			test_pages[i].allocated = true;
			test_pages[i].flags = flags;
			last_pmm_flags = flags;
			*page = &test_pages[i];
			return true;
		}
	}

	return false;
}

bool plane_pmm_free_page_phys(uint64_t phys_addr)
{
	uint64_t page = phys_addr / PAGE_SIZE;

	if ((phys_addr & (PAGE_SIZE - 1)) != 0 ||
	    page >= TEST_PAGE_COUNT ||
	    !test_pages[page].allocated ||
	    test_pages[page].wire_count != 0 ||
	    test_pages[page].object != NULL) {
		return false;
	}

	test_pages[page].allocated = false;
	test_pages[page].flags = 0;
	test_pages[page].object = NULL;
	test_pages[page].object_offset = 0;
	return true;
}

bool plane_vm_page_wire(struct plane_page *page)
{
	if (page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT] ||
	    !page->allocated ||
	    page->wire_count == UINT64_MAX) {
		return false;
	}

	page->wire_count++;
	return true;
}

bool plane_vm_page_unwire(struct plane_page *page)
{
	if (page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT] ||
	    !page->allocated ||
	    page->wire_count == 0) {
		return false;
	}

	page->wire_count--;
	return true;
}

struct plane_page *plane_vm_page_from_phys(uint64_t phys_addr)
{
	uint64_t page = phys_addr / PAGE_SIZE;

	if ((phys_addr & (PAGE_SIZE - 1)) != 0 ||
	    page >= TEST_PAGE_COUNT) {
		return NULL;
	}

	return &test_pages[page];
}

uint64_t plane_vm_page_phys(const struct plane_page *page)
{
	if (page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT]) {
		return UINT64_MAX;
	}

	return page->phys_addr;
}

enum plane_vm_page_state plane_vm_page_state(const struct plane_page *page)
{
	if (page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT]) {
		return PLANE_VM_PAGE_INVALID;
	}

	return page->allocated ? PLANE_VM_PAGE_ALLOCATED : PLANE_VM_PAGE_FREE;
}

struct plane_vm_object *plane_vm_page_object(const struct plane_page *page)
{
	if (page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT]) {
		return NULL;
	}

	return page->object;
}

bool plane_vm_page_object_offset(const struct plane_page *page,
				 uint64_t *offset)
{
	if (offset == NULL ||
	    page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT] ||
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
	if (page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT] ||
	    object == NULL ||
	    !page->allocated ||
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
	if (page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT] ||
	    object == NULL ||
	    !page->allocated ||
	    page->object != object ||
	    page->object_offset != offset) {
		return false;
	}

	page->object = NULL;
	page->object_offset = 0;
	return true;
}

bool plane_vm_page_wire_count(const struct plane_page *page, uint64_t *wire_count)
{
	if (wire_count == NULL ||
	    page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT]) {
		return false;
	}

	*wire_count = page->wire_count;
	return true;
}

static int test_alloc_and_free_pages(void)
{
	void *addr = NULL;
	struct plane_vm_map_allocation_info info = {0};
	struct test_mapping *first;
	int failures = 0;

	failures += test_expect_bool("alloc pages",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     true);
	failures += test_expect_ptr("alloc addr", addr, (void *)TEST_KMEM_BASE);
	failures += test_expect_u64("alloc pmm pages", allocated_page_count(), 2);
	failures += test_expect_u64("alloc wired pages", wired_page_count(), 2);
	failures += test_expect_u64("alloc object pages", object_page_count(), 2);
	failures += test_expect_u64("alloc object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("alloc object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("alloc mappings", mapping_count(), 2);
	failures += test_expect_bool("alloc lookup",
				     plane_vm_map_lookup_allocation(&test_map,
					     TEST_KMEM_BASE, 2, &info),
				     true);
	failures += test_expect_u64("alloc map wired count",
				    info.wired_count, 1);
	failures += test_expect_ptr("alloc object first page",
				    plane_vm_object_lookup_page(&test_object,
								TEST_KMEM_BASE),
				    &test_pages[0]);
	failures += test_expect_ptr("alloc object second page",
				    plane_vm_object_lookup_page(&test_object,
								TEST_KMEM_BASE +
								PAGE_SIZE),
				    &test_pages[1]);
	failures += test_expect_ptr("alloc page object",
				    plane_vm_page_object(&test_pages[0]),
				    &test_object);
	failures += test_expect_u64("alloc page object offset",
				    test_pages[0].object_offset,
				    TEST_KMEM_BASE);

	first = find_mapping(TEST_KMEM_BASE);
	failures += test_expect_not_null("first mapping", first);
	if (first != NULL) {
		failures += test_expect_u64("first mapping phys",
					    first->phys_addr, 0);
		failures += test_expect_u32("first mapping writable",
					    first->flags, HAL_MMU_MAP_WRITE);
	}

	failures += test_expect_bool("free pages",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, addr, 2), true);
	failures += test_expect_u64("free pmm pages", allocated_page_count(), 0);
	failures += test_expect_u64("free wired pages", wired_page_count(), 0);
	failures += test_expect_u64("free object pages", object_page_count(), 0);
	failures += test_expect_u64("free object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("free object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_null("free page object cleared",
				     plane_vm_page_object(&test_pages[0]));
	failures += test_expect_u64("free mappings", mapping_count(), 0);
	return failures;
}

static int test_readonly_alloc_maps_without_write_flag(void)
{
	void *addr = NULL;
	struct test_mapping *mapping;
	int failures = 0;

	failures += test_expect_bool("readonly alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 1, PLANE_KMEM_ALLOC_READONLY,
					     &addr),
				     true);
	mapping = find_mapping(TEST_KMEM_BASE);
	failures += test_expect_not_null("readonly mapping", mapping);
	if (mapping != NULL) {
		failures += test_expect_u32("readonly mapping flags",
					    mapping->flags, 0);
	}
	failures += test_expect_u64("readonly pmm pages",
				    allocated_page_count(), 1);
	failures += test_expect_bool("readonly free",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, addr, 1), true);
	return failures;
}

static int test_protect_pages_updates_mapping_flags(void)
{
	void *addr = NULL;
	struct test_mapping *first;
	struct test_mapping *second;
	int failures = 0;

	failures += test_expect_bool("protect alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     true);
	first = find_mapping(kmem_page_vaddr(0));
	second = find_mapping(kmem_page_vaddr(1));
	failures += test_expect_not_null("protect first mapping", first);
	failures += test_expect_not_null("protect second mapping", second);
	if (first != NULL) {
		failures += test_expect_u32("protect first writable",
					    first->flags, HAL_MMU_MAP_WRITE);
	}
	if (second != NULL) {
		failures += test_expect_u32("protect second writable",
					    second->flags, HAL_MMU_MAP_WRITE);
	}

	failures += test_expect_bool("protect readonly",
				     plane_kmem_protect_pages_in_map(&test_map,
					     addr, 2, PLANE_VM_PROT_READ),
				     true);
	if (first != NULL) {
		failures += test_expect_u32("protect first readonly",
					    first->flags, 0);
	}
	if (second != NULL) {
		failures += test_expect_u32("protect second readonly",
					    second->flags, 0);
	}

	failures += test_expect_bool("protect writable",
				     plane_kmem_protect_pages_in_map(&test_map,
					     addr, 2, PLANE_VM_PROT_DEFAULT),
				     true);
	if (first != NULL) {
		failures += test_expect_u32("protect first writable again",
					    first->flags, HAL_MMU_MAP_WRITE);
	}
	if (second != NULL) {
		failures += test_expect_u32("protect second writable again",
					    second->flags, HAL_MMU_MAP_WRITE);
	}
	return failures;
}

static int test_readonly_allocation_can_be_promoted_to_writable(void)
{
	void *addr = NULL;
	struct test_mapping *mapping;
	int failures = 0;

	failures += test_expect_bool("readonly promote alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 1, PLANE_KMEM_ALLOC_READONLY,
					     &addr),
				     true);
	mapping = find_mapping(TEST_KMEM_BASE);
	failures += test_expect_not_null("readonly promote mapping", mapping);
	if (mapping != NULL) {
		failures += test_expect_u32("readonly promote starts ro",
					    mapping->flags, 0);
	}
	failures += test_expect_bool("readonly promote writable",
				     plane_kmem_protect_pages_in_map(&test_map,
					     addr, 1, PLANE_VM_PROT_DEFAULT),
				     true);
	if (mapping != NULL) {
		failures += test_expect_u32("readonly promote flags",
					    mapping->flags, HAL_MMU_MAP_WRITE);
	}
	return failures;
}

static int test_protect_bytes_rounds_to_exact_allocation(void)
{
	void *addr = NULL;
	struct test_mapping *first;
	struct test_mapping *second;
	int failures = 0;

	failures += test_expect_bool("byte protect alloc",
				     plane_kmem_alloc_in_map(&test_map, &test_object, PAGE_SIZE + 1, 0, &addr),
				     true);
	first = find_mapping(kmem_page_vaddr(0));
	second = find_mapping(kmem_page_vaddr(1));
	failures += test_expect_bool("byte protect rejects partial",
				     plane_kmem_protect_in_map(&test_map, addr, 1,
							PLANE_VM_PROT_READ),
				     false);
	if (first != NULL) {
		failures += test_expect_u32("byte partial first unchanged",
					    first->flags, HAL_MMU_MAP_WRITE);
	}
	if (second != NULL) {
		failures += test_expect_u32("byte partial second unchanged",
					    second->flags, HAL_MMU_MAP_WRITE);
	}
	failures += test_expect_bool("byte protect exact rounded",
				     plane_kmem_protect_in_map(&test_map, addr, PAGE_SIZE + 1,
							PLANE_VM_PROT_READ),
				     true);
	if (first != NULL) {
		failures += test_expect_u32("byte protect first readonly",
					    first->flags, 0);
	}
	if (second != NULL) {
		failures += test_expect_u32("byte protect second readonly",
					    second->flags, 0);
	}
	return failures;
}

static int test_guard_protect_updates_only_user_pages(void)
{
	void *addr = NULL;
	struct test_mapping *first;
	struct test_mapping *second;
	int failures = 0;

	failures += test_expect_bool("guard protect alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, PLANE_KMEM_ALLOC_GUARD, &addr),
				     true);
	failures += test_expect_bool("guard protect readonly",
				     plane_kmem_protect_pages_in_map(&test_map,
					     addr, 2, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_null("guard protect left unmapped",
				     find_mapping(kmem_page_vaddr(0)));
	first = find_mapping(kmem_page_vaddr(1));
	second = find_mapping(kmem_page_vaddr(2));
	failures += test_expect_not_null("guard protect first user", first);
	failures += test_expect_not_null("guard protect second user", second);
	if (first != NULL) {
		failures += test_expect_u32("guard protect first readonly",
					    first->flags, 0);
	}
	if (second != NULL) {
		failures += test_expect_u32("guard protect second readonly",
					    second->flags, 0);
	}
	failures += test_expect_null("guard protect right unmapped",
				     find_mapping(kmem_page_vaddr(3)));
	return failures;
}

static int test_protect_rejects_invalid_inputs(void)
{
	void *addr = NULL;
	struct test_mapping *mapping;
	int failures = 0;

	failures += test_expect_bool("protect invalid alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     true);
	mapping = find_mapping(kmem_page_vaddr(0));
	failures += test_expect_bool("protect null",
				     plane_kmem_protect_pages_in_map(&test_map, NULL, 2,
							      PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("protect zero pages",
				     plane_kmem_protect_pages_in_map(&test_map, addr, 0,
							      PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("protect unaligned",
				     plane_kmem_protect_pages_in_map(&test_map,
					     (void *)(TEST_KMEM_BASE + 1), 2,
					     PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("protect none",
				     plane_kmem_protect_pages_in_map(&test_map, addr, 2, 0),
				     false);
	failures += test_expect_bool("protect unknown",
				     plane_kmem_protect_pages_in_map(&test_map, addr, 2, BIT(8)),
				     false);
	failures += test_expect_bool("protect partial",
				     plane_kmem_protect_pages_in_map(&test_map,
					     addr, 1, PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("byte protect zero",
				     plane_kmem_protect_in_map(&test_map, addr, 0,
							PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("byte protect overflow",
				     plane_kmem_protect_in_map(&test_map, addr, UINT64_MAX,
							PLANE_VM_PROT_READ),
				     false);
	if (mapping != NULL) {
		failures += test_expect_u32("protect invalid unchanged",
					    mapping->flags, HAL_MMU_MAP_WRITE);
	}
	return failures;
}

static int test_guard_alloc_and_free_pages(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("guard alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, PLANE_KMEM_ALLOC_GUARD, &addr),
				     true);
	failures += test_expect_ptr("guard user addr",
				    addr, (void *)kmem_page_vaddr(1));
	failures += test_expect_u64("guard pmm pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("guard wired pages", wired_page_count(), 2);
	failures += test_expect_u64("guard object pages", object_page_count(), 2);
	failures += test_expect_u64("guard object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("guard object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("guard mappings", mapping_count(), 2);
	failures += test_expect_null("guard object left absent",
				     plane_vm_object_lookup_page(&test_object,
								 TEST_KMEM_BASE));
	failures += test_expect_ptr("guard object first user",
				    plane_vm_object_lookup_page(&test_object,
								kmem_page_vaddr(1)),
				    &test_pages[0]);
	failures += test_expect_ptr("guard object second user",
				    plane_vm_object_lookup_page(&test_object,
								kmem_page_vaddr(2)),
				    &test_pages[1]);
	failures += test_expect_ptr("guard page object",
				    plane_vm_page_object(&test_pages[0]),
				    &test_object);
	failures += test_expect_u64("guard page object offset",
				    test_pages[0].object_offset,
				    kmem_page_vaddr(1));
	failures += test_expect_null("guard left unmapped",
				     find_mapping(kmem_page_vaddr(0)));
	failures += test_expect_not_null("guard first user mapped",
					 find_mapping(kmem_page_vaddr(1)));
	failures += test_expect_not_null("guard second user mapped",
					 find_mapping(kmem_page_vaddr(2)));
	failures += test_expect_null("guard right unmapped",
				     find_mapping(kmem_page_vaddr(3)));

	failures += test_expect_bool("guard free",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, addr, 2), true);
	failures += test_expect_u64("guard free pmm pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("guard free wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("guard free object pages",
				    object_page_count(), 0);
	failures += test_expect_u64("guard free object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard free object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard free mappings", mapping_count(), 0);
	failures += test_expect_bool("guard hole reuse",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 4, 0, &addr),
				     true);
	failures += test_expect_ptr("guard reused reserved hole",
				    addr, (void *)TEST_KMEM_BASE);
	return failures;
}

static int test_alloc_and_free_bytes(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("byte alloc",
				     plane_kmem_alloc_in_map(&test_map, &test_object, 1, 0, &addr), true);
	failures += test_expect_ptr("byte addr", addr, (void *)TEST_KMEM_BASE);
	failures += test_expect_u64("byte pmm pages", allocated_page_count(), 1);
	failures += test_expect_u64("byte wired pages", wired_page_count(), 1);
	failures += test_expect_u64("byte mappings", mapping_count(), 1);
	failures += test_expect_bool("byte free", plane_kmem_free_in_map(&test_map, &test_object, addr, 1), true);
	failures += test_expect_u64("byte free pmm pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("byte free wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("byte free mappings", mapping_count(), 0);
	return failures;
}

static int test_byte_guard_alloc_and_free(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("byte guard alloc",
				     plane_kmem_alloc_in_map(&test_map, &test_object, 1, PLANE_KMEM_ALLOC_GUARD,
						      &addr),
				     true);
	failures += test_expect_ptr("byte guard user addr",
				    addr, (void *)kmem_page_vaddr(1));
	failures += test_expect_u64("byte guard pmm pages",
				    allocated_page_count(), 1);
	failures += test_expect_u64("byte guard wired pages",
				    wired_page_count(), 1);
	failures += test_expect_u64("byte guard mappings", mapping_count(), 1);
	failures += test_expect_null("byte guard left unmapped",
				     find_mapping(kmem_page_vaddr(0)));
	failures += test_expect_not_null("byte guard user mapped",
					 find_mapping(kmem_page_vaddr(1)));
	failures += test_expect_null("byte guard right unmapped",
				     find_mapping(kmem_page_vaddr(2)));
	failures += test_expect_bool("byte guard free",
				     plane_kmem_free_in_map(&test_map, &test_object, addr, 1), true);
	failures += test_expect_u64("byte guard free pmm pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("byte guard free wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("byte guard free mappings",
				    mapping_count(), 0);
	return failures;
}

static int test_byte_alloc_rounds_up_to_pages(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("round alloc",
				     plane_kmem_alloc_in_map(&test_map, &test_object, PAGE_SIZE + 1, 0, &addr),
				     true);
	failures += test_expect_u64("round pmm pages", allocated_page_count(), 2);
	failures += test_expect_u64("round wired pages", wired_page_count(), 2);
	failures += test_expect_u64("round mappings", mapping_count(), 2);
	failures += test_expect_bool("round free",
				     plane_kmem_free_in_map(&test_map, &test_object, addr, PAGE_SIZE + 1),
				     true);
	failures += test_expect_u64("round free pmm pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("round free wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("round free mappings", mapping_count(), 0);
	return failures;
}

static int test_zero_flag_reaches_pmm(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("zero alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 1, PLANE_KMEM_ALLOC_ZERO, &addr),
				     true);
	failures += test_expect_u32("zero pmm flag",
				    last_pmm_flags, PLANE_PMM_ALLOC_ZERO);
	return failures;
}

static int test_byte_zero_flag_reaches_all_pages(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("byte zero alloc",
				     plane_kmem_alloc_in_map(&test_map, &test_object, PAGE_SIZE + 1,
						      PLANE_KMEM_ALLOC_ZERO,
						      &addr),
				     true);
	failures += test_expect_u64("byte zero pmm pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("byte zero flagged pages",
				    allocated_page_count_with_flags(
					    PLANE_PMM_ALLOC_ZERO),
				    2);
	return failures;
}

static int test_readonly_zero_maps_without_write_and_zeros_pages(void)
{
	void *addr = NULL;
	struct test_mapping *first;
	struct test_mapping *second;
	int failures = 0;

	failures += test_expect_bool("readonly zero alloc",
				     plane_kmem_alloc_in_map(&test_map, &test_object, PAGE_SIZE + 1,
						      PLANE_KMEM_ALLOC_READONLY |
						      PLANE_KMEM_ALLOC_ZERO,
						      &addr),
				     true);
	failures += test_expect_u64("readonly zero pmm pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("readonly zero flagged pages",
				    allocated_page_count_with_flags(
					    PLANE_PMM_ALLOC_ZERO),
				    2);
	first = find_mapping(kmem_page_vaddr(0));
	second = find_mapping(kmem_page_vaddr(1));
	failures += test_expect_not_null("readonly zero first mapping", first);
	failures += test_expect_not_null("readonly zero second mapping", second);
	if (first != NULL) {
		failures += test_expect_u32("readonly zero first flags",
					    first->flags, 0);
	}
	if (second != NULL) {
		failures += test_expect_u32("readonly zero second flags",
					    second->flags, 0);
	}
	return failures;
}

static int test_guard_zero_flag_reaches_user_pages(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("guard zero alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2,
					     PLANE_KMEM_ALLOC_GUARD |
					     PLANE_KMEM_ALLOC_ZERO,
					     &addr),
				     true);
	failures += test_expect_u64("guard zero pmm pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("guard zero flagged pages",
				    allocated_page_count_with_flags(
					    PLANE_PMM_ALLOC_ZERO),
				    2);
	failures += test_expect_u32("guard zero last flag",
				    last_pmm_flags, PLANE_PMM_ALLOC_ZERO);
	return failures;
}

static int test_readonly_guard_maps_only_user_pages(void)
{
	void *addr = NULL;
	struct test_mapping *first;
	struct test_mapping *second;
	int failures = 0;

	failures += test_expect_bool("readonly guard alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2,
					     PLANE_KMEM_ALLOC_READONLY |
					     PLANE_KMEM_ALLOC_GUARD,
					     &addr),
				     true);
	failures += test_expect_ptr("readonly guard user addr",
				    addr, (void *)kmem_page_vaddr(1));
	failures += test_expect_null("readonly guard left unmapped",
				     find_mapping(kmem_page_vaddr(0)));
	first = find_mapping(kmem_page_vaddr(1));
	second = find_mapping(kmem_page_vaddr(2));
	failures += test_expect_not_null("readonly guard first user", first);
	failures += test_expect_not_null("readonly guard second user", second);
	if (first != NULL) {
		failures += test_expect_u32("readonly guard first flags",
					    first->flags, 0);
	}
	if (second != NULL) {
		failures += test_expect_u32("readonly guard second flags",
					    second->flags, 0);
	}
	failures += test_expect_null("readonly guard right unmapped",
				     find_mapping(kmem_page_vaddr(3)));
	failures += test_expect_bool("readonly guard free",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, addr, 2), true);
	return failures;
}

static int test_byte_guard_zero_flag_reaches_user_pages(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("byte guard zero alloc",
				     plane_kmem_alloc_in_map(&test_map, &test_object, 1,
						      PLANE_KMEM_ALLOC_GUARD |
						      PLANE_KMEM_ALLOC_ZERO,
						      &addr),
				     true);
	failures += test_expect_u64("byte guard zero pmm pages",
				    allocated_page_count(), 1);
	failures += test_expect_u64("byte guard zero flagged pages",
				    allocated_page_count_with_flags(
					    PLANE_PMM_ALLOC_ZERO),
				    1);
	failures += test_expect_u64("byte guard zero mappings",
				    mapping_count(), 1);
	return failures;
}

static int test_pmm_failure_rolls_back_vaddr(void)
{
	void *addr = NULL;
	int failures = 0;

	pmm_force_fail = true;
	failures += test_expect_bool("pmm fail alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     false);
	failures += test_expect_u64("pmm fail allocated pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("pmm fail wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("pmm fail object pages",
				    object_page_count(), 0);
	failures += test_expect_u64("pmm fail object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("pmm fail object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("pmm fail mappings", mapping_count(), 0);
	pmm_force_fail = false;
	failures += test_expect_bool("pmm fail reuse alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     true);
	failures += test_expect_ptr("pmm fail reused addr",
				    addr, (void *)TEST_KMEM_BASE);
	return failures;
}

static int test_map_failure_rolls_back_pages(void)
{
	void *addr = NULL;
	int failures = 0;

	map_fail_after = 1;
	failures += test_expect_bool("map fail alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     false);
	failures += test_expect_u64("map fail allocated pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("map fail wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("map fail object pages",
				    object_page_count(), 0);
	failures += test_expect_u64("map fail object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("map fail object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("map fail mappings", mapping_count(), 0);
	map_fail_after = UINT64_MAX;
	failures += test_expect_bool("map fail reuse alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     true);
	failures += test_expect_ptr("map fail reused addr",
				    addr, (void *)TEST_KMEM_BASE);
	return failures;
}

static int test_guard_failures_roll_back_vaddr(void)
{
	void *addr = NULL;
	int failures = 0;

	pmm_force_fail = true;
	failures += test_expect_bool("guard pmm fail alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, PLANE_KMEM_ALLOC_GUARD, &addr),
				     false);
	failures += test_expect_u64("guard pmm fail pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("guard pmm fail wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("guard pmm fail object pages",
				    object_page_count(), 0);
	failures += test_expect_u64("guard pmm fail object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard pmm fail object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard pmm fail mappings",
				    mapping_count(), 0);
	pmm_force_fail = false;

	map_fail_after = 1;
	failures += test_expect_bool("guard map fail alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, PLANE_KMEM_ALLOC_GUARD, &addr),
				     false);
	failures += test_expect_u64("guard map fail pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("guard map fail wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("guard map fail object pages",
				    object_page_count(), 0);
	failures += test_expect_u64("guard map fail object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard map fail object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard map fail mappings",
				    mapping_count(), 0);
	map_fail_after = UINT64_MAX;

	failures += test_expect_bool("guard fail reuse alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, PLANE_KMEM_ALLOC_GUARD, &addr),
				     true);
	failures += test_expect_ptr("guard fail reused addr",
				    addr, (void *)kmem_page_vaddr(1));
	return failures;
}

static int test_rejects_invalid_inputs(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("alloc zero pages",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 0, 0, &addr),
				     false);
	failures += test_expect_bool("alloc unknown flag",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 1, BIT(8), &addr),
				     false);
	failures += test_expect_bool("alloc null out",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 1, 0, NULL),
				     false);
	failures += test_expect_bool("free null",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, NULL, 1), false);
	failures += test_expect_bool("free zero pages",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, (void *)TEST_KMEM_BASE,
							   0),
				     false);
	failures += test_expect_bool("free unaligned",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, (void *)(TEST_KMEM_BASE + 1), 1),
				     false);
	failures += test_expect_bool("byte alloc zero",
				     plane_kmem_alloc_in_map(&test_map, &test_object, 0, 0, &addr), false);
	failures += test_expect_bool("byte alloc unknown flag",
				     plane_kmem_alloc_in_map(&test_map, &test_object, 1, BIT(8), &addr), false);
	failures += test_expect_bool("byte alloc null out",
				     plane_kmem_alloc_in_map(&test_map, &test_object, 1, 0, NULL), false);
	failures += test_expect_bool("byte alloc size overflow",
				     plane_kmem_alloc_in_map(&test_map, &test_object, UINT64_MAX, 0, &addr),
				     false);
	failures += test_expect_bool("byte free null",
				     plane_kmem_free_in_map(&test_map, &test_object, NULL, 1), false);
	failures += test_expect_bool("byte free zero",
				     plane_kmem_free_in_map(&test_map, &test_object, (void *)TEST_KMEM_BASE, 0),
				     false);
	failures += test_expect_bool("byte free size overflow",
				     plane_kmem_free_in_map(&test_map, &test_object, (void *)TEST_KMEM_BASE,
						     UINT64_MAX),
				     false);

	failures += test_expect_bool("alloc valid",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     true);
	failures += test_expect_bool("partial free rejected",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, addr, 1), false);
	failures += test_expect_bool("exact free accepted",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, addr, 2), true);
	failures += test_expect_bool("double free rejected",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, addr, 2), false);
	return failures;
}

static int test_byte_free_size_mismatch_does_not_unmap(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("mismatch alloc",
				     plane_kmem_alloc_in_map(&test_map, &test_object, PAGE_SIZE + 1, 0, &addr),
				     true);
	failures += test_expect_bool("mismatch free",
				     plane_kmem_free_in_map(&test_map, &test_object, addr, 1), false);
	failures += test_expect_u64("mismatch pmm pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("mismatch mappings", mapping_count(), 2);
	failures += test_expect_bool("mismatch exact free",
				     plane_kmem_free_in_map(&test_map, &test_object, addr, PAGE_SIZE + 1),
				     true);
	failures += test_expect_u64("mismatch free pmm pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("mismatch free mappings", mapping_count(), 0);
	return failures;
}

static int test_rejects_exhausted_records(void)
{
	void *addr = NULL;
	int failures = 0;

	for (uint64_t i = 0; i < TEST_ALLOCATION_RECORDS; i++) {
		failures += test_expect_bool("record alloc",
					     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 1, 0, &addr),
					     true);
	}
	failures += test_expect_bool("record exhausted",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 1, 0, &addr),
				     false);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_alloc_and_free_pages),
		TEST_CASE(test_readonly_alloc_maps_without_write_flag),
		TEST_CASE(test_protect_pages_updates_mapping_flags),
		TEST_CASE(test_readonly_allocation_can_be_promoted_to_writable),
		TEST_CASE(test_protect_bytes_rounds_to_exact_allocation),
		TEST_CASE(test_guard_protect_updates_only_user_pages),
		TEST_CASE(test_protect_rejects_invalid_inputs),
		TEST_CASE(test_guard_alloc_and_free_pages),
		TEST_CASE(test_alloc_and_free_bytes),
		TEST_CASE(test_byte_guard_alloc_and_free),
		TEST_CASE(test_byte_alloc_rounds_up_to_pages),
		TEST_CASE(test_zero_flag_reaches_pmm),
		TEST_CASE(test_byte_zero_flag_reaches_all_pages),
		TEST_CASE(test_readonly_zero_maps_without_write_and_zeros_pages),
		TEST_CASE(test_guard_zero_flag_reaches_user_pages),
		TEST_CASE(test_readonly_guard_maps_only_user_pages),
		TEST_CASE(test_byte_guard_zero_flag_reaches_user_pages),
		TEST_CASE(test_pmm_failure_rolls_back_vaddr),
		TEST_CASE(test_map_failure_rolls_back_pages),
		TEST_CASE(test_guard_failures_roll_back_vaddr),
		TEST_CASE(test_rejects_invalid_inputs),
		TEST_CASE(test_byte_free_size_mismatch_does_not_unmap),
		TEST_CASE(test_rejects_exhausted_records),
	};

	return test_run_cases_with_fixture("kmem_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_kmem_test, NULL);
}
