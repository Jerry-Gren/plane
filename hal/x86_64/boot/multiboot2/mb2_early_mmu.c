#include <hal/x86_64/boot/multiboot2/mb2_early_mmu.h>

#include <hal/mmu.h>

#include <plane/util.h>

#define FB_PAGE_FLAGS (PAGE_PRESENT | PAGE_RW | PAGE_PWT | PAGE_PS)

/* in mb2_entry_entry.S */
extern uint64_t x86_64_mb2_early_pml4[];
extern uint64_t x86_64_mb2_early_pd_kernel[];
extern uint64_t x86_64_mb2_early_pd_fb[];

static bool checked_align_up(uint64_t value, uint64_t align, uint64_t *out) {
	if (value > UINT64_MAX - (align - 1)) {
		return false;
	}

	*out = ALIGN(value, align);
	return true;
}

bool x86_64_mb2_early_map_framebuffer(uint64_t phys_addr, uint64_t size,
				      void **vaddr) {
	if (vaddr == NULL || size == 0) {
		return false;
	}

	uint64_t phys_base = ALIGN_DOWN(phys_addr, ARCH_LARGE_PAGE_SIZE);
	uint64_t page_offset = phys_addr - phys_base;

	if (size > UINT64_MAX - page_offset) {
		return false;
	}

	uint64_t fb_size_with_offset = size + page_offset;
	uint64_t fb_aligned_size;
	if (!checked_align_up(fb_size_with_offset, ARCH_LARGE_PAGE_SIZE,
			      &fb_aligned_size)) {
		return false;
	}

	if ((fb_aligned_size - 1) > UINT64_MAX - phys_base) {
		return false;
	}

	uint64_t pages_needed = fb_aligned_size / ARCH_LARGE_PAGE_SIZE;

	uint64_t *target_pd = x86_64_mb2_early_pd_fb;
#if PDPT_INDEX(KERNEL_VMA_BASE) == PDPT_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE)
	target_pd = x86_64_mb2_early_pd_kernel;
#endif

	uint64_t start_idx = PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	if (pages_needed > X86_64_PAGE_TABLE_ENTRIES - start_idx) {
		return false;
	}

	for (uint64_t i = 0; i < pages_needed; i++) {
		uint64_t offset = i * ARCH_LARGE_PAGE_SIZE;
		uint64_t current_vaddr = X86_64_MB2_FRAMEBUFFER_VMA_BASE + offset;
		
		target_pd[start_idx + i] = (phys_base + offset) | FB_PAGE_FLAGS;
		hal_mmu_invalidate_tlb(current_vaddr);
	}

	*vaddr = (void *)(X86_64_MB2_FRAMEBUFFER_VMA_BASE + page_offset);
	return true;
}

void x86_64_mb2_early_remove_identity_mapping(void) {
	x86_64_mb2_early_pml4[0] = 0;
	hal_mmu_flush_tlb_all();
}
