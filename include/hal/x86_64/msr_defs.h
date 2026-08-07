#ifndef HAL_X86_64_MSR_DEFS_H
#define HAL_X86_64_MSR_DEFS_H

#include <plane/bits.h>

/*
 * x86-64 MSR numbers and fields used by Plane today.
 *
 * Intel SDM Vol.3 system programming chapters and Vol.4 MSR tables plus AMD
 * APM Vol.2 define these architectural MSRs. Keep this as a narrow register
 * namespace; subsystem behavior stays in CPU/LAPIC code.
 */
#define X86_64_MSR_IA32_APIC_BASE 0x1b
#define X86_64_MSR_IA32_CR_PAT    0x277
#define X86_64_MSR_IA32_EFER      0xc0000080
#define X86_64_MSR_IA32_GS_BASE   0xc0000101

#define X86_64_MSR_IA32_EFER_LME BIT(8)

#define X86_64_MSR_IA32_CR_PAT_MEMORY_WC 0x01
#define X86_64_MSR_IA32_CR_PAT_ENTRY_BITS 8
#define X86_64_MSR_IA32_CR_PAT_ENTRY_SHIFT(n) \
	((n) * X86_64_MSR_IA32_CR_PAT_ENTRY_BITS)
#ifdef __ASSEMBLER__
#define X86_64_MSR_IA32_CR_PAT_ENTRY_MASK(n) \
	(0xff << X86_64_MSR_IA32_CR_PAT_ENTRY_SHIFT(n))
#else
#define X86_64_MSR_IA32_CR_PAT_ENTRY_MASK(n) \
	(0xffull << X86_64_MSR_IA32_CR_PAT_ENTRY_SHIFT(n))
#endif

#ifndef __ASSEMBLER__

#define X86_64_MSR_IA32_APIC_BASE_X2APIC BIT_ULL(10)
#define X86_64_MSR_IA32_APIC_BASE_ENABLE BIT_ULL(11)
#define X86_64_MSR_IA32_APIC_BASE_ADDR_LOW_BIT  12
#define X86_64_MSR_IA32_APIC_BASE_ADDR_HIGH_BIT 51
#define X86_64_MSR_IA32_APIC_BASE_ADDR \
	GENMASK_ULL(X86_64_MSR_IA32_APIC_BASE_ADDR_HIGH_BIT, \
		    X86_64_MSR_IA32_APIC_BASE_ADDR_LOW_BIT)

#endif /* !__ASSEMBLER__ */

#endif /* HAL_X86_64_MSR_DEFS_H */
