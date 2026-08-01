#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <plane/kmem.h>
#include <plane/mm.h>
#include <plane/vm_map.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include "support/test.h"
#include "../kernel/mm/vm_page_internal.h"
#include "../kernel/mm/vm_zone_internal.h"

#define TEST_KMEM_BASE 0xffff900000000000ull
#define TEST_KMEM_PAGES 256
#define TEST_PAGE_COUNT 256
#define TEST_GUARD_PAGE_COUNT 64
#define TEST_MAP_COUNT 256
#define TEST_KMEM_SIZE (TEST_KMEM_PAGES * PAGE_SIZE)
#define TEST_KMEM_OBJECT_SIZE (TEST_KMEM_BASE + TEST_KMEM_SIZE)
#define TEST_ALLOCATION_RECORDS 128

static struct plane_vm_map_entry test_map_entries[TEST_ALLOCATION_RECORDS];
static struct plane_vm_map test_map;
static struct plane_vm_object test_object;

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
	uint32_t flags;
};

struct test_mapping {
	uint64_t vaddr;
	uint64_t phys_addr;
	uint32_t flags;
	bool used;
};

static struct plane_page test_pages[TEST_PAGE_COUNT];
static struct plane_page test_guard_pages[TEST_GUARD_PAGE_COUNT];
static struct test_mapping test_mappings[TEST_MAP_COUNT];
static uint64_t test_kmem_base;
static uint64_t test_kmem_size;
static uint64_t grab_attempts;
static uint64_t grab_fail_after;
static bool grab_force_fail;
static uint64_t map_attempts;
static uint64_t map_fail_after;
static uint32_t last_grab_flags;

static void cleanup_test_object(void)
{
	while (test_object.initialized && test_object.resident_head != NULL) {
		struct plane_page *page = test_object.resident_head;
		struct plane_page *removed;

		removed = plane_vm_object_remove_page(&test_object,
						      page->object_offset);
		if (removed == NULL) {
			break;
		}
	}
}

static bool is_test_page(const struct plane_page *page)
{
	return page != NULL &&
	       page >= &test_pages[0] &&
	       page < &test_pages[TEST_PAGE_COUNT];
}

static bool is_test_guard_page(const struct plane_page *page)
{
	return page != NULL &&
	       page >= &test_guard_pages[0] &&
	       page < &test_guard_pages[TEST_GUARD_PAGE_COUNT];
}

static bool is_test_active_guard_page(const struct plane_page *page)
{
	return is_test_guard_page(page) && page->guard;
}

static bool is_test_vm_page(const struct plane_page *page)
{
	return is_test_page(page) || is_test_active_guard_page(page);
}

static void reset_kmem_test(void)
{
	cleanup_test_object();
	test_map = (struct plane_vm_map){0};
	test_object = (struct plane_vm_object){0};
	for (uint64_t i = 0; i < TEST_ALLOCATION_RECORDS; i++) {
		test_map_entries[i] = (struct plane_vm_map_entry){0};
	}

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		test_pages[i].phys_addr = i * PAGE_SIZE;
		test_pages[i].wire_count = 0;
		test_pages[i].object = NULL;
		test_pages[i].object_offset = 0;
		test_pages[i].object_prev = NULL;
		test_pages[i].object_next = NULL;
		test_pages[i].object_hash_next = NULL;
		test_pages[i].object_tabled = false;
		test_pages[i].object_hashed = false;
		test_pages[i].allocated = false;
		test_pages[i].flags = 0;
	}
	for (uint64_t i = 0; i < TEST_GUARD_PAGE_COUNT; i++) {
		test_guard_pages[i] = (struct plane_page){0};
		test_guard_pages[i].phys_addr = PLANE_VM_PAGE_NO_PHYS_RAW;
	}

	for (uint64_t i = 0; i < TEST_MAP_COUNT; i++) {
		test_mappings[i] = (struct test_mapping){0};
	}

	test_kmem_base = TEST_KMEM_BASE;
	test_kmem_size = TEST_KMEM_SIZE;
	grab_attempts = 0;
	grab_fail_after = UINT64_MAX;
	grab_force_fail = false;
	map_attempts = 0;
	map_fail_after = UINT64_MAX;
	last_grab_flags = 0;

	if (!plane_vm_map_init(&test_map, test_map_entries,
			       TEST_ALLOCATION_RECORDS,
			       TEST_KMEM_BASE,
			       TEST_KMEM_SIZE)) {
		test_fail("failed to init kmem test map");
	}
	if (!plane_vm_object_init(&test_object, TEST_KMEM_OBJECT_SIZE)) {
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
	return plane_vm_object_resident_page_count(&test_object);
}

