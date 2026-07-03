#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <hal/x86_64/arch_mmu.h>
#include <hal/x86_64/pmap.h>
#include <plane/mm.h>
#include <plane/pmm.h>

#include "support/test.h"

#define TEST_PAGE_COUNT 32
#define TEST_ALLOC_START_PAGE 16
#define TEST_PHYS_SIZE (TEST_PAGE_COUNT * PAGE_SIZE)

static uint8_t phys_storage[TEST_PHYS_SIZE] __attribute__((aligned(PAGE_SIZE)));
static bool page_allocated[TEST_PAGE_COUNT];
static uint64_t alloc_attempts;
static uint64_t alloc_fail_after;
static uint64_t free_fail_phys;
static uint64_t direct_map_blocked_phys;
static uintptr_t invalidated_vaddr;
static uint64_t invalidate_count;
static uint64_t flush_count;

static uint64_t test_page_phys(uint64_t page)
{
	return page * PAGE_SIZE;
}

uint64_t x86_64_pmap_current_root_phys(void)
{
	return test_page_phys(0);
}

static uint64_t *test_table(uint64_t page)
{
	return (uint64_t *)&phys_storage[test_page_phys(page)];
}

static uint64_t pte_flags(uint64_t entry)
{
	return entry & ~X86_64_PAGE_ENTRY_ADDR_MASK;
}

static uint64_t pte_phys(uint64_t entry)
{
	return entry & X86_64_PAGE_ENTRY_ADDR_MASK;
}

static uint64_t allocated_page_count(void)
{
	uint64_t count = 0;

	for (uint64_t i = 0; i < TEST_PAGE_COUNT; i++) {
		if (page_allocated[i]) {
			count++;
		}
	}

	return count;
}

static void reset_pmap_test(void)
{
	memset(phys_storage, 0, sizeof(phys_storage));
	memset(page_allocated, 0, sizeof(page_allocated));
	alloc_attempts = 0;
	alloc_fail_after = UINT64_MAX;
	free_fail_phys = UINT64_MAX;
	direct_map_blocked_phys = UINT64_MAX;
	invalidated_vaddr = UINTPTR_MAX;
	invalidate_count = 0;
	flush_count = 0;
}

void *hal_mmu_direct_phys_range_to_virt(uint64_t phys_addr, uint64_t size)
{
	uint64_t end;

	if (size == 0 || phys_addr > UINT64_MAX - size) {
		return NULL;
	}

	end = phys_addr + size;
	if (end > TEST_PHYS_SIZE) {
		return NULL;
	}

	if (direct_map_blocked_phys != UINT64_MAX &&
	    phys_addr < direct_map_blocked_phys + PAGE_SIZE &&
	    end > direct_map_blocked_phys) {
		return NULL;
	}

	return &phys_storage[phys_addr];
}

void *hal_mmu_direct_phys_to_virt(uint64_t phys_addr)
{
	return hal_mmu_direct_phys_range_to_virt(phys_addr, 1);
}

void hal_mmu_invalidate_tlb(uintptr_t vaddr)
{
	invalidated_vaddr = vaddr;
	invalidate_count++;
}

void hal_mmu_flush_tlb_all(void)
{
	flush_count++;
}

bool plane_pmm_alloc_pages_phys_flags(uint64_t page_count,
				      uint64_t alignment_pages,
				      uint32_t flags,
				      uint64_t *phys_addr)
{
	if (phys_addr == NULL || page_count != 1 || alignment_pages != 1 ||
	    (flags & ~PLANE_PMM_ALLOC_ZERO) != 0) {
		return false;
	}

	if (alloc_attempts++ >= alloc_fail_after) {
		return false;
	}

	for (uint64_t i = TEST_ALLOC_START_PAGE; i < TEST_PAGE_COUNT; i++) {
		if (!page_allocated[i]) {
			page_allocated[i] = true;
			*phys_addr = test_page_phys(i);
			if ((flags & PLANE_PMM_ALLOC_ZERO) != 0) {
				memset(&phys_storage[*phys_addr], 0, PAGE_SIZE);
			}
			return true;
		}
	}

	return false;
}

