#include <stdint.h>

#include <plane/bits.h>
#include <plane/mm.h>
#include <plane/vm_map.h>

#include "support/test.h"

#define TEST_KERNEL_MAP_BASE 0xffff900000000000ull
#define TEST_KERNEL_MAP_PAGES 256
#define TEST_KERNEL_MAP_SIZE (TEST_KERNEL_MAP_PAGES * PAGE_SIZE)
#define TEST_MAP_ENTRIES 128

static struct plane_vm_map_entry test_entries[TEST_MAP_ENTRIES];
static struct plane_vm_map test_map;

static void reset_vm_map_test(void)
{
	test_map = (struct plane_vm_map){0};
	for (uint64_t i = 0; i < TEST_MAP_ENTRIES; i++) {
		test_entries[i] = (struct plane_vm_map_entry){0};
	}
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
	struct plane_vm_map_stats stats = plane_vm_map_get_stats(&test_map);
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
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
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
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   0),
				     false);
	failures += test_expect_bool("unaligned base init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE + 1,
							   TEST_KERNEL_MAP_SIZE),
				     false);
	failures += test_expect_bool("unaligned size init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE - 1),
				     false);
	failures += test_expect_bool("wrapping init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, UINT64_MAX - PAGE_SIZE + 1,
							   2 * PAGE_SIZE),
				     false);

	stats = plane_vm_map_get_stats(&test_map);
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

	failures += test_expect_bool("oneshot init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("oneshot alloc",
				     plane_vm_map_alloc_pages(&test_map, 2, &vaddr),
				     true);

	failures += test_expect_bool("oneshot reject valid reinit",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     false);
	failures += test_expect_bool("oneshot reject invalid reinit",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   0),
				     false);
	failures += test_expect_bool("oneshot allocation preserved",
				     plane_vm_map_has_allocation(&test_map, vaddr, 2),
				     true);
	failures += check_stats("oneshot stats preserved",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 1, 1);

	return failures;
}

