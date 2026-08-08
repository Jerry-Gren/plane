#include <hal/cpu.h>
#include <hal/irq.h>

static bool test_irq_enabled = true;

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
