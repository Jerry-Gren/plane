#ifndef HAL_CPU_H
#define HAL_CPU_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/compiler.h>
#include <plane/address.h>

struct plane_smp_info;
struct plane_cpu_data;

void hal_cpu_hang(void) __noreturn;
void hal_cpu_relax(void);
bool hal_cpu_set_current_data(struct plane_cpu_data *data);
bool hal_cpu_prepare_ap_startup_context(struct plane_cpu_data *data);
bool hal_cpu_install_ap_startup_context(struct plane_cpu_data *data);
bool hal_cpu_init_bsp_local_interrupts(const struct plane_smp_info *info);
bool hal_cpu_init_ap_local_interrupts(struct plane_cpu_data *data);
bool hal_cpu_local_eoi(void);
bool hal_cpu_send_fixed_ipi(uint32_t logical_id, uint8_t vector);
void hal_cpu_enter_on_stack(plane_vaddr_t stack_top,
			    void (*entry)(struct plane_cpu_data *data),
			    struct plane_cpu_data *data) __noreturn;

#endif /* HAL_CPU_H */
