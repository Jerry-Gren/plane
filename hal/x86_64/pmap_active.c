#include <hal/x86_64/arch_mmu.h>

#include <x86_64/pmap_internal.h>

plane_paddr_t x86_64_pmap_active_root_phys(void)
{
	uint64_t cr3;

	__asm__ volatile ("mov %%cr3, %0" : "=r" (cr3));
	return plane_paddr_make(cr3 & X86_64_PAGING_ENTRY_ADDR_MASK);
}
