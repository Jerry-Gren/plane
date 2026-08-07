#include <stdint.h>

#include <hal/x86_64/processor_defs.h>

#include "support/test.h"

static int test_control_and_flags_bits_match_manual_fields(void)
{
	int failures = 0;

	failures += test_expect_u64("rflags id",
				    X86_64_RFLAGS_ID, 1ull << 21);
	failures += test_expect_u64("cr0 pe", X86_64_CR0_PE, 1ull << 0);
	failures += test_expect_u64("cr0 pg", X86_64_CR0_PG, 1ull << 31);
	failures += test_expect_u64("cr4 pae", X86_64_CR4_PAE, 1ull << 5);
	failures += test_expect_u64("efer lme",
				    X86_64_EFER_LME, 1ull << 8);

	return failures;
}

static int test_pat_entry_helpers_encode_memory_type_slots(void)
{
	int failures = 0;

	failures += test_expect_u32("pat wc type",
				    X86_64_PAT_MEMORY_WC, 0x01);
	failures += test_expect_u32("pat entry 0 shift",
				    X86_64_PAT_ENTRY_SHIFT(0), 0);
	failures += test_expect_u32("pat entry 1 shift",
				    X86_64_PAT_ENTRY_SHIFT(1), 8);
	failures += test_expect_u32("pat entry 7 shift",
				    X86_64_PAT_ENTRY_SHIFT(7), 56);
	failures += test_expect_u64("pat entry 1 mask",
				    X86_64_PAT_ENTRY_MASK(1), 0xff00ull);
	failures += test_expect_u64("pat entry 7 mask",
				    X86_64_PAT_ENTRY_MASK(7),
				    0xffull << 56);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_control_and_flags_bits_match_manual_fields),
		TEST_CASE(test_pat_entry_helpers_encode_memory_type_slots),
	};

	return test_run_cases("x86_64_processor_defs_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
