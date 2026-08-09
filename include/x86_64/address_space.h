#ifndef X86_64_ADDRESS_SPACE_H
#define X86_64_ADDRESS_SPACE_H

#include <x86_64/paging_defs.h>

#ifdef __ASSEMBLER__
	#define KERNEL_VMA_BASE        0xffffffff80000000
	#define X86_64_PHYSMAP_BASE 0xffff800000000000
	#define X86_64_KERNEL_MAP_BASE 0xffff900000000000
	/*
	 * Plane-owned physmap capacity. One x86-64 PML4 slot covers
	 * 512GiB; Plane computes the runtime slot count from sanitized memory.
	 * MB2 still uses a one-slot pre-kmain bootstrap physmap.
	 */
	#define X86_64_PHYSMAP_PML4_COUNT_MAX \
		((X86_64_KERNEL_MAP_BASE - X86_64_PHYSMAP_BASE) / X86_64_PAGING_PML4_SLOT_SIZE)
	#define X86_64_PHYSMAP_WINDOW_SIZE \
		(X86_64_PHYSMAP_PML4_COUNT_MAX * X86_64_PAGING_PML4_SLOT_SIZE)
	#define X86_64_PHYSMAP_BOOTSTRAP_SIZE X86_64_PAGING_PML4_SLOT_SIZE
	/* Kernel dynamic mapping window. */
	#define X86_64_KERNEL_MAP_SIZE 0x40000000
#else
	#define KERNEL_VMA_BASE        0xffffffff80000000ull
	#define X86_64_PHYSMAP_BASE 0xffff800000000000ull
	#define X86_64_KERNEL_MAP_BASE 0xffff900000000000ull
	/*
	 * Plane-owned physmap capacity. One x86-64 PML4 slot covers
	 * 512GiB; Plane computes the runtime slot count from sanitized memory.
	 * MB2 still uses a one-slot pre-kmain bootstrap physmap.
	 */
	#define X86_64_PHYSMAP_PML4_COUNT_MAX \
		((X86_64_KERNEL_MAP_BASE - X86_64_PHYSMAP_BASE) / X86_64_PAGING_PML4_SLOT_SIZE)
	#define X86_64_PHYSMAP_WINDOW_SIZE \
		(X86_64_PHYSMAP_PML4_COUNT_MAX * X86_64_PAGING_PML4_SLOT_SIZE)
	#define X86_64_PHYSMAP_BOOTSTRAP_SIZE X86_64_PAGING_PML4_SLOT_SIZE
	/* Kernel dynamic mapping window. */
	#define X86_64_KERNEL_MAP_SIZE 0x40000000ull
#endif

#define X86_64_PHYSMAP_WINDOW_END \
	(X86_64_PHYSMAP_BASE + X86_64_PHYSMAP_WINDOW_SIZE)
#define X86_64_KERNEL_MAP_END (X86_64_KERNEL_MAP_BASE + X86_64_KERNEL_MAP_SIZE)

#if (KERNEL_VMA_BASE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "KERNEL_VMA_BASE must be 2MB aligned!"
#endif

#if (X86_64_PHYSMAP_BASE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_PHYSMAP_BASE must be 2MB aligned!"
#endif

#if X86_64_PHYSMAP_PML4_COUNT_MAX == 0
	#error "X86_64_PHYSMAP_PML4_COUNT_MAX must be non-zero"
#endif

#if X86_64_PHYSMAP_PML4_COUNT_MAX > X86_64_PAGING_TABLE_ENTRIES
	#error "x86_64 physmap cannot consume more than the whole PML4"
#endif

#if (X86_64_PHYSMAP_WINDOW_SIZE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_PHYSMAP_WINDOW_SIZE must be 2MB aligned!"
#endif

#if (X86_64_PHYSMAP_WINDOW_SIZE & (ARCH_HUGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_PHYSMAP_WINDOW_SIZE must be 1GB aligned!"
#endif

#if (X86_64_PHYSMAP_BOOTSTRAP_SIZE & (ARCH_HUGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_PHYSMAP_BOOTSTRAP_SIZE must be 1GB aligned!"
#endif

#if X86_64_PHYSMAP_BOOTSTRAP_SIZE > X86_64_PHYSMAP_WINDOW_SIZE
	#error "x86_64 bootstrap physmap exceeds final physmap window"
#endif

#if (X86_64_KERNEL_MAP_BASE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_KERNEL_MAP_BASE must be 2MB aligned!"
#endif

#if (X86_64_KERNEL_MAP_SIZE & (ARCH_LARGE_PAGE_SIZE - 1)) != 0
	#error "X86_64_KERNEL_MAP_SIZE must be 2MB aligned!"
#endif

#if X86_64_PHYSMAP_WINDOW_END <= X86_64_PHYSMAP_BASE
	#error "x86_64 physmap range wraps"
#endif

#if X86_64_KERNEL_MAP_END <= X86_64_KERNEL_MAP_BASE
	#error "x86_64 kernel map range wraps"
#endif

#if KERNEL_VMA_BASE >= X86_64_PHYSMAP_BASE && KERNEL_VMA_BASE < X86_64_PHYSMAP_WINDOW_END
	#error "KERNEL_VMA_BASE overlaps x86_64 physmap"
#endif

#if KERNEL_VMA_BASE >= X86_64_KERNEL_MAP_BASE && KERNEL_VMA_BASE < X86_64_KERNEL_MAP_END
	#error "KERNEL_VMA_BASE overlaps x86_64 kernel map"
#endif

#if X86_64_PHYSMAP_BASE < X86_64_KERNEL_MAP_END && \
    X86_64_KERNEL_MAP_BASE < X86_64_PHYSMAP_WINDOW_END
	#error "x86_64 physmap overlaps kernel map"
#endif

#endif /* X86_64_ADDRESS_SPACE_H */
