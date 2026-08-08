#ifndef HAL_X86_64_BOOT_MULTIBOOT2_MB2_EARLY_MMU_H
#define HAL_X86_64_BOOT_MULTIBOOT2_MB2_EARLY_MMU_H

#include <hal/x86_64/arch_mmu.h>

#ifdef __ASSEMBLER__
	#define X86_64_MB2_FRAMEBUFFER_VMA_BASE 0xffffffffc0000000
#else
	#define X86_64_MB2_FRAMEBUFFER_VMA_BASE 0xffffffffc0000000ull
#endif

#define X86_64_MB2_FRAMEBUFFER_MIN_VMA_GAP (16 * ARCH_LARGE_PAGE_SIZE)

#if (X86_64_MB2_FRAMEBUFFER_VMA_BASE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_MB2_FRAMEBUFFER_VMA_BASE must be 2MB aligned!"
#endif

#if KERNEL_VMA_BASE == X86_64_MB2_FRAMEBUFFER_VMA_BASE
	#error "Kernel and Multiboot2 framebuffer cannot share the same VMA base!"
#endif

#if X86_64_MB2_FRAMEBUFFER_VMA_BASE >= X86_64_DIRECT_MAP_BASE && \
    X86_64_MB2_FRAMEBUFFER_VMA_BASE < X86_64_DIRECT_MAP_END
	#error "X86_64_MB2_FRAMEBUFFER_VMA_BASE overlaps x86_64 direct map!"
#endif

#if X86_64_MB2_FRAMEBUFFER_VMA_BASE >= X86_64_KERNEL_MAP_BASE && \
    X86_64_MB2_FRAMEBUFFER_VMA_BASE < X86_64_KERNEL_MAP_END
	#error "X86_64_MB2_FRAMEBUFFER_VMA_BASE overlaps x86_64 kernel map!"
#endif

#if ((KERNEL_VMA_BASE > X86_64_MB2_FRAMEBUFFER_VMA_BASE ? \
      KERNEL_VMA_BASE - X86_64_MB2_FRAMEBUFFER_VMA_BASE : \
      X86_64_MB2_FRAMEBUFFER_VMA_BASE - KERNEL_VMA_BASE) < \
      X86_64_MB2_FRAMEBUFFER_MIN_VMA_GAP)
	#error "Kernel and Multiboot2 framebuffer VMA bases are too close!"
#endif

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>

bool x86_64_mb2_early_map_framebuffer(plane_paddr_t phys_addr, uint64_t size,
				      plane_vaddr_t *vaddr);
bool x86_64_mb2_early_unmap_framebuffer(plane_vaddr_t vaddr, uint64_t size);
void x86_64_mb2_early_remove_identity_mapping(void);
#endif /* !__ASSEMBLER__ */

#endif /* HAL_X86_64_BOOT_MULTIBOOT2_MB2_EARLY_MMU_H */
