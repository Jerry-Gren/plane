#include <stdint.h>

#include <x86_64/cpuid_defs.h>

#include "support/test.h"

static int test_leaf1_signature_field_masks(void)
{
	int failures = 0;

	failures += test_expect_u64("stepping",
				    X86_64_CPUID_1_EAX_STEPPING, 0x0000000f);
	failures += test_expect_u64("base model",
				    X86_64_CPUID_1_EAX_BASE_MODEL,
				    0x000000f0);
	failures += test_expect_u64("base family",
				    X86_64_CPUID_1_EAX_BASE_FAMILY,
				    0x00000f00);
	failures += test_expect_u64("processor type",
				    X86_64_CPUID_1_EAX_PROCESSOR_TYPE,
				    0x00003000);
	failures += test_expect_u64("extended model",
				    X86_64_CPUID_1_EAX_EXT_MODEL,
				    0x000f0000);
	failures += test_expect_u64("extended family",
				    X86_64_CPUID_1_EAX_EXT_FAMILY,
				    0x0ff00000);
	failures += test_expect_u64("display ext model",
				    X86_64_CPUID_DISPLAY_EXT_MODEL,
				    0x000000f0);

	return failures;
}

static int test_leaf1_ebx_field_masks(void)
{
	int failures = 0;

	failures += test_expect_u64("clflush line size",
				    X86_64_CPUID_1_EBX_CLFLUSH_LINE_SIZE,
				    0x0000ff00);
	failures += test_expect_u64("logical processors",
				    X86_64_CPUID_1_EBX_LOGICAL_PROCESSORS,
				    0x00ff0000);
	failures += test_expect_u64("initial apic id",
				    X86_64_CPUID_1_EBX_INITIAL_APIC_ID,
				    0xff000000);

	return failures;
}

static int test_xsave_xcr0_field_masks(void)
{
	int failures = 0;

	failures += test_expect_u64("xcr0 low",
				    X86_64_CPUID_D_0_XCR0_LOW,
				    0x00000000ffffffffull);
	failures += test_expect_u64("xcr0 high",
				    X86_64_CPUID_D_0_XCR0_HIGH,
				    0xffffffff00000000ull);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_leaf1_signature_field_masks),
		TEST_CASE(test_leaf1_ebx_field_masks),
		TEST_CASE(test_xsave_xcr0_field_masks),
	};

	return test_run_cases("x86_64_cpuid_defs_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