bool plane_pmm_free_page_phys(uint64_t phys_addr)
{
	uint64_t page = phys_addr / PAGE_SIZE;

	if ((phys_addr & (PAGE_SIZE - 1)) != 0 || page >= TEST_PAGE_COUNT ||
	    !page_allocated[page]) {
		return false;
	}

	if (phys_addr == free_fail_phys) {
		return false;
	}

	page_allocated[page] = false;
	return true;
}

static int test_map_page_allocates_missing_path(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t phys = 0x12345000ull;
	uint64_t out = UINT64_MAX;
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;
	int failures = 0;

	failures += test_expect_bool("map missing path",
				     x86_64_pmap_map_page_in_owned_root(
					     test_page_phys(0), vaddr, phys,
					     X86_64_PMAP_WRITE),
				     true);
	failures += test_expect_u64("map allocated intermediate tables",
				    allocated_page_count(), 3);
	failures += test_expect_u64("root map does not invalidate",
				    invalidate_count, 0);
	failures += test_expect_u64("root map leaves invalidated vaddr",
				    invalidated_vaddr, UINTPTR_MAX);

	pdpt = hal_mmu_direct_phys_to_virt(pte_phys(pml4[PML4_INDEX(vaddr)]));
	pd = hal_mmu_direct_phys_to_virt(pte_phys(pdpt[PDPT_INDEX(vaddr)]));
	pt = hal_mmu_direct_phys_to_virt(pte_phys(pd[PD_INDEX(vaddr)]));
	failures += test_expect_u64("map pml4 flags",
				    pte_flags(pml4[PML4_INDEX(vaddr)]),
				    PAGE_PRESENT | PAGE_RW);
	failures += test_expect_u64("map pdpt flags",
				    pte_flags(pdpt[PDPT_INDEX(vaddr)]),
				    PAGE_PRESENT | PAGE_RW);
	failures += test_expect_u64("map pd flags",
				    pte_flags(pd[PD_INDEX(vaddr)]),
				    PAGE_PRESENT | PAGE_RW);
	failures += test_expect_u64("map pte",
				    pt[PT_INDEX(vaddr)],
				    phys | PAGE_PRESENT | PAGE_RW);
	failures += test_expect_bool("map translate",
				     x86_64_pmap_translate_in_root(test_page_phys(0),
							    vaddr, &out),
				     true);
	failures += test_expect_u64("map translate phys", out, phys);

	return failures;
}

static int test_map_page_reuses_existing_tables(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t next_vaddr = vaddr + PAGE_SIZE;
	uint64_t phys = 0x12345000ull;
	uint64_t next_phys = 0x12346000ull;
	int failures = 0;

	failures += test_expect_bool("map first page",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr, phys, 0),
				     true);
	failures += test_expect_bool("map adjacent page",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  next_vaddr,
							  next_phys, 0),
				     true);
	failures += test_expect_u64("map reuse allocation count",
				    allocated_page_count(), 3);
	failures += test_expect_u64("root map reuse invalidate count",
				    invalidate_count, 0);

	return failures;
}

static int test_active_kernel_map_invalidates(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("active map",
				     x86_64_pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     X86_64_PMAP_WRITE),
				     true);
	failures += test_expect_u64("active map invalidates",
				    invalidate_count, 1);
	failures += test_expect_u64("active map invalidated vaddr",
				    invalidated_vaddr, vaddr);

	return failures;
}

static int test_map_page_rejects_invalid_inputs(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("map reject unaligned vaddr",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr + 1,
							  0x12345000ull, 0),
				     false);
	failures += test_expect_bool("map reject unaligned phys",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345001ull, 0),
				     false);
	failures += test_expect_bool("map reject bad flags",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345000ull,
							  BIT(31)),
				     false);
	failures += test_expect_bool("map reject unaligned root",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0) + 1,
							  vaddr,
							  0x12345000ull, 0),
				     false);
	failures += test_expect_u64("map invalid inputs allocate nothing",
				    allocated_page_count(), 0);

	return failures;
}

