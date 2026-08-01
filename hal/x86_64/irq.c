#include <hal/irq.h>

#include <stdint.h>

#define X86_64_RFLAGS_IF (1ull << 9)

static uint64_t x86_64_read_rflags(void)
{
	uint64_t rflags;

	__asm__ volatile ("pushfq; popq %0" : "=r"(rflags) :: "memory");
	return rflags;
}

void hal_irq_disable(void)
{
	__asm__ volatile ("cli" ::: "memory");
}

void hal_irq_enable(void)
{
	__asm__ volatile ("sti" ::: "memory");
}

bool hal_irq_enabled(void)
{
	return (x86_64_read_rflags() & X86_64_RFLAGS_IF) != 0;
}

plane_irq_state_t hal_irq_save(void)
{
	plane_irq_state_t state = {
		.enabled = hal_irq_enabled()
	};

	hal_irq_disable();
	return state;
}

void hal_irq_restore(plane_irq_state_t state)
{
	if (state.enabled) {
		hal_irq_enable();
	} else {
		hal_irq_disable();
	}
}
