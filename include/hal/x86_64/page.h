#ifndef HAL_X86_64_PAGE_H
#define HAL_X86_64_PAGE_H

#ifdef __ASSEMBLER__
	#define ARCH_PAGE_SIZE         0x1000        /* 4KB base page */
	#define ARCH_PAGE_SHIFT        12

	#define ARCH_LARGE_PAGE_SIZE   0x200000      /* 2MB large page (pd) */
	#define ARCH_LARGE_PAGE_SHIFT  21

	#define ARCH_HUGE_PAGE_SIZE    0x40000000    /* 1GB huge page (pdpt) */
	#define ARCH_HUGE_PAGE_SHIFT   30
#else
	#define ARCH_PAGE_SIZE         0x1000ull
	#define ARCH_PAGE_SHIFT        12ull

	#define ARCH_LARGE_PAGE_SIZE   0x200000ull
	#define ARCH_LARGE_PAGE_SHIFT  21ull

	#define ARCH_HUGE_PAGE_SIZE    0x40000000ull
	#define ARCH_HUGE_PAGE_SHIFT   30ull
#endif

#endif /* HAL_X86_64_PAGE_H */
