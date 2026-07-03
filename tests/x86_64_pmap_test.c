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
static uint64_t direct_map_blocked_phys;
static uint64_t flush_count;

static uint64_t test_page_phys(uint64_t page)
{
	return page * PAGE_SIZE;
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
	direct_map_blocked_phys = UINT64_MAX;
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

	page_allocated[page] = false;
	return true;
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
		TEST_CASE(test_clone_copies_4k_leaf_path),
		TEST_CASE(test_clone_preserves_huge_leaf_entries),
		TEST_CASE(test_clone_failure_releases_allocated_tables),
		TEST_CASE(test_clone_direct_map_failure_releases_allocated_tables),
	};

	return test_run_cases_with_fixture("x86_64_pmap_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_pmap_test, NULL);
}
