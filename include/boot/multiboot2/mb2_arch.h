#ifndef BOOT_MULTIBOOT2_MB2_ARCH_H
#define BOOT_MULTIBOOT2_MB2_ARCH_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>

struct plane_mem_info;

plane_vaddr_t boot_mb2_arch_phys_to_virt(plane_paddr_t phys_addr);
bool boot_mb2_arch_map_bootstrap_framebuffer(plane_paddr_t phys_addr,
					     uint64_t size,
					     plane_vaddr_t *vaddr);
bool boot_mb2_arch_release_bootstrap_framebuffer_mapping(
	plane_vaddr_t vaddr, uint64_t size);
void boot_mb2_arch_reserve_kernel_image(struct plane_mem_info *mem);
void boot_mb2_arch_finish_handoff(void);

#endif /* BOOT_MULTIBOOT2_MB2_ARCH_H */
