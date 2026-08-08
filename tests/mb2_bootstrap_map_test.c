#include <stdint.h>
#include <string.h>

#include <hal/mmu.h>
#include <hal/x86_64/boot/multiboot2/mb2_bootstrap_map.h>

#include <plane/util.h>

#include "support/test.h"

uint64_t x86_64_mb2_bootstrap_pml4[X86_64_PAGING_TABLE_ENTRIES];
uint64_t x86_64_mb2_bootstrap_pd_kernel[X86_64_PAGING_TABLE_ENTRIES];
uint64_t x86_64_mb2_bootstrap_pd_fb[X86_64_PAGING_TABLE_ENTRIES];

static uintptr_t invalidated_vaddrs[X86_64_PAGING_TABLE_ENTRIES];
static uint64_t invalidate_count;
static uint64_t flush_count;
static bool physmap_available;

plane_vaddr_t hal_mmu_physmap_phys_range_to_virt(plane_paddr_t phys_addr,
						uint64_t size)
{
	if (!physmap_available || size != ARCH_PAGE_SIZE) {
		return plane_vaddr_make(0);
	}

	return plane_vaddr_make(plane_paddr_raw(phys_addr));
}

void hal_mmu_invalidate_tlb(plane_vaddr_t vaddr)
{
	if (invalidate_count < X86_64_PAGING_TABLE_ENTRIES) {
		invalidated_vaddrs[invalidate_count] = plane_vaddr_raw(vaddr);
	}
	invalidate_count++;
}

void hal_mmu_flush_tlb_all(void)
{
	flush_count++;
}

static void reset_state(void)
{
	memset(x86_64_mb2_bootstrap_pml4, 0, sizeof(x86_64_mb2_bootstrap_pml4));
	memset(x86_64_mb2_bootstrap_pd_kernel, 0,
	       sizeof(x86_64_mb2_bootstrap_pd_kernel));
	memset(x86_64_mb2_bootstrap_pd_fb, 0, sizeof(x86_64_mb2_bootstrap_pd_fb));
	memset(invalidated_vaddrs, 0, sizeof(invalidated_vaddrs));
	invalidate_count = 0;
	flush_count = 0;
	physmap_available = true;
}

static void reset_invalidation_state(void)
{
	memset(invalidated_vaddrs, 0, sizeof(invalidated_vaddrs));
	invalidate_count = 0;
	flush_count = 0;
}

static uint64_t *framebuffer_target_pd(void)
{
	uint64_t *target_pd = x86_64_mb2_bootstrap_pd_fb;
#if X86_64_PAGING_PDPT_INDEX(KERNEL_VMA_BASE) == \
	X86_64_PAGING_PDPT_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE)
	target_pd = x86_64_mb2_bootstrap_pd_kernel;
#endif
	return target_pd;
}

static uint64_t framebuffer_page_flags(void)
{
	return X86_64_PAGING_ENTRY_PRESENT |
	       X86_64_PAGING_ENTRY_WRITE |
	       X86_64_PAGING_ENTRY_PWT |
	       X86_64_PAGING_ENTRY_PS;
}

static int page_tables_untouched(void)
{
	for (uint64_t i = 0; i < X86_64_PAGING_TABLE_ENTRIES; i++) {
		if (x86_64_mb2_bootstrap_pd_fb[i] != 0 ||
		    x86_64_mb2_bootstrap_pd_kernel[i] != 0) {
			return 0;
		}
	}

	return 1;
}

