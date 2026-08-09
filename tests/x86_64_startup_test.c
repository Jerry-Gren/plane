#include <stdbool.h>
#include <stdint.h>

#include <machine/machine_routines.h>

#include "support/test.h"

static bool cpu_features_ok;
static bool pat_ok;
static uint32_t cpu_features_count;
static uint32_t pat_count;
static uint32_t gdt_count;
static uint32_t idt_count;

#include <x86_64/startup.c>

bool x86_64_cpu_features_init(void)
{
	cpu_features_count++;
	return cpu_features_ok;
}

bool x86_64_pat_init(void)
{
	pat_count++;
	return pat_ok;
}

void x86_64_gdt_init(void)
{
	gdt_count++;
}

void x86_64_idt_init(void)
{
	idt_count++;
}

static void reset_startup_test(void)
{
	cpu_features_ok = true;
	pat_ok = true;
	cpu_features_count = 0;
	pat_count = 0;
	gdt_count = 0;
	idt_count = 0;
}

static int test_startup_init_succeeds_in_order(void)
{
	int failures = 0;

	failures += test_expect_bool("startup init succeeds",
				     ml_startup_init(), true);
	failures += test_expect_u32("cpu features called",
				    cpu_features_count, 1);
	failures += test_expect_u32("pat called", pat_count, 1);
	failures += test_expect_u32("gdt called", gdt_count, 1);
	failures += test_expect_u32("idt called", idt_count, 1);
	return failures;
}

static int test_startup_init_rejects_cpu_feature_failure(void)
{
	int failures = 0;

	cpu_features_ok = false;
	failures += test_expect_bool("cpu feature failure rejected",
				     ml_startup_init(), false);
	failures += test_expect_u32("cpu features attempted",
				    cpu_features_count, 1);
	failures += test_expect_u32("pat skipped", pat_count, 0);
	failures += test_expect_u32("gdt skipped", gdt_count, 0);
	failures += test_expect_u32("idt skipped", idt_count, 0);
	return failures;
}

static int test_startup_init_rejects_pat_failure(void)
{
	int failures = 0;

	pat_ok = false;
	failures += test_expect_bool("pat failure rejected",
				     ml_startup_init(), false);
	failures += test_expect_u32("cpu features called",
				    cpu_features_count, 1);
	failures += test_expect_u32("pat attempted", pat_count, 1);
	failures += test_expect_u32("gdt skipped", gdt_count, 0);
	failures += test_expect_u32("idt skipped", idt_count, 0);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_startup_init_succeeds_in_order),
		TEST_CASE(test_startup_init_rejects_cpu_feature_failure),
		TEST_CASE(test_startup_init_rejects_pat_failure),
	};

	return test_run_cases_with_fixture("x86_64_startup_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_startup_test, NULL);
}
