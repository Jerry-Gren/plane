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
#define TEST_LAST_ALLOCATION (TEST_ALLOCATION_RECORDS - 1)
#define TEST_LAST_FRAGMENTED_ALLOCATION (TEST_ALLOCATION_RECORDS - 4)

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

static int check_stats(const char *name,
		       uint64_t free_pages,
		       uint64_t allocated_pages,
		       uint64_t free_ranges,
		       uint64_t allocations)
{
	struct plane_kmem_stats stats = plane_kmem_get_stats();
	int failures = 0;

	failures += test_expect_u64(name, stats.total_pages, TEST_KMEM_PAGES);
	failures += test_expect_u64("kmem free pages",
				    stats.free_pages, free_pages);
	failures += test_expect_u64("kmem allocated pages",
				    stats.allocated_pages, allocated_pages);
	failures += test_expect_u64("kmem free ranges",
				    stats.free_range_count, free_ranges);
	failures += test_expect_u64("kmem allocations",
				    stats.allocation_count, allocations);
	return failures;
}

static int test_init_stats(void)
{
	int failures = 0;

	failures += test_expect_bool("kmem init", plane_kmem_init(), true);
	failures += check_stats("kmem total pages", TEST_KMEM_PAGES, 0, 1, 0);
	return failures;
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
	failures += check_stats("alloc stats", TEST_KMEM_PAGES - 2, 2, 1, 1);

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
	failures += check_stats("free stats", TEST_KMEM_PAGES, 0, 1, 0);
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
	failures += check_stats("pmm fail stats", TEST_KMEM_PAGES, 0, 1, 0);
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
	failures += check_stats("map fail stats", TEST_KMEM_PAGES, 0, 1, 0);
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

static int test_rejects_exhausted_vaddr_space(void)
{
	void *addr = NULL;
	struct plane_kmem_stats stats;
	int failures = 0;

	test_kmem_size = PAGE_SIZE;
	failures += test_expect_bool("space init", plane_kmem_init(), true);
	failures += test_expect_bool("space alloc too large",
				     plane_kmem_alloc_pages(2, 0, &addr),
				     false);
	stats = plane_kmem_get_stats();
	failures += test_expect_u64("space total", stats.total_pages, 1);
	failures += test_expect_u64("space free", stats.free_pages, 1);
	failures += test_expect_u64("space allocated", stats.allocated_pages, 0);
	failures += test_expect_u64("space ranges", stats.free_range_count, 1);
	failures += test_expect_u64("space allocations",
				    stats.allocation_count, 0);
	return failures;
}

static int test_rejects_wrapping_vaddr_space(void)
{
	struct plane_kmem_stats stats;
	int failures = 0;

	test_kmem_base = UINT64_MAX - PAGE_SIZE + 1;
	test_kmem_size = 2 * PAGE_SIZE;
	failures += test_expect_bool("wrap init", plane_kmem_init(), false);

	stats = plane_kmem_get_stats();
	failures += test_expect_u64("wrap total", stats.total_pages, 0);
	failures += test_expect_u64("wrap free", stats.free_pages, 0);
	failures += test_expect_u64("wrap allocated", stats.allocated_pages, 0);
	failures += test_expect_u64("wrap ranges", stats.free_range_count, 0);
	failures += test_expect_u64("wrap allocations",
				    stats.allocation_count, 0);
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
	failures += check_stats("record stats",
				TEST_KMEM_PAGES - TEST_ALLOCATION_RECORDS,
				TEST_ALLOCATION_RECORDS, 1,
				TEST_ALLOCATION_RECORDS);
	return failures;
}

static int test_free_merges_when_free_range_list_is_full(void)
{
	void *addrs[TEST_ALLOCATION_RECORDS];
	int failures = 0;

	failures += test_expect_bool("merge full init", plane_kmem_init(), true);
	for (uint64_t i = 0; i < TEST_ALLOCATION_RECORDS; i++) {
		failures += test_expect_bool("merge full alloc",
					     plane_kmem_alloc_pages(1, 0,
								   &addrs[i]),
					     true);
	}

	for (uint64_t i = 0; i <= TEST_LAST_FRAGMENTED_ALLOCATION; i += 2) {
		failures += test_expect_bool("merge full fragment",
					     plane_kmem_free_pages(addrs[i], 1),
					     true);
	}
	failures += check_stats("merge full fragmented",
				TEST_KMEM_PAGES - 65, 65, 64, 65);

	failures += test_expect_bool("merge full next merge",
				     plane_kmem_free_pages(
					     addrs[TEST_LAST_ALLOCATION], 1),
				     true);
	failures += check_stats("merge full merged",
				TEST_KMEM_PAGES - 64, 64, 64, 64);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_init_stats),
		TEST_CASE(test_alloc_and_free_pages),
		TEST_CASE(test_zero_flag_reaches_pmm),
		TEST_CASE(test_pmm_failure_rolls_back_vaddr),
		TEST_CASE(test_map_failure_rolls_back_pages),
		TEST_CASE(test_rejects_invalid_inputs),
		TEST_CASE(test_rejects_exhausted_vaddr_space),
		TEST_CASE(test_rejects_wrapping_vaddr_space),
		TEST_CASE(test_rejects_exhausted_records),
		TEST_CASE(test_free_merges_when_free_range_list_is_full),
	};

	return test_run_cases_with_fixture("kmem_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_kmem_test, NULL);
}
