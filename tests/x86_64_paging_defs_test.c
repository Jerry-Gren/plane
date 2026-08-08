#include <stdbool.h>
#include <stdint.h>

#include <hal/x86_64/arch_mmu.h>

#include "support/test.h"

static int test_index_helpers_match_named_macros(void)
{
	uint64_t vaddr = (1ull << 39) | (2ull << 30) | (3ull << 21) |
			 (4ull << 12);
	int failures = 0;

	failures += test_expect_u64("pml4 index",
				    x86_64_paging_index(4, vaddr),
				    X86_64_PAGING_PML4_INDEX(vaddr));
	failures += test_expect_u64("pdpt index",
				    x86_64_paging_index(3, vaddr),
				    X86_64_PAGING_PDPT_INDEX(vaddr));
	failures += test_expect_u64("pd index",
				    x86_64_paging_index(2, vaddr),
				    X86_64_PAGING_PD_INDEX(vaddr));
	failures += test_expect_u64("pt index",
				    x86_64_paging_index(1, vaddr),
				    X86_64_PAGING_PT_INDEX(vaddr));
	failures += test_expect_u64("unknown level falls back to pt",
				    x86_64_paging_index(0, vaddr),
				    X86_64_PAGING_PT_INDEX(vaddr));

	return failures;
}

static int test_entry_helpers_decode_current_bits(void)
{
	uint64_t phys = 0x0000abcd12345000ull;
	uint64_t entry = x86_64_paging_entry_make(
		phys | 0x123, X86_64_PAGING_ENTRY_PRESENT |
				     X86_64_PAGING_ENTRY_WRITE);
	int failures = 0;

	failures += test_expect_bool("present entry",
				     x86_64_paging_entry_present(entry), true);
	failures += test_expect_bool("absent entry",
				     x86_64_paging_entry_present(0), false);
	failures += test_expect_u64("entry phys",
				    x86_64_paging_entry_phys(entry), phys);
	failures += test_expect_u64("entry flags",
				    x86_64_paging_entry_flags(entry),
				    X86_64_PAGING_ENTRY_PRESENT |
					    X86_64_PAGING_ENTRY_WRITE);

	return failures;
}

static int test_leaf_helper_matches_current_pmap_levels(void)
{
	uint64_t ps_entry = X86_64_PAGING_ENTRY_PRESENT |
			    X86_64_PAGING_ENTRY_PS;
	int failures = 0;

	failures += test_expect_bool("pt entry is leaf",
				     x86_64_paging_entry_leaf(
					     X86_64_PAGING_ENTRY_PRESENT, 1),
				     true);
	failures += test_expect_bool("pd ps entry is leaf",
				     x86_64_paging_entry_leaf(ps_entry, 2),
				     true);
	failures += test_expect_bool("pdpt ps entry is leaf",
				     x86_64_paging_entry_leaf(ps_entry, 3),
				     true);
	failures += test_expect_bool("pml4 ps bit is not leaf",
				     x86_64_paging_entry_leaf(ps_entry, 4),
				     false);
	failures += test_expect_bool("non-ps pd entry is table",
				     x86_64_paging_entry_leaf(
					     X86_64_PAGING_ENTRY_PRESENT, 2),
				     false);

	return failures;
}

static int test_named_constants_match_current_manual_fields(void)
{
	int failures = 0;

	failures += test_expect_u64("present bit", X86_64_PAGING_ENTRY_PRESENT,
				    BIT_ULL(0));
	failures += test_expect_u64("write bit", X86_64_PAGING_ENTRY_WRITE,
				    BIT_ULL(1));
	failures += test_expect_u64("pwt bit", X86_64_PAGING_ENTRY_PWT,
				    BIT_ULL(3));
	failures += test_expect_u64("pcd bit", X86_64_PAGING_ENTRY_PCD,
				    BIT_ULL(4));
	failures += test_expect_u64("ps bit", X86_64_PAGING_ENTRY_PS,
				    BIT_ULL(7));
	failures += test_expect_u64("table entries",
				    X86_64_PAGING_TABLE_ENTRIES, 512);
	failures += test_expect_u64("pml4 slot size",
				    X86_64_PAGING_PML4_SLOT_SIZE,
				    0x8000000000ull);
	failures += test_expect_u64("address mask",
				    X86_64_PAGING_ENTRY_ADDR_MASK,
				    0x000ffffffffff000ull);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_index_helpers_match_named_macros),
		TEST_CASE(test_entry_helpers_decode_current_bits),
		TEST_CASE(test_leaf_helper_matches_current_pmap_levels),
		TEST_CASE(test_named_constants_match_current_manual_fields),
	};

	return test_run_cases("x86_64_paging_defs_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
