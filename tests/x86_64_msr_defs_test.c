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
	failures += test_expect_u64("ia32 efer lme",
				    X86_64_MSR_IA32_EFER_LME, 1ull << 8);

	return failures;
}

static int test_apic_base_fields_match_xapic_mode(void)
{
	int failures = 0;

	failures += test_expect_u64("apic base x2apic",
				    X86_64_MSR_IA32_APIC_BASE_X2APIC,
				    1ull << 10);
	failures += test_expect_u64("apic base enable",
				    X86_64_MSR_IA32_APIC_BASE_ENABLE,
				    1ull << 11);
	failures += test_expect_u64("apic base addr mask",
				    X86_64_MSR_IA32_APIC_BASE_ADDR,
				    GENMASK_ULL(51, 12));

	return failures;
}

static int test_cr_pat_fields_match_memory_type_slots(void)
{
	int failures = 0;

	failures += test_expect_u32("cr pat wc type",
				    X86_64_MSR_IA32_CR_PAT_MEMORY_WC,
				    0x01);
	failures += test_expect_u32("cr pat entry 0 shift",
				    X86_64_MSR_IA32_CR_PAT_ENTRY_SHIFT(0),
				    0);
	failures += test_expect_u32("cr pat entry 1 shift",
				    X86_64_MSR_IA32_CR_PAT_ENTRY_SHIFT(1),
				    8);
	failures += test_expect_u32("cr pat entry 7 shift",
				    X86_64_MSR_IA32_CR_PAT_ENTRY_SHIFT(7),
				    56);
	failures += test_expect_u64("cr pat entry 1 mask",
				    X86_64_MSR_IA32_CR_PAT_ENTRY_MASK(1),
				    0xff00ull);
	failures += test_expect_u64("cr pat entry 7 mask",
				    X86_64_MSR_IA32_CR_PAT_ENTRY_MASK(7),
				    0xffull << 56);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_msr_numbers_match_manual_indices),
		TEST_CASE(test_apic_base_fields_match_xapic_mode),
		TEST_CASE(test_cr_pat_fields_match_memory_type_slots),
	};

	return test_run_cases("x86_64_msr_defs_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
