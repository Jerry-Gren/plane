#include <stdbool.h>
#include <stdint.h>

#include <plane/kmem.h>
#include <plane/vm_prot.h>
#include <x86_64/lapic.h>
#include <x86_64/trap.h>

#include "support/test.h"

char __kernel_text_start[1];
char __kernel_text_end[1];

static bool kmem_fault_result;
static uint64_t kmem_fault_calls;
static plane_vaddr_t last_fault_addr;
static uint32_t last_fault_type;
static bool cpu_interrupt_dispatch_result;
static uint32_t cpu_interrupt_dispatch_calls;
static uint32_t last_cpu_interrupt_vector;

static void reset_trap_test(void)
{
	kmem_fault_result = true;
	kmem_fault_calls = 0;
	last_fault_addr = plane_vaddr_make(0);
	last_fault_type = 0;
	cpu_interrupt_dispatch_result = true;
	cpu_interrupt_dispatch_calls = 0;
	last_cpu_interrupt_vector = 0;
}

bool plane_kmem_fault_page(plane_vaddr_t vaddr, uint32_t fault_type)
{
	kmem_fault_calls++;
	last_fault_addr = vaddr;
	last_fault_type = fault_type;
	return kmem_fault_result;
}

bool lapic_interrupt(uint32_t vector)
{
	cpu_interrupt_dispatch_calls++;
	last_cpu_interrupt_vector = vector;
	return cpu_interrupt_dispatch_result;
}

static bool test_trap_try_handle_page_fault(uint64_t int_no,
					    uint64_t fault_addr,
					    uint64_t error_code)
{
	return x86_64_trap_try_handle_page_fault(
		int_no, plane_vaddr_make(fault_addr), error_code);
}

static int test_kernel_read_fault_enters_kmem_fault(void)
{
	uint64_t addr = 0xffff900000123456ull;
	int failures = 0;

	failures += test_expect_bool("read fault handled",
				     test_trap_try_handle_page_fault(
					     X86_64_INTR_VECTOR_PAGE_FAULT,
					     addr, 0),
				     true);
	failures += test_expect_u64("read fault calls",
				    kmem_fault_calls, 1);
	failures += test_expect_u64("read fault addr",
				    plane_vaddr_raw(last_fault_addr), addr);
	failures += test_expect_u32("read fault type",
				    last_fault_type,
				    PLANE_VM_PROT_READ);
	return failures;
}

static int test_kernel_write_fault_adds_write_protection(void)
{
	uint64_t addr = 0xffff900000abcdefull;
	int failures = 0;

	failures += test_expect_bool("write fault handled",
				     test_trap_try_handle_page_fault(
					     X86_64_INTR_VECTOR_PAGE_FAULT,
					     addr,
					     X86_64_INTR_PF_ERROR_WRITE),
				     true);
	failures += test_expect_u64("write fault calls",
				    kmem_fault_calls, 1);
	failures += test_expect_u64("write fault addr",
				    plane_vaddr_raw(last_fault_addr), addr);
	failures += test_expect_u32("write fault type",
				    last_fault_type,
				    PLANE_VM_PROT_READ |
					    PLANE_VM_PROT_WRITE);
	return failures;
}

static int test_kmem_fault_failure_is_not_swallowed(void)
{
	uint64_t addr = 0xffff900000001000ull;
	int failures = 0;

	kmem_fault_result = false;
	failures += test_expect_bool("kmem failure",
				     test_trap_try_handle_page_fault(
					     X86_64_INTR_VECTOR_PAGE_FAULT,
					     addr, 0),
				     false);
	failures += test_expect_u64("kmem failure calls",
				    kmem_fault_calls, 1);
	failures += test_expect_u64("kmem failure addr",
				    plane_vaddr_raw(last_fault_addr), addr);
	return failures;
}

static int test_unsupported_page_faults_are_rejected(void)
{
	static const uint64_t unsupported_errors[] = {
		X86_64_INTR_PF_ERROR_USER,
		X86_64_INTR_PF_ERROR_RSVD,
		X86_64_INTR_PF_ERROR_EXECUTE,
		0x20,
	};
	int failures = 0;

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(unsupported_errors); i++) {
		reset_trap_test();
		failures += test_expect_bool("unsupported fault",
					     test_trap_try_handle_page_fault(
						     X86_64_INTR_VECTOR_PAGE_FAULT,
						     0xffff900000002000ull,
						     unsupported_errors[i]),
					     false);
		failures += test_expect_u64("unsupported no calls",
					    kmem_fault_calls, 0);
	}

	return failures;
}

static int test_non_page_fault_vector_is_ignored(void)
{
	int failures = 0;

	failures += test_expect_bool("non pf ignored",
				     test_trap_try_handle_page_fault(
					     13, 0xffff900000003000ull, 0),
				     false);
	failures += test_expect_u64("non pf no calls", kmem_fault_calls, 0);
	return failures;
}

static int test_external_interrupt_dispatches_cpu_interrupt(void)
{
	struct x86_64_intr_frame frame = {
		.int_no = 33,
	};
	int failures = 0;

	x86_64_trap_handler(&frame);

	failures += test_expect_u32("external dispatch calls",
				    cpu_interrupt_dispatch_calls, 1);
	failures += test_expect_u32("external dispatch vector",
				    last_cpu_interrupt_vector, 33);
	failures += test_expect_u64("external no kmem fault",
				    kmem_fault_calls, 0);
	return failures;
}

static int test_external_interrupt_dispatch_failure_is_not_panic(void)
{
	struct x86_64_intr_frame frame = {
		.int_no = X86_64_INTR_VECTOR_EXTERNAL_MAX,
	};
	int failures = 0;

	cpu_interrupt_dispatch_result = false;
	x86_64_trap_handler(&frame);

	failures += test_expect_u32("failed dispatch calls",
				    cpu_interrupt_dispatch_calls, 1);
	failures += test_expect_u32("failed dispatch vector",
				    last_cpu_interrupt_vector,
				    X86_64_INTR_VECTOR_EXTERNAL_MAX);
	failures += test_expect_u64("failed dispatch no kmem fault",
				    kmem_fault_calls, 0);
	return failures;
}

static int test_invalid_non_exception_vector_is_ignored(void)
{
	struct x86_64_intr_frame frame = {
		.int_no = X86_64_INTR_VECTOR_EXTERNAL_MAX + 1,
	};
	int failures = 0;

	x86_64_trap_handler(&frame);

	failures += test_expect_u32("invalid vector no dispatch",
				    cpu_interrupt_dispatch_calls, 0);
	failures += test_expect_u64("invalid vector no kmem fault",
				    kmem_fault_calls, 0);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_kernel_read_fault_enters_kmem_fault),
		TEST_CASE(test_kernel_write_fault_adds_write_protection),
		TEST_CASE(test_kmem_fault_failure_is_not_swallowed),
		TEST_CASE(test_unsupported_page_faults_are_rejected),
		TEST_CASE(test_non_page_fault_vector_is_ignored),
		TEST_CASE(test_external_interrupt_dispatches_cpu_interrupt),
		TEST_CASE(test_external_interrupt_dispatch_failure_is_not_panic),
		TEST_CASE(test_invalid_non_exception_vector_is_ignored),
	};

	return test_run_cases_with_fixture("x86_64_trap_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_trap_test,
					   NULL);
}
