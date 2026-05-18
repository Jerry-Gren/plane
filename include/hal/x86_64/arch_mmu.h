#ifndef HAL_ARCH_MMU_H
#define HAL_ARCH_MMU_H

/* Page flags */
#define PAGE_PRESENT         (1 << 0)
#define PAGE_RW              (1 << 1)
#define PAGE_PWT             (1 << 3)
#define PAGE_HUGE            (1 << 7)

/* X86_64 specific page sizes */
#ifdef __ASSEMBLER__
    #define ARCH_PAGE_SIZE         0x1000        /* 4KB base page */
    #define ARCH_PAGE_SHIFT        12
    
    #define ARCH_LARGE_PAGE_SIZE   0x200000      /* 2MB large page (pd) */
    #define ARCH_LARGE_PAGE_SHIFT  21
    
    #define ARCH_HUGE_PAGE_SIZE    0x40000000    /* 1GB huge page (pdpt) */
    #define ARCH_HUGE_PAGE_SHIFT   30

    #define KERNEL_VMA_BASE        0xffffffff80000000
    #define FRAMEBUFFER_VMA_BASE   0xffffffffc0000000
#else
    #define ARCH_PAGE_SIZE         0x1000ULL
    #define ARCH_PAGE_SHIFT        12ULL
    
    #define ARCH_LARGE_PAGE_SIZE   0x200000ULL
    #define ARCH_LARGE_PAGE_SHIFT  21ULL
    
    #define ARCH_HUGE_PAGE_SIZE    0x40000000ULL
    #define ARCH_HUGE_PAGE_SHIFT   30ULL

    #define KERNEL_VMA_BASE        0xffffffff80000000ULL
    #define FRAMEBUFFER_VMA_BASE   0xffffffffc0000000ULL
#endif

#endif /* HAL_ARCH_MMU_H */