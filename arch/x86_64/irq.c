#include <machine/machine_routines.h>
#include <x86_64/processor_defs.h>

#include <stdint.h>

static uint64_t x86_64_read_rflags(void)
{
	uint64_t rflags;

	__asm__ volatile ("pushfq; popq %0" : "=r"(rflags) :: "memory");
	return rflags;
}

void ml_interrupts_disable(void)
{
	__asm__ volatile ("cli" ::: "memory");
}

void ml_interrupts_enable(void)
{
	__asm__ volatile ("sti" ::: "memory");
}

bool ml_get_interrupts_enabled(void)
{
	return (x86_64_read_rflags() & X86_64_RFLAGS_IF) != 0;
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