static int test_map_page_rejects_existing_leaf(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("map original leaf",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345000ull, 0),
				     true);
	failures += test_expect_bool("map reject duplicate leaf",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12346000ull, 0),
				     false);
	failures += test_expect_u64("map duplicate does not allocate",
				    allocated_page_count(), 3);

	return failures;
}

static int test_map_page_rejects_huge_intermediate(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt = test_table(1);
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	pml4[PML4_INDEX(vaddr)] = test_page_phys(1) | PAGE_PRESENT | PAGE_RW;
	pdpt[PDPT_INDEX(vaddr)] = 0x40000000ull | PAGE_PRESENT | PAGE_RW |
				  PAGE_PS;

	failures += test_expect_bool("map reject huge intermediate",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345000ull, 0),
				     false);
	failures += test_expect_u64("map huge allocate nothing",
				    allocated_page_count(), 0);

	return failures;
}

static int test_map_page_rolls_back_on_allocation_failure(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	alloc_fail_after = 2;
	failures += test_expect_bool("map allocation failure",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345000ull, 0),
				     false);
	failures += test_expect_u64("map allocation rollback",
				    allocated_page_count(), 0);

	return failures;
}

static int test_map_page_rolls_back_on_direct_map_failure(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	direct_map_blocked_phys = test_page_phys(TEST_ALLOC_START_PAGE + 1);
	failures += test_expect_bool("map direct-map failure",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345000ull, 0),
				     false);
	failures += test_expect_u64("map direct-map rollback",
				    allocated_page_count(), 0);

	return failures;
}

static int test_translate_handles_leaf_sizes(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt = test_table(1);
	uint64_t *pd = test_table(2);
	uint64_t *pt = test_table(3);
	uint64_t vaddr_4k = 0xffff800000402123ull;
	uint64_t vaddr_2m = 0xffff800000600456ull;
	uint64_t vaddr_1g = 0xffff80008000789aull;
	uint64_t out = UINT64_MAX;
	int failures = 0;

	pml4[PML4_INDEX(vaddr_4k)] = test_page_phys(1) | PAGE_PRESENT | PAGE_RW;
	pdpt[PDPT_INDEX(vaddr_4k)] = test_page_phys(2) | PAGE_PRESENT | PAGE_RW;
	pd[PD_INDEX(vaddr_4k)] = test_page_phys(3) | PAGE_PRESENT | PAGE_RW;
	pt[PT_INDEX(vaddr_4k)] = 0x12345000ull | PAGE_PRESENT | PAGE_RW;
	pd[PD_INDEX(vaddr_2m)] = 0x200000ull | BIT_ULL(12) | PAGE_PRESENT |
				 PAGE_RW | PAGE_PS;
	pdpt[PDPT_INDEX(vaddr_1g)] =
		0x40000000ull | BIT_ULL(12) | PAGE_PRESENT | PAGE_RW |
		PAGE_PS;

	failures += test_expect_bool("translate 4k leaf",
				     x86_64_pmap_translate_in_root(test_page_phys(0),
							    vaddr_4k, &out),
				     true);
	failures += test_expect_u64("translate 4k phys",
				    out, 0x12345000ull + 0x123);
	failures += test_expect_bool("translate 2m leaf",
				     x86_64_pmap_translate_in_root(test_page_phys(0),
							    vaddr_2m, &out),
				     true);
	failures += test_expect_u64("translate 2m phys",
				    out, 0x200000ull + (vaddr_2m &
							(ARCH_LARGE_PAGE_SIZE - 1)));
	failures += test_expect_bool("translate 1g leaf",
				     x86_64_pmap_translate_in_root(test_page_phys(0),
							    vaddr_1g, &out),
				     true);
	failures += test_expect_u64("translate 1g phys",
				    out, 0x40000000ull + (vaddr_1g &
							  (ARCH_HUGE_PAGE_SIZE - 1)));
	failures += test_expect_bool("translate absent",
				     x86_64_pmap_translate_in_root(test_page_phys(0),
							    0x1000, &out),
				     false);
	failures += test_expect_bool("translate reject null out",
				     x86_64_pmap_translate_in_root(test_page_phys(0),
							    vaddr_4k, NULL),
				     false);

	return failures;
}

