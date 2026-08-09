#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <x86_64/descriptor_defs.h>

#include "support/test.h"

static int test_selector_helper_encodes_index_and_rpl(void)
{
	int failures = 0;

	failures += test_expect_u32("kernel cs selector",
				    X86_64_DESC_SELECTOR_KERNEL_CS,
				    x86_64_desc_selector(
					    X86_64_DESC_GDT_KERNEL_CODE,
					    X86_64_DESC_RPL_KERNEL));
	failures += test_expect_u32("kernel ds selector",
				    X86_64_DESC_SELECTOR_KERNEL_DS,
				    x86_64_desc_selector(
					    X86_64_DESC_GDT_KERNEL_DATA,
					    X86_64_DESC_RPL_KERNEL));
	failures += test_expect_u32("user ds selector",
				    X86_64_DESC_SELECTOR_USER_DS,
				    x86_64_desc_selector(
					    X86_64_DESC_GDT_USER_DATA,
					    X86_64_DESC_RPL_USER));
	failures += test_expect_u32("user cs selector",
				    X86_64_DESC_SELECTOR_USER_CS,
				    x86_64_desc_selector(
					    X86_64_DESC_GDT_USER_CODE,
					    X86_64_DESC_RPL_USER));
	failures += test_expect_u32("tss selector",
				    X86_64_DESC_SELECTOR_TSS,
				    x86_64_desc_selector(
					    X86_64_DESC_GDT_TSS,
					    X86_64_DESC_RPL_KERNEL));

	return failures;
}

static int test_access_and_flags_helpers_encode_manual_fields(void)
{
	int failures = 0;

	failures += test_expect_u32("kernel code access",
				    x86_64_desc_access(
					    true, X86_64_DESC_DPL_KERNEL,
					    true, X86_64_DESC_TYPE_CODE_XR),
				    X86_64_DESC_ACCESS_PRESENT |
					    X86_64_DESC_ACCESS_CODE_DATA |
					    X86_64_DESC_TYPE_CODE_XR);
	failures += test_expect_u32("user data access",
				    x86_64_desc_access(
					    true, X86_64_DESC_DPL_USER,
					    true, X86_64_DESC_TYPE_DATA_RW),
				    X86_64_DESC_ACCESS_PRESENT |
					    (X86_64_DESC_DPL_USER <<
					     X86_64_DESC_ACCESS_DPL_SHIFT) |
					    X86_64_DESC_ACCESS_CODE_DATA |
					    X86_64_DESC_TYPE_DATA_RW);
	failures += test_expect_u32("tss access",
				    x86_64_desc_access(
					    true, X86_64_DESC_DPL_KERNEL,
					    false,
					    X86_64_DESC_TYPE_TSS_AVAILABLE),
				    X86_64_DESC_ACCESS_PRESENT |
					    X86_64_DESC_TYPE_TSS_AVAILABLE);
	failures += test_expect_u32("64-bit code flags",
				    x86_64_desc_flags(true, false, true,
						      false),
				    X86_64_DESC_FLAGS_GRAN_4K |
					    X86_64_DESC_FLAGS_LONG_MODE);
	failures += test_expect_u32("32-bit data flags",
				    x86_64_desc_flags(true, true, false,
						      false),
				    X86_64_DESC_FLAGS_GRAN_4K |
					    X86_64_DESC_FLAGS_DEFAULT_BIG);

	return failures;
}

static int test_gdt_entry_helper_sets_base_limit_access_flags(void)
{
	struct x86_64_desc_gdt_entry entry = {0};
	uint8_t access = x86_64_desc_access(true, X86_64_DESC_DPL_USER,
					    true, X86_64_DESC_TYPE_CODE_XR);
	uint8_t flags = x86_64_desc_flags(true, false, true, false);
	int failures = 0;

	x86_64_desc_set_gdt_entry(&entry, 0x12345678u, 0x000abcdeu,
				  access, flags);

	failures += test_expect_u32("base low", entry.base_low, 0x5678);
	failures += test_expect_u32("base middle", entry.base_middle, 0x34);
	failures += test_expect_u32("base high", entry.base_high, 0x12);
	failures += test_expect_u32("limit low", entry.limit_low, 0xbcde);
	failures += test_expect_u32("flags limit", entry.flags_limit,
				    (flags & X86_64_DESC_FLAGS_MASK) | 0x0a);
	failures += test_expect_u32("access", entry.access, access);

	return failures;
}

static int test_tss_entry_helper_sets_64bit_system_descriptor(void)
{
	struct x86_64_desc_tss_entry entry;
	uint64_t base = 0xffff800012345678ull;
	int failures = 0;

	memset(&entry, 0xa5, sizeof(entry));
	x86_64_desc_set_tss_entry(&entry, (uintptr_t)base, 0x0067);

	failures += test_expect_u64("tss base",
				    x86_64_desc_tss_entry_base(&entry), base);
	failures += test_expect_u32("tss limit", entry.limit_low, 0x0067);
	failures += test_expect_u32("tss access", entry.access,
				    X86_64_DESC_ACCESS_PRESENT |
					    X86_64_DESC_TYPE_TSS_AVAILABLE);
	failures += test_expect_u32("tss flags limit", entry.flags_limit, 0);
	failures += test_expect_u32("tss reserved", entry.reserved, 0);
	failures += test_expect_u32("tss descriptor size", sizeof(entry),
				    2 * sizeof(struct x86_64_desc_gdt_entry));

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_selector_helper_encodes_index_and_rpl),
		TEST_CASE(test_access_and_flags_helpers_encode_manual_fields),
		TEST_CASE(test_gdt_entry_helper_sets_base_limit_access_flags),
		TEST_CASE(test_tss_entry_helper_sets_64bit_system_descriptor),
	};

	return test_run_cases("x86_64_descriptor_defs_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
