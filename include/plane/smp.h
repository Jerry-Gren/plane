#ifndef PLANE_SMP_H
#define PLANE_SMP_H

#include <stdbool.h>
#include <stdint.h>

#define PLANE_MAX_CPUS 64

/*
 * Early SMP foundation records topology and installs BSP CPU data. APs are
 * not started yet; PMM/VM/pmap/kmem remain BSP-only.
 */
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
	uint32_t logical_id;
	uint32_t lapic_id;
	bool is_bsp;
	bool present;
	bool online;
};

bool plane_smp_info_init(struct plane_smp_info *info);
bool plane_smp_info_init_bsp(struct plane_smp_info *info, uint32_t lapic_id);
bool plane_smp_info_record_cpu(struct plane_smp_info *info,
			       uint32_t lapic_id,
			       bool is_bsp);

bool plane_smp_init_bsp(const struct plane_smp_info *info);
bool plane_smp_is_initialized(void);
uint32_t plane_cpu_count(void);
uint32_t plane_cpu_current_id(void);
bool plane_cpu_is_bsp(void);
const struct plane_cpu_data *plane_cpu_current_data(void);
const struct plane_cpu_data *plane_cpu_data_get(uint32_t logical_id);

#endif /* PLANE_SMP_H */
