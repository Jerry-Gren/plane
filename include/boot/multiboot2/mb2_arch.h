#ifndef BOOT_MULTIBOOT2_MB2_ARCH_H
#define BOOT_MULTIBOOT2_MB2_ARCH_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/memmap.h>

void *boot_mb2_arch_phys_to_virt(uint64_t phys_addr);
bool boot_mb2_arch_map_framebuffer(uint64_t phys_addr, uint64_t size,
				   void **vaddr);
void boot_mb2_arch_reserve_kernel_image(struct plane_mem_info *mem);
void boot_mb2_arch_finish_handoff(void);

#endif /* BOOT_MULTIBOOT2_MB2_ARCH_H */
