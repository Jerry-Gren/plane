#ifndef HAL_ARCH_MMU_H
#define HAL_ARCH_MMU_H

#include <hal/x86_64/page.h>
#include <plane/bits.h>

/* Page flags */
#define PAGE_PRESENT         BIT(0)
#define PAGE_RW              BIT(1)
#define PAGE_PWT             BIT(3)
#define PAGE_PS              BIT(7)

#define X86_64_PAGE_TABLE_ENTRIES 512

/* Page table indices */
#define PML4_INDEX(vaddr) (((vaddr) >> 39) & 0x1ff)
#define PDPT_INDEX(vaddr) (((vaddr) >> 30) & 0x1ff)
#define PD_INDEX(vaddr)   (((vaddr) >> 21) & 0x1ff)
#define PT_INDEX(vaddr)   (((vaddr) >> 12) & 0x1ff)

#ifdef __ASSEMBLER__
	#define KERNEL_VMA_BASE        0xffffffff80000000
	#define X86_64_DIRECT_MAP_BASE 0xffff800000000000
	/*
	 * v1 direct-map window size, not the final physical limit.
	 * GRUB builds this low 4GiB window in early page tables; Limine
	 * supplies the base through HHDM.
	 */
	#define X86_64_DIRECT_MAP_SIZE 0x100000000
#else
	#define KERNEL_VMA_BASE        0xffffffff80000000ull
	#define X86_64_DIRECT_MAP_BASE 0xffff800000000000ull
	/*
	 * v1 direct-map window size, not the final physical limit.
	 * GRUB builds this low 4GiB window in early page tables; Limine
	 * supplies the base through HHDM.
	 */
	#define X86_64_DIRECT_MAP_SIZE 0x100000000ull
#endif

#define X86_64_DIRECT_MAP_END (X86_64_DIRECT_MAP_BASE + X86_64_DIRECT_MAP_SIZE)

#if (KERNEL_VMA_BASE & 0x1fffff) != 0
	#error "KERNEL_VMA_BASE must be 2MB aligned!"
#endif

#if (X86_64_DIRECT_MAP_BASE & 0x1fffff) != 0
	#error "X86_64_DIRECT_MAP_BASE must be 2MB aligned!"
#endif

#if (X86_64_DIRECT_MAP_SIZE & 0x1fffff) != 0
	#error "X86_64_DIRECT_MAP_SIZE must be 2MB aligned!"
#endif

#if X86_64_DIRECT_MAP_END <= X86_64_DIRECT_MAP_BASE
	#error "x86_64 direct map range wraps"
#endif

#if KERNEL_VMA_BASE >= X86_64_DIRECT_MAP_BASE && KERNEL_VMA_BASE < X86_64_DIRECT_MAP_END
	#error "KERNEL_VMA_BASE overlaps x86_64 direct map"
#endif

#endif /* HAL_ARCH_MMU_H */
