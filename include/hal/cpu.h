#ifndef HAL_CPU_H
#define HAL_CPU_H

#include <stdbool.h>

#include <plane/compiler.h>
#include <plane/address.h>

struct plane_cpu_data;

void hal_cpu_hang(void) __noreturn;
void hal_cpu_relax(void);
bool hal_cpu_set_current_data(struct plane_cpu_data *data);
bool hal_cpu_prepare_ap_startup_context(struct plane_cpu_data *data);
bool hal_cpu_install_ap_startup_context(struct plane_cpu_data *data);
void hal_cpu_enter_on_stack(plane_vaddr_t stack_top,
			    void (*entry)(struct plane_cpu_data *data),
			    struct plane_cpu_data *data) __noreturn;

#endif /* HAL_CPU_H */