static uint64_t guard_page_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_GUARD_PAGE_COUNT; i++) {
		if (test_guard_pages[i].guard) {
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

bool hal_mmu_kernel_vma_range(plane_vaddr_t *base, uint64_t *size)
{
	if (base == NULL || size == NULL) {
		return false;
	}

	*base = plane_vaddr_make(test_kmem_base);
	*size = test_kmem_size;
	return true;
}

bool hal_mmu_map_kernel_page(plane_vaddr_t vaddr,
			     plane_paddr_t phys_addr,
			     uint32_t flags)
{
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);

	if ((flags & ~HAL_MMU_MAP_WRITE) != 0 ||
	    find_mapping(raw_vaddr) != NULL ||
	    map_attempts++ >= map_fail_after) {
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
	struct test_mapping *mapping;

	if ((flags & ~HAL_MMU_MAP_WRITE) != 0) {
		return false;
	}

	mapping = find_mapping(plane_vaddr_raw(vaddr));
	if (mapping == NULL) {
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

	if (grab_force_fail || grab_attempts++ >= grab_fail_after) {
		return false;
	}

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (!test_pages[i].allocated) {
			test_pages[i].allocated = true;
			test_pages[i].flags = flags;
			last_grab_flags = flags;
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
	page->flags = 0;
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
	if (storage == NULL || count == 0 || segment == NULL) {
		return false;
	}

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
	failures += test_expect_u64("alloc backing pages", allocated_page_count(), 2);
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
	failures += test_expect_u64("alloc object ref count",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_u64("alloc mappings", mapping_count(), 2);
	failures += test_expect_bool("alloc lookup",
				     plane_vm_map_lookup_allocation(&test_map,
					     TEST_KMEM_BASE, 2, &info),
				     true);
	failures += test_expect_u64("alloc map wired count",
				    info.wired_count, 1);
	failures += test_expect_ptr("alloc map explicit object",
				    info.object, &test_object);
	failures += test_expect_bool("alloc map object caller owned",
				     info.object->allocated, false);
	failures += test_expect_u64("alloc map object offset",
				    info.object_offset, TEST_KMEM_BASE);
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
	failures += test_expect_u64("free backing pages", allocated_page_count(), 0);
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
	failures += test_expect_u64("free object ref count",
				    plane_vm_object_ref_count(&test_object), 1);
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
	failures += test_expect_u64("readonly backing pages",
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
	failures += test_expect_u64("guard backing pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("guard wired pages", wired_page_count(), 2);
	failures += test_expect_u64("guard object pages", object_page_count(), 2);
	failures += test_expect_u64("guard active pages", guard_page_count(), 0);
	failures += test_expect_u64("guard object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("guard object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    2);
	failures += test_expect_u64("guard object ref count",
				    plane_vm_object_ref_count(&test_object), 2);
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
	failures += test_expect_null("guard object right absent",
				     plane_vm_object_lookup_page(&test_object,
								 kmem_page_vaddr(3)));
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
	failures += test_expect_u64("guard free backing pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("guard free wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("guard free object pages",
				    object_page_count(), 0);
	failures += test_expect_u64("guard free active pages",
				    guard_page_count(), 0);
	failures += test_expect_u64("guard free object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard free object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard free object ref count",
				    plane_vm_object_ref_count(&test_object), 1);
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
	failures += test_expect_u64("byte backing pages", allocated_page_count(), 1);
	failures += test_expect_u64("byte wired pages", wired_page_count(), 1);
	failures += test_expect_u64("byte mappings", mapping_count(), 1);
	failures += test_expect_bool("byte free", plane_kmem_free_in_map(&test_map, &test_object, addr, 1), true);
	failures += test_expect_u64("byte free backing pages",
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
	failures += test_expect_u64("byte guard backing pages",
				    allocated_page_count(), 1);
	failures += test_expect_u64("byte guard wired pages",
				    wired_page_count(), 1);
	failures += test_expect_u64("byte guard object pages",
				    object_page_count(), 1);
	failures += test_expect_u64("byte guard active pages",
				    guard_page_count(), 0);
	failures += test_expect_u64("byte guard object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    1);
	failures += test_expect_u64("byte guard object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    1);
	failures += test_expect_u64("byte guard mappings", mapping_count(), 1);
	failures += test_expect_null("byte guard left unmapped",
				     find_mapping(kmem_page_vaddr(0)));
	failures += test_expect_not_null("byte guard user mapped",
					 find_mapping(kmem_page_vaddr(1)));
	failures += test_expect_null("byte guard right unmapped",
				     find_mapping(kmem_page_vaddr(2)));
	failures += test_expect_bool("byte guard free",
				     plane_kmem_free_in_map(&test_map, &test_object, addr, 1), true);
	failures += test_expect_u64("byte guard free backing pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("byte guard free wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("byte guard free object pages",
				    object_page_count(), 0);
	failures += test_expect_u64("byte guard free active pages",
				    guard_page_count(), 0);
	failures += test_expect_u64("byte guard free object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("byte guard free object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
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
	failures += test_expect_u64("round backing pages", allocated_page_count(), 2);
	failures += test_expect_u64("round wired pages", wired_page_count(), 2);
	failures += test_expect_u64("round mappings", mapping_count(), 2);
	failures += test_expect_bool("round free",
				     plane_kmem_free_in_map(&test_map, &test_object, addr, PAGE_SIZE + 1),
				     true);
	failures += test_expect_u64("round free backing pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("round free wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("round free mappings", mapping_count(), 0);
	return failures;
}

static int test_zero_flag_reaches_vm_page_grab(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("zero alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 1, PLANE_KMEM_ALLOC_ZERO, &addr),
				     true);
	failures += test_expect_u32("zero grab flag",
				    last_grab_flags, PLANE_VM_PAGE_GRAB_ZERO);
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
	failures += test_expect_u64("byte zero backing pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("byte zero flagged pages",
				    allocated_page_count_with_flags(
					    PLANE_VM_PAGE_GRAB_ZERO),
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
	failures += test_expect_u64("readonly zero backing pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("readonly zero flagged pages",
				    allocated_page_count_with_flags(
					    PLANE_VM_PAGE_GRAB_ZERO),
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
	failures += test_expect_u64("guard zero backing pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("guard zero flagged pages",
				    allocated_page_count_with_flags(
					    PLANE_VM_PAGE_GRAB_ZERO),
				    2);
	failures += test_expect_u32("guard zero last flag",
				    last_grab_flags, PLANE_VM_PAGE_GRAB_ZERO);
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
	failures += test_expect_u64("byte guard zero backing pages",
				    allocated_page_count(), 1);
	failures += test_expect_u64("byte guard zero flagged pages",
				    allocated_page_count_with_flags(
					    PLANE_VM_PAGE_GRAB_ZERO),
				    1);
	failures += test_expect_u64("byte guard zero mappings",
				    mapping_count(), 1);
	return failures;
}

static int test_grab_failure_rolls_back_vaddr(void)
{
	void *addr = NULL;
	int failures = 0;

	grab_force_fail = true;
	failures += test_expect_bool("grab fail alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     false);
	failures += test_expect_u64("grab fail backing pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("grab fail wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("grab fail object pages",
				    object_page_count(), 0);
	failures += test_expect_u64("grab fail object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("grab fail object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("grab fail object ref count",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_u64("grab fail mappings", mapping_count(), 0);
	grab_force_fail = false;
	failures += test_expect_bool("grab fail reuse alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, 0, &addr),
				     true);
	failures += test_expect_ptr("grab fail reused addr",
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
	failures += test_expect_u64("map fail object ref count",
				    plane_vm_object_ref_count(&test_object), 1);
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

	grab_force_fail = true;
	failures += test_expect_bool("guard grab fail alloc",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 2, PLANE_KMEM_ALLOC_GUARD, &addr),
				     false);
	failures += test_expect_u64("guard grab fail backing pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("guard grab fail wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("guard grab fail object pages",
				    object_page_count(), 0);
	failures += test_expect_u64("guard grab fail object resident count",
				    plane_vm_object_resident_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard grab fail object wired count",
				    plane_vm_object_wired_page_count(
					    &test_object),
				    0);
	failures += test_expect_u64("guard grab fail object ref count",
				    plane_vm_object_ref_count(&test_object), 1);
	failures += test_expect_u64("guard grab fail mappings",
				    mapping_count(), 0);
	grab_force_fail = false;

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
	failures += test_expect_u64("guard map fail object ref count",
				    plane_vm_object_ref_count(&test_object), 1);
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

static int test_alloc_rejects_object_too_small_for_auto_offset(void)
{
	struct plane_vm_object small_object = {0};
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("small object init",
				     plane_vm_object_init(&small_object,
							  PAGE_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("small object alloc rejected",
				     plane_kmem_alloc_pages_in_map(
					     &test_map, &small_object, 1, 0,
					     &addr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("small object ref unchanged",
				    plane_vm_object_ref_count(&small_object), 1);
	failures += test_expect_u64("small object backing pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("small object wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("small object mappings",
				    mapping_count(), 0);
	failures += test_expect_u64("small object free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("small object allocations unchanged",
				    after.allocation_count,
				    before.allocation_count);
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
	failures += test_expect_u64("partial free ref unchanged",
				    plane_vm_object_ref_count(&test_object), 2);
	failures += test_expect_bool("exact free accepted",
				     plane_kmem_free_pages_in_map(&test_map, &test_object, addr, 2), true);
	failures += test_expect_u64("exact free ref restored",
				    plane_vm_object_ref_count(&test_object), 1);
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
	failures += test_expect_u64("mismatch backing pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("mismatch mappings", mapping_count(), 2);
	failures += test_expect_bool("mismatch exact free",
				     plane_kmem_free_in_map(&test_map, &test_object, addr, PAGE_SIZE + 1),
				     true);
	failures += test_expect_u64("mismatch free backing pages",
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
	failures += test_expect_u64("record ref count full",
				    plane_vm_object_ref_count(&test_object),
				    TEST_ALLOCATION_RECORDS + 1);
	failures += test_expect_bool("record exhausted",
				     plane_kmem_alloc_pages_in_map(&test_map, &test_object, 1, 0, &addr),
				     false);
	failures += test_expect_u64("record exhausted ref unchanged",
				    plane_vm_object_ref_count(&test_object),
				    TEST_ALLOCATION_RECORDS + 1);
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
		TEST_CASE(test_zero_flag_reaches_vm_page_grab),
		TEST_CASE(test_byte_zero_flag_reaches_all_pages),
		TEST_CASE(test_readonly_zero_maps_without_write_and_zeros_pages),
		TEST_CASE(test_guard_zero_flag_reaches_user_pages),
		TEST_CASE(test_readonly_guard_maps_only_user_pages),
		TEST_CASE(test_byte_guard_zero_flag_reaches_user_pages),
		TEST_CASE(test_grab_failure_rolls_back_vaddr),
		TEST_CASE(test_map_failure_rolls_back_pages),
		TEST_CASE(test_guard_failures_roll_back_vaddr),
		TEST_CASE(test_alloc_rejects_object_too_small_for_auto_offset),
		TEST_CASE(test_rejects_invalid_inputs),
		TEST_CASE(test_byte_free_size_mismatch_does_not_unmap),
		TEST_CASE(test_rejects_exhausted_records),
	};

	return test_run_cases_with_fixture("kmem_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_kmem_test, NULL);
}