static int test_unmap_page_clears_leaf(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t out = UINT64_MAX;
	int failures = 0;

	failures += test_expect_bool("unmap setup map",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345000ull, 0),
				     true);
	invalidate_count = 0;
	invalidated_vaddr = UINTPTR_MAX;

	failures += test_expect_bool("unmap leaf",
				     x86_64_pmap_unmap_page_in_owned_root(test_page_phys(0),
							    vaddr),
				     true);
	failures += test_expect_u64("root unmap does not invalidate",
				    invalidate_count, 0);
	failures += test_expect_u64("root unmap leaves invalidated vaddr",
				    invalidated_vaddr, UINTPTR_MAX);
	failures += test_expect_u64("root unmap frees empty tables",
				    allocated_page_count(), 0);
	failures += test_expect_bool("unmap translate missing",
				     x86_64_pmap_translate_in_root(test_page_phys(0),
							    vaddr, &out),
				     false);
	failures += test_expect_bool("unmap reject double unmap",
				     x86_64_pmap_unmap_page_in_owned_root(test_page_phys(0),
							    vaddr),
				     false);

	return failures;
}

static int test_unmap_preserves_parent_on_free_failure(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t pdpt_phys;
	uint64_t pd_phys;
	uint64_t pt_phys;
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;
	uint64_t parent_entry;
	int failures = 0;

	failures += test_expect_bool("free failure setup map",
				     x86_64_pmap_map_page_in_owned_root(
					     test_page_phys(0), vaddr,
					     0x12345000ull, 0),
				     true);

	pdpt_phys = pte_phys(pml4[PML4_INDEX(vaddr)]);
	pdpt = hal_mmu_direct_phys_to_virt(pdpt_phys);
	pd_phys = pte_phys(pdpt[PDPT_INDEX(vaddr)]);
	pd = hal_mmu_direct_phys_to_virt(pd_phys);
	pt_phys = pte_phys(pd[PD_INDEX(vaddr)]);
	pt = hal_mmu_direct_phys_to_virt(pt_phys);
	parent_entry = pd[PD_INDEX(vaddr)];

	free_fail_phys = pt_phys;
	failures += test_expect_bool("unmap reports free failure",
				     x86_64_pmap_unmap_page_in_owned_root(
					     test_page_phys(0), vaddr),
				     false);
	failures += test_expect_u64("unmap preserves parent entry",
				    pd[PD_INDEX(vaddr)], parent_entry);
	failures += test_expect_u64("unmap keeps failed table allocated",
				    allocated_page_count(), 3);
	failures += test_expect_u64("unmap clears requested leaf",
				    pt[PT_INDEX(vaddr)], 0);

	return failures;
}

static int test_active_kernel_unmap_invalidates(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("active unmap setup",
				     x86_64_pmap_map_page_in_owned_root(
					     test_page_phys(0), vaddr,
					     0x12345000ull, 0),
				     true);
	invalidate_count = 0;
	invalidated_vaddr = UINTPTR_MAX;

	failures += test_expect_bool("active unmap",
				     x86_64_pmap_unmap_kernel_page(vaddr),
				     true);
	failures += test_expect_u64("active unmap invalidates",
				    invalidate_count, 1);
	failures += test_expect_u64("active unmap invalidated vaddr",
				    invalidated_vaddr, vaddr);

	return failures;
}

static int test_unmap_keeps_shared_tables_until_empty(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t next_vaddr = vaddr + PAGE_SIZE;
	int failures = 0;

	failures += test_expect_bool("shared setup first",
				     x86_64_pmap_map_page_in_owned_root(
					     test_page_phys(0), vaddr,
					     0x12345000ull, 0),
				     true);
	failures += test_expect_bool("shared setup second",
				     x86_64_pmap_map_page_in_owned_root(
					     test_page_phys(0), next_vaddr,
					     0x12346000ull, 0),
				     true);
	failures += test_expect_u64("shared setup tables",
				    allocated_page_count(), 3);
	failures += test_expect_bool("shared unmap first",
				     x86_64_pmap_unmap_page_in_owned_root(
					     test_page_phys(0), vaddr),
				     true);
	failures += test_expect_u64("shared tables remain",
				    allocated_page_count(), 3);
	failures += test_expect_bool("shared unmap second",
				     x86_64_pmap_unmap_page_in_owned_root(
					     test_page_phys(0), next_vaddr),
				     true);
	failures += test_expect_u64("shared tables freed",
				    allocated_page_count(), 0);

	return failures;
}

