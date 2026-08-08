#include "limine_smp_internal.h"

#include <plane/smp.h>

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
		plane_smp_startup_enter_ap(NULL);
	}

	data = (struct plane_cpu_data *)(uintptr_t)cpu->extra_argument;
	plane_smp_startup_enter_ap(data);
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
		    !plane_smp_startup_ap_is_launchable(i)) {
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
		struct plane_smp_ap_launch launch;
		struct limine_mp_info *cpu = limine_cpu_handles[i];

		const struct plane_cpu_data *data = plane_cpu_data_get(i);
		if (data == NULL || data->is_bsp) {
			continue;
		}
		if (!plane_smp_startup_prepare_ap_launch(i, &launch)) {
			return false;
		}

		cpu->extra_argument = (uint64_t)(uintptr_t)launch.argument;
		__atomic_store_n(&cpu->goto_address, boot_limine_ap_entry,
				 __ATOMIC_RELEASE);
	}

	return true;
}
