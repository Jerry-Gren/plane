#include <hal/x86_64/boot/multiboot2/mb2_bootstrap_map.h>

#include <hal/mmu.h>

#include <plane/overflow.h>
#include <plane/util.h>

/*
 * Boot-only framebuffer mapping.
 *
 * This is a pre-kmain Multiboot2 bootstrap mapping with a dedicated framebuffer
 * VMA, not a general MMIO mapper. kmain releases this mapping before cloning
 * PMM-owned kernel page tables; runtime framebuffer use is then remapped
 * through plane_io_map after kmem/io-map init.
 */
#define FB_PAGE_FLAGS \
	(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE | \
	 X86_64_PAGING_ENTRY_PWT | X86_64_PAGING_ENTRY_PS)

/* in mb2_entry_entry.S */
extern uint64_t x86_64_mb2_bootstrap_pml4[];
extern uint64_t x86_64_mb2_bootstrap_pd_kernel[];
extern uint64_t x86_64_mb2_bootstrap_pd_fb[];

static uint64_t *mb2_framebuffer_pd(void)
{
	uint64_t *target_pd = x86_64_mb2_bootstrap_pd_fb;
#if X86_64_PAGING_PDPT_INDEX(KERNEL_VMA_BASE) == \
	X86_64_PAGING_PDPT_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE)
	target_pd = x86_64_mb2_bootstrap_pd_kernel;
#endif
	return target_pd;
}

static uint64_t *mb2_framebuffer_pd_physmap(void)
{
	plane_vaddr_t vaddr = hal_mmu_physmap_phys_range_to_virt(
		plane_paddr_make((uint64_t)mb2_framebuffer_pd()),
		ARCH_PAGE_SIZE);

	if (plane_vaddr_is_null(vaddr)) {
		return NULL;
	}

	return plane_vaddr_to_ptr(vaddr);
}

static bool mb2_bootstrap_framebuffer_pde_range(plane_vaddr_t vaddr,
				      uint64_t size,
				      uint64_t *start_idx,
				      uint64_t *page_count)
{
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t va_base = ALIGN_DOWN(raw_vaddr, ARCH_LARGE_PAGE_SIZE);
	uint64_t page_offset = raw_vaddr - va_base;
	uint64_t size_with_offset;
	uint64_t aligned_size;
	uint64_t start_base_idx =
		X86_64_PAGING_PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	uint64_t start_page;

	if (start_idx == NULL ||
	    page_count == NULL ||
	    plane_vaddr_is_null(vaddr) ||
	    size == 0 ||
	    raw_vaddr < X86_64_MB2_FRAMEBUFFER_VMA_BASE ||
	    va_base < X86_64_MB2_FRAMEBUFFER_VMA_BASE ||
	    !plane_checked_add_u64(size, page_offset, &size_with_offset) ||
	    !plane_checked_align_up_u64(size_with_offset,
					ARCH_LARGE_PAGE_SIZE,
					&aligned_size)) {
		return false;
	}

	start_page = (va_base - X86_64_MB2_FRAMEBUFFER_VMA_BASE) /
		     ARCH_LARGE_PAGE_SIZE;
	if (start_page > X86_64_PAGING_TABLE_ENTRIES - start_base_idx) {
		return false;
	}

	*start_idx = start_base_idx + start_page;
	*page_count = aligned_size / ARCH_LARGE_PAGE_SIZE;
	return *page_count <= X86_64_PAGING_TABLE_ENTRIES - *start_idx;
}

bool x86_64_mb2_bootstrap_map_framebuffer(plane_paddr_t phys_addr,
					  uint64_t size,
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
	uint64_t *target_pd = mb2_framebuffer_pd();

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

bool x86_64_mb2_bootstrap_unmap_framebuffer(plane_vaddr_t vaddr, uint64_t size)
{
	uint64_t start_idx;
	uint64_t page_count;
	uint64_t *target_pd = mb2_framebuffer_pd_physmap();
	uint64_t start_base_idx =
		X86_64_PAGING_PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);

	if (target_pd == NULL ||
	    !mb2_bootstrap_framebuffer_pde_range(vaddr, size, &start_idx,
				       &page_count)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		uint64_t entry = target_pd[start_idx + i];

		if (!x86_64_paging_entry_is_present(entry) ||
		    !x86_64_paging_entry_is_leaf(entry, 2)) {
			return false;
		}
	}

	for (uint64_t i = 0; i < page_count; i++) {
		uint64_t page = start_idx - start_base_idx + i;
		uint64_t current_vaddr =
			X86_64_MB2_FRAMEBUFFER_VMA_BASE +
			page * ARCH_LARGE_PAGE_SIZE;

		target_pd[start_idx + i] = 0;
		hal_mmu_invalidate_tlb(plane_vaddr_make(current_vaddr));
	}

	return true;
}

void x86_64_mb2_bootstrap_remove_identity_mapping(void)
{
	x86_64_mb2_bootstrap_pml4[0] = 0;
	hal_mmu_flush_tlb_all();
}