static int test_maps_unaligned_framebuffer(void)
{
	int failures = 0;
	plane_vaddr_t vaddr = plane_vaddr_make(0);
	uint64_t phys_addr = 0x123450;
	uint64_t phys_base = ALIGN_DOWN(phys_addr, ARCH_LARGE_PAGE_SIZE);
	uint64_t page_offset = phys_addr - phys_base;
	uint64_t start_idx = X86_64_PAGING_PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	uint64_t flags = X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE | X86_64_PAGING_ENTRY_PWT | X86_64_PAGING_ENTRY_PS;

	reset_state();

	failures += test_expect_bool("map unaligned framebuffer",
				     x86_64_mb2_bootstrap_map_framebuffer(
					     plane_paddr_make(phys_addr),
					     0x300000, &vaddr),
				     true);
	failures += test_expect_u64("mapped virtual address",
				    plane_vaddr_raw(vaddr),
				    X86_64_MB2_FRAMEBUFFER_VMA_BASE +
					    page_offset);
	failures += test_expect_u64("first framebuffer pde",
				    x86_64_mb2_bootstrap_pd_fb[start_idx],
				    phys_base | flags);
	failures += test_expect_u64("second framebuffer pde",
				    x86_64_mb2_bootstrap_pd_fb[start_idx + 1],
				    (phys_base + ARCH_LARGE_PAGE_SIZE) | flags);
	failures += test_expect_u64("third framebuffer pde",
				    x86_64_mb2_bootstrap_pd_fb[start_idx + 2],
				    (phys_base + (2 * ARCH_LARGE_PAGE_SIZE)) |
					    flags);
	failures += test_expect_u64("invalidate count", invalidate_count, 3);
	failures += test_expect_u64("first invalidated vaddr",
				    invalidated_vaddrs[0],
				    X86_64_MB2_FRAMEBUFFER_VMA_BASE);

	return failures;
}

static int check_map_failure(const char *name, uint64_t phys_addr,
			      uint64_t size) {
	plane_vaddr_t vaddr = plane_vaddr_make(0xfeedface);
	int failures = 0;

	reset_state();

	failures += test_expect_bool(name,
				     x86_64_mb2_bootstrap_map_framebuffer(
					     plane_paddr_make(phys_addr), size,
					     &vaddr),
				     false);
	failures += test_expect_u64("failure leaves out address unchanged",
				    plane_vaddr_raw(vaddr), 0xfeedface);
	failures += test_expect_bool("failure leaves page directories untouched",
				     page_tables_untouched(), true);
	failures += test_expect_u64("failure does not invalidate tlb",
				    invalidate_count, 0);

	return failures;
}

static int test_rejects_invalid_mappings(void)
{
	int failures = 0;

	failures += check_map_failure("reject zero framebuffer size", 0, 0);
	failures += check_map_failure("reject size plus page offset overflow",
				       1, UINT64_MAX);
	failures += check_map_failure("reject aligned size overflow",
				       0, UINT64_MAX - 1);
	failures += check_map_failure("reject physical range overflow",
				       UINT64_MAX - (ARCH_LARGE_PAGE_SIZE / 2),
				       ARCH_LARGE_PAGE_SIZE);
	failures += check_map_failure("reject framebuffer beyond pd capacity",
				       0,
				       (X86_64_PAGING_TABLE_ENTRIES + 1) *
					       ARCH_LARGE_PAGE_SIZE);

	return failures;
}

