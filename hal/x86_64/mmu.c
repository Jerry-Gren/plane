#include <hal/mmu.h>

void hal_mmu_invalidate_tlb(uintptr_t vaddr) {
	__asm__ volatile ("invlpg (%0)" : : "r" (vaddr) : "memory");
}

void hal_mmu_flush_tlb_all(void) {
	__asm__ volatile (
		"mov %%cr3, %%rax\n\t"
		"mov %%rax, %%cr3\n\t"
		: /* no input */
		: /* no output */
		: "rax", "memory"
	);
}