static int test_alloc_and_free_pages(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("alloc init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("alloc pages",
				     plane_vm_map_alloc_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_u64("alloc vaddr", vaddr,
				    TEST_KERNEL_MAP_BASE);
	failures += test_expect_bool("has allocation",
				     plane_vm_map_has_allocation(&test_map, vaddr, 2),
				     true);
	failures += check_stats("alloc stats", TEST_KERNEL_MAP_PAGES - 2,
				2, 2, 1, 1);

	failures += test_expect_bool("free pages",
				     plane_vm_map_free_pages(&test_map, vaddr, 2),
				     true);
	failures += check_stats("free stats", TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	return failures;
}

static int test_rejects_invalid_alloc_and_free(void)
{
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("invalid init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("alloc zero pages",
				     plane_vm_map_alloc_pages(&test_map, 0, &vaddr),
				     false);
	failures += test_expect_bool("alloc null out",
				     plane_vm_map_alloc_pages(&test_map, 1, NULL),
				     false);
	failures += test_expect_bool("free zero pages",
				     plane_vm_map_free_pages(&test_map, TEST_KERNEL_MAP_BASE,
								 0),
				     false);
	failures += test_expect_bool("free unaligned",
				     plane_vm_map_free_pages(&test_map, TEST_KERNEL_MAP_BASE + 1,
								 1),
				     false);

	failures += test_expect_bool("alloc valid",
				     plane_vm_map_alloc_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool("partial free rejected",
				     plane_vm_map_free_pages(&test_map, vaddr, 1),
				     false);
	failures += test_expect_bool("partial allocation absent",
				     plane_vm_map_has_allocation(&test_map, vaddr, 1),
				     false);
	failures += test_expect_bool("exact free accepted",
				     plane_vm_map_free_pages(&test_map, vaddr, 2),
				     true);
	failures += test_expect_bool("double free rejected",
				     plane_vm_map_free_pages(&test_map, vaddr, 2),
				     false);
	return failures;
}

static int test_rejects_exhausted_vaddr_space(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats stats;
	int failures = 0;

	failures += test_expect_bool("space init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   PAGE_SIZE),
				     true);
	failures += test_expect_bool("space alloc too large",
				     plane_vm_map_alloc_pages(&test_map, 2, &vaddr),
				     false);
	stats = plane_vm_map_get_stats(&test_map);
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
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_MAP_ENTRIES; i++) {
		failures += test_expect_bool("entry alloc",
					     plane_vm_map_alloc_pages(&test_map, 1,
									  &vaddr),
					     true);
	}

	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("entry exhausted",
				     plane_vm_map_alloc_pages(&test_map, 1, &vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
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
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("fit alloc first",
				     plane_vm_map_alloc_pages(&test_map, 2, &first),
				     true);
	failures += test_expect_bool("fit alloc second",
				     plane_vm_map_alloc_pages(&test_map, 2, &second),
				     true);
	failures += test_expect_u64("fit second address", second,
				    page_vaddr(2));
	failures += test_expect_bool("fit free first",
				     plane_vm_map_free_pages(&test_map, first, 2),
				     true);
	failures += check_stats("fit hole stats", TEST_KERNEL_MAP_PAGES - 2,
				2, 2, 2, 1);
	failures += test_expect_bool("fit reuse hole",
				     plane_vm_map_alloc_pages(&test_map, 1, &reused),
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
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(addrs); i++) {
		failures += test_expect_bool("merge alloc",
					     plane_vm_map_alloc_pages(&test_map, 1,
									  &addrs[i]),
					     true);
	}

	failures += test_expect_bool("merge free page 1",
				     plane_vm_map_free_pages(&test_map, addrs[1], 1),
				     true);
	failures += test_expect_bool("merge free page 3",
				     plane_vm_map_free_pages(&test_map, addrs[3], 1),
				     true);
	failures += check_stats("merge separated holes",
				TEST_KERNEL_MAP_PAGES - 3, 3, 3, 3, 3);
	failures += test_expect_bool("merge bridge holes",
				     plane_vm_map_free_pages(&test_map, addrs[2], 1),
				     true);
	failures += check_stats("merge bridged holes",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 2, 2);
	failures += test_expect_bool("merge with tail hole",
				     plane_vm_map_free_pages(&test_map, addrs[4], 1),
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
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("guard alloc",
				     plane_vm_map_alloc_pages_guarded(&test_map, 2, 1,
									  &vaddr),
				     true);
	failures += test_expect_u64("guard user address", vaddr,
				    page_vaddr(1));
	failures += test_expect_bool("guard has user allocation",
				     plane_vm_map_has_allocation(&test_map, vaddr, 2),
				     true);
	failures += test_expect_bool("guard base not allocation",
				     plane_vm_map_has_allocation(&test_map, page_vaddr(0),
								     1),
				     false);
	failures += check_stats("guard stats", TEST_KERNEL_MAP_PAGES - 4,
				4, 2, 1, 1);

	failures += test_expect_bool("guard partial free rejected",
				     plane_vm_map_free_pages(&test_map, vaddr, 1),
				     false);
	failures += test_expect_bool("guard free",
				     plane_vm_map_free_pages(&test_map, vaddr, 2),
				     true);
	failures += check_stats("guard free stats",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	failures += test_expect_bool("guard hole reused",
				     plane_vm_map_alloc_pages(&test_map, 4, &reused),
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
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   2 * PAGE_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool("guard zero user",
				     plane_vm_map_alloc_pages_guarded(&test_map, 0, 1,
									  &vaddr),
				     false);
	failures += test_expect_bool("guard no room",
				     plane_vm_map_alloc_pages_guarded(&test_map, 1, 1,
									  &vaddr),
				     false);
	failures += test_expect_bool("guard overflow",
				     plane_vm_map_alloc_pages_guarded(&test_map,
					     1, UINT64_MAX / 2 + 1, &vaddr),
				     false);
	after = plane_vm_map_get_stats(&test_map);
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
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool(
		"prot rejects none",
		plane_vm_map_alloc_pages_protected(&test_map, 1, 0, 0, &vaddr),
		false);
	failures += test_expect_bool(
		"prot rejects unknown",
		plane_vm_map_alloc_pages_protected(&test_map, 1, 0, BIT(8), &vaddr),
		false);
	after = plane_vm_map_get_stats(&test_map);
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
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("write-only init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool(
		"write-only alloc",
		plane_vm_map_alloc_pages_protected(&test_map,
			1, 0, PLANE_VM_PROT_WRITE, &vaddr),
		true);
	failures += test_expect_bool(
		"write-only lookup",
		plane_vm_map_lookup_allocation(&test_map, vaddr, 1, &info),
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
	failures += test_expect_u32("write-only max prot",
				    info.max_prot,
				    PLANE_VM_PROT_ALL);
	return failures;
}

static int test_protected_guarded_alloc_keeps_user_range_semantics(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("prot guard init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool(
		"prot guard alloc",
		plane_vm_map_alloc_pages_protected(&test_map,
			2, 1, PLANE_VM_PROT_READ, &vaddr),
		true);
	failures += test_expect_u64("prot guard user address", vaddr,
				    page_vaddr(1));
	failures += test_expect_bool("prot guard has user allocation",
				     plane_vm_map_has_allocation(&test_map, vaddr, 2),
				     true);
	failures += test_expect_bool("prot guard base not allocation",
				     plane_vm_map_has_allocation(&test_map, page_vaddr(0),
								     1),
				     false);
	failures += test_expect_bool(
		"prot guard lookup",
		plane_vm_map_lookup_allocation(&test_map, vaddr, 2, &info),
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
	failures += test_expect_u32("prot guard max prot",
				    info.max_prot,
				    PLANE_VM_PROT_ALL);
	failures += check_stats("prot guard stats",
				TEST_KERNEL_MAP_PAGES - 4, 4, 2, 1, 1);
	failures += test_expect_bool("prot guard free",
				     plane_vm_map_free_pages(&test_map, vaddr, 2),
				     true);
	failures += check_stats("prot guard free stats",
				TEST_KERNEL_MAP_PAGES, 0, 0, 1, 0);
	return failures;
}

static int test_protect_pages_updates_exact_allocation(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("protect init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("protect alloc",
				     plane_vm_map_alloc_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool("protect readonly",
				     plane_vm_map_protect_pages(&test_map,
					     vaddr, 2, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_bool("protect lookup readonly",
				     plane_vm_map_lookup_allocation(&test_map, vaddr,
								       2,
								       &info),
				     true);
	failures += test_expect_u32("protect readonly prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("protect readonly max",
				    info.max_prot, PLANE_VM_PROT_ALL);
	failures += test_expect_bool("protect writable again",
				     plane_vm_map_protect_pages(&test_map,
					     vaddr, 2, PLANE_VM_PROT_DEFAULT),
				     true);
	failures += test_expect_bool("protect lookup writable",
				     plane_vm_map_lookup_allocation(&test_map, vaddr,
								       2,
								       &info),
				     true);
	failures += test_expect_u32("protect writable prot",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	return failures;
}

static int test_protect_pages_rejects_invalid_ranges(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("protect reject init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool("protect reject alloc",
				     plane_vm_map_alloc_pages(&test_map, 2, &vaddr),
				     true);
	failures += test_expect_bool("protect rejects none",
				     plane_vm_map_protect_pages(&test_map, vaddr, 2, 0),
				     false);
	failures += test_expect_bool("protect rejects unknown",
				     plane_vm_map_protect_pages(&test_map, vaddr, 2,
								    BIT(8)),
				     false);
	failures += test_expect_bool("protect rejects partial",
				     plane_vm_map_protect_pages(&test_map,
					     vaddr, 1, PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("protect rejects absent",
				     plane_vm_map_protect_pages(&test_map,
					     page_vaddr(10), 1,
					     PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("protect unchanged lookup",
				     plane_vm_map_lookup_allocation(&test_map, vaddr,
								       2,
								       &info),
				     true);
	failures += test_expect_u32("protect unchanged prot",
				    info.prot, PLANE_VM_PROT_DEFAULT);
	failures += check_stats("protect reject stats",
				TEST_KERNEL_MAP_PAGES - 2, 2, 2, 1, 1);
	return failures;
}

static int test_protected_max_allocation_records_explicit_max(void)
{
	struct plane_vm_map_allocation_info info = {0};
	uint64_t vaddr = 0;
	int failures = 0;

	failures += test_expect_bool("max init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	failures += test_expect_bool(
		"max readonly alloc",
		plane_vm_map_alloc_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_READ, PLANE_VM_PROT_READ, &vaddr),
		true);
	failures += test_expect_bool(
		"max readonly lookup",
		plane_vm_map_lookup_allocation(&test_map, vaddr, 1, &info),
		true);
	failures += test_expect_u32("max readonly prot",
				    info.prot, PLANE_VM_PROT_READ);
	failures += test_expect_u32("max readonly max",
				    info.max_prot, PLANE_VM_PROT_READ);
	failures += test_expect_bool(
		"max readonly protect read",
		plane_vm_map_protect_pages(&test_map, vaddr, 1, PLANE_VM_PROT_READ),
		true);
	failures += test_expect_bool(
		"max readonly reject write",
		plane_vm_map_protect_pages(&test_map, vaddr, 1, PLANE_VM_PROT_WRITE),
		false);
	failures += test_expect_bool(
		"max readonly reject rw",
		plane_vm_map_protect_pages(&test_map, vaddr, 1, PLANE_VM_PROT_DEFAULT),
		false);
	return failures;
}

static int test_protected_max_allocation_rejects_invalid_pairs(void)
{
	uint64_t vaddr = 0;
	struct plane_vm_map_stats before;
	struct plane_vm_map_stats after;
	int failures = 0;

	failures += test_expect_bool("max reject init",
				     plane_vm_map_init(&test_map, test_entries, TEST_MAP_ENTRIES, TEST_KERNEL_MAP_BASE,
							   TEST_KERNEL_MAP_SIZE),
				     true);
	before = plane_vm_map_get_stats(&test_map);
	failures += test_expect_bool(
		"max rejects prot none",
		plane_vm_map_alloc_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_NONE, PLANE_VM_PROT_ALL, &vaddr),
		false);
	failures += test_expect_bool(
		"max rejects max none",
		plane_vm_map_alloc_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_READ, PLANE_VM_PROT_NONE, &vaddr),
		false);
	failures += test_expect_bool(
		"max rejects prot outside max",
		plane_vm_map_alloc_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_DEFAULT, PLANE_VM_PROT_READ, &vaddr),
		false);
	failures += test_expect_bool(
		"max rejects unknown prot",
		plane_vm_map_alloc_pages_protected_max(&test_map,
			1, 0, BIT(8), PLANE_VM_PROT_ALL, &vaddr),
		false);
	failures += test_expect_bool(
		"max rejects unknown max",
		plane_vm_map_alloc_pages_protected_max(&test_map,
			1, 0, PLANE_VM_PROT_READ, BIT(8), &vaddr),
		false);
	after = plane_vm_map_get_stats(&test_map);
	failures += test_expect_u64("max reject free unchanged",
				    after.free_pages, before.free_pages);
	failures += test_expect_u64("max reject reserved unchanged",
				    after.reserved_pages, before.reserved_pages);
	failures += test_expect_u64("max reject user unchanged",
				    after.user_pages, before.user_pages);
	failures += test_expect_u64("max reject count unchanged",
				    after.allocation_count, before.allocation_count);
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
		TEST_CASE(test_protect_pages_updates_exact_allocation),
		TEST_CASE(test_protect_pages_rejects_invalid_ranges),
		TEST_CASE(test_protected_max_allocation_records_explicit_max),
		TEST_CASE(test_protected_max_allocation_rejects_invalid_pairs),
	};

	return test_run_cases_with_fixture("vm_map_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_vm_map_test,
					   NULL);
}
