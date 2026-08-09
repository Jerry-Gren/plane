#ifndef X86_64_LOCAL_INTERRUPT_H
#define X86_64_LOCAL_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

struct plane_cpu_data;
struct plane_smp_info;

bool ml_local_interrupt_init_bsp(const struct plane_smp_info *info);
bool ml_local_interrupt_init_ap(struct plane_cpu_data *data);
bool ml_local_interrupt_dispatch(uint32_t vector);
bool ml_local_interrupt_end_of_interrupt(void);
bool ml_local_interrupt_send_ipi(uint32_t logical_id, uint8_t vector);

#endif /* X86_64_LOCAL_INTERRUPT_H */
