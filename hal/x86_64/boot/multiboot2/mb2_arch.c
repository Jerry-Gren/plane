#include <boot/multiboot2/mb2_arch.h>

#include <hal/x86_64/boot/multiboot2/mb2_bootstrap_map.h>

#include <plane/memmap.h>
#include <plane/overflow.h>
#include <plane/printk.h>

/* in linker_grub.lds.S */
extern char __kernel_phys_start[];
extern char __kernel_phys_end[];

plane_vaddr_t boot_mb2_arch_phys_to_virt(plane_paddr_t phys_addr)
{
	uint64_t vaddr;

	BUG_ON_MSG(!plane_checked_add_u64(plane_paddr_raw(phys_addr),
					  KERNEL_VMA_BASE, &vaddr),
		   "multiboot2 phys-to-virt overflow: phys=0x%016llx",
		   (unsigned long long)plane_paddr_raw(phys_addr));
	return plane_vaddr_make(vaddr);
}

bool boot_mb2_arch_map_bootstrap_framebuffer(plane_paddr_t phys_addr,
					     uint64_t size,
					     plane_vaddr_t *vaddr)
{
	return x86_64_mb2_bootstrap_map_framebuffer(phys_addr, size, vaddr);
}

bool boot_mb2_arch_release_bootstrap_framebuffer_mapping(
	plane_vaddr_t vaddr, uint64_t size)
{
	return x86_64_mb2_bootstrap_unmap_framebuffer(vaddr, size);
}

void boot_mb2_arch_reserve_kernel_image(struct plane_mem_info *mem)
{
	uint64_t kernel_phys_start = (uint64_t)__kernel_phys_start;
	uint64_t kernel_phys_end = (uint64_t)__kernel_phys_end;

	BUG_ON_MSG(!plane_memmap_reserve(mem, plane_paddr_make(kernel_phys_start),
					 kernel_phys_end - kernel_phys_start,
					 PLANE_MEM_EXECUTABLE_AND_MODULES),
		   "failed to reserve kernel image: start=0x%016llx end=0x%016llx",
		   (unsigned long long)kernel_phys_start,
		   (unsigned long long)kernel_phys_end);
}

void boot_mb2_arch_finish_handoff(void)
{
	x86_64_mb2_bootstrap_remove_identity_mapping();
}
