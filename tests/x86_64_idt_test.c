#include <stdint.h>
#include <string.h>

#include <x86_64/descriptor_defs.h>
#include <x86_64/interrupt_defs.h>

#include "support/test.h"

static uint64_t last_idtr;

void x86_64_idt_flush(uint64_t idtr_addr)
{
	last_idtr = idtr_addr;
}

#define DEF_ISR(n) void x86_64_isr##n(void) {}
DEF_ISR(0)  DEF_ISR(1)  DEF_ISR(2)  DEF_ISR(3)
DEF_ISR(4)  DEF_ISR(5)  DEF_ISR(6)  DEF_ISR(7)
DEF_ISR(8)  DEF_ISR(9)  DEF_ISR(10) DEF_ISR(11)
DEF_ISR(12) DEF_ISR(13) DEF_ISR(14) DEF_ISR(15)
DEF_ISR(16) DEF_ISR(17) DEF_ISR(18) DEF_ISR(19)
DEF_ISR(20) DEF_ISR(21) DEF_ISR(22) DEF_ISR(23)
DEF_ISR(24) DEF_ISR(25) DEF_ISR(26) DEF_ISR(27)
DEF_ISR(28) DEF_ISR(29) DEF_ISR(30) DEF_ISR(31)

static void external_stub_default(void)
{
}

static void external_stub_33(void)
{
}

static void external_stub_240(void)
{
}

void (*x86_64_isr_external_stub_table[
	X86_64_INTR_EXTERNAL_VECTOR_COUNT])(void);

#include <x86_64/idt.c>

static uintptr_t idt_entry_isr(const struct x86_64_intr_idt_entry *entry)
{
	return (uintptr_t)entry->offset_low |
	       ((uintptr_t)entry->offset_middle << 16) |
	       ((uintptr_t)entry->offset_high << 32);
}

static void reset_idt_test(void)
{
	memset(idt, 0, sizeof(idt));
	memset(&idtr, 0, sizeof(idtr));
	for (uint32_t i = 0; i < X86_64_INTR_EXTERNAL_VECTOR_COUNT; i++) {
		x86_64_isr_external_stub_table[i] = external_stub_default;
	}
	x86_64_isr_external_stub_table[
		33 - X86_64_INTR_VECTOR_EXTERNAL_MIN] = external_stub_33;
	x86_64_isr_external_stub_table[
		240 - X86_64_INTR_VECTOR_EXTERNAL_MIN] = external_stub_240;
	last_idtr = 0;
}

static int test_static_external_gate_uses_vector_stub(void)
{
	int failures = 0;
	uint8_t attributes = x86_64_intr_idt_attr(
		true, X86_64_DESC_DPL_KERNEL,
		X86_64_INTR_GATE_TYPE_INTERRUPT64);

	reset_idt_test();
	idt_set_descriptor(33,
			   (uintptr_t)x86_64_isr_external_stub_table[
				   33 - X86_64_INTR_VECTOR_EXTERNAL_MIN],
			   attributes);
	idt_set_descriptor(240,
			   (uintptr_t)x86_64_isr_external_stub_table[
				   240 - X86_64_INTR_VECTOR_EXTERNAL_MIN],
			   attributes);

	failures += test_expect_u64("external 33 gate isr",
				    idt_entry_isr(&idt[33]),
				    (uintptr_t)external_stub_33);
	failures += test_expect_u64("external 240 gate isr",
				    idt_entry_isr(&idt[240]),
				    (uintptr_t)external_stub_240);
	failures += test_expect_u32("external gate selector",
				    idt[33].selector,
				    X86_64_DESC_SELECTOR_KERNEL_CS);
	failures += test_expect_u32("external gate ist", idt[33].ist, 0);
	failures += test_expect_u32("external gate attributes",
				    idt[33].attributes, attributes);
	failures += test_expect_u32("external gate reserved",
				    idt[33].reserved, 0);

	return failures;
}

static int test_static_exception_gate_uses_exception_stub(void)
{
	int failures = 0;
	uint8_t attributes = x86_64_intr_idt_attr(
		true, X86_64_DESC_DPL_KERNEL,
		X86_64_INTR_GATE_TYPE_INTERRUPT64);

	reset_idt_test();
	idt_set_descriptor(X86_64_INTR_VECTOR_PAGE_FAULT,
			   (uintptr_t)x86_64_isr14, attributes);

	failures += test_expect_u64("exception gate isr",
				    idt_entry_isr(&idt[
					    X86_64_INTR_VECTOR_PAGE_FAULT]),
				    (uintptr_t)x86_64_isr14);
	failures += test_expect_u32("exception gate selector",
				    idt[X86_64_INTR_VECTOR_PAGE_FAULT].selector,
				    X86_64_DESC_SELECTOR_KERNEL_CS);
	failures += test_expect_u32("exception gate ist",
				    idt[X86_64_INTR_VECTOR_PAGE_FAULT].ist, 0);
	failures += test_expect_u32("exception gate attributes",
				    idt[X86_64_INTR_VECTOR_PAGE_FAULT].attributes,
				    attributes);

	return failures;
}

static int test_load_current_uses_shared_idtr(void)
{
	int failures = 0;

	reset_idt_test();
	x86_64_idt_load_current();

	failures += test_expect_u64("loaded idtr", last_idtr,
				    (uint64_t)(uintptr_t)&idtr);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_static_external_gate_uses_vector_stub),
		TEST_CASE(test_static_exception_gate_uses_exception_stub),
		TEST_CASE(test_load_current_uses_shared_idtr),
	};

	return test_run_cases("x86_64_idt_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
