#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <plane/kmem.h>
#include <plane/mm.h>
#include <plane/pmm.h>

#include "support/test.h"

#define TEST_KMEM_BASE 0xffff900000000000ull
#define TEST_KMEM_PAGES 256
#define TEST_PAGE_COUNT 256
#define TEST_MAP_COUNT 256
#define TEST_KMEM_SIZE (TEST_KMEM_PAGES * PAGE_SIZE)
#define TEST_ALLOCATION_RECORDS 128

struct plane_page {
	uint64_t phys_addr;
	bool allocated;
	uint32_t flags;
};

struct test_mapping {
	uint64_t vaddr;
	uint64_t phys_addr;
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
	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		test_pages[i].phys_addr = i * PAGE_SIZE;
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
	    !test_pages[page].allocated) {
		return false;
	}

	test_pages[page].allocated = false;
	test_pages[page].flags = 0;
	return true;
}

uint64_t plane_page_phys(const struct plane_page *page)
{
	if (page == NULL ||
	    page < &test_pages[0] ||
	    page >= &test_pages[TEST_PAGE_COUNT]) {
		return UINT64_MAX;
	}

	return page->phys_addr;
}

static int test_alloc_and_free_pages(void)
{
	void *addr = NULL;
	struct test_mapping *first;
	int failures = 0;

	failures += test_expect_bool("alloc init", plane_kmem_init(), true);
	failures += test_expect_bool("alloc pages",
				     plane_kmem_alloc_pages(2, 0, &addr),
				     true);
	failures += test_expect_ptr("alloc addr", addr, (void *)TEST_KMEM_BASE);
	failures += test_expect_u64("alloc pmm pages", allocated_page_count(), 2);
	failures += test_expect_u64("alloc mappings", mapping_count(), 2);

	first = find_mapping(TEST_KMEM_BASE);
	failures += test_expect_not_null("first mapping", first);
	if (first != NULL) {
		failures += test_expect_u64("first mapping phys",
					    first->phys_addr, 0);
	}

	failures += test_expect_bool("free pages",
				     plane_kmem_free_pages(addr, 2), true);
	failures += test_expect_u64("free pmm pages", allocated_page_count(), 0);
	failures += test_expect_u64("free mappings", mapping_count(), 0);
	return failures;
}

