#include <hal/x86_64/cpu_features.h>
#include <hal/x86_64/msr_defs.h>
#include <hal/x86_64/pat.h>

#include <stdint.h>

#include <x86_64/msr.h>

static bool pat_write_combine_ready;

bool x86_64_pat_init(void)
{
	uint64_t pat;

	if (!x86_64_cpu_has_feature(X86_64_CPU_FEATURE_MSR) ||
	    !x86_64_cpu_has_feature(X86_64_CPU_FEATURE_PAT)) {
		return false;
	}

	pat = x86_64_msr_read(X86_64_MSR_IA32_CR_PAT);
	pat &= ~X86_64_MSR_IA32_CR_PAT_ENTRY_MASK(1);
	pat |= X86_64_MSR_IA32_CR_PAT_MEMORY_WC <<
	       X86_64_MSR_IA32_CR_PAT_ENTRY_SHIFT(1);
	if (!x86_64_msr_write(X86_64_MSR_IA32_CR_PAT, pat)) {
		return false;
	}

	pat_write_combine_ready = true;
	return true;
}

bool x86_64_pat_write_combine_is_ready(void)
{
	return pat_write_combine_ready;
}