static int test_unmap_page_rejects_invalid_paths(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt = test_table(1);
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("unmap reject unaligned",
				     x86_64_pmap_unmap_page_in_owned_root(test_page_phys(0),
							    vaddr + 1),
				     false);
	failures += test_expect_bool("unmap reject absent",
				     x86_64_pmap_unmap_page_in_owned_root(test_page_phys(0),
							    vaddr),
				     false);

	pml4[PML4_INDEX(vaddr)] = test_page_phys(1) | PAGE_PRESENT | PAGE_RW;
	pdpt[PDPT_INDEX(vaddr)] = 0x40000000ull | PAGE_PRESENT | PAGE_RW |
				  PAGE_PS;
	failures += test_expect_bool("unmap reject huge leaf",
				     x86_64_pmap_unmap_page_in_owned_root(test_page_phys(0),
							    vaddr),
				     false);

	return failures;
}

static int test_clone_copies_4k_leaf_path(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt = test_table(1);
	uint64_t *pd = test_table(2);
	uint64_t *pt = test_table(3);
	uint64_t new_pml4_phys = UINT64_MAX;
	uint64_t new_pdpt_phys;
	uint64_t new_pd_phys;
	uint64_t new_pt_phys;
	uint64_t *new_pml4;
	uint64_t *new_pdpt;
	uint64_t *new_pd;
	uint64_t *new_pt;
	int failures = 0;

	pml4[1] = test_page_phys(1) | PAGE_PRESENT | PAGE_RW;
	pdpt[2] = test_page_phys(2) | PAGE_PRESENT | PAGE_RW;
	pd[3] = test_page_phys(3) | PAGE_PRESENT | PAGE_RW;
	pt[4] = 0x12345000ull | PAGE_PRESENT | PAGE_RW;

	failures += test_expect_bool("clone 4k leaf",
				     x86_64_pmap_clone_kernel_page_tables(
					     test_page_phys(0), &new_pml4_phys),
				     true);
	failures += test_expect_u64("clone 4k allocated tables",
				    allocated_page_count(), 4);

	new_pml4 = hal_mmu_direct_phys_to_virt(new_pml4_phys);
	new_pdpt_phys = pte_phys(new_pml4[1]);
	new_pdpt = hal_mmu_direct_phys_to_virt(new_pdpt_phys);
	new_pd_phys = pte_phys(new_pdpt[2]);
	new_pd = hal_mmu_direct_phys_to_virt(new_pd_phys);
	new_pt_phys = pte_phys(new_pd[3]);
	new_pt = hal_mmu_direct_phys_to_virt(new_pt_phys);

	failures += test_expect_bool("clone pml4 child replaced",
				     new_pdpt_phys != test_page_phys(1), true);
	failures += test_expect_bool("clone pdpt child replaced",
				     new_pd_phys != test_page_phys(2), true);
	failures += test_expect_bool("clone pd child replaced",
				     new_pt_phys != test_page_phys(3), true);
	failures += test_expect_u64("clone pml4 flags", pte_flags(new_pml4[1]),
				    PAGE_PRESENT | PAGE_RW);
	failures += test_expect_u64("clone pdpt flags", pte_flags(new_pdpt[2]),
				    PAGE_PRESENT | PAGE_RW);
	failures += test_expect_u64("clone pd flags", pte_flags(new_pd[3]),
				    PAGE_PRESENT | PAGE_RW);
	failures += test_expect_u64("clone 4k leaf entry", new_pt[4], pt[4]);
	failures += test_expect_u64("clone absent entry", new_pml4[5], 0);

	return failures;
}

