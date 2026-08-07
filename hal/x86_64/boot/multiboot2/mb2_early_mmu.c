#include <hal/x86_64/boot/multiboot2/mb2_early_mmu.h>

#include <hal/mmu.h>

#include <plane/overflow.h>
#include <plane/util.h>

/*
 * Boot-only framebuffer mapping.
 *
 * This is an early Multiboot2 handoff bridge with a dedicated framebuffer
 * VMA, not a general MMIO mapper. The long-term IO-map path should own
 * device/cache attributes consistently for framebuffer and LAPIC mappings.
 */
#define FB_PAGE_FLAGS \
	(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE | \
	 X86_64_PAGING_ENTRY_PWT | X86_64_PAGING_ENTRY_PS)

/* in mb2_entry_entry.S */
extern uint64_t x86_64_mb2_early_pml4[];
extern uint64_t x86_64_mb2_early_pd_kernel[];
extern uint64_t x86_64_mb2_early_pd_fb[];

bool x86_64_mb2_early_map_framebuffer(plane_paddr_t phys_addr, uint64_t size,
				      plane_vaddr_t *vaddr)
{
	uint64_t raw_phys = plane_paddr_raw(phys_addr);

	if (vaddr == NULL || size == 0) {
		return false;
	}

	uint64_t phys_base = ALIGN_DOWN(raw_phys, ARCH_LARGE_PAGE_SIZE);
	uint64_t page_offset = raw_phys - phys_base;
	uint64_t fb_size_with_offset;
	uint64_t fb_aligned_size;
	uint64_t phys_end;
	uint64_t mapped_vaddr;

	if (!plane_checked_add_u64(size, page_offset, &fb_size_with_offset)) {
		return false;
	}

	if (!plane_checked_align_up_u64(fb_size_with_offset,
					ARCH_LARGE_PAGE_SIZE,
					&fb_aligned_size)) {
		return false;
	}

	if (!plane_checked_add_u64(phys_base, fb_aligned_size - 1,
				   &phys_end)) {
		return false;
	}

	uint64_t pages_needed = fb_aligned_size / ARCH_LARGE_PAGE_SIZE;

	uint64_t *target_pd = x86_64_mb2_early_pd_fb;
#if X86_64_PAGING_PDPT_INDEX(KERNEL_VMA_BASE) == \
	X86_64_PAGING_PDPT_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE)
	target_pd = x86_64_mb2_early_pd_kernel;
#endif

	uint64_t start_idx =
		X86_64_PAGING_PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	if (pages_needed > X86_64_PAGING_TABLE_ENTRIES - start_idx) {
		return false;
	}

	for (uint64_t i = 0; i < pages_needed; i++) {
		uint64_t offset = i * ARCH_LARGE_PAGE_SIZE;
		uint64_t current_vaddr = X86_64_MB2_FRAMEBUFFER_VMA_BASE + offset;

		target_pd[start_idx + i] = (phys_base + offset) | FB_PAGE_FLAGS;
		hal_mmu_invalidate_tlb(plane_vaddr_make(current_vaddr));
	}

	if (!plane_checked_add_u64(X86_64_MB2_FRAMEBUFFER_VMA_BASE,
				   page_offset, &mapped_vaddr)) {
		return false;
	}

	*vaddr = plane_vaddr_make(mapped_vaddr);
	return true;
}

void x86_64_mb2_early_remove_identity_mapping(void)
{
	x86_64_mb2_early_pml4[0] = 0;
	hal_mmu_flush_tlb_all();
}