static int test_unmaps_unaligned_framebuffer(void)
{
	int failures = 0;
	plane_vaddr_t vaddr = plane_vaddr_make(0);
	uint64_t phys_addr = 0x123450;
	uint64_t start_idx =
		X86_64_PAGING_PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	uint64_t *target_pd = framebuffer_target_pd();

	reset_state();

	failures += test_expect_bool("map before unmap",
				     x86_64_mb2_bootstrap_map_framebuffer(
					     plane_paddr_make(phys_addr),
					     0x300000, &vaddr),
				     true);
	if (failures != 0) {
		return failures;
	}

	reset_invalidation_state();
	failures += test_expect_bool("unmap framebuffer",
				     x86_64_mb2_bootstrap_unmap_framebuffer(
					     vaddr, 0x300000),
				     true);
	failures += test_expect_u64("first pde cleared",
				    target_pd[start_idx], 0);
	failures += test_expect_u64("second pde cleared",
				    target_pd[start_idx + 1], 0);
	failures += test_expect_u64("third pde cleared",
				    target_pd[start_idx + 2], 0);
	failures += test_expect_u64("unmap invalidate count",
				    invalidate_count, 3);
	failures += test_expect_u64("unmap first invalidated vaddr",
				    invalidated_vaddrs[0],
				    X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	failures += test_expect_u64("unmap second invalidated vaddr",
				    invalidated_vaddrs[1],
				    X86_64_MB2_FRAMEBUFFER_VMA_BASE +
					    ARCH_LARGE_PAGE_SIZE);
	failures += test_expect_u64("unmap third invalidated vaddr",
				    invalidated_vaddrs[2],
				    X86_64_MB2_FRAMEBUFFER_VMA_BASE +
					    2 * ARCH_LARGE_PAGE_SIZE);

	return failures;
}

static int check_unmap_failure(const char *name, plane_vaddr_t vaddr,
			       uint64_t size)
{
	uint64_t before_fb[X86_64_PAGING_TABLE_ENTRIES];
	uint64_t before_kernel[X86_64_PAGING_TABLE_ENTRIES];
	uint64_t start_idx =
		X86_64_PAGING_PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	uint64_t *target_pd;
	int failures = 0;

	reset_state();
	target_pd = framebuffer_target_pd();
	target_pd[start_idx] = 0x123000 | framebuffer_page_flags();
	target_pd[start_idx + 1] =
		(0x123000 + ARCH_LARGE_PAGE_SIZE) |
		framebuffer_page_flags();
	memcpy(before_fb, x86_64_mb2_bootstrap_pd_fb, sizeof(before_fb));
	memcpy(before_kernel, x86_64_mb2_bootstrap_pd_kernel,
	       sizeof(before_kernel));

	failures += test_expect_bool(name,
				     x86_64_mb2_bootstrap_unmap_framebuffer(
					     vaddr, size),
				     false);
	failures += test_expect_bool("failed unmap leaves fb pd unchanged",
				     memcmp(before_fb, x86_64_mb2_bootstrap_pd_fb,
					    sizeof(before_fb)) == 0,
				     true);
	failures += test_expect_bool("failed unmap leaves kernel pd unchanged",
				     memcmp(before_kernel,
					    x86_64_mb2_bootstrap_pd_kernel,
					    sizeof(before_kernel)) == 0,
				     true);
	failures += test_expect_u64("failed unmap does not invalidate",
				    invalidate_count, 0);

	return failures;
}

static int test_rejects_invalid_unmappings(void)
{
	int failures = 0;

	failures += check_unmap_failure("reject null framebuffer vaddr",
					plane_vaddr_make(0),
					ARCH_LARGE_PAGE_SIZE);
	failures += check_unmap_failure("reject zero unmap size",
					plane_vaddr_make(
						X86_64_MB2_FRAMEBUFFER_VMA_BASE),
					0);
	failures += check_unmap_failure("reject size plus offset overflow",
					plane_vaddr_make(UINT64_MAX - 1),
					UINT64_MAX);
	failures += check_unmap_failure("reject out of window",
					plane_vaddr_make(
						X86_64_MB2_FRAMEBUFFER_VMA_BASE -
						ARCH_LARGE_PAGE_SIZE),
					ARCH_LARGE_PAGE_SIZE);
	failures += check_unmap_failure("reject range beyond pd capacity",
					plane_vaddr_make(
						X86_64_MB2_FRAMEBUFFER_VMA_BASE),
					(X86_64_PAGING_TABLE_ENTRIES + 1) *
						ARCH_LARGE_PAGE_SIZE);

	return failures;
}

static int test_unmap_rejects_missing_physmap(void)
{
	uint64_t start_idx =
		X86_64_PAGING_PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	uint64_t *target_pd;
	int failures = 0;

	reset_state();
	target_pd = framebuffer_target_pd();
	target_pd[start_idx] = 0x123000 | framebuffer_page_flags();
	physmap_available = false;

	failures += test_expect_bool("reject missing physmap",
				     x86_64_mb2_bootstrap_unmap_framebuffer(
					     plane_vaddr_make(
						     X86_64_MB2_FRAMEBUFFER_VMA_BASE),
					     ARCH_LARGE_PAGE_SIZE),
				     false);
	failures += test_expect_u64("missing physmap keeps pde",
				    target_pd[start_idx],
				    0x123000 | framebuffer_page_flags());
	failures += test_expect_u64("missing physmap no invalidate",
				    invalidate_count, 0);

	return failures;
}

static int test_unmap_preflight_rejects_absent_or_non_large_pde(void)
{
	uint64_t start_idx =
		X86_64_PAGING_PD_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	uint64_t *target_pd;
	uint64_t before_fb[X86_64_PAGING_TABLE_ENTRIES];
	uint64_t before_kernel[X86_64_PAGING_TABLE_ENTRIES];
	plane_vaddr_t vaddr =
		plane_vaddr_make(X86_64_MB2_FRAMEBUFFER_VMA_BASE);
	int failures = 0;

	reset_state();
	failures += test_expect_bool("reject absent pde",
				     x86_64_mb2_bootstrap_unmap_framebuffer(
					     vaddr, ARCH_LARGE_PAGE_SIZE),
				     false);

	reset_state();
	target_pd = framebuffer_target_pd();
	target_pd[start_idx] = X86_64_PAGING_ENTRY_WRITE |
			       X86_64_PAGING_ENTRY_PS;
	memcpy(before_fb, x86_64_mb2_bootstrap_pd_fb, sizeof(before_fb));
	memcpy(before_kernel, x86_64_mb2_bootstrap_pd_kernel,
	       sizeof(before_kernel));
	failures += test_expect_bool("reject non-present pde",
				     x86_64_mb2_bootstrap_unmap_framebuffer(
					     vaddr, ARCH_LARGE_PAGE_SIZE),
				     false);
	failures += test_expect_bool("non-present leaves fb pd unchanged",
				     memcmp(before_fb, x86_64_mb2_bootstrap_pd_fb,
					    sizeof(before_fb)) == 0,
				     true);
	failures += test_expect_bool("non-present leaves kernel pd unchanged",
				     memcmp(before_kernel,
					    x86_64_mb2_bootstrap_pd_kernel,
					    sizeof(before_kernel)) == 0,
				     true);

	reset_state();
	target_pd = framebuffer_target_pd();
	target_pd[start_idx] = X86_64_PAGING_ENTRY_PRESENT |
			       X86_64_PAGING_ENTRY_WRITE;
	memcpy(before_fb, x86_64_mb2_bootstrap_pd_fb, sizeof(before_fb));
	memcpy(before_kernel, x86_64_mb2_bootstrap_pd_kernel,
	       sizeof(before_kernel));
	failures += test_expect_bool("reject non-large pde",
				     x86_64_mb2_bootstrap_unmap_framebuffer(
					     vaddr, ARCH_LARGE_PAGE_SIZE),
				     false);
	failures += test_expect_bool("non-large leaves fb pd unchanged",
				     memcmp(before_fb, x86_64_mb2_bootstrap_pd_fb,
					    sizeof(before_fb)) == 0,
				     true);
	failures += test_expect_bool("non-large leaves kernel pd unchanged",
				     memcmp(before_kernel,
					    x86_64_mb2_bootstrap_pd_kernel,
					    sizeof(before_kernel)) == 0,
				     true);
	failures += test_expect_u64("preflight failures do not invalidate",
				    invalidate_count, 0);

	return failures;
}

static int test_remove_identity_mapping_flushes_all(void)
{
	int failures = 0;

	reset_state();
	x86_64_mb2_bootstrap_pml4[0] = 0x123000 | X86_64_PAGING_ENTRY_PRESENT;

	x86_64_mb2_bootstrap_remove_identity_mapping();
	failures += test_expect_u64("identity pml4 cleared",
				    x86_64_mb2_bootstrap_pml4[0], 0);
	failures += test_expect_u64("identity removal flushes",
				    flush_count, 1);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_maps_unaligned_framebuffer),
		TEST_CASE(test_rejects_invalid_mappings),
		TEST_CASE(test_unmaps_unaligned_framebuffer),
		TEST_CASE(test_rejects_invalid_unmappings),
		TEST_CASE(test_unmap_rejects_missing_physmap),
		TEST_CASE(test_unmap_preflight_rejects_absent_or_non_large_pde),
		TEST_CASE(test_remove_identity_mapping_flushes_all),
	};

	return test_run_cases("mb2_bootstrap_map_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
