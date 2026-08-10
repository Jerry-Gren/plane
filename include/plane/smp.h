#ifndef PLANE_SMP_H
#define PLANE_SMP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/compiler.h>

#define PLANE_MAX_CPUS 64

/*
 * Initial SMP foundation records topology and installs BSP CPU data into the
 * arch current-data slot. APs may be started only far enough to install
 * per-CPU architecture context, current data, and CPU interrupt state before
 * parking in a halt loop. Plane has a minimal CPU signal dispatch scaffold for
 * architecture CPU-interrupt delivery, but APs remain parked/offline and are
 * not running TLB-flush targets. TLB-flush events enter pmap's CPU-local
 * invalid state path. Plane does not expose a CPU-local fast accessor, range
 * shootdown rendezvous, scheduling, or general AP execution yet.
 */
enum plane_cpu_startup_state {
	PLANE_CPU_STARTUP_OFFLINE = 0,
	PLANE_CPU_STARTUP_PREPARED,
	PLANE_CPU_STARTUP_STARTING,
	PLANE_CPU_STARTUP_PARKED,
	PLANE_CPU_STARTUP_FAILED,
};

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

struct plane_cpu_info {
	uint32_t logical_id;
	uint32_t physical_id;
	bool is_bsp;
	bool present;
	bool online;
};

struct plane_smp_info {
	uint32_t cpu_count;
	uint32_t discovered_cpu_count;
	uint32_t bsp_logical_id;
	bool truncated;
	struct plane_cpu_info cpus[PLANE_MAX_CPUS];
};

struct plane_cpu_data {
	struct plane_cpu_data *self;
	uint32_t logical_id;
	uint32_t physical_id;
	bool is_bsp;
	bool present;
	bool online;
	plane_vaddr_t ap_stack_base;
	plane_vaddr_t ap_stack_top;
	uint64_t ap_stack_pages;
	uint32_t startup_state;
	uint32_t cpu_signals;
	/* Pmap-owned aggregate CPU-local TLB invalid bitmask; not an SMP event bit. */
	uint32_t cpu_tlb_invalid;
};

struct plane_smp_ap_launch {
	struct plane_cpu_data *argument;
};

bool plane_smp_info_init(struct plane_smp_info *info);
bool plane_smp_info_init_bsp(struct plane_smp_info *info, uint32_t physical_id);
bool plane_smp_info_record_cpu(struct plane_smp_info *info,
			       uint32_t physical_id,
			       bool is_bsp);

bool plane_smp_init_bsp(const struct plane_smp_info *info);
bool plane_smp_is_initialized(void);
bool plane_smp_prepare_ap_stack(uint32_t logical_id,
				plane_vaddr_t stack_base,
				uint64_t stack_pages);
uint32_t plane_cpu_parked_count(void);
uint32_t plane_cpu_count(void);
uint32_t plane_cpu_current_id(void);
bool plane_cpu_is_bsp(void);
bool plane_cpu_is_running(uint32_t logical_id);
const struct plane_cpu_data *plane_cpu_current_data(void);
const struct plane_cpu_data *plane_cpu_get_data(uint32_t logical_id);
enum plane_cpu_startup_state plane_cpu_startup_state(uint32_t logical_id);
bool plane_cpu_mark_tlb_invalid_local(uint32_t logical_id, bool *was_invalid);
bool plane_cpu_mark_tlb_invalid_global(uint32_t logical_id, bool *was_invalid);
bool plane_cpu_clear_tlb_invalid(uint32_t logical_id);
bool plane_cpu_tlb_is_invalid(uint32_t logical_id);
uint32_t plane_cpu_tlb_invalid_snapshot(uint32_t logical_id);
bool plane_smp_signal_cpu(uint32_t logical_id,
			  enum plane_smp_event event,
			  enum plane_smp_signal_mode mode);
bool plane_smp_signal_handler(void);

/*
 * Bootloader-specific AP launch code uses these helpers to publish AP start
 * requests without reaching into the kernel SMP state machine internals.
 */
bool plane_smp_startup_ap_is_launchable(uint32_t logical_id);
bool plane_smp_startup_prepare_ap_launch(uint32_t logical_id,
				      struct plane_smp_ap_launch *launch);
void plane_smp_startup_enter_ap(struct plane_cpu_data *data) __noreturn;

#endif /* PLANE_SMP_H */
