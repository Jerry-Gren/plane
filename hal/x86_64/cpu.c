#include <hal/cpu.h>
#include <hal/x86_64/cpu_features.h>
#include <hal/x86_64/msr_defs.h>

#include "msr_internal.h"

#include <stddef.h>
#include <stdint.h>

#include <plane/smp.h>

void hal_cpu_hang(void)
{
	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}

void hal_cpu_relax(void)
{
	__asm__ volatile ("pause" ::: "memory");
}

bool hal_cpu_set_current_data(struct plane_cpu_data *data)
{
	if (data == NULL ||
	    !x86_64_cpu_has_feature(X86_64_CPU_FEATURE_MSR)) {
		return false;
	}

	return x86_64_msr_write(X86_64_MSR_IA32_GS_BASE,
				(uint64_t)(uintptr_t)data);
}
