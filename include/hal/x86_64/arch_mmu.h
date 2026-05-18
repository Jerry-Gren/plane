#ifndef HAL_ARCH_MMU_H
#define HAL_ARCH_MMU_H

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

#if (KERNEL_VMA_BASE & 0x1FFFFF) != 0
    #error "KERNEL_VMA_BASE must be 2MB aligned!"
#endif

#if (FRAMEBUFFER_VMA_BASE & 0x1FFFFF) != 0
    #error "FRAMEBUFFER_VMA_BASE must be 2MB aligned!"
#endif

#if KERNEL_VMA_BASE == FRAMEBUFFER_VMA_BASE
    #error "Kernel and Framebuffer cannot share the same VMA base!"
#endif

#define VMA_DIFF (KERNEL_VMA_BASE > FRAMEBUFFER_VMA_BASE ? \
                 (KERNEL_VMA_BASE - FRAMEBUFFER_VMA_BASE) : \
                 (FRAMEBUFFER_VMA_BASE - KERNEL_VMA_BASE))

#if VMA_DIFF < 0x2000000ULL
    #error "Kernel and Framebuffer VMA bases are too close! Risk of overlap."
#endif

#endif /* HAL_ARCH_MMU_H */