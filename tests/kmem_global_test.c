#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <plane/kmem.h>
#include <plane/mm.h>
#include <plane/vm_page.h>
#include <plane/vm_object.h>

#include "support/test.h"
#include "../kernel/mm/vm_page_internal.h"

#define TEST_KMEM_BASE 0xffff900000000000ull
#define TEST_KMEM_PAGES 16
#define TEST_KMEM_SIZE (TEST_KMEM_PAGES * PAGE_SIZE)
#define TEST_PAGE_COUNT 16
#define TEST_GUARD_PAGE_COUNT 8
#define TEST_MAP_COUNT 16

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
	bool used;
};

static struct plane_page test_pages[TEST_PAGE_COUNT];
static struct plane_page test_guard_pages[TEST_GUARD_PAGE_COUNT];
static struct test_mapping test_mappings[TEST_MAP_COUNT];

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

	*base = TEST_KMEM_BASE;
	*size = TEST_KMEM_SIZE;
	return true;
}

bool hal_mmu_map_kernel_page(uint64_t vaddr, uint64_t phys_addr, uint32_t flags)
{
	if ((flags & ~HAL_MMU_MAP_WRITE) != 0 || find_mapping(vaddr) != NULL) {
		return false;
	}

	for (uint64_t i = 0; i < TEST_MAP_COUNT; i++) {
		if (!test_mappings[i].used) {
			test_mappings[i].vaddr = vaddr;
			test_mappings[i].phys_addr = phys_addr;
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
	return (flags & ~HAL_MMU_MAP_WRITE) == 0 && find_mapping(vaddr) != NULL;
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
	if (is_test_guard_page(page)) {
		return page->guard ? PLANE_VM_PAGE_GUARD_PHYS :
				     PLANE_VM_PAGE_NO_PHYS;
	}
	if (!is_test_page(page)) {
		return PLANE_VM_PAGE_NO_PHYS;
	}

	return page->phys_addr;
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

struct plane_page *plane_vm_page_create_guard(void)
{
	for (uint64_t i = 0; i < TEST_GUARD_PAGE_COUNT; i++) {
		if (!test_guard_pages[i].guard) {
			test_guard_pages[i] = (struct plane_page){0};
			test_guard_pages[i].phys_addr = PLANE_VM_PAGE_GUARD_PHYS;
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
	page->phys_addr = PLANE_VM_PAGE_NO_PHYS;
	return true;
}

static int test_global_kmem_init_is_one_shot(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("global init", plane_kmem_init(), true);
	failures += test_expect_bool("global alloc",
				     plane_kmem_alloc_pages(2, 0, &addr),
				     true);
	failures += test_expect_u64("global wired pages",
				    wired_page_count(), 2);
	failures += test_expect_bool("global repeat init",
				     plane_kmem_init(), false);
	failures += test_expect_bool("global preserved free",
				     plane_kmem_free_pages(addr, 2), true);
	failures += test_expect_u64("global free backing pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("global free wired pages",
				    wired_page_count(), 0);
	failures += test_expect_u64("global free mappings",
				    mapping_count(), 0);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_global_kmem_init_is_one_shot),
	};

	return test_run_cases("kmem_global_test", cases, TEST_ARRAY_SIZE(cases));
}
