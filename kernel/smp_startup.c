#include <hal/cpu.h>

#include <plane/kmem.h>
#include <plane/smp.h>

#include "smp_internal.h"

#define AP_STACK_PAGES 4

static bool cpu_data_is_startable_ap(const struct plane_cpu_data *cpu)
{
	return cpu != NULL && !cpu->is_bsp && cpu->present;
}

bool plane_smp_startup_prepare_ap_stacks(void)
{
	for (uint32_t i = 0; i < plane_cpu_count(); i++) {
		const struct plane_cpu_data *cpu = plane_cpu_get_data(i);
		plane_vaddr_t stack;

		if (cpu == NULL || cpu->is_bsp) {
			continue;
		}

		if (!plane_kmem_alloc_pages(AP_STACK_PAGES,
					    PLANE_KMEM_ALLOC_ZERO |
					    PLANE_KMEM_ALLOC_GUARD,
					    &stack)) {
			return false;
		}
		if (!plane_smp_prepare_ap_stack(i, stack, AP_STACK_PAGES)) {
			plane_kmem_free_pages(stack, AP_STACK_PAGES);
			return false;
		}
	}

	return true;
}

bool plane_smp_startup_ap_is_launchable(uint32_t logical_id)
{
	const struct plane_cpu_data *cpu = plane_cpu_get_data(logical_id);

	return cpu_data_is_startable_ap(cpu) &&
	       plane_cpu_startup_state(logical_id) == PLANE_CPU_STARTUP_PREPARED;
}

bool plane_smp_startup_prepare_ap_launch(uint32_t logical_id,
				      struct plane_smp_ap_launch *launch)
{
	struct plane_cpu_data *cpu;

	if (launch == NULL || !plane_smp_startup_ap_is_launchable(logical_id)) {
		return false;
	}

	cpu = plane_cpu_get_startup_data(logical_id);
	if (cpu == NULL || !plane_smp_mark_ap_starting(logical_id)) {
		return false;
	}

	*launch = (struct plane_smp_ap_launch){
		.argument = cpu
	};
	return true;
}

void plane_smp_startup_enter_ap(struct plane_cpu_data *data)
{
	if (data == NULL || data->self != data ||
	    plane_vaddr_is_null(data->ap_stack_top)) {
		hal_cpu_hang();
	}

	hal_cpu_enter_on_stack(data->ap_stack_top,
			       plane_smp_ap_park_entry, data);
}
