#include <stddef.h>

#include <hal/cpu.h>

#include <plane/smp.h>

#define CPU_INVALID_ID UINT32_MAX

static struct plane_cpu_data cpu_data[PLANE_MAX_CPUS];
static struct plane_cpu_data *current_cpu_data;
static uint32_t runtime_cpu_count;
static bool smp_initialized;

bool plane_smp_info_init(struct plane_smp_info *info)
{
	if (info == NULL) {
		return false;
	}

	*info = (struct plane_smp_info){
		.bsp_logical_id = CPU_INVALID_ID
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
	if (is_bsp && info->bsp_logical_id != CPU_INVALID_ID) {
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

		for (uint32_t j = i + 1; j < info->cpu_count; j++) {
			if (info->cpus[j].present &&
			    info->cpus[j].lapic_id == cpu->lapic_id) {
				return false;
			}
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

	for (uint32_t i = 0; i < info->cpu_count; i++) {
		const struct plane_cpu_info *src = &info->cpus[i];

		cpu_data[i] = (struct plane_cpu_data){
			.self = &cpu_data[i],
			.logical_id = src->logical_id,
			.lapic_id = src->lapic_id,
			.is_bsp = src->is_bsp,
			.present = src->present,
			.online = false
		};
	}

	runtime_cpu_count = info->cpu_count;
	current_cpu_data = &cpu_data[info->bsp_logical_id];
	if (!hal_cpu_set_current_data(current_cpu_data)) {
		current_cpu_data = NULL;
		runtime_cpu_count = 0;
		return false;
	}

	current_cpu_data->online = true;
	smp_initialized = true;
	return true;
}

bool plane_smp_is_initialized(void)
{
	return smp_initialized;
}

uint32_t plane_cpu_count(void)
{
	return smp_initialized ? runtime_cpu_count : 1;
}

uint32_t plane_cpu_current_id(void)
{
	return current_cpu_data != NULL ? current_cpu_data->logical_id : 0;
}

bool plane_cpu_is_bsp(void)
{
	return current_cpu_data != NULL && current_cpu_data->is_bsp;
}

const struct plane_cpu_data *plane_cpu_current_data(void)
{
	return current_cpu_data;
}

const struct plane_cpu_data *plane_cpu_data_get(uint32_t logical_id)
{
	if (!smp_initialized || logical_id >= runtime_cpu_count) {
		return NULL;
	}

	return &cpu_data[logical_id];
}
