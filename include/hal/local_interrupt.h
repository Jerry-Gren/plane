#ifndef HAL_LOCAL_INTERRUPT_H
#define HAL_LOCAL_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

struct plane_cpu_data;
struct plane_smp_info;

bool hal_local_interrupt_init_bsp(const struct plane_smp_info *info);
bool hal_local_interrupt_init_ap(struct plane_cpu_data *data);
bool hal_local_interrupt_eoi(void);
bool hal_local_interrupt_send_fixed_ipi(uint32_t logical_id, uint8_t vector);

#endif /* HAL_LOCAL_INTERRUPT_H */
