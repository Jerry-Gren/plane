#include <machine/machine_routines.h>
#include <x86_64/cpu_features.h>

#include <stddef.h>
#include <stdint.h>

#include <plane/smp.h>
#include <x86_64/proc_reg.h>

void ml_cpu_halt(void)
{
	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}

void cpu_pause(void)
{
	__asm__ volatile ("pause" ::: "memory");
}

bool ml_cpu_set_current_data(struct plane_cpu_data *data)
{
	if (data == NULL ||
	    !x86_64_cpu_has_feature(X86_64_CPU_FEATURE_MSR)) {
		return false;
	}

	return wrmsr64(X86_64_MSR_IA32_GS_BASE, (uint64_t)(uintptr_t)data);
}
