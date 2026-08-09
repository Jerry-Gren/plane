#ifndef X86_64_MACHINE_ROUTINES_H
#define X86_64_MACHINE_ROUTINES_H

#include <stdbool.h>

#include <plane/address.h>
#include <plane/compiler.h>

struct plane_cpu_data;

typedef struct {
	bool enabled;
} plane_irq_state_t;

bool ml_startup_init(void);

void ml_cpu_halt(void) __noreturn;
void cpu_pause(void);
bool ml_cpu_set_current_data(struct plane_cpu_data *data);
bool ml_cpu_prepare_ap_startup_context(struct plane_cpu_data *data);
bool ml_cpu_install_ap_startup_context(struct plane_cpu_data *data);
void ml_cpu_enter_on_stack(plane_vaddr_t stack_top,
			   void (*entry)(struct plane_cpu_data *data),
			   struct plane_cpu_data *data) __noreturn;

void ml_interrupts_disable(void);
void ml_interrupts_enable(void);
bool ml_get_interrupts_enabled(void);
plane_irq_state_t ml_irq_save(void);
void ml_irq_restore(plane_irq_state_t state);

#endif /* X86_64_MACHINE_ROUTINES_H */
