#include <x86_64/proc_reg.h>

uint64_t rdmsr64(uint32_t msr)
{
	uint32_t lo;
	uint32_t hi;

	__asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
	return ((uint64_t)hi << 32) | lo;
}

bool wrmsr64(uint32_t msr, uint64_t value)
{
	uint32_t lo = (uint32_t)value;
	uint32_t hi = (uint32_t)(value >> 32);

	__asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
	return true;
}

uint64_t get_cr3_raw(void)
{
	uint64_t cr3;

	__asm__ volatile ("mov %%cr3, %0" : "=r" (cr3));
	return cr3;
}

void set_cr3_raw(uint64_t value)
{
	__asm__ volatile ("mov %0, %%cr3" : : "r" (value) : "memory");
}

void invlpg(plane_vaddr_t vaddr)
{
	/*
	 * INVLPG invalidates cached translations for one linear address on the
	 * current CPU. Cross-CPU range shootdown and rendezvous stay in pmap.
	 */
	__asm__ volatile ("invlpg (%0)" : : "r" (plane_vaddr_raw(vaddr)) : "memory");
}