static int test_clone_preserves_huge_leaf_entries(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt = test_table(1);
	uint64_t *pd = test_table(2);
	uint64_t new_pml4_phys = UINT64_MAX;
	uint64_t *new_pml4;
	uint64_t *new_pdpt;
	uint64_t *new_pd;
	int failures = 0;

	pml4[0] = test_page_phys(1) | PAGE_PRESENT | PAGE_RW;
	pdpt[1] = 0x40000000ull | PAGE_PRESENT | PAGE_RW | PAGE_PS;
	pdpt[2] = test_page_phys(2) | PAGE_PRESENT | PAGE_RW;
	pd[7] = 0x200000ull | PAGE_PRESENT | PAGE_RW | PAGE_PS;

	failures += test_expect_bool("clone huge leaves",
				     x86_64_pmap_clone_kernel_page_tables(
					     test_page_phys(0), &new_pml4_phys),
				     true);
	failures += test_expect_u64("clone huge allocated tables",
				    allocated_page_count(), 3);

	new_pml4 = hal_mmu_direct_phys_to_virt(new_pml4_phys);
	new_pdpt = hal_mmu_direct_phys_to_virt(pte_phys(new_pml4[0]));
	new_pd = hal_mmu_direct_phys_to_virt(pte_phys(new_pdpt[2]));

	failures += test_expect_u64("clone 1g leaf", new_pdpt[1], pdpt[1]);
	failures += test_expect_u64("clone 2m leaf", new_pd[7], pd[7]);

	return failures;
}

static int test_clone_failure_releases_allocated_tables(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt = test_table(1);
	uint64_t *pd = test_table(2);
	uint64_t *pt = test_table(3);
	uint64_t new_pml4_phys = UINT64_MAX;
	int failures = 0;

	pml4[1] = test_page_phys(1) | PAGE_PRESENT | PAGE_RW;
	pdpt[2] = test_page_phys(2) | PAGE_PRESENT | PAGE_RW;
	pd[3] = test_page_phys(3) | PAGE_PRESENT | PAGE_RW;
	pt[4] = 0x12345000ull | PAGE_PRESENT | PAGE_RW;
	alloc_fail_after = 2;

	failures += test_expect_bool("clone allocation failure",
				     x86_64_pmap_clone_kernel_page_tables(
					     test_page_phys(0), &new_pml4_phys),
				     false);
	failures += test_expect_u64("clone allocation rollback",
				    allocated_page_count(), 0);

	return failures;
}

static int test_clone_direct_map_failure_releases_allocated_tables(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t new_pml4_phys = UINT64_MAX;
	int failures = 0;

	pml4[0] = test_page_phys(1) | PAGE_PRESENT | PAGE_RW;
	direct_map_blocked_phys = test_page_phys(1);

	failures += test_expect_bool("clone direct-map failure",
				     x86_64_pmap_clone_kernel_page_tables(
					     test_page_phys(0), &new_pml4_phys),
				     false);
	failures += test_expect_u64("clone direct-map rollback",
				    allocated_page_count(), 0);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_map_page_allocates_missing_path),
		TEST_CASE(test_map_page_reuses_existing_tables),
		TEST_CASE(test_active_kernel_map_invalidates),
		TEST_CASE(test_map_page_rejects_invalid_inputs),
		TEST_CASE(test_map_page_rejects_existing_leaf),
		TEST_CASE(test_map_page_rejects_huge_intermediate),
		TEST_CASE(test_map_page_rolls_back_on_allocation_failure),
		TEST_CASE(test_map_page_rolls_back_on_direct_map_failure),
		TEST_CASE(test_translate_handles_leaf_sizes),
		TEST_CASE(test_unmap_page_clears_leaf),
		TEST_CASE(test_unmap_preserves_parent_on_free_failure),
		TEST_CASE(test_active_kernel_unmap_invalidates),
		TEST_CASE(test_unmap_keeps_shared_tables_until_empty),
		TEST_CASE(test_unmap_page_rejects_invalid_paths),
		TEST_CASE(test_clone_copies_4k_leaf_path),
		TEST_CASE(test_clone_preserves_huge_leaf_entries),
		TEST_CASE(test_clone_failure_releases_allocated_tables),
		TEST_CASE(test_clone_direct_map_failure_releases_allocated_tables),
	};

	return test_run_cases_with_fixture("x86_64_pmap_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_pmap_test, NULL);
}
