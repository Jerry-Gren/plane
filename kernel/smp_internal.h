#ifndef KERNEL_SMP_INTERNAL_H
#define KERNEL_SMP_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/compiler.h>
#include <plane/smp.h>

enum plane_smp_event {
	PLANE_SMP_EVENT_AST = 0,
	PLANE_SMP_EVENT_TLB_FLUSH,
	PLANE_SMP_EVENT_COUNT,
};

enum plane_smp_signal_mode {
	PLANE_SMP_SIGNAL_SYNC = 0,
	PLANE_SMP_SIGNAL_ASYNC,
	PLANE_SMP_SIGNAL_NOSYNC,
};

struct plane_cpu_data *plane_cpu_get_startup_data(uint32_t logical_id);
bool plane_smp_cpu_data_is_startable_ap(const struct plane_cpu_data *cpu);
bool plane_smp_signal_cpu(uint32_t logical_id,
			  enum plane_smp_event event,
			  enum plane_smp_signal_mode mode);
uint64_t plane_smp_event_count(enum plane_smp_event event);
bool plane_smp_startup_prepare_ap_stacks(void);
bool plane_smp_mark_ap_starting(uint32_t logical_id);
bool plane_smp_mark_ap_parked(struct plane_cpu_data *data);
bool plane_smp_mark_ap_failed(struct plane_cpu_data *data);
void plane_smp_ap_park_entry(struct plane_cpu_data *data) __noreturn;

#endif /* KERNEL_SMP_INTERNAL_H */
