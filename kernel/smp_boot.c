#include <plane/kmem.h>
#include <plane/smp.h>

#include "smp_internal.h"

#define AP_STACK_PAGES 4

bool plane_smp_prepare_ap_stacks(void)
{
	for (uint32_t i = 0; i < plane_cpu_count(); i++) {
		const struct plane_cpu_data *cpu = plane_cpu_data_get(i);
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
