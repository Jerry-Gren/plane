#include <stdint.h>

#include <hal/x86_64/msr_defs.h>

#include "support/test.h"

static int test_msr_numbers_match_manual_indices(void)
{
	int failures = 0;

	failures += test_expect_u32("ia32 apic base",
				    X86_64_MSR_IA32_APIC_BASE, 0x1b);
	failures += test_expect_u32("ia32 cr pat",
				    X86_64_MSR_IA32_CR_PAT, 0x277);
	failures += test_expect_u32("ia32 efer",
				    X86_64_MSR_IA32_EFER, 0xc0000080u);
	failures += test_expect_u32("ia32 gs base",
				    X86_64_MSR_IA32_GS_BASE, 0xc0000101u);

	return failures;
}

static int test_apic_base_fields_match_xapic_mode(void)
{
	int failures = 0;

	failures += test_expect_u64("apic base x2apic",
				    X86_64_APIC_BASE_X2APIC, 1ull << 10);
	failures += test_expect_u64("apic base enable",
				    X86_64_APIC_BASE_ENABLE, 1ull << 11);
	failures += test_expect_u64("apic base addr mask",
				    X86_64_APIC_BASE_ADDR,
				    GENMASK_ULL(51, 12));

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_msr_numbers_match_manual_indices),
		TEST_CASE(test_apic_base_fields_match_xapic_mode),
	};

	return test_run_cases("x86_64_msr_defs_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
