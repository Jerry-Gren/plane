#include <hal/mmu.h>

#include <plane/mm.h>

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

void *hal_mmu_phys_to_virt(uintptr_t phys_addr) {
	return (void *)(phys_addr + KERNEL_VMA_BASE);
}

uintptr_t hal_mmu_virt_to_phys(void *virt_addr) {
	return (uintptr_t)virt_addr - KERNEL_VMA_BASE;
}