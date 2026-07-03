#include <stdint.h>

#include <plane/mm.h>
#include <plane/vm_map.h>

#include "support/test.h"

#define TEST_KERNEL_MAP_BASE 0xffff900000000000ull
#define TEST_KERNEL_MAP_PAGES 256
#define TEST_KERNEL_MAP_SIZE (TEST_KERNEL_MAP_PAGES * PAGE_SIZE)
#define TEST_ALLOCATION_RECORDS 128
#define TEST_LAST_ALLOCATION (TEST_ALLOCATION_RECORDS - 1)
#define TEST_LAST_FRAGMENTED_ALLOCATION (TEST_ALLOCATION_RECORDS - 4)
#define TEST_UNMERGEABLE_ALLOCATION (TEST_ALLOCATION_RECORDS - 2)

static int check_stats(const char *name,
		       uint64_t free_pages,
		       uint64_t allocated_pages,
		       uint64_t free_ranges,
		       uint64_t allocations)
{
	struct plane_vm_map_stats stats = plane_kernel_map_get_stats();
	int failures = 0;

	failures += test_expect_u64(name, stats.total_pages,
				    TEST_KERNEL_MAP_PAGES);
	failures += test_expect_u64("kernel map free pages",
				    stats.free_pages, free_pages);
	failures += test_expect_u64("kernel map allocated pages",
				    stats.allocated_pages, allocated_pages);
	failures += test_expect_u64("kernel map free ranges",
				    stats.free_range_count, free_ranges);
	failures += test_expect_u64("kernel map allocations",
				    stats.allocation_count, allocations);
	return failures;
}

static int test_init_stats(void)
{
	int failures = 0;

	failures += test_expect_bool("map init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += check_stats("kernel map total pages",
				TEST_KERNEL_MAP_PAGES, 0, 1, 0);
	return failures;
}

static int test_rejects_invalid_init(void)
{
	struct plane_vm_map_stats stats;
	int failures = 0;

	failures += test_expect_bool("zero size init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   0),
				     false);
	failures += test_expect_bool("unaligned base init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE + 1,
							   TEST_KERNEL_MAP_SIZE),
				     false);
	failures += test_expect_bool("unaligned size init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE - 1),
				     false);
	failures += test_expect_bool("wrapping init",
				     plane_kernel_map_init(UINT64_MAX - PAGE_SIZE + 1,
							   2 * PAGE_SIZE),
				     false);

	stats = plane_kernel_map_get_stats();
	failures += test_expect_u64("invalid init total", stats.total_pages, 0);
	failures += test_expect_u64("invalid init free", stats.free_pages, 0);
	failures += test_expect_u64("invalid init allocated",
				    stats.allocated_pages, 0);
	failures += test_expect_u64("invalid init ranges",
				    stats.free_range_count, 0);
	failures += test_expect_u64("invalid init allocations",
				    stats.allocation_count, 0);
	return failures;
}

static int test_alloc_and_free_pages(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("alloc init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("alloc pages",
				     plane_kernel_map_alloc_pages(2, &vaddr),
				     true);
	failures += test_expect_u64("alloc vaddr", vaddr,
				    TEST_KERNEL_MAP_BASE);
	failures += test_expect_bool("has allocation",
				     plane_kernel_map_has_allocation(vaddr, 2),
				     true);
	failures += check_stats("alloc stats", TEST_KERNEL_MAP_PAGES - 2,
				2, 1, 1);

	failures += test_expect_bool("free pages",
				     plane_kernel_map_free_pages(vaddr, 2),
				     true);
	failures += check_stats("free stats", TEST_KERNEL_MAP_PAGES, 0, 1, 0);
	return failures;
}

static int test_rejects_invalid_alloc_and_free(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("invalid init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("alloc zero pages",
				     plane_kernel_map_alloc_pages(0, &vaddr),
				     false);
	failures += test_expect_bool("alloc null out",
				     plane_kernel_map_alloc_pages(1, NULL),
				     false);
	failures += test_expect_bool("free zero pages",
				     plane_kernel_map_free_pages(TEST_KERNEL_MAP_BASE,
								 0),
				     false);
	failures += test_expect_bool("free unaligned",
				     plane_kernel_map_free_pages(TEST_KERNEL_MAP_BASE + 1,
								 1),
				     false);

	failures += test_expect_bool("alloc valid",
				     plane_kernel_map_alloc_pages(2, &vaddr),
				     true);
	failures += test_expect_bool("partial free rejected",
				     plane_kernel_map_free_pages(vaddr, 1),
				     false);
	failures += test_expect_bool("partial allocation absent",
				     plane_kernel_map_has_allocation(vaddr, 1),
				     false);
	failures += test_expect_bool("exact free accepted",
				     plane_kernel_map_free_pages(vaddr, 2),
				     true);
	failures += test_expect_bool("double free rejected",
				     plane_kernel_map_free_pages(vaddr, 2),
				     false);
	return failures;
}

