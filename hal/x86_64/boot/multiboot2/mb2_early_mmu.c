#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>

#include <plane/kernel.h>
#include <plane/mm.h>

#define FB_PAGE_FLAGS (PAGE_PRESENT | PAGE_RW | PAGE_PWT | PAGE_PS)

extern uint64_t boot_fb_pd[];
extern uint64_t boot_pml4[];

void *hal_mmu_map_early_framebuffer(uint64_t phys_addr, uint64_t size) {
    uint64_t phys_base = ALIGN_DOWN(phys_addr, ARCH_LARGE_PAGE_SIZE);
    uint64_t page_offset = phys_addr - phys_base;

    uint64_t fb_aligned_size = ALIGN(size + page_offset, ARCH_LARGE_PAGE_SIZE);
    uint64_t pages_needed = fb_aligned_size / ARCH_LARGE_PAGE_SIZE;

    for (uint64_t i = 0; i < pages_needed; i++) {
        uint64_t offset = i * ARCH_LARGE_PAGE_SIZE;
        uint64_t current_vaddr = FRAMEBUFFER_VMA_BASE + offset;
        
        boot_fb_pd[i] = (phys_base + offset) | FB_PAGE_FLAGS;
        hal_mmu_invalidate_tlb(current_vaddr);
    }

    return (void *)(FRAMEBUFFER_VMA_BASE + page_offset);
}

void hal_mmu_remove_identity_mapping(void) {
    boot_pml4[0] = 0;
    hal_mmu_flush_tlb_all();
}