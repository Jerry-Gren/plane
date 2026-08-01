#ifndef PLANE_SMP_H
#define PLANE_SMP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/compiler.h>

#define PLANE_MAX_CPUS 64

/*
 * Early SMP foundation records topology and installs BSP CPU data into the
 * arch current-data slot. x86_64 APs may be started only far enough to install
 * per-CPU descriptors/current data and local APIC state before parking in a
 * halt loop; PMM/VM/pmap/kmem remain BSP-only. Plane does not expose a
 * GS-relative accessor, IPI dispatch, TLB shootdown, or scheduling yet.
 */
enum plane_cpu_boot_state {
	PLANE_CPU_BOOT_OFFLINE = 0,
	PLANE_CPU_BOOT_PREPARED,
	PLANE_CPU_BOOT_STARTING,
	PLANE_CPU_BOOT_PARKED,
	PLANE_CPU_BOOT_FAILED,
};

struct plane_cpu_info {
	uint32_t logical_id;
	uint32_t lapic_id;
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
	uint32_t lapic_id;
	bool is_bsp;
	bool present;
	bool online;
	plane_vaddr_t ap_stack_base;
	plane_vaddr_t ap_stack_top;
	uint64_t ap_stack_pages;
	uint32_t boot_state;
};

bool plane_smp_info_init(struct plane_smp_info *info);
bool plane_smp_info_init_bsp(struct plane_smp_info *info, uint32_t lapic_id);
bool plane_smp_info_record_cpu(struct plane_smp_info *info,
			       uint32_t lapic_id,
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
const struct plane_cpu_data *plane_cpu_current_data(void);
const struct plane_cpu_data *plane_cpu_data_get(uint32_t logical_id);
enum plane_cpu_boot_state plane_cpu_boot_state(uint32_t logical_id);

#endif /* PLANE_SMP_H */