static int test_rejects_exhausted_vaddr_space(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats stats;
	int failures = 0;

	failures += test_expect_bool("space init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   PAGE_SIZE),
				     true);
	failures += test_expect_bool("space alloc too large",
				     plane_kernel_map_alloc_pages(2, &vaddr),
				     false);
	stats = plane_kernel_map_get_stats();
	failures += test_expect_u64("space total", stats.total_pages, 1);
	failures += test_expect_u64("space free", stats.free_pages, 1);
	failures += test_expect_u64("space allocated", stats.allocated_pages, 0);
	failures += test_expect_u64("space ranges", stats.free_range_count, 1);
	failures += test_expect_u64("space allocations",
				    stats.allocation_count, 0);
	return failures;
}

static int test_rejects_exhausted_records(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("records init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_ALLOCATION_RECORDS; i++) {
		failures += test_expect_bool("record alloc",
					     plane_kernel_map_alloc_pages(1,
									  &vaddr),
					     true);
	}
	failures += test_expect_bool("record exhausted",
				     plane_kernel_map_alloc_pages(1, &vaddr),
				     false);
	failures += check_stats("record stats",
				TEST_KERNEL_MAP_PAGES - TEST_ALLOCATION_RECORDS,
				TEST_ALLOCATION_RECORDS, 1,
				TEST_ALLOCATION_RECORDS);
	return failures;
}

static int test_free_merges_when_free_range_list_is_full(void)
{
	uint64_t addrs[TEST_ALLOCATION_RECORDS];
	int failures = 0;

	failures += test_expect_bool("merge full init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_ALLOCATION_RECORDS; i++) {
		failures += test_expect_bool("merge full alloc",
					     plane_kernel_map_alloc_pages(1,
									  &addrs[i]),
					     true);
	}

	for (uint64_t i = 0; i <= TEST_LAST_FRAGMENTED_ALLOCATION; i += 2) {
		failures += test_expect_bool("merge full fragment",
					     plane_kernel_map_free_pages(addrs[i],
									 1),
					     true);
	}
	failures += check_stats("merge full fragmented",
				TEST_KERNEL_MAP_PAGES - 65, 65, 64, 65);

	failures += test_expect_bool("merge full next merge",
				     plane_kernel_map_free_pages(
					     addrs[TEST_LAST_ALLOCATION], 1),
				     true);
	failures += check_stats("merge full merged",
				TEST_KERNEL_MAP_PAGES - 64, 64, 64, 64);
	return failures;
}

static int test_failed_free_keeps_allocation_record(void)
{
	uint64_t addrs[TEST_ALLOCATION_RECORDS];
	int failures = 0;

	failures += test_expect_bool("failed free init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_ALLOCATION_RECORDS; i++) {
		failures += test_expect_bool("failed free alloc",
					     plane_kernel_map_alloc_pages(1,
									  &addrs[i]),
					     true);
	}

	for (uint64_t i = 0; i <= TEST_LAST_FRAGMENTED_ALLOCATION; i += 2) {
		failures += test_expect_bool("failed free fragment",
					     plane_kernel_map_free_pages(addrs[i],
									 1),
					     true);
	}
	failures += check_stats("failed free fragmented",
				TEST_KERNEL_MAP_PAGES - 65, 65, 64, 65);

	failures += test_expect_bool("failed free non-merge",
				     plane_kernel_map_free_pages(
					     addrs[TEST_UNMERGEABLE_ALLOCATION], 1),
				     false);
	failures += test_expect_bool("failed free record remains",
				     plane_kernel_map_has_allocation(
					     addrs[TEST_UNMERGEABLE_ALLOCATION], 1),
				     true);
	failures += check_stats("failed free stats unchanged",
				TEST_KERNEL_MAP_PAGES - 65, 65, 64, 65);
	failures += test_expect_bool("failed free later merge",
				     plane_kernel_map_free_pages(
					     addrs[TEST_LAST_ALLOCATION], 1),
				     true);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_init_stats),
		TEST_CASE(test_rejects_invalid_init),
		TEST_CASE(test_alloc_and_free_pages),
		TEST_CASE(test_rejects_invalid_alloc_and_free),
		TEST_CASE(test_rejects_exhausted_vaddr_space),
		TEST_CASE(test_rejects_exhausted_records),
		TEST_CASE(test_free_merges_when_free_range_list_is_full),
		TEST_CASE(test_failed_free_keeps_allocation_record),
	};

	return test_run_cases("vm_map_test", cases, TEST_ARRAY_SIZE(cases));
}
