#include <stdbool.h>
#include <stdint.h>

#include <hal/x86_64/cpu_features.h>
#include <hal/x86_64/msr_defs.h>

#include "support/test.h"

static bool test_has_msr;
static bool test_has_pat;
static bool test_msr_write_should_fail;
static uint64_t test_pat_msr_value;
static uint32_t test_read_count;
static uint32_t test_write_count;
static uint32_t test_last_write_msr;
static uint64_t test_last_write_value;

#include "../hal/x86_64/pat.c"

bool x86_64_cpu_has_feature(enum x86_64_cpu_feature feature)
{
	if (feature == X86_64_CPU_FEATURE_MSR) {
		return test_has_msr;
	}
	if (feature == X86_64_CPU_FEATURE_PAT) {
		return test_has_pat;
	}

	return false;
}

uint64_t x86_64_msr_read(uint32_t msr)
{
	test_read_count++;
	return msr == X86_64_MSR_IA32_CR_PAT ? test_pat_msr_value : 0;
}

bool x86_64_msr_write(uint32_t msr, uint64_t value)
{
	test_write_count++;
	test_last_write_msr = msr;
	test_last_write_value = value;
	return !test_msr_write_should_fail;
}

static void reset_pat_test(void)
{
	test_has_msr = true;
	test_has_pat = true;
	test_msr_write_should_fail = false;
	test_pat_msr_value = 0x0706050403020100ull;
	test_read_count = 0;
	test_write_count = 0;
	test_last_write_msr = 0;
	test_last_write_value = 0;
	pat_write_combine_ready = false;
}

static int test_pat_init_requires_features(void)
{
	int failures = 0;

	reset_pat_test();
	test_has_msr = false;
	failures += test_expect_bool("missing msr rejected",
				     x86_64_pat_init(), false);
	failures += test_expect_u32("missing msr no read", test_read_count, 0);
	failures += test_expect_u32("missing msr no write",
				    test_write_count, 0);
	failures += test_expect_bool("missing msr leaves wc not ready",
				     x86_64_pat_write_combine_ready(), false);

	reset_pat_test();
	test_has_pat = false;
	failures += test_expect_bool("missing pat rejected",
				     x86_64_pat_init(), false);
	failures += test_expect_u32("missing pat no read", test_read_count, 0);
	failures += test_expect_u32("missing pat no write",
				    test_write_count, 0);
	failures += test_expect_bool("missing pat leaves wc not ready",
				     x86_64_pat_write_combine_ready(), false);

	return failures;
}

static int test_pat_init_sets_entry_one_to_write_combine(void)
{
	uint64_t expected;
	int failures = 0;

	reset_pat_test();
	expected = (test_pat_msr_value &
		    ~X86_64_MSR_IA32_CR_PAT_ENTRY_MASK(1)) |
		   ((uint64_t)X86_64_MSR_IA32_CR_PAT_MEMORY_WC
		    << X86_64_MSR_IA32_CR_PAT_ENTRY_SHIFT(1));

	failures += test_expect_bool("pat init succeeds",
				     x86_64_pat_init(), true);
	failures += test_expect_u32("pat read once", test_read_count, 1);
	failures += test_expect_u32("pat write once", test_write_count, 1);
	failures += test_expect_u32("pat write msr", test_last_write_msr,
				    X86_64_MSR_IA32_CR_PAT);
	failures += test_expect_u64("pat entry one wc",
				    test_last_write_value, expected);
	failures += test_expect_bool("wc ready",
				     x86_64_pat_write_combine_ready(), true);

	return failures;
}

static int test_pat_init_propagates_write_failure(void)
{
	int failures = 0;

	reset_pat_test();
	test_msr_write_should_fail = true;
	failures += test_expect_bool("pat write failure rejected",
				     x86_64_pat_init(), false);
	failures += test_expect_u32("write attempted", test_write_count, 1);
	failures += test_expect_bool("write failure leaves wc not ready",
				     x86_64_pat_write_combine_ready(), false);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_pat_init_requires_features),
		TEST_CASE(test_pat_init_sets_entry_one_to_write_combine),
		TEST_CASE(test_pat_init_propagates_write_failure),
	};

	return test_run_cases_with_fixture("x86_64_pat_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_pat_test, NULL);
}
