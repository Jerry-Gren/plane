#include <stdint.h>

#include <hal/x86_64/processor_defs.h>

#include "support/test.h"

static int test_control_and_flags_bits_match_manual_fields(void)
{
	int failures = 0;

	failures += test_expect_u64("rflags if",
				    X86_64_RFLAGS_IF, 1ull << 9);
	failures += test_expect_u64("rflags id",
				    X86_64_RFLAGS_ID, 1ull << 21);
	failures += test_expect_u64("cr0 pe", X86_64_CR0_PE, 1ull << 0);
	failures += test_expect_u64("cr0 pg", X86_64_CR0_PG, 1ull << 31);
	failures += test_expect_u64("cr4 pae", X86_64_CR4_PAE, 1ull << 5);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_control_and_flags_bits_match_manual_fields),
	};

	return test_run_cases("x86_64_processor_defs_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
