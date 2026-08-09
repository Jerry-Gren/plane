#include <hal/cpu.h>
#include <hal/irq.h>

#include <stdint.h>

#include "spinlock_stubs.h"

static bool test_irq_enabled = true;
static uint64_t irqsave_count;
static uint64_t irqrestore_count;
static uint64_t irqsave_depth;
static uint64_t irqsave_max_depth;

void test_spinlock_stub_reset_counts(void)
{
	irqsave_count = 0;
	irqrestore_count = 0;
	irqsave_depth = 0;
	irqsave_max_depth = 0;
	test_irq_enabled = true;
}

uint64_t test_spinlock_stub_irqsave_count(void)
{
	return irqsave_count;
}

uint64_t test_spinlock_stub_irqrestore_count(void)
{
	return irqrestore_count;
}

uint64_t test_spinlock_stub_irqsave_depth(void)
{
	return irqsave_depth;
}

uint64_t test_spinlock_stub_irqsave_max_depth(void)
{
	return irqsave_max_depth;
}

void hal_irq_disable(void)
{
	test_irq_enabled = false;
}

void hal_irq_enable(void)
{
	test_irq_enabled = true;
}

bool hal_irq_is_enabled(void)
{
	return test_irq_enabled;
}

plane_irq_state_t hal_irq_save(void)
{
	plane_irq_state_t state = {
		.enabled = test_irq_enabled
	};

	irqsave_count++;
	if (irqsave_depth != UINT64_MAX) {
		irqsave_depth++;
	}
	if (irqsave_depth > irqsave_max_depth) {
		irqsave_max_depth = irqsave_depth;
	}
	hal_irq_disable();
	return state;
}

void hal_irq_restore(plane_irq_state_t state)
{
	irqrestore_count++;
	if (irqsave_depth != 0) {
		irqsave_depth--;
	}
	test_irq_enabled = state.enabled;
}

void hal_cpu_relax(void)
{
}
