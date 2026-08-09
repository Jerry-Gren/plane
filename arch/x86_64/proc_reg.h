#ifndef X86_64_PROC_REG_H
#define X86_64_PROC_REG_H

#include <plane/bits.h>

/*
 * x86-64 processor register definitions and primitives used by Plane today.
 *
 * Intel SDM Vol.3 system programming chapters and Vol.4 MSR tables plus AMD
 * APM Vol.2 define these architectural registers. Keep this as the narrow
 * processor-register service boundary; subsystem behavior stays in pmap,
 * trap, LAPIC, PAT, and CPU-data code.
 */
#define X86_64_RFLAGS_IF BIT(9)
#define X86_64_RFLAGS_ID BIT(21)

#define X86_64_CR0_PE BIT(0)
#define X86_64_CR0_PG BIT(31)

#define X86_64_CR4_PAE BIT(5)

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

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <x86_64/paging_defs.h>

#define X86_64_MSR_IA32_APIC_BASE_X2APIC BIT_ULL(10)
#define X86_64_MSR_IA32_APIC_BASE_ENABLE BIT_ULL(11)
#define X86_64_MSR_IA32_APIC_BASE_ADDR_LOW_BIT  12
#define X86_64_MSR_IA32_APIC_BASE_ADDR_HIGH_BIT 51
#define X86_64_MSR_IA32_APIC_BASE_ADDR \
	GENMASK_ULL(X86_64_MSR_IA32_APIC_BASE_ADDR_HIGH_BIT, \
		    X86_64_MSR_IA32_APIC_BASE_ADDR_LOW_BIT)

uint64_t rdmsr64(uint32_t msr);
bool wrmsr64(uint32_t msr, uint64_t value);

static inline uint64_t read_rflags(void)
{
	uint64_t rflags;

	__asm__ volatile ("pushfq; popq %0" : "=r"(rflags) :: "memory");
	return rflags;
}

static inline uint64_t read_cr2(void)
{
	uint64_t cr2;

	__asm__ volatile ("mov %%cr2, %0" : "=r" (cr2));
	return cr2;
}

static inline plane_paddr_t read_cr3_phys(void)
{
	uint64_t cr3;

	__asm__ volatile ("mov %%cr3, %0" : "=r" (cr3));
	return plane_paddr_make(cr3 & X86_64_PAGING_ENTRY_ADDR_MASK);
}

static inline void write_cr3_phys(plane_paddr_t phys_addr)
{
	__asm__ volatile ("mov %0, %%cr3" : : "r" (plane_paddr_raw(phys_addr)) : "memory");
}

static inline void invlpg(plane_vaddr_t vaddr)
{
	__asm__ volatile ("invlpg (%0)" : : "r" (plane_vaddr_raw(vaddr)) : "memory");
}

static inline void reload_cr3(void)
{
	__asm__ volatile (
		"mov %%cr3, %%rax\n\t"
		"mov %%rax, %%cr3\n\t"
		: /* no output */
		: /* no input */
		: "rax", "memory"
	);
}

static inline void cli(void)
{
	__asm__ volatile ("cli" ::: "memory");
}

static inline void sti(void)
{
	__asm__ volatile ("sti" ::: "memory");
}

#endif /* !__ASSEMBLER__ */

#endif /* X86_64_PROC_REG_H */
