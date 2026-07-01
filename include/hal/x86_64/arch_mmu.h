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
#else
	#define KERNEL_VMA_BASE        0xffffffff80000000ull
#endif

#if (KERNEL_VMA_BASE & 0x1fffff) != 0
	#error "KERNEL_VMA_BASE must be 2MB aligned!"
#endif

#endif /* HAL_ARCH_MMU_H */
