#include <stddef.h>

#include <hal/cpu.h>
#include <hal/irq.h>
#include <hal/local_interrupt.h>

#include <plane/atomic.h>
#include <plane/smp.h>

#include "smp_internal.h"

#define CPU_INVALID_ID UINT32_MAX

static struct plane_cpu_data cpu_data[PLANE_MAX_CPUS];
static struct plane_cpu_data *current_cpu_data;
static uint32_t runtime_cpu_count;
static bool smp_initialized;
static uint64_t ipi_reschedule_count;
static uint64_t ipi_pmap_update_count;

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
			       uint32_t physical_id,
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
		if (info->cpus[i].physical_id == physical_id) {
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
		.physical_id = physical_id,
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

bool plane_smp_info_init_bsp(struct plane_smp_info *info, uint32_t physical_id)
{
	return plane_smp_info_init(info) &&
	       plane_smp_info_record_cpu(info, physical_id, true);
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
			    info->cpus[j].physical_id == cpu->physical_id) {
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
			.physical_id = src->physical_id,
			.is_bsp = src->is_bsp,
			.present = src->present,
			.online = false,
			.startup_state = PLANE_CPU_STARTUP_OFFLINE
		};
	}

	runtime_cpu_count = info->cpu_count;
	current_cpu_data = &cpu_data[info->bsp_logical_id];
	if (!hal_cpu_set_current_data(current_cpu_data)) {
		current_cpu_data = NULL;
		runtime_cpu_count = 0;
		return false;
	}

	ipi_reschedule_count = 0;
	ipi_pmap_update_count = 0;
	current_cpu_data->online = true;
	smp_initialized = true;
	return true;
}

bool plane_smp_cpu_data_is_startable_ap(const struct plane_cpu_data *cpu)
{
	return cpu != NULL && !cpu->is_bsp && cpu->present;
}

bool plane_smp_prepare_ap_stack(uint32_t logical_id,
				plane_vaddr_t stack_base,
				uint64_t stack_pages)
{
	struct plane_cpu_data *cpu = plane_cpu_get_startup_data(logical_id);
	plane_vaddr_t stack_top;

	if (!plane_smp_cpu_data_is_startable_ap(cpu) || stack_pages == 0 ||
	    plane_vaddr_is_null(stack_base) ||
	    !plane_vaddr_is_page_aligned(stack_base) ||
	    !plane_vaddr_add_pages(stack_base, stack_pages, &stack_top)) {
		return false;
	}

	if (plane_atomic_load_u32(&cpu->startup_state) !=
	    PLANE_CPU_STARTUP_OFFLINE) {
		return false;
	}

	cpu->ap_stack_base = stack_base;
	cpu->ap_stack_top = stack_top;
	cpu->ap_stack_pages = stack_pages;
	if (!hal_cpu_prepare_ap_startup_context(cpu)) {
		cpu->ap_stack_base = plane_vaddr_make(0);
		cpu->ap_stack_top = plane_vaddr_make(0);
		cpu->ap_stack_pages = 0;
		return false;
	}

	plane_atomic_store_u32(&cpu->startup_state, PLANE_CPU_STARTUP_PREPARED);
	return true;
}

bool plane_smp_mark_ap_starting(uint32_t logical_id)
{
	struct plane_cpu_data *cpu = plane_cpu_get_startup_data(logical_id);
	uint32_t expected = PLANE_CPU_STARTUP_PREPARED;

	if (!plane_smp_cpu_data_is_startable_ap(cpu)) {
		return false;
	}

	return plane_atomic_compare_exchange_u32(&cpu->startup_state, &expected,
						 PLANE_CPU_STARTUP_STARTING);
}

bool plane_smp_mark_ap_parked(struct plane_cpu_data *data)
{
	uint32_t expected = PLANE_CPU_STARTUP_STARTING;

	if (!plane_smp_cpu_data_is_startable_ap(data) || data->self != data) {
		return false;
	}

	return plane_atomic_compare_exchange_u32(&data->startup_state, &expected,
						 PLANE_CPU_STARTUP_PARKED);
}

