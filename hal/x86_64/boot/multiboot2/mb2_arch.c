#include <boot/multiboot2/mb2_arch.h>

#include <hal/x86_64/boot/multiboot2/mb2_early_mmu.h>

#include <plane/memmap.h>
#include <plane/printk.h>

/* in linker_grub.lds.S */
extern char __kernel_phys_start[];
extern char __kernel_phys_end[];

void *boot_mb2_arch_phys_to_virt(uint64_t phys_addr)
{
	return (void *)(phys_addr + KERNEL_VMA_BASE);
}

bool boot_mb2_arch_map_framebuffer(uint64_t phys_addr, uint64_t size,
				   void **vaddr) {
	return x86_64_mb2_early_map_framebuffer(phys_addr, size, vaddr);
}

void boot_mb2_arch_reserve_kernel_image(struct plane_mem_info *mem)
{
	uint64_t kernel_phys_start = (uint64_t)__kernel_phys_start;
	uint64_t kernel_phys_end = (uint64_t)__kernel_phys_end;

	BUG_ON_MSG(!plane_memmap_reserve(mem, kernel_phys_start,
					 kernel_phys_end - kernel_phys_start,
					 PLANE_MEM_EXECUTABLE_AND_MODULES),
		   "failed to reserve kernel image: start=0x%016llx end=0x%016llx",
		   (unsigned long long)kernel_phys_start,
		   (unsigned long long)kernel_phys_end);
}

void boot_mb2_arch_finish_handoff(void)
{
	x86_64_mb2_early_remove_identity_mapping();
}
