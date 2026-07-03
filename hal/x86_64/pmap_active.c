#include <hal/x86_64/arch_mmu.h>
#include <hal/x86_64/pmap.h>

uint64_t x86_64_pmap_active_root_phys(void)
{
	uint64_t cr3;

	__asm__ volatile ("mov %%cr3, %0" : "=r" (cr3));
	return cr3 & X86_64_PAGE_ENTRY_ADDR_MASK;
}
