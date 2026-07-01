#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <hal/x86_64/boot/multiboot2/mb2_early_mmu.h>

#include <plane/util.h>

uint64_t early_pml4[X86_64_PAGE_TABLE_ENTRIES];
uint64_t early_pd_kernel[X86_64_PAGE_TABLE_ENTRIES];
uint64_t early_pd_fb[X86_64_PAGE_TABLE_ENTRIES];

static uintptr_t invalidated_vaddrs[X86_64_PAGE_TABLE_ENTRIES];
static uint64_t invalidate_count;
static uint64_t flush_count;

void hal_mmu_invalidate_tlb(uintptr_t vaddr) {
	if (invalidate_count < X86_64_PAGE_TABLE_ENTRIES) {
		invalidated_vaddrs[invalidate_count] = vaddr;
	}
	invalidate_count++;
}

void hal_mmu_flush_tlb_all(void) {
	flush_count++;
}

static void reset_state(void) {
	memset(early_pml4, 0, sizeof(early_pml4));
	memset(early_pd_kernel, 0, sizeof(early_pd_kernel));
	memset(early_pd_fb, 0, sizeof(early_pd_fb));
	memset(invalidated_vaddrs, 0, sizeof(invalidated_vaddrs));
	invalidate_count = 0;
	flush_count = 0;
}

static int expect_bool(const char *name, int actual, int expected) {
	if (!!actual == !!expected) {
		return 1;
	}

	printf("FAIL: %s expected=%d actual=%d\n", name, expected, actual);
	return 0;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected) {
	if (actual == expected) {
		return 1;
	}

	printf("FAIL: %s expected=0x%016llx actual=0x%016llx\n",
	       name, (unsigned long long)expected, (unsigned long long)actual);
	return 0;
}

static int expect_ptr(const char *name, const void *actual,
		      const void *expected) {
	if (actual == expected) {
		return 1;
	}

	printf("FAIL: %s expected=%p actual=%p\n", name, expected, actual);
	return 0;
}

static int page_directory_untouched(void) {
	for (uint64_t i = 0; i < X86_64_PAGE_TABLE_ENTRIES; i++) {
		if (early_pd_fb[i] != 0 || early_pd_kernel[i] != 0) {
			return 0;
		}
	}

	return 1;
}

static int test_maps_unaligned_framebuffer(void) {
	int passed = 0;
	void *vaddr = NULL;
	uint64_t phys_addr = 0x123450;
	uint64_t phys_base = ALIGN_DOWN(phys_addr, ARCH_LARGE_PAGE_SIZE);
	uint64_t page_offset = phys_addr - phys_base;
	uint64_t start_idx = PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	uint64_t flags = PAGE_PRESENT | PAGE_RW | PAGE_PWT | PAGE_PS;

	reset_state();

	passed += expect_bool("map unaligned framebuffer",
			      x86_64_mb2_early_map_framebuffer(phys_addr,
							       0x300000,
							       &vaddr),
			      1);
	passed += expect_ptr("mapped virtual address", vaddr,
			     (void *)(X86_64_MB2_FRAMEBUFFER_VMA_BASE +
				      page_offset));
	passed += expect_u64("first framebuffer pde",
			     early_pd_fb[start_idx],
			     phys_base | flags);
	passed += expect_u64("second framebuffer pde",
			     early_pd_fb[start_idx + 1],
			     (phys_base + ARCH_LARGE_PAGE_SIZE) | flags);
	passed += expect_u64("third framebuffer pde",
			     early_pd_fb[start_idx + 2],
			     (phys_base + (2 * ARCH_LARGE_PAGE_SIZE)) | flags);
	passed += expect_u64("invalidate count", invalidate_count, 3);
	passed += expect_u64("first invalidated vaddr", invalidated_vaddrs[0],
			     X86_64_MB2_FRAMEBUFFER_VMA_BASE);

	return passed;
}

static int expect_map_failure(const char *name, uint64_t phys_addr,
			      uint64_t size) {
	void *vaddr = (void *)0xfeedface;
	int passed = 0;

	reset_state();

	passed += expect_bool(name,
			      x86_64_mb2_early_map_framebuffer(phys_addr,
							       size, &vaddr),
			      0);
	passed += expect_ptr("failure leaves out pointer unchanged", vaddr,
			     (void *)0xfeedface);
	passed += expect_bool("failure leaves page directories untouched",
			      page_directory_untouched(), 1);
	passed += expect_u64("failure does not invalidate tlb",
			     invalidate_count, 0);

	return passed;
}

static int test_rejects_invalid_mappings(void) {
	int passed = 0;

	passed += expect_map_failure("reject zero framebuffer size", 0, 0);
	passed += expect_map_failure("reject size plus page offset overflow",
				     1, UINT64_MAX);
	passed += expect_map_failure("reject aligned size overflow",
				     0, UINT64_MAX - 1);
	passed += expect_map_failure("reject physical range overflow",
				     UINT64_MAX - (ARCH_LARGE_PAGE_SIZE / 2),
				     ARCH_LARGE_PAGE_SIZE);
	passed += expect_map_failure("reject framebuffer beyond pd capacity",
				     0,
				     (X86_64_PAGE_TABLE_ENTRIES + 1) *
				     ARCH_LARGE_PAGE_SIZE);

	return passed;
}

int main(void) {
	int passed = 0;
	int total = 7 + 20;

	passed += test_maps_unaligned_framebuffer();
	passed += test_rejects_invalid_mappings();

	if (passed != total) {
		printf("mb2_early_mmu_test: %d/%d passed\n", passed, total);
		return 1;
	}

	printf("mb2_early_mmu_test: %d cases passed\n", passed);
	return 0;
}
