#include "limine_smp_internal.h"

#include <hal/cpu.h>

#include <plane/smp.h>

#include "../../kernel/smp_internal.h"

static struct limine_mp_info *limine_cpu_handles[PLANE_MAX_CPUS];

void boot_limine_smp_reset_cpu_handles(void)
{
	for (uint32_t i = 0; i < PLANE_MAX_CPUS; i++) {
		limine_cpu_handles[i] = NULL;
	}
}

bool boot_limine_smp_set_cpu_handle(uint32_t logical_id,
				    struct limine_mp_info *cpu)
{
	if (logical_id >= PLANE_MAX_CPUS || cpu == NULL) {
		return false;
	}

	limine_cpu_handles[logical_id] = cpu;
	return true;
}

static void boot_limine_ap_entry(struct limine_mp_info *cpu)
{
	struct plane_cpu_data *data;

	if (cpu == NULL || cpu->extra_argument == 0) {
		hal_cpu_hang();
	}

	data = (struct plane_cpu_data *)(uintptr_t)cpu->extra_argument;
	hal_cpu_enter_on_stack(data->ap_stack_top, plane_smp_ap_park_entry,
			       data);
}

static bool limine_smp_preflight_aps(void)
{
	for (uint32_t i = 0; i < plane_cpu_count(); i++) {
		const struct plane_cpu_data *cpu = plane_cpu_data_get(i);

		if (cpu == NULL) {
			return false;
		}
		if (cpu->is_bsp) {
			continue;
		}
		if (limine_cpu_handles[i] == NULL ||
		    plane_cpu_boot_state(i) != PLANE_CPU_BOOT_PREPARED) {
			return false;
		}
	}

	return true;
}

bool boot_limine_smp_start_aps(void)
{
	if (!plane_smp_is_initialized() || !limine_smp_preflight_aps()) {
		return false;
	}

	for (uint32_t i = 0; i < plane_cpu_count(); i++) {
		struct plane_cpu_data *data = plane_cpu_boot_data_get(i);
		struct limine_mp_info *cpu = limine_cpu_handles[i];

		if (data == NULL) {
			return false;
		}
		if (data->is_bsp) {
			continue;
		}
		if (!plane_smp_mark_ap_starting(i)) {
			return false;
		}

		cpu->extra_argument = (uint64_t)(uintptr_t)data;
		__atomic_store_n(&cpu->goto_address, boot_limine_ap_entry,
				 __ATOMIC_RELEASE);
	}

	return true;
}