bool plane_smp_mark_ap_failed(struct plane_cpu_data *data)
{
	uint32_t expected = PLANE_CPU_STARTUP_STARTING;

	if (!plane_smp_cpu_data_is_startable_ap(data) || data->self != data) {
		return false;
	}

	return plane_atomic_compare_exchange_u32(&data->startup_state, &expected,
						 PLANE_CPU_STARTUP_FAILED);
}

uint32_t plane_cpu_parked_count(void)
{
	uint32_t count = 0;

	if (!smp_initialized) {
		return 0;
	}

	for (uint32_t i = 0; i < runtime_cpu_count; i++) {
		const struct plane_cpu_data *cpu = &cpu_data[i];

		if (!cpu->is_bsp &&
		    plane_atomic_load_u32(&cpu->startup_state) ==
			    PLANE_CPU_STARTUP_PARKED) {
			count++;
		}
	}

	return count;
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

const struct plane_cpu_data *plane_cpu_get_data(uint32_t logical_id)
{
	return plane_cpu_get_startup_data(logical_id);
}

struct plane_cpu_data *plane_cpu_get_startup_data(uint32_t logical_id)
{
	if (!smp_initialized || logical_id >= runtime_cpu_count) {
		return NULL;
	}

	return &cpu_data[logical_id];
}

enum plane_cpu_startup_state plane_cpu_startup_state(uint32_t logical_id)
{
	const struct plane_cpu_data *cpu = plane_cpu_get_data(logical_id);

	if (cpu == NULL) {
		return PLANE_CPU_STARTUP_FAILED;
	}

	return plane_atomic_load_u32(&cpu->startup_state);
}

static bool smp_cpu_can_handle_ipi(const struct plane_cpu_data *cpu)
{
	uint32_t state;

	if (!smp_initialized || cpu == NULL || cpu->self != cpu ||
	    !cpu->present) {
		return false;
	}
	if (cpu->is_bsp) {
		return cpu->online;
	}

	state = plane_atomic_load_u32(&cpu->startup_state);
	return state == PLANE_CPU_STARTUP_PREPARED ||
	       state == PLANE_CPU_STARTUP_PARKED;
}

bool plane_smp_handle_ipi(uint8_t vector)
{
	if (!smp_cpu_can_handle_ipi(current_cpu_data)) {
		return false;
	}

	switch (vector) {
	case PLANE_SMP_IPI_VECTOR_RESCHEDULE:
		ipi_reschedule_count++;
		return true;
	case PLANE_SMP_IPI_VECTOR_PMAP_UPDATE:
		/*
		 * This is only the event-dispatch landing pad. Remote TLB
		 * payload and rendezvous state belong to the later pmap
		 * shootdown milestone.
		 */
		ipi_pmap_update_count++;
		return true;
	default:
		return false;
	}
}

uint64_t plane_smp_ipi_count(uint8_t vector)
{
	switch (vector) {
	case PLANE_SMP_IPI_VECTOR_RESCHEDULE:
		return ipi_reschedule_count;
	case PLANE_SMP_IPI_VECTOR_PMAP_UPDATE:
		return ipi_pmap_update_count;
	default:
		return 0;
	}
}

void plane_smp_ap_park_entry(struct plane_cpu_data *data)
{
	hal_irq_disable();

	if (data == NULL || data->self != data ||
	    !hal_cpu_install_ap_startup_context(data) ||
	    !hal_cpu_set_current_data(data) ||
	    !hal_local_interrupt_init_ap(data) ||
	    !plane_smp_mark_ap_parked(data)) {
		if (data != NULL) {
			plane_smp_mark_ap_failed(data);
		}
	}

	hal_cpu_hang();
}
