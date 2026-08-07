#include <stdbool.h>
#include <stdint.h>

#include <hal/cpu.h>
#include <hal/x86_64/cpu_features.h>
#include <hal/x86_64/msr_defs.h>
#include <plane/smp.h>

#include "support/test.h"

static bool test_has_msr;
static bool test_msr_write_should_fail;
static uint32_t last_msr;
static uint64_t last_msr_value;
static uint32_t msr_write_count;

bool x86_64_cpu_has_feature(enum x86_64_cpu_feature feature)
{
	return feature == X86_64_CPU_FEATURE_MSR && test_has_msr;
}

bool x86_64_msr_write(uint32_t msr, uint64_t value)
{
	msr_write_count++;
	last_msr = msr;
	last_msr_value = value;
	return !test_msr_write_should_fail;
}

static void reset_msr_test(void)
{
	test_has_msr = false;
	test_msr_write_should_fail = false;
	last_msr = 0;
	last_msr_value = 0;
	msr_write_count = 0;
}

static int test_set_current_data_rejects_null(void)
{
	int failures = 0;

	reset_msr_test();
	test_has_msr = true;
	failures += test_expect_bool("null current data rejected",
				     hal_cpu_set_current_data(NULL), false);
	failures += test_expect_u32("null does not write msr",
				    msr_write_count, 0);
	return failures;
}

static int test_set_current_data_requires_msr_feature(void)
{
	int failures = 0;
	struct plane_cpu_data data = {0};

	reset_msr_test();
	failures += test_expect_bool("missing msr rejected",
				     hal_cpu_set_current_data(&data), false);
	failures += test_expect_u32("missing msr does not write",
				    msr_write_count, 0);
	return failures;
}

static int test_set_current_data_writes_gs_base(void)
{
	int failures = 0;
	struct plane_cpu_data data = {
		.logical_id = 7,
		.physical_id = 9,
		.is_bsp = true,
		.present = true,
	};

	reset_msr_test();
	test_has_msr = true;
	failures += test_expect_bool("set current data succeeds",
				     hal_cpu_set_current_data(&data), true);
	failures += test_expect_u32("writes once", msr_write_count, 1);
	failures += test_expect_u32("writes gs base msr",
				    last_msr, X86_64_MSR_IA32_GS_BASE);
	failures += test_expect_u64("writes data pointer",
				    last_msr_value,
				    (uint64_t)(uintptr_t)&data);
	return failures;
}

static int test_set_current_data_propagates_msr_write_failure(void)
{
	int failures = 0;
	struct plane_cpu_data data = {0};

	reset_msr_test();
	test_has_msr = true;
	test_msr_write_should_fail = true;
	failures += test_expect_bool("write failure rejected",
				     hal_cpu_set_current_data(&data), false);
	failures += test_expect_u32("write attempted once",
				    msr_write_count, 1);
	failures += test_expect_u32("write failure uses gs base",
				    last_msr, X86_64_MSR_IA32_GS_BASE);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_set_current_data_rejects_null),
		TEST_CASE(test_set_current_data_requires_msr_feature),
		TEST_CASE(test_set_current_data_writes_gs_base),
		TEST_CASE(test_set_current_data_propagates_msr_write_failure),
	};

	return test_run_cases("x86_64_cpu_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
