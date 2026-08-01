#include <stddef.h>

#include <plane/smp.h>

static struct plane_smp_info smp_state;
static bool smp_initialized;

bool plane_smp_info_init(struct plane_smp_info *info)
{
	if (info == NULL) {
		return false;
	}

	*info = (struct plane_smp_info){
		.bsp_logical_id = PLANE_CPU_INVALID_ID
	};
	return true;
}

bool plane_smp_info_record_cpu(struct plane_smp_info *info,
			       uint32_t lapic_id,
			       bool is_bsp)
{
	uint32_t logical_id;

	if (info == NULL) {
		return false;
	}

	info->discovered_cpu_count++;
	if (is_bsp && info->bsp_logical_id != PLANE_CPU_INVALID_ID) {
		return false;
	}

	for (uint32_t i = 0; i < info->cpu_count; i++) {
		if (info->cpus[i].lapic_id == lapic_id) {
			return false;
		}
	}

	if (info->cpu_count >= PLANE_MAX_CPUS) {
		info->truncated = true;
		return false;
	}

	logical_id = info->cpu_count;
	info->cpus[logical_id] = (struct plane_cpu_info){
		.logical_id = logical_id,
		.lapic_id = lapic_id,
		.is_bsp = is_bsp,
		.present = true,
		.online = false
	};
	info->cpu_count++;

	if (is_bsp) {
		info->bsp_logical_id = logical_id;
	}

	return true;
}

bool plane_smp_info_init_bsp(struct plane_smp_info *info, uint32_t lapic_id)
{
	return plane_smp_info_init(info) &&
	       plane_smp_info_record_cpu(info, lapic_id, true);
}

static bool smp_info_validate(const struct plane_smp_info *info)
{
	uint32_t bsp_count = 0;

	if (info == NULL || info->cpu_count == 0 ||
	    info->cpu_count > PLANE_MAX_CPUS ||
	    info->bsp_logical_id != 0) {
		return false;
	}

	for (uint32_t i = 0; i < info->cpu_count; i++) {
		const struct plane_cpu_info *cpu = &info->cpus[i];

		if (!cpu->present || cpu->logical_id != i) {
			return false;
		}

		if (cpu->is_bsp) {
			bsp_count++;
			if (info->bsp_logical_id != i) {
				return false;
			}
		}
	}

	return bsp_count == 1;
}

bool plane_smp_init_bsp(const struct plane_smp_info *info)
{
	if (smp_initialized || !smp_info_validate(info)) {
		return false;
	}

	smp_state = *info;
	for (uint32_t i = 0; i < smp_state.cpu_count; i++) {
		smp_state.cpus[i].online = false;
	}
	smp_state.cpus[smp_state.bsp_logical_id].online = true;
	smp_initialized = true;
	return true;
}

bool plane_smp_is_initialized(void)
{
	return smp_initialized;
}

uint32_t plane_cpu_count(void)
{
	return smp_initialized ? smp_state.cpu_count : 1;
}

uint32_t plane_cpu_current_id(void)
{
	return 0;
}

bool plane_cpu_is_bsp(void)
{
	return plane_cpu_current_id() == 0;
}

const struct plane_cpu_info *plane_cpu_get(uint32_t logical_id)
{
	if (!smp_initialized || logical_id >= smp_state.cpu_count) {
		return NULL;
	}

	return &smp_state.cpus[logical_id];
}
