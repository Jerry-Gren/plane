#ifndef HAL_ARCH_MMU_H
#define HAL_ARCH_MMU_H

#include <hal/x86_64/paging_defs.h>

#ifdef __ASSEMBLER__
	#define KERNEL_VMA_BASE        0xffffffff80000000
	#define X86_64_DIRECT_MAP_BASE 0xffff800000000000
	#define X86_64_KERNEL_MAP_BASE 0xffff900000000000
	/*
	 * v1 direct-map capacity, not the final physical limit. GRUB builds
	 * this maximum window in early page tables; Limine supplies the base
	 * through HHDM. Runtime helpers enable only the coverage required by
	 * the sanitized memory map.
	 */
	#define X86_64_DIRECT_MAP_MAX_SIZE 0x1000000000
	/* v1 kernel dynamic mapping window. */
	#define X86_64_KERNEL_MAP_SIZE 0x40000000
#else
	#define KERNEL_VMA_BASE        0xffffffff80000000ull
	#define X86_64_DIRECT_MAP_BASE 0xffff800000000000ull
	#define X86_64_KERNEL_MAP_BASE 0xffff900000000000ull
	/*
	 * v1 direct-map capacity, not the final physical limit. GRUB builds
	 * this maximum window in early page tables; Limine supplies the base
	 * through HHDM. Runtime helpers enable only the coverage required by
	 * the sanitized memory map.
	 */
	#define X86_64_DIRECT_MAP_MAX_SIZE 0x1000000000ull
	/* v1 kernel dynamic mapping window. */
	#define X86_64_KERNEL_MAP_SIZE 0x40000000ull
#endif

#define X86_64_DIRECT_MAP_MAX_END \
	(X86_64_DIRECT_MAP_BASE + X86_64_DIRECT_MAP_MAX_SIZE)
#define X86_64_KERNEL_MAP_END (X86_64_KERNEL_MAP_BASE + X86_64_KERNEL_MAP_SIZE)

#if (KERNEL_VMA_BASE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "KERNEL_VMA_BASE must be 2MB aligned!"
#endif

#if (X86_64_DIRECT_MAP_BASE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_DIRECT_MAP_BASE must be 2MB aligned!"
#endif

#if (X86_64_DIRECT_MAP_MAX_SIZE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_DIRECT_MAP_MAX_SIZE must be 2MB aligned!"
#endif

#if (X86_64_DIRECT_MAP_MAX_SIZE & (ARCH_HUGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_DIRECT_MAP_MAX_SIZE must be 1GB aligned!"
#endif

#if X86_64_DIRECT_MAP_MAX_SIZE > \
    (X86_64_PAGING_TABLE_ENTRIES * ARCH_HUGE_PAGE_SIZE)
	#error "x86_64 v1 direct map must fit in one PML4 slot"
#endif

#if (X86_64_KERNEL_MAP_BASE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_KERNEL_MAP_BASE must be 2MB aligned!"
#endif

#if (X86_64_KERNEL_MAP_SIZE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_KERNEL_MAP_SIZE must be 2MB aligned!"
#endif

#if X86_64_DIRECT_MAP_MAX_END <= X86_64_DIRECT_MAP_BASE
	#error "x86_64 direct map range wraps"
#endif

#if X86_64_KERNEL_MAP_END <= X86_64_KERNEL_MAP_BASE
	#error "x86_64 kernel map range wraps"
#endif

#if KERNEL_VMA_BASE >= X86_64_DIRECT_MAP_BASE && KERNEL_VMA_BASE < X86_64_DIRECT_MAP_MAX_END
	#error "KERNEL_VMA_BASE overlaps x86_64 direct map"
#endif

#if KERNEL_VMA_BASE >= X86_64_KERNEL_MAP_BASE && KERNEL_VMA_BASE < X86_64_KERNEL_MAP_END
	#error "KERNEL_VMA_BASE overlaps x86_64 kernel map"
#endif

#if X86_64_DIRECT_MAP_BASE < X86_64_KERNEL_MAP_END && \
    X86_64_KERNEL_MAP_BASE < X86_64_DIRECT_MAP_MAX_END
	#error "x86_64 direct map overlaps kernel map"
#endif

#endif /* HAL_ARCH_MMU_H */
