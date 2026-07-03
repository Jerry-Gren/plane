#include <stdint.h>

#include <plane/bits.h>
#include <plane/mm.h>
#include <plane/vm_map.h>

#include "support/test.h"

#define TEST_KERNEL_MAP_BASE 0xffff900000000000ull
#define TEST_KERNEL_MAP_PAGES 256
#define TEST_KERNEL_MAP_SIZE (TEST_KERNEL_MAP_PAGES * PAGE_SIZE)
#define TEST_MAP_ENTRIES 128

static bool test_reset_enabled = true;

bool plane_vm_map_test_reset_enabled(void)
{
	return test_reset_enabled;
}

static uint64_t page_vaddr(uint64_t page)
{
	return TEST_KERNEL_MAP_BASE + page * PAGE_SIZE;
}

static int check_stats(const char *name,
		       uint64_t free_pages,
		       uint64_t reserved_pages,
		       uint64_t user_pages,
		       uint64_t free_range_total,
		       uint64_t allocations)
{
	struct plane_vm_map_stats stats = plane_kernel_map_get_stats();
	int failures = 0;

	failures += test_expect_u64(name, stats.total_pages,
				    TEST_KERNEL_MAP_PAGES);
	failures += test_expect_u64("kernel map free pages",
				    stats.free_pages, free_pages);
	failures += test_expect_u64("kernel map reserved pages",
				    stats.reserved_pages, reserved_pages);
	failures += test_expect_u64("kernel map user pages",
				    stats.user_pages, user_pages);
	failures += test_expect_u64("kernel map free ranges",
				    stats.free_range_count, free_range_total);
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
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
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
	failures += test_expect_u64("invalid init reserved",
				    stats.reserved_pages, 0);
	failures += test_expect_u64("invalid init user", stats.user_pages, 0);
	failures += test_expect_u64("invalid init ranges",
				    stats.free_range_count, 0);
	failures += test_expect_u64("invalid init allocations",
				    stats.allocation_count, 0);
	return failures;
}

static int test_init_is_one_shot_in_production_mode(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	test_reset_enabled = true;
	failures += test_expect_bool("oneshot init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("oneshot alloc",
				     plane_kernel_map_alloc_pages(2, &vaddr),
				     true);

	test_reset_enabled = false;
	failures += test_expect_bool("oneshot reject valid reinit",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     false);
	failures += test_expect_bool("oneshot reject invalid reinit",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   0),
				     false);
	failures += test_expect_bool("oneshot allocation preserved",
				     plane_kernel_map_has_allocation(vaddr, 2),
				     true);
	failures += check_stats("oneshot stats preserved",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 1, 1);

	test_reset_enabled = true;
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
				2, 2, 1, 1);

	failures += test_expect_bool("free pages",
				     plane_kernel_map_free_pages(vaddr, 2),
				     true);
	failures += check_stats("free stats", TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
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
	failures += test_expect_u64("space reserved", stats.reserved_pages, 0);
	failures += test_expect_u64("space user", stats.user_pages, 0);
	failures += test_expect_u64("space ranges", stats.free_range_count, 1);
	failures += test_expect_u64("space allocations",
				    stats.allocation_count, 0);
	return failures;
}

