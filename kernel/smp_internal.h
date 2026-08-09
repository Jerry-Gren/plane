#ifndef KERNEL_SMP_INTERNAL_H
#define KERNEL_SMP_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/compiler.h>
#include <plane/smp.h>

struct plane_cpu_data *plane_cpu_get_startup_data(uint32_t logical_id);
bool plane_smp_cpu_data_is_startable_ap(const struct plane_cpu_data *cpu);
bool plane_smp_startup_prepare_ap_stacks(void);
bool plane_smp_mark_ap_starting(uint32_t logical_id);
bool plane_smp_mark_ap_parked(struct plane_cpu_data *data);
bool plane_smp_mark_ap_failed(struct plane_cpu_data *data);
void plane_smp_ap_park_entry(struct plane_cpu_data *data) __noreturn;

#endif /* KERNEL_SMP_INTERNAL_H */
