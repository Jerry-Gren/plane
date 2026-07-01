#ifndef HAL_X86_64_BOOT_MULTIBOOT2_MB2_EARLY_MMU_H
#define HAL_X86_64_BOOT_MULTIBOOT2_MB2_EARLY_MMU_H

#include <stdint.h>

/*
 * Multiboot2 handoff page-table helpers. These operate on the temporary
 * x86_64 page tables built by the Multiboot2 entry path.
 */
void *x86_64_mb2_early_map_framebuffer(uint64_t phys_addr, uint64_t size);
void x86_64_mb2_early_remove_identity_mapping(void);
void *x86_64_mb2_early_direct_phys_to_virt(uintptr_t phys_addr);

#endif /* HAL_X86_64_BOOT_MULTIBOOT2_MB2_EARLY_MMU_H */