static int test_rejects_exhausted_entries(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	int failures = 0;

	failures += test_expect_bool("entries init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_MAP_ENTRIES; i++) {
		failures += test_expect_bool("entry alloc",
					     plane_kernel_map_alloc_pages(1,
									  &vaddr),
					     true);
	}

	before = plane_kernel_map_get_stats();
	failures += test_expect_bool("entry exhausted",
				     plane_kernel_map_alloc_pages(1, &vaddr),
				     false);
	after = plane_kernel_map_get_stats();
	failures += test_expect_u64("entry exhausted free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("entry exhausted reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("entry exhausted user unchanged",
				    after.user_pages, before.user_pages);
	failures += test_expect_u64("entry exhausted range unchanged",
				    after.free_range_count, before.free_range_count);
	failures += test_expect_u64("entry exhausted count unchanged",
				    after.allocation_count, before.allocation_count);
	return failures;
}

static int test_first_fit_reuses_lowest_hole(void)
{
	uint64_t first = 0;
	uint64_t second = 0;
	uint64_t reused = 0;
	int failures = 0;

	failures += test_expect_bool("fit init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("fit alloc first",
				     plane_kernel_map_alloc_pages(2, &first),
				     true);
	failures += test_expect_bool("fit alloc second",
				     plane_kernel_map_alloc_pages(2, &second),
				     true);
	failures += test_expect_u64("fit second address", second,
				    page_vaddr(2));
	failures += test_expect_bool("fit free first",
				     plane_kernel_map_free_pages(first, 2),
				     true);
	failures += check_stats("fit hole stats", TEST_KERNEL_MAP_PAGES - 2,
				2, 2, 2, 1);
	failures += test_expect_bool("fit reuse hole",
				     plane_kernel_map_alloc_pages(1, &reused),
				     true);
	failures += test_expect_u64("fit reused lowest hole", reused, first);
	failures += check_stats("fit reused stats", TEST_KERNEL_MAP_PAGES - 3,
				3, 3, 2, 2);
	return failures;
}

static int test_holes_merge_after_entry_removal(void)
{
	uint64_t addrs[5];
	int failures = 0;

	failures += test_expect_bool("merge init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(addrs); i++) {
		failures += test_expect_bool("merge alloc",
					     plane_kernel_map_alloc_pages(1,
									  &addrs[i]),
					     true);
	}

	failures += test_expect_bool("merge free page 1",
				     plane_kernel_map_free_pages(addrs[1], 1),
				     true);
	failures += test_expect_bool("merge free page 3",
				     plane_kernel_map_free_pages(addrs[3], 1),
				     true);
	failures += check_stats("merge separated holes",
				TEST_KERNEL_MAP_PAGES - 3, 3, 3, 3, 3);
	failures += test_expect_bool("merge bridge holes",
				     plane_kernel_map_free_pages(addrs[2], 1),
				     true);
	failures += check_stats("merge bridged holes",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 2, 2);
	failures += test_expect_bool("merge with tail hole",
				     plane_kernel_map_free_pages(addrs[4], 1),
				     true);
	failures += check_stats("merge tail hole",
				TEST_KERNEL_MAP_PAGES - 1, 1, 1, 1, 1);
	return failures;
}

static int test_guarded_alloc_reserves_unmapped_sentinels(void)
{
	uint64_t vaddr = 0;
	uint64_t reused = 0;
	int failures = 0;

	failures += test_expect_bool("guard init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("guard alloc",
				     plane_kernel_map_alloc_pages_guarded(2, 1,
									  &vaddr),
				     true);
	failures += test_expect_u64("guard user address", vaddr,
				    page_vaddr(1));
	failures += test_expect_bool("guard has user allocation",
				     plane_kernel_map_has_allocation(vaddr, 2),
				     true);
	failures += test_expect_bool("guard base not allocation",
				     plane_kernel_map_has_allocation(page_vaddr(0),
								     1),
				     false);
	failures += check_stats("guard stats", TEST_KERNEL_MAP_PAGES - 4,
				4, 2, 1, 1);

	failures += test_expect_bool("guard partial free rejected",
				     plane_kernel_map_free_pages(vaddr, 1),
				     false);
	failures += test_expect_bool("guard free",
				     plane_kernel_map_free_pages(vaddr, 2),
				     true);
	failures += check_stats("guard free stats",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	failures += test_expect_bool("guard hole reused",
				     plane_kernel_map_alloc_pages(4, &reused),
				     true);
	failures += test_expect_u64("guard reused reserved hole", reused,
				    TEST_KERNEL_MAP_BASE);
	return failures;
}

static int test_guarded_alloc_rejects_invalid_ranges(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	int failures = 0;

	failures += test_expect_bool("guard reject init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   2 * PAGE_SIZE),
				     true);
	before = plane_kernel_map_get_stats();
	failures += test_expect_bool("guard zero user",
				     plane_kernel_map_alloc_pages_guarded(0, 1,
									  &vaddr),
				     false);
	failures += test_expect_bool("guard no room",
				     plane_kernel_map_alloc_pages_guarded(1, 1,
									  &vaddr),
				     false);
	failures += test_expect_bool("guard overflow",
				     plane_kernel_map_alloc_pages_guarded(
					     1, UINT64_MAX / 2 + 1, &vaddr),
				     false);
	after = plane_kernel_map_get_stats();
	failures += test_expect_u64("guard reject free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("guard reject reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("guard reject user unchanged",
				    after.user_pages, before.user_pages);
	failures += test_expect_u64("guard reject count unchanged",
				    after.allocation_count, before.allocation_count);
	return failures;
}

static int test_protected_alloc_rejects_invalid_protection(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	int failures = 0;

	failures += test_expect_bool("prot init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_kernel_map_get_stats();
	failures += test_expect_bool(
		"prot rejects none",
		plane_kernel_map_alloc_pages_protected(1, 0, 0, &vaddr),
		false);
	failures += test_expect_bool(
		"prot rejects unknown",
		plane_kernel_map_alloc_pages_protected(1, 0, BIT(8), &vaddr),
		false);
	after = plane_kernel_map_get_stats();
	failures += test_expect_u64("prot reject free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("prot reject reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("prot reject user unchanged",
				    after.user_pages, before.user_pages);
	failures += test_expect_u64("prot reject count unchanged",
				    after.allocation_count, before.allocation_count);
	return failures;
}

static int test_protected_alloc_accepts_write_only_protection(void)
{
	struct plane_kernel_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("write-only init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool(
		"write-only alloc",
		plane_kernel_map_alloc_pages_protected(
			1, 0, PLANE_VM_PROT_WRITE, &vaddr),
		true);
	failures += test_expect_bool(
		"write-only lookup",
		plane_kernel_map_lookup_allocation(vaddr, 1, &info),
		true);
	failures += test_expect_u64("write-only reserved start",
				    info.reserved_start, TEST_KERNEL_MAP_BASE);
	failures += test_expect_u64("write-only reserved pages",
				    info.reserved_pages, 1);
	failures += test_expect_u64("write-only user start",
				    info.user_start, TEST_KERNEL_MAP_BASE);
	failures += test_expect_u64("write-only user pages",
				    info.user_pages, 1);
	failures += test_expect_u32("write-only prot",
				    info.prot, PLANE_VM_PROT_WRITE);
	return failures;
}

static int test_protected_guarded_alloc_keeps_user_range_semantics(void)
{
	struct plane_kernel_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("prot guard init",
				     plane_kernel_map_init(TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool(
		"prot guard alloc",
		plane_kernel_map_alloc_pages_protected(
			2, 1, PLANE_VM_PROT_READ, &vaddr),
		true);
	failures += test_expect_u64("prot guard user address", vaddr,
				    page_vaddr(1));
	failures += test_expect_bool("prot guard has user allocation",
				     plane_kernel_map_has_allocation(vaddr, 2),
				     true);
	failures += test_expect_bool("prot guard base not allocation",
				     plane_kernel_map_has_allocation(page_vaddr(0),
								     1),
				     false);
	failures += test_expect_bool(
		"prot guard lookup",
		plane_kernel_map_lookup_allocation(vaddr, 2, &info),
		true);
	failures += test_expect_u64("prot guard reserved start",
				    info.reserved_start, TEST_KERNEL_MAP_BASE);
	failures += test_expect_u64("prot guard reserved pages",
				    info.reserved_pages, 4);
	failures += test_expect_u64("prot guard user start",
				    info.user_start, page_vaddr(1));
	failures += test_expect_u64("prot guard user pages",
				    info.user_pages, 2);
	failures += test_expect_u32("prot guard prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += check_stats("prot guard stats",
				TEST_KERNEL_MAP_PAGES - 4, 4, 2, 1, 1);
	failures += test_expect_bool("prot guard free",
				     plane_kernel_map_free_pages(vaddr, 2),
				     true);
	failures += check_stats("prot guard free stats",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_rejects_invalid_init),
		TEST_CASE(test_init_stats),
		TEST_CASE(test_init_is_one_shot_in_production_mode),
		TEST_CASE(test_alloc_and_free_pages),
		TEST_CASE(test_rejects_invalid_alloc_and_free),
		TEST_CASE(test_rejects_exhausted_vaddr_space),
		TEST_CASE(test_rejects_exhausted_entries),
		TEST_CASE(test_first_fit_reuses_lowest_hole),
		TEST_CASE(test_holes_merge_after_entry_removal),
		TEST_CASE(test_guarded_alloc_reserves_unmapped_sentinels),
		TEST_CASE(test_guarded_alloc_rejects_invalid_ranges),
		TEST_CASE(test_protected_alloc_rejects_invalid_protection),
		TEST_CASE(test_protected_alloc_accepts_write_only_protection),
		TEST_CASE(test_protected_guarded_alloc_keeps_user_range_semantics),
	};

	return test_run_cases("vm_map_test", cases, TEST_ARRAY_SIZE(cases));
}
