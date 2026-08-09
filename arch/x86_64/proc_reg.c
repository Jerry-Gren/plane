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
