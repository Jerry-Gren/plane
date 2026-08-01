#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/cpu.h>
#include <hal/irq.h>
#include <hal/serial.h>
#include <plane/printk.h>

#include "support/test.h"

#define OUTPUT_SIZE 256

static char output[OUTPUT_SIZE];
static size_t output_len;
static bool test_irq_enabled;
static bool panic_on_next_serial_putchar;
static bool panic_reentered;
static jmp_buf panic_env;

static void reset_output(void)
{
	output_len = 0;
	output[0] = '\0';
	test_irq_enabled = true;
	panic_on_next_serial_putchar = false;
	panic_reentered = false;
}

void hal_serial_init(void)
{
}

void hal_serial_putchar(char c)
{
	if (panic_on_next_serial_putchar && !panic_reentered) {
		panic_reentered = true;
		panic_on_next_serial_putchar = false;
		panic("nested");
	}

	if (output_len + 1 < OUTPUT_SIZE) {
		output[output_len++] = c;
		output[output_len] = '\0';
	}
}

void hal_irq_disable(void)
{
	test_irq_enabled = false;
}

void hal_irq_enable(void)
{
	test_irq_enabled = true;
}

bool hal_irq_enabled(void)
{
	return test_irq_enabled;
}

plane_irq_state_t hal_irq_save(void)
{
	plane_irq_state_t state = {
		.enabled = test_irq_enabled
	};

	hal_irq_disable();
	return state;
}

void hal_irq_restore(plane_irq_state_t state)
{
	test_irq_enabled = state.enabled;
}

void hal_cpu_relax(void)
{
}

void hal_cpu_hang(void)
{
	longjmp(panic_env, 1);
	__builtin_unreachable();
}

static int test_printk_formats_and_restores_irq(void)
{
	int failures = 0;

	reset_output();
	printk("value=%d", 42);
	failures += test_expect_str("printk output", output, "value=42");
	failures += test_expect_bool("irq restored", test_irq_enabled, true);
	return failures;
}

static int test_panic_outputs_prefix_and_leaves_irq_disabled(void)
{
	int failures = 0;

	reset_output();
	if (setjmp(panic_env) == 0) {
		panic("boom %d", 7);
		return 1;
	}

	failures += test_expect_str("panic output",
				    output, "[PANIC] boom 7\n");
	failures += test_expect_bool("panic leaves irq disabled",
				     test_irq_enabled, false);
	return failures;
}

static int test_panic_does_not_recurse_on_printk_lock(void)
{
	int failures = 0;

	reset_output();
	panic_on_next_serial_putchar = true;
	if (setjmp(panic_env) == 0) {
		printk("outer");
		return 1;
	}

	failures += test_expect_str("nested panic output",
				    output, "[PANIC] nested\n");
	failures += test_expect_bool("nested panic happened",
				     panic_reentered, true);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_printk_formats_and_restores_irq),
		TEST_CASE(test_panic_outputs_prefix_and_leaves_irq_disabled),
		TEST_CASE(test_panic_does_not_recurse_on_printk_lock),
	};

	return test_run_cases("printk_runtime_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
