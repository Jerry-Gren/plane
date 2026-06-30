#ifndef HAL_ARCH_MMU_H
#define HAL_ARCH_MMU_H

#include <hal/x86_64/page.h>

/* Page flags */
#define PAGE_PRESENT         (1 << 0)
#define PAGE_RW              (1 << 1)
#define PAGE_PWT             (1 << 3)
#define PAGE_PS              (1 << 7)

/* Page table indices */
#define PML4_INDEX(vaddr) (((vaddr) >> 39) & 0x1ff)
#define PDPT_INDEX(vaddr) (((vaddr) >> 30) & 0x1ff)
#define PD_INDEX(vaddr)   (((vaddr) >> 21) & 0x1ff)
#define PT_INDEX(vaddr)   (((vaddr) >> 12) & 0x1ff)

#ifdef __ASSEMBLER__
	#define KERNEL_VMA_BASE        0xffffffff80000000
	#define FRAMEBUFFER_VMA_BASE   0xffffffffc0000000
#else
	#define KERNEL_VMA_BASE        0xffffffff80000000ull
	#define FRAMEBUFFER_VMA_BASE   0xffffffffc0000000ull
#endif

#if (KERNEL_VMA_BASE & 0x1fffff) != 0
	#error "KERNEL_VMA_BASE must be 2MB aligned!"
#endif

#if (FRAMEBUFFER_VMA_BASE & 0x1fffff) != 0
	#error "FRAMEBUFFER_VMA_BASE must be 2MB aligned!"
#endif

#if KERNEL_VMA_BASE == FRAMEBUFFER_VMA_BASE
	#error "Kernel and Framebuffer cannot share the same VMA base!"
#endif

#define VMA_DIFF (KERNEL_VMA_BASE > FRAMEBUFFER_VMA_BASE ? \
		 (KERNEL_VMA_BASE - FRAMEBUFFER_VMA_BASE) : \
		 (FRAMEBUFFER_VMA_BASE - KERNEL_VMA_BASE))

#if VMA_DIFF < 0x2000000ull
	#error "Kernel and Framebuffer VMA bases are too close! Risk of overlap."
#endif

#endif /* HAL_ARCH_MMU_H */
