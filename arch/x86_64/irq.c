#include <machine/machine_routines.h>
#include <x86_64/proc_reg.h>

#include <stdint.h>

void ml_interrupts_disable(void)
{
	cli();
}

void ml_interrupts_enable(void)
{
	sti();
}

bool ml_get_interrupts_enabled(void)
{
	return (read_rflags() & X86_64_RFLAGS_IF) != 0;
}

plane_irq_state_t ml_irq_save(void)
{
	plane_irq_state_t state = {
		.enabled = ml_get_interrupts_enabled()
	};

	ml_interrupts_disable();
	return state;
}

void ml_irq_restore(plane_irq_state_t state)
{
	if (state.enabled) {
		ml_interrupts_enable();
	} else {
		ml_interrupts_disable();
	}
}
