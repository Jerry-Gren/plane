#ifndef PLANE_MM_H
#define PLANE_MM_H

#if defined(__x86_64__)
	#include <hal/x86_64/arch_mmu.h>
#else
	#error "Unsupported architecture for Memory Management!"
#endif

/* must have */
#ifndef ARCH_PAGE_SIZE
	#error "Architecture must define ARCH_PAGE_SIZE!"
#endif
#define PAGE_SIZE       ARCH_PAGE_SIZE
#define PAGE_SHIFT      ARCH_PAGE_SHIFT
#define PAGE_MASK       (~(PAGE_SIZE - 1))

/* optional */
/* large page */
#ifdef ARCH_LARGE_PAGE_SIZE
	#define LARGE_PAGE_SIZE     ARCH_LARGE_PAGE_SIZE
	#define LARGE_PAGE_SHIFT    ARCH_LARGE_PAGE_SHIFT
	#define LARGE_PAGE_MASK     (~(LARGE_PAGE_SIZE - 1))
#endif
/* huge page */
#ifdef ARCH_HUGE_PAGE_SIZE
	#define HUGE_PAGE_SIZE      ARCH_HUGE_PAGE_SIZE
	#define HUGE_PAGE_SHIFT     ARCH_HUGE_PAGE_SHIFT
	#define HUGE_PAGE_MASK      (~(HUGE_PAGE_SIZE - 1))
#endif

#endif /* PLANE_MM_H */