static int test_alloc_and_free_bytes(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("byte init", plane_kmem_init(), true);
	failures += test_expect_bool("byte alloc",
				     plane_kmem_alloc(1, 0, &addr), true);
	failures += test_expect_ptr("byte addr", addr, (void *)TEST_KMEM_BASE);
	failures += test_expect_u64("byte pmm pages", allocated_page_count(), 1);
	failures += test_expect_u64("byte mappings", mapping_count(), 1);
	failures += test_expect_bool("byte free", plane_kmem_free(addr, 1), true);
	failures += test_expect_u64("byte free pmm pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("byte free mappings", mapping_count(), 0);
	return failures;
}

static int test_byte_alloc_rounds_up_to_pages(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("round init", plane_kmem_init(), true);
	failures += test_expect_bool("round alloc",
				     plane_kmem_alloc(PAGE_SIZE + 1, 0, &addr),
				     true);
	failures += test_expect_u64("round pmm pages", allocated_page_count(), 2);
	failures += test_expect_u64("round mappings", mapping_count(), 2);
	failures += test_expect_bool("round free",
				     plane_kmem_free(addr, PAGE_SIZE + 1),
				     true);
	failures += test_expect_u64("round free pmm pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("round free mappings", mapping_count(), 0);
	return failures;
}

static int test_zero_flag_reaches_pmm(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("zero init", plane_kmem_init(), true);
	failures += test_expect_bool("zero alloc",
				     plane_kmem_alloc_pages(
					     1, PLANE_KMEM_ALLOC_ZERO, &addr),
				     true);
	failures += test_expect_u32("zero pmm flag",
				    last_pmm_flags, PLANE_PMM_ALLOC_ZERO);
	return failures;
}

static int test_byte_zero_flag_reaches_all_pages(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("byte zero init", plane_kmem_init(), true);
	failures += test_expect_bool("byte zero alloc",
				     plane_kmem_alloc(PAGE_SIZE + 1,
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

static int test_pmm_failure_rolls_back_vaddr(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("pmm fail init", plane_kmem_init(), true);
	pmm_force_fail = true;
	failures += test_expect_bool("pmm fail alloc",
				     plane_kmem_alloc_pages(2, 0, &addr),
				     false);
	failures += test_expect_u64("pmm fail allocated pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("pmm fail mappings", mapping_count(), 0);
	pmm_force_fail = false;
	failures += test_expect_bool("pmm fail reuse alloc",
				     plane_kmem_alloc_pages(2, 0, &addr),
				     true);
	failures += test_expect_ptr("pmm fail reused addr",
				    addr, (void *)TEST_KMEM_BASE);
	return failures;
}

static int test_map_failure_rolls_back_pages(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("map fail init", plane_kmem_init(), true);
	map_fail_after = 1;
	failures += test_expect_bool("map fail alloc",
				     plane_kmem_alloc_pages(2, 0, &addr),
				     false);
	failures += test_expect_u64("map fail allocated pages",
				    allocated_page_count(), 0);
	failures += test_expect_u64("map fail mappings", mapping_count(), 0);
	map_fail_after = UINT64_MAX;
	failures += test_expect_bool("map fail reuse alloc",
				     plane_kmem_alloc_pages(2, 0, &addr),
				     true);
	failures += test_expect_ptr("map fail reused addr",
				    addr, (void *)TEST_KMEM_BASE);
	return failures;
}

static int test_rejects_invalid_inputs(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("invalid init", plane_kmem_init(), true);
	failures += test_expect_bool("alloc zero pages",
				     plane_kmem_alloc_pages(0, 0, &addr),
				     false);
	failures += test_expect_bool("alloc unknown flag",
				     plane_kmem_alloc_pages(1, BIT(8), &addr),
				     false);
	failures += test_expect_bool("alloc null out",
				     plane_kmem_alloc_pages(1, 0, NULL),
				     false);
	failures += test_expect_bool("free null",
				     plane_kmem_free_pages(NULL, 1), false);
	failures += test_expect_bool("free zero pages",
				     plane_kmem_free_pages((void *)TEST_KMEM_BASE,
							   0),
				     false);
	failures += test_expect_bool("free unaligned",
				     plane_kmem_free_pages(
					     (void *)(TEST_KMEM_BASE + 1), 1),
				     false);
	failures += test_expect_bool("byte alloc zero",
				     plane_kmem_alloc(0, 0, &addr), false);
	failures += test_expect_bool("byte alloc unknown flag",
				     plane_kmem_alloc(1, BIT(8), &addr), false);
	failures += test_expect_bool("byte alloc null out",
				     plane_kmem_alloc(1, 0, NULL), false);
	failures += test_expect_bool("byte alloc size overflow",
				     plane_kmem_alloc(UINT64_MAX, 0, &addr),
				     false);
	failures += test_expect_bool("byte free null",
				     plane_kmem_free(NULL, 1), false);
	failures += test_expect_bool("byte free zero",
				     plane_kmem_free((void *)TEST_KMEM_BASE, 0),
				     false);
	failures += test_expect_bool("byte free size overflow",
				     plane_kmem_free((void *)TEST_KMEM_BASE,
						     UINT64_MAX),
				     false);

	failures += test_expect_bool("alloc valid",
				     plane_kmem_alloc_pages(2, 0, &addr),
				     true);
	failures += test_expect_bool("partial free rejected",
				     plane_kmem_free_pages(addr, 1), false);
	failures += test_expect_bool("exact free accepted",
				     plane_kmem_free_pages(addr, 2), true);
	failures += test_expect_bool("double free rejected",
				     plane_kmem_free_pages(addr, 2), false);
	return failures;
}

static int test_byte_free_size_mismatch_does_not_unmap(void)
{
	void *addr = NULL;
	int failures = 0;

	failures += test_expect_bool("mismatch init", plane_kmem_init(), true);
	failures += test_expect_bool("mismatch alloc",
				     plane_kmem_alloc(PAGE_SIZE + 1, 0, &addr),
				     true);
	failures += test_expect_bool("mismatch free",
				     plane_kmem_free(addr, 1), false);
	failures += test_expect_u64("mismatch pmm pages",
				    allocated_page_count(), 2);
	failures += test_expect_u64("mismatch mappings", mapping_count(), 2);
	failures += test_expect_bool("mismatch exact free",
				     plane_kmem_free(addr, PAGE_SIZE + 1),
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

	failures += test_expect_bool("records init", plane_kmem_init(), true);
	for (uint64_t i = 0; i < TEST_ALLOCATION_RECORDS; i++) {
		failures += test_expect_bool("record alloc",
					     plane_kmem_alloc_pages(1, 0, &addr),
					     true);
	}
	failures += test_expect_bool("record exhausted",
				     plane_kmem_alloc_pages(1, 0, &addr),
				     false);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_alloc_and_free_pages),
		TEST_CASE(test_alloc_and_free_bytes),
		TEST_CASE(test_byte_alloc_rounds_up_to_pages),
		TEST_CASE(test_zero_flag_reaches_pmm),
		TEST_CASE(test_byte_zero_flag_reaches_all_pages),
		TEST_CASE(test_pmm_failure_rolls_back_vaddr),
		TEST_CASE(test_map_failure_rolls_back_pages),
		TEST_CASE(test_rejects_invalid_inputs),
		TEST_CASE(test_byte_free_size_mismatch_does_not_unmap),
		TEST_CASE(test_rejects_exhausted_records),
	};

	return test_run_cases_with_fixture("kmem_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_kmem_test, NULL);
}
