#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <hal/x86_64/descriptor_defs.h>
#include <hal/x86_64/interrupt_defs.h>

#include "support/test.h"

static int test_idt_attribute_helper_encodes_manual_fields(void)
{
	int failures = 0;

	failures += test_expect_u32("kernel interrupt gate",
				    x86_64_intr_idt_attr(
					    true, X86_64_DESC_DPL_KERNEL,
					    X86_64_INTR_GATE_TYPE_INTERRUPT64),
				    X86_64_INTR_GATE_ATTR_PRESENT |
					    X86_64_INTR_GATE_TYPE_INTERRUPT64);
	failures += test_expect_u32("user trap gate",
				    x86_64_intr_idt_attr(
					    true, X86_64_DESC_DPL_USER,
					    X86_64_INTR_GATE_TYPE_TRAP64),
				    X86_64_INTR_GATE_ATTR_PRESENT |
					    (X86_64_DESC_DPL_USER <<
					     X86_64_INTR_GATE_ATTR_DPL_SHIFT) |
					    X86_64_INTR_GATE_TYPE_TRAP64);

	return failures;
}

static int test_idt_entry_helper_sets_offset_selector_ist_and_attrs(void)
{
	struct x86_64_intr_idt_entry entry;
	uintptr_t isr = 0xffffffff81234567ull;
	uint8_t attributes = x86_64_intr_idt_attr(
		true, X86_64_DESC_DPL_KERNEL,
		X86_64_INTR_GATE_TYPE_INTERRUPT64);
	int failures = 0;

	memset(&entry, 0xa5, sizeof(entry));
	x86_64_intr_set_idt_entry(&entry, isr,
				  X86_64_DESC_SELECTOR_KERNEL_CS,
				  0xff, attributes);

	failures += test_expect_u32("offset low", entry.offset_low, 0x4567);
	failures += test_expect_u32("selector", entry.selector,
				    X86_64_DESC_SELECTOR_KERNEL_CS);
	failures += test_expect_u32("ist masked", entry.ist, 0x07);
	failures += test_expect_u32("attributes", entry.attributes,
				    attributes);
	failures += test_expect_u32("offset middle", entry.offset_middle,
				    0x8123);
	failures += test_expect_u32("offset high", entry.offset_high,
				    0xffffffffu);
	failures += test_expect_u32("reserved cleared", entry.reserved, 0);

	return failures;
}

static int test_exception_vector_helper(void)
{
	int failures = 0;

	failures += test_expect_bool("divide is exception",
				     x86_64_intr_vector_is_exception(
					     X86_64_INTR_VECTOR_DIVIDE_ERROR),
				     true);
	failures += test_expect_bool("security is exception",
				     x86_64_intr_vector_is_exception(
					     X86_64_INTR_VECTOR_SECURITY_EXCEPTION),
				     true);
	failures += test_expect_bool("external vector is not exception",
				     x86_64_intr_vector_is_exception(32),
				     false);
	failures += test_expect_bool("external min is external",
				     x86_64_intr_vector_is_external(
					     X86_64_INTR_VECTOR_EXTERNAL_MIN),
				     true);
	failures += test_expect_bool("external max is external",
				     x86_64_intr_vector_is_external(
					     X86_64_INTR_VECTOR_EXTERNAL_MAX),
				     true);
	failures += test_expect_bool("exception is not external",
				     x86_64_intr_vector_is_external(31),
				     false);
	failures += test_expect_bool("past idt is not external",
				     x86_64_intr_vector_is_external(256),
				     false);
	failures += test_expect_u32("external vector count",
				    X86_64_INTR_EXTERNAL_VECTOR_COUNT, 224);

	return failures;
}

static int test_page_fault_error_helpers(void)
{
	uint64_t known = X86_64_INTR_PF_ERROR_PRESENT |
			 X86_64_INTR_PF_ERROR_WRITE |
			 X86_64_INTR_PF_ERROR_USER |
			 X86_64_INTR_PF_ERROR_RSVD |
			 X86_64_INTR_PF_ERROR_EXECUTE;
	int failures = 0;

	failures += test_expect_bool("known pf bits",
				     x86_64_intr_pf_error_is_known(known), true);
	failures += test_expect_bool("unknown pf bit",
				     x86_64_intr_pf_error_is_known(BIT_ULL(5)),
				     false);
	failures += test_expect_bool("kernel read supported",
				     x86_64_intr_pf_error_is_plane_supported(0),
				     true);
	failures += test_expect_bool("kernel write supported",
				     x86_64_intr_pf_error_is_plane_supported(
					     X86_64_INTR_PF_ERROR_WRITE),
				     true);
	failures += test_expect_bool("user fault rejected",
				     x86_64_intr_pf_error_is_plane_supported(
					     X86_64_INTR_PF_ERROR_USER),
				     false);
	failures += test_expect_bool("execute fault rejected",
				     x86_64_intr_pf_error_is_plane_supported(
					     X86_64_INTR_PF_ERROR_EXECUTE),
				     false);
	failures += test_expect_u32("read fault type",
				    x86_64_intr_pf_error_fault_type(0),
				    PLANE_VM_PROT_READ);
	failures += test_expect_u32("write fault type",
				    x86_64_intr_pf_error_fault_type(
					    X86_64_INTR_PF_ERROR_WRITE),
				    PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_idt_attribute_helper_encodes_manual_fields),
		TEST_CASE(test_idt_entry_helper_sets_offset_selector_ist_and_attrs),
		TEST_CASE(test_exception_vector_helper),
		TEST_CASE(test_page_fault_error_helpers),
	};

	return test_run_cases("x86_64_interrupt_defs_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
