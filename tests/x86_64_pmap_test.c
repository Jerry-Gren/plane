#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>
#include <hal/x86_64/pmap.h>
#include <plane/compiler.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/vm_prot.h>

#include "support/test.h"
#include <x86_64/physmap_internal.h>
#include <x86_64/pmap_internal.h>

#define TEST_PAGE_COUNT 720
#define TEST_ALLOC_START_PAGE 16
#define TEST_PHYS_SIZE (TEST_PAGE_COUNT * PAGE_SIZE)

static uint8_t phys_storage[TEST_PHYS_SIZE] __aligned(PAGE_SIZE);
static bool page_allocated[TEST_PAGE_COUNT];
static uint64_t alloc_attempts;
static uint64_t alloc_fail_after;
static uint64_t physmap_blocked_phys;
static uintptr_t invalidated_vaddr;
static uint64_t invalidate_count;
static uint64_t flush_count;
static bool test_pat_wc_ready;
static plane_vaddr_t test_physmap_base;
static uint64_t test_physmap_size;
static bool test_physmap_initialized;

static uint64_t test_page_phys(uint64_t page)
{
	return page * PAGE_SIZE;
}

static plane_vaddr_t test_vaddr(uint64_t raw)
{
	return plane_vaddr_make(raw);
}

static plane_paddr_t test_paddr(uint64_t raw)
{
	return plane_paddr_make(raw);
}

static uint64_t test_paddr_raw(plane_paddr_t addr)
{
	return plane_paddr_raw(addr);
}

plane_paddr_t x86_64_pmap_active_root_phys(void)
{
	return test_paddr(test_page_phys(0));
}

static uint64_t *test_table(uint64_t page)
{
	return (uint64_t *)&phys_storage[test_page_phys(page)];
}

static uint64_t pte_flags(uint64_t entry)
{
	return x86_64_paging_entry_flags(entry);
}

static uint64_t pte_phys(uint64_t entry)
{
	return x86_64_paging_entry_phys(entry);
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
	physmap_blocked_phys = UINT64_MAX;
	invalidated_vaddr = UINTPTR_MAX;
	invalidate_count = 0;
	flush_count = 0;
	test_pat_wc_ready = true;
	test_physmap_base = test_vaddr(X86_64_PHYSMAP_BASE);
	test_physmap_size = ARCH_HUGE_PAGE_SIZE;
	test_physmap_initialized = true;
}

plane_vaddr_t hal_mmu_physmap_phys_range_to_virt(plane_paddr_t phys_addr,
						uint64_t size)
{
	uint64_t raw = test_paddr_raw(phys_addr);
	uint64_t end;

	if (size == 0 || raw > UINT64_MAX - size) {
		return plane_vaddr_make(0);
	}

	end = raw + size;
	if (end > TEST_PHYS_SIZE) {
		return plane_vaddr_make(0);
	}

	if (physmap_blocked_phys != UINT64_MAX &&
	    raw < physmap_blocked_phys + PAGE_SIZE &&
	    end > physmap_blocked_phys) {
		return plane_vaddr_make(0);
	}

	return plane_vaddr_from_ptr(&phys_storage[raw]);
}

plane_vaddr_t hal_mmu_physmap_phys_to_virt(plane_paddr_t phys_addr)
{
	return hal_mmu_physmap_phys_range_to_virt(phys_addr, 1);
}

void hal_mmu_invalidate_tlb(plane_vaddr_t vaddr)
{
	invalidated_vaddr = plane_vaddr_raw(vaddr);
	invalidate_count++;
}

void hal_mmu_flush_tlb_all(void)
{
	flush_count++;
}

bool plane_pmm_alloc_pages_phys_flags(uint64_t page_count,
				      uint64_t alignment_pages,
				      uint32_t flags,
				      plane_paddr_t *phys_addr)
{
	uint64_t raw;

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
			raw = test_page_phys(i);
			*phys_addr = test_paddr(raw);
			if ((flags & PLANE_PMM_ALLOC_ZERO) != 0) {
				memset(&phys_storage[raw], 0, PAGE_SIZE);
			}
			return true;
		}
	}

	return false;
}

bool plane_pmm_free_page_phys(plane_paddr_t phys_addr)
{
	uint64_t raw = test_paddr_raw(phys_addr);
	uint64_t page = raw / PAGE_SIZE;

	if ((raw & (PAGE_SIZE - 1)) != 0 || page >= TEST_PAGE_COUNT ||
	    !page_allocated[page]) {
		return false;
	}

	page_allocated[page] = false;
	return true;
}

bool x86_64_pat_write_combine_ready(void)
{
	return test_pat_wc_ready;
}

bool x86_64_physmap_get_runtime(struct x86_64_physmap_runtime *runtime)
{
	if (runtime == NULL || !test_physmap_initialized) {
		return false;
	}

	runtime->bootstrap_base = test_physmap_base;
	runtime->bootstrap_size = X86_64_PHYSMAP_BOOTSTRAP_SIZE;
	runtime->required_size = test_physmap_size;
	runtime->owned_window_size = X86_64_PHYSMAP_BOOTSTRAP_SIZE;
	runtime->owned_pml4_count = 1;
	return true;
}

void x86_64_physmap_commit_owned(void)
{
	test_physmap_base = test_vaddr(X86_64_PHYSMAP_BASE);
}

static void *test_physmap_phys_to_virt(uint64_t phys_addr)
{
	plane_vaddr_t vaddr = hal_mmu_physmap_phys_to_virt(
		test_paddr(phys_addr));

	if (plane_vaddr_is_null(vaddr)) {
		return NULL;
	}

	return plane_vaddr_to_ptr(vaddr);
}

static struct hal_mmu_map_options test_map_options(
	uint32_t prot,
	enum hal_mmu_mapping_attr attr)
{
	return (struct hal_mmu_map_options){
		.prot = prot,
		.attr = attr,
	};
}

static struct hal_mmu_map_options test_default_options(uint32_t prot)
{
	return hal_mmu_default_map_options(prot);
}

static bool test_pmap_map_in_root(uint64_t root,
				  uint64_t vaddr,
				  uint64_t phys_addr,
				  struct hal_mmu_map_options options)
{
	return x86_64_pmap_map_page_in_owned_root(
		test_paddr(root), test_vaddr(vaddr), test_paddr(phys_addr),
		options);
}

static bool test_pmap_unmap_in_root(uint64_t root, uint64_t vaddr)
{
	return x86_64_pmap_unmap_page_in_owned_root(test_paddr(root),
						    test_vaddr(vaddr));
}

static bool test_pmap_translate_in_root(uint64_t root,
					uint64_t vaddr,
					uint64_t *phys_addr)
{
	plane_paddr_t out;

	if (!x86_64_pmap_translate_in_root(test_paddr(root),
					   test_vaddr(vaddr),
					   phys_addr == NULL ? NULL : &out)) {
		return false;
	}

	*phys_addr = test_paddr_raw(out);
	return true;
}

static bool test_pmap_clone_kernel_page_tables(uint64_t source_pml4_phys,
					       uint64_t *new_pml4_phys)
{
	plane_paddr_t out;

	if (new_pml4_phys == NULL ||
	    !x86_64_pmap_clone_kernel_page_tables(test_paddr(source_pml4_phys),
						  NULL, 0, &out)) {
		return false;
	}

	*new_pml4_phys = test_paddr_raw(out);
	return true;
}

static bool test_pmap_clone_kernel_page_tables_skipping(
	uint64_t source_pml4_phys,
	const struct x86_64_pmap_skip_range *skip,
	uint64_t skip_count,
	uint64_t *new_pml4_phys)
{
	plane_paddr_t out;

	if (new_pml4_phys == NULL ||
	    !x86_64_pmap_clone_kernel_page_tables(test_paddr(source_pml4_phys),
						  skip, skip_count, &out)) {
		return false;
	}

	*new_pml4_phys = test_paddr_raw(out);
	return true;
}

static bool test_pmap_map_kernel_page(uint64_t vaddr,
				      uint64_t phys_addr,
				      struct hal_mmu_map_options options)
{
	return x86_64_pmap_map_kernel_page(test_vaddr(vaddr),
					   test_paddr(phys_addr),
					   options);
}

static bool test_pmap_unmap_kernel_page(uint64_t vaddr)
{
	return x86_64_pmap_unmap_kernel_page(test_vaddr(vaddr));
}

static bool test_pmap_protect_kernel_page(uint64_t vaddr, uint32_t prot)
{
	return x86_64_pmap_protect_kernel_page(test_vaddr(vaddr), prot);
}

static bool test_hal_map_kernel_page(uint64_t vaddr,
				     uint64_t phys_addr,
				     struct hal_mmu_map_options options)
{
	return hal_mmu_map_kernel_page(test_vaddr(vaddr), test_paddr(phys_addr),
				       options);
}

static bool test_hal_translate_kernel_page(uint64_t vaddr, uint64_t *phys_addr)
{
	plane_paddr_t out;

	if (!hal_mmu_translate_kernel_page(test_vaddr(vaddr),
					   phys_addr == NULL ? NULL : &out)) {
		return false;
	}

	*phys_addr = test_paddr_raw(out);
	return true;
}

static bool test_hal_unmap_kernel_page(uint64_t vaddr)
{
	return hal_mmu_unmap_kernel_page(test_vaddr(vaddr));
}

static bool test_hal_protect_kernel_page(uint64_t vaddr, uint32_t prot)
{
	return hal_mmu_protect_kernel_page(test_vaddr(vaddr), prot);
}

static uint64_t *test_kernel_pte(uint64_t vaddr)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt;
	uint64_t *pd;
	uint64_t *pt;

	pdpt = test_physmap_phys_to_virt(
		pte_phys(pml4[X86_64_PAGING_PML4_INDEX(vaddr)]));
	pd = test_physmap_phys_to_virt(
		pte_phys(pdpt[X86_64_PAGING_PDPT_INDEX(vaddr)]));
	pt = test_physmap_phys_to_virt(
		pte_phys(pd[X86_64_PAGING_PD_INDEX(vaddr)]));
	return &pt[X86_64_PAGING_PT_INDEX(vaddr)];
}

#define hal_mmu_physmap_phys_to_virt(phys_addr) \
	test_physmap_phys_to_virt((phys_addr))
#define x86_64_pmap_map_page_in_owned_root(root, vaddr, phys_addr, options) \
	test_pmap_map_in_root((root), (vaddr), (phys_addr), (options))
#define x86_64_pmap_unmap_page_in_owned_root(root, vaddr) \
	test_pmap_unmap_in_root((root), (vaddr))
#define x86_64_pmap_translate_in_root(root, vaddr, phys_addr) \
	test_pmap_translate_in_root((root), (vaddr), (phys_addr))
#define x86_64_pmap_clone_kernel_page_tables(source, out) \
	test_pmap_clone_kernel_page_tables((source), (out))
#define x86_64_pmap_map_kernel_page(vaddr, phys_addr, options) \
	test_pmap_map_kernel_page((vaddr), (phys_addr), (options))
#define x86_64_pmap_unmap_kernel_page(vaddr) \
	test_pmap_unmap_kernel_page((vaddr))
#define x86_64_pmap_protect_kernel_page(vaddr, prot) \
	test_pmap_protect_kernel_page((vaddr), (prot))
#define hal_mmu_map_kernel_page(vaddr, phys_addr, options) \
	test_hal_map_kernel_page((vaddr), (phys_addr), (options))
#define hal_mmu_translate_kernel_page(vaddr, phys_addr) \
	test_hal_translate_kernel_page((vaddr), (phys_addr))
#define hal_mmu_unmap_kernel_page(vaddr) \
	test_hal_unmap_kernel_page((vaddr))
#define hal_mmu_protect_kernel_page(vaddr, prot) \
	test_hal_protect_kernel_page((vaddr), (prot))

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
					     test_default_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE)),
				     true);
	failures += test_expect_u64("map allocated intermediate tables",
				    allocated_page_count(), 3);
	failures += test_expect_u64("root map does not invalidate",
				    invalidate_count, 0);
	failures += test_expect_u64("root map leaves invalidated vaddr",
				    invalidated_vaddr, UINTPTR_MAX);

	pdpt = hal_mmu_physmap_phys_to_virt(pte_phys(pml4[X86_64_PAGING_PML4_INDEX(vaddr)]));
	pd = hal_mmu_physmap_phys_to_virt(pte_phys(pdpt[X86_64_PAGING_PDPT_INDEX(vaddr)]));
	pt = hal_mmu_physmap_phys_to_virt(pte_phys(pd[X86_64_PAGING_PD_INDEX(vaddr)]));
	failures += test_expect_u64("map pml4 flags",
				    pte_flags(pml4[X86_64_PAGING_PML4_INDEX(vaddr)]),
				    X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE);
	failures += test_expect_u64("map pdpt flags",
				    pte_flags(pdpt[X86_64_PAGING_PDPT_INDEX(vaddr)]),
				    X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE);
	failures += test_expect_u64("map pd flags",
				    pte_flags(pd[X86_64_PAGING_PD_INDEX(vaddr)]),
				    X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE);
	failures += test_expect_u64("map pte",
				    pt[X86_64_PAGING_PT_INDEX(vaddr)],
				    phys | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE);
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
							  vaddr, phys,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
				     true);
	failures += test_expect_bool("map adjacent page",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  next_vaddr,
							  next_phys,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
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
					     test_default_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE)),
				     true);
	failures += test_expect_u64("active map invalidates",
				    invalidate_count, 1);
	failures += test_expect_u64("active map invalidated vaddr",
				    invalidated_vaddr, vaddr);

	return failures;
}

static int test_active_kernel_protect_updates_writable_bit(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t *pte;
	int failures = 0;

	failures += test_expect_bool("protect setup map",
				     x86_64_pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE)),
				     true);
	pte = test_kernel_pte(vaddr);
	failures += test_expect_u64("protect setup writable",
				    *pte & X86_64_PAGING_ENTRY_WRITE, X86_64_PAGING_ENTRY_WRITE);

	invalidate_count = 0;
	invalidated_vaddr = UINTPTR_MAX;
	failures += test_expect_bool("protect readonly",
				     x86_64_pmap_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_u64("protect readonly clears write",
				    *pte & X86_64_PAGING_ENTRY_WRITE, 0);
	failures += test_expect_u64("protect readonly invalidates",
				    invalidate_count, 1);
	failures += test_expect_u64("protect readonly vaddr",
				    invalidated_vaddr, vaddr);

	failures += test_expect_bool("protect writable",
				     x86_64_pmap_protect_kernel_page(
					     vaddr,
					     PLANE_VM_PROT_READ |
					     PLANE_VM_PROT_WRITE),
				     true);
	failures += test_expect_u64("protect writable sets write",
				    *pte & X86_64_PAGING_ENTRY_WRITE, X86_64_PAGING_ENTRY_WRITE);
	failures += test_expect_u64("protect writable invalidates again",
				    invalidate_count, 2);
	return failures;
}

static int test_mapping_attrs_encode_pte_cache_bits(void)
{
	uint64_t device_vaddr = 0xffff800000402000ull;
	uint64_t wc_vaddr = device_vaddr + PAGE_SIZE;
	uint64_t *device_pte;
	uint64_t *wc_pte;
	int failures = 0;

	failures += test_expect_bool("map device",
				     x86_64_pmap_map_kernel_page(
					     device_vaddr, 0x12345000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE,
						     HAL_MMU_MAPPING_DEVICE)),
				     true);
	device_pte = test_kernel_pte(device_vaddr);
	failures += test_expect_u64("device pcd",
				    *device_pte & X86_64_PAGING_ENTRY_PCD,
				    X86_64_PAGING_ENTRY_PCD);
	failures += test_expect_u64("device pwt",
				    *device_pte & X86_64_PAGING_ENTRY_PWT,
				    X86_64_PAGING_ENTRY_PWT);
	failures += test_expect_u64("device writable",
				    *device_pte & X86_64_PAGING_ENTRY_WRITE,
				    X86_64_PAGING_ENTRY_WRITE);

	failures += test_expect_bool("map wc",
				     hal_mmu_map_kernel_page(
					     wc_vaddr, 0x12346000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ,
						     HAL_MMU_MAPPING_WRITE_COMBINE)),
				     true);
	wc_pte = test_kernel_pte(wc_vaddr);
	failures += test_expect_u64("wc pwt",
				    *wc_pte & X86_64_PAGING_ENTRY_PWT,
				    X86_64_PAGING_ENTRY_PWT);
	failures += test_expect_u64("wc no pcd",
				    *wc_pte & X86_64_PAGING_ENTRY_PCD, 0);
	failures += test_expect_u64("wc readonly",
				    *wc_pte & X86_64_PAGING_ENTRY_WRITE, 0);

	return failures;
}

static int test_mapping_attrs_validate_and_protect_preserves_cache_bits(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t *pte;
	int failures = 0;

	failures += test_expect_bool("reject pmap invalid attr",
				     x86_64_pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ,
						     (enum hal_mmu_mapping_attr)99)),
				     false);
	failures += test_expect_bool("reject hal invalid attr",
				     hal_mmu_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ,
						     (enum hal_mmu_mapping_attr)99)),
				     false);
	test_pat_wc_ready = false;
	failures += test_expect_bool("reject wc before pat init",
				     hal_mmu_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ,
						     HAL_MMU_MAPPING_WRITE_COMBINE)),
				     false);
	test_pat_wc_ready = true;
	failures += test_expect_bool("map device writable",
				     hal_mmu_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE,
						     HAL_MMU_MAPPING_DEVICE)),
				     true);
	pte = test_kernel_pte(vaddr);
	failures += test_expect_bool("protect readonly",
				     hal_mmu_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_u64("protect preserves pcd",
				    *pte & X86_64_PAGING_ENTRY_PCD,
				    X86_64_PAGING_ENTRY_PCD);
	failures += test_expect_u64("protect preserves pwt",
				    *pte & X86_64_PAGING_ENTRY_PWT,
				    X86_64_PAGING_ENTRY_PWT);
	failures += test_expect_u64("protect clears write",
				    *pte & X86_64_PAGING_ENTRY_WRITE, 0);

	return failures;
}

static int test_hal_kernel_page_wrappers(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t phys = UINT64_MAX;
	int failures = 0;

	failures += test_expect_bool("hal map",
				     hal_mmu_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE)),
				     true);
	failures += test_expect_u64("hal map invalidates",
				    invalidate_count, 1);
	failures += test_expect_bool("hal translate",
				     hal_mmu_translate_kernel_page(vaddr, &phys),
				     true);
	failures += test_expect_u64("hal translate phys", phys,
				    0x12345000ull);
	failures += test_expect_bool("hal protect readonly",
				     hal_mmu_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_bool("hal unmap",
				     hal_mmu_unmap_kernel_page(vaddr), true);
	failures += test_expect_u64("hal wrappers invalidate",
				    invalidate_count, 3);
	failures += test_expect_bool("hal reject invalid attr",
				     hal_mmu_map_kernel_page(vaddr,
							     0x12345000ull,
							     test_map_options(
								     PLANE_VM_PROT_READ,
								     (enum hal_mmu_mapping_attr)99)),
				     false);
	failures += test_expect_bool("hal reject invalid protect prot",
				     hal_mmu_protect_kernel_page(vaddr,
								 BIT(8)),
				     false);

	return failures;
}

static int test_protect_page_rejects_invalid_paths(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt = test_table(1);
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("protect reject unaligned",
				     x86_64_pmap_protect_kernel_page(vaddr + 1,
								     PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("protect reject bad prot",
				     x86_64_pmap_protect_kernel_page(vaddr,
								     BIT(31)),
				     false);
	failures += test_expect_bool("protect reject absent",
				     x86_64_pmap_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     false);

	pml4[X86_64_PAGING_PML4_INDEX(vaddr)] = test_page_phys(1) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pdpt[X86_64_PAGING_PDPT_INDEX(vaddr)] = 0x40000000ull | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE |
				  X86_64_PAGING_ENTRY_PS;
	failures += test_expect_bool("protect reject huge",
				     x86_64_pmap_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("protect reject no invalidate",
				    invalidate_count, 0);
	return failures;
}

static int test_map_page_rejects_invalid_inputs(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("map reject unaligned vaddr",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr + 1,
							  0x12345000ull,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
				     false);
	failures += test_expect_bool("map reject unaligned phys",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345001ull,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
				     false);
	failures += test_expect_bool("map reject bad prot",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345000ull,
							  test_default_options(
								  BIT(31))),
				     false);
	failures += test_expect_bool("map reject unaligned root",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0) + 1,
							  vaddr,
							  0x12345000ull,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
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
							  0x12345000ull,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
				     true);
	failures += test_expect_bool("map reject duplicate leaf",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12346000ull,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
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

	pml4[X86_64_PAGING_PML4_INDEX(vaddr)] = test_page_phys(1) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pdpt[X86_64_PAGING_PDPT_INDEX(vaddr)] = 0x40000000ull | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE |
				  X86_64_PAGING_ENTRY_PS;

	failures += test_expect_bool("map reject huge intermediate",
				     x86_64_pmap_map_page_in_owned_root(test_page_phys(0),
							  vaddr,
							  0x12345000ull,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
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
							  0x12345000ull,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
				     false);
	failures += test_expect_u64("map allocation rollback",
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

	pml4[X86_64_PAGING_PML4_INDEX(vaddr_4k)] = test_page_phys(1) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pdpt[X86_64_PAGING_PDPT_INDEX(vaddr_4k)] = test_page_phys(2) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pd[X86_64_PAGING_PD_INDEX(vaddr_4k)] = test_page_phys(3) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pt[X86_64_PAGING_PT_INDEX(vaddr_4k)] = 0x12345000ull | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pd[X86_64_PAGING_PD_INDEX(vaddr_2m)] = 0x200000ull | BIT_ULL(12) | X86_64_PAGING_ENTRY_PRESENT |
				 X86_64_PAGING_ENTRY_WRITE | X86_64_PAGING_ENTRY_PS;
	pdpt[X86_64_PAGING_PDPT_INDEX(vaddr_1g)] =
		0x40000000ull | BIT_ULL(12) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE |
		X86_64_PAGING_ENTRY_PS;

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
							  0x12345000ull,
							  test_default_options(
								  PLANE_VM_PROT_READ)),
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

static int test_active_kernel_unmap_invalidates(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("active unmap setup",
				     x86_64_pmap_map_page_in_owned_root(
					     test_page_phys(0), vaddr,
					     0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ)),
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
					     0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ)),
				     true);
	failures += test_expect_bool("shared setup second",
				     x86_64_pmap_map_page_in_owned_root(
					     test_page_phys(0), next_vaddr,
					     0x12346000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ)),
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

	pml4[X86_64_PAGING_PML4_INDEX(vaddr)] = test_page_phys(1) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pdpt[X86_64_PAGING_PDPT_INDEX(vaddr)] = 0x40000000ull | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE |
				  X86_64_PAGING_ENTRY_PS;
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

	pml4[1] = test_page_phys(1) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pdpt[2] = test_page_phys(2) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pd[3] = test_page_phys(3) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pt[4] = 0x12345000ull | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;

	failures += test_expect_bool("clone 4k leaf",
				     x86_64_pmap_clone_kernel_page_tables(
					     test_page_phys(0), &new_pml4_phys),
				     true);
	failures += test_expect_bool("clone pml4 phys nonzero",
				     new_pml4_phys != 0, true);
	failures += test_expect_u64("clone 4k allocated tables",
				    allocated_page_count(), 4);

	new_pml4 = hal_mmu_physmap_phys_to_virt(new_pml4_phys);
	new_pdpt_phys = pte_phys(new_pml4[1]);
	new_pdpt = hal_mmu_physmap_phys_to_virt(new_pdpt_phys);
	new_pd_phys = pte_phys(new_pdpt[2]);
	new_pd = hal_mmu_physmap_phys_to_virt(new_pd_phys);
	new_pt_phys = pte_phys(new_pd[3]);
	new_pt = hal_mmu_physmap_phys_to_virt(new_pt_phys);

	failures += test_expect_bool("clone pml4 child replaced",
				     new_pdpt_phys != test_page_phys(1), true);
	failures += test_expect_bool("clone pdpt child replaced",
				     new_pd_phys != test_page_phys(2), true);
	failures += test_expect_bool("clone pd child replaced",
				     new_pt_phys != test_page_phys(3), true);
	failures += test_expect_u64("clone pml4 flags", pte_flags(new_pml4[1]),
				    X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE);
	failures += test_expect_u64("clone pdpt flags", pte_flags(new_pdpt[2]),
				    X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE);
	failures += test_expect_u64("clone pd flags", pte_flags(new_pd[3]),
				    X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE);
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

	pml4[0] = test_page_phys(1) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pdpt[1] = 0x40000000ull | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE | X86_64_PAGING_ENTRY_PS;
	pdpt[2] = test_page_phys(2) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pd[7] = 0x200000ull | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE | X86_64_PAGING_ENTRY_PS;

	failures += test_expect_bool("clone huge leaves",
				     x86_64_pmap_clone_kernel_page_tables(
					     test_page_phys(0), &new_pml4_phys),
				     true);
	failures += test_expect_bool("clone huge pml4 phys nonzero",
				     new_pml4_phys != 0, true);
	failures += test_expect_u64("clone huge allocated tables",
				    allocated_page_count(), 3);

	new_pml4 = hal_mmu_physmap_phys_to_virt(new_pml4_phys);
	new_pdpt = hal_mmu_physmap_phys_to_virt(pte_phys(new_pml4[0]));
	new_pd = hal_mmu_physmap_phys_to_virt(pte_phys(new_pdpt[2]));

	failures += test_expect_u64("clone 1g leaf", new_pdpt[1], pdpt[1]);
	failures += test_expect_u64("clone 2m leaf", new_pd[7], pd[7]);

	return failures;
}

static int test_clone_skips_physmap_pml4_ranges(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *ordinary_pdpt = test_table(3);
	uint64_t new_pml4_phys = UINT64_MAX;
	uint64_t *new_pml4;
	struct x86_64_pmap_skip_range skip[2] = {
		{
			.base = test_vaddr(X86_64_PHYSMAP_BASE),
			.size = ARCH_HUGE_PAGE_SIZE,
		},
		{
			.base = test_vaddr(X86_64_PHYSMAP_BASE +
					   X86_64_PHYSMAP_WINDOW_SIZE),
			.size = ARCH_HUGE_PAGE_SIZE,
		},
	};
	uint64_t final_index =
		X86_64_PAGING_PML4_INDEX(X86_64_PHYSMAP_BASE);
	uint64_t boot_index = X86_64_PAGING_PML4_INDEX(
		X86_64_PHYSMAP_BASE + X86_64_PHYSMAP_WINDOW_SIZE);
	int failures = 0;

	pml4[final_index] = test_page_phys(1) |
			    X86_64_PAGING_ENTRY_PRESENT |
			    X86_64_PAGING_ENTRY_WRITE;
	pml4[boot_index] = test_page_phys(2) |
			   X86_64_PAGING_ENTRY_PRESENT |
			   X86_64_PAGING_ENTRY_WRITE;
	pml4[0] = test_page_phys(3) |
		  X86_64_PAGING_ENTRY_PRESENT |
		  X86_64_PAGING_ENTRY_WRITE;
	ordinary_pdpt[0] = 0x40000000ull |
			   X86_64_PAGING_ENTRY_PRESENT |
			   X86_64_PAGING_ENTRY_WRITE |
			   X86_64_PAGING_ENTRY_PS;

	failures += test_expect_bool(
		"clone skip physmap ranges",
		test_pmap_clone_kernel_page_tables_skipping(
			test_page_phys(0), skip, 2, &new_pml4_phys),
		true);
	new_pml4 = hal_mmu_physmap_phys_to_virt(new_pml4_phys);

	failures += test_expect_u64("clone skips final physmap",
				    new_pml4[final_index], 0);
	failures += test_expect_u64("clone skips boot physmap",
				    new_pml4[boot_index], 0);
	failures += test_expect_bool("clone keeps ordinary pml4",
				     x86_64_paging_entry_present(new_pml4[0]),
				     true);

	return failures;
}

static int test_physmap_builder_uses_2m_leaves(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt;
	uint64_t *pd0;
	uint64_t *pd1;
	uint64_t size = ARCH_HUGE_PAGE_SIZE + ARCH_LARGE_PAGE_SIZE;
	uint64_t pml4_index =
		X86_64_PAGING_PML4_INDEX(X86_64_PHYSMAP_BASE);
	int failures = 0;

	failures += test_expect_bool(
		"build physmap",
		x86_64_pmap_build_physmap_in_owned_root(
			test_paddr(test_page_phys(0)),
			test_vaddr(X86_64_PHYSMAP_BASE), size,
			X86_64_PHYSMAP_BOOTSTRAP_SIZE),
		true);
	failures += test_expect_u64("build physmap allocations",
				    allocated_page_count(), 3);

	pdpt = hal_mmu_physmap_phys_to_virt(pte_phys(pml4[pml4_index]));
	pd0 = hal_mmu_physmap_phys_to_virt(pte_phys(pdpt[0]));
	pd1 = hal_mmu_physmap_phys_to_virt(pte_phys(pdpt[1]));

	failures += test_expect_u64(
		"build first 2m leaf",
		pd0[0],
		X86_64_PAGING_ENTRY_PRESENT |
		X86_64_PAGING_ENTRY_WRITE |
		X86_64_PAGING_ENTRY_PS);
	failures += test_expect_u64(
		"build last 2m leaf",
		pd1[0],
		ARCH_HUGE_PAGE_SIZE |
		X86_64_PAGING_ENTRY_PRESENT |
		X86_64_PAGING_ENTRY_WRITE |
		X86_64_PAGING_ENTRY_PS);
	failures += test_expect_u64("build stops after requested size",
				    pd1[1], 0);

	return failures;
}

static int test_physmap_builder_crosses_pml4_slots(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t size = X86_64_PAGING_PML4_SLOT_SIZE + ARCH_LARGE_PAGE_SIZE;
	uint64_t first_index =
		X86_64_PAGING_PML4_INDEX(X86_64_PHYSMAP_BASE);
	uint64_t second_index =
		X86_64_PAGING_PML4_INDEX(X86_64_PHYSMAP_BASE +
					 X86_64_PAGING_PML4_SLOT_SIZE);
	uint64_t *second_pdpt;
	uint64_t *second_pd;
	int failures = 0;

	failures += test_expect_bool(
		"build physmap crosses pml4",
		x86_64_pmap_build_physmap_in_owned_root(
			test_paddr(test_page_phys(0)),
			test_vaddr(X86_64_PHYSMAP_BASE), size,
			2 * X86_64_PAGING_PML4_SLOT_SIZE),
		true);
	failures += test_expect_bool("first pml4 present",
				     x86_64_paging_entry_present(
					     pml4[first_index]),
				     true);
	failures += test_expect_bool("second pml4 present",
				     x86_64_paging_entry_present(
					     pml4[second_index]),
				     true);

	second_pdpt = hal_mmu_physmap_phys_to_virt(
		pte_phys(pml4[second_index]));
	second_pd = hal_mmu_physmap_phys_to_virt(pte_phys(second_pdpt[0]));
	failures += test_expect_u64(
		"second pml4 first leaf",
		second_pd[0],
		X86_64_PAGING_PML4_SLOT_SIZE |
		X86_64_PAGING_ENTRY_PRESENT |
		X86_64_PAGING_ENTRY_WRITE |
		X86_64_PAGING_ENTRY_PS);
	failures += test_expect_u64("second pml4 stops after requested size",
				    second_pd[1], 0);

	return failures;
}

static int test_physmap_builder_failure_rolls_back(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t pml4_index =
		X86_64_PAGING_PML4_INDEX(X86_64_PHYSMAP_BASE);
	int failures = 0;

	alloc_fail_after = 1;
	failures += test_expect_bool(
		"build physmap allocation failure",
		x86_64_pmap_build_physmap_in_owned_root(
			test_paddr(test_page_phys(0)),
			test_vaddr(X86_64_PHYSMAP_BASE),
			ARCH_HUGE_PAGE_SIZE,
			X86_64_PHYSMAP_BOOTSTRAP_SIZE),
		false);
	failures += test_expect_u64("build physmap rollback entry",
				    pml4[pml4_index], 0);
	failures += test_expect_u64("build physmap rollback allocations",
				    allocated_page_count(), 0);

	return failures;
}

static int test_physmap_builder_rejects_existing_range(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t pml4_index =
		X86_64_PAGING_PML4_INDEX(X86_64_PHYSMAP_BASE);
	int failures = 0;

	pml4[pml4_index] = test_page_phys(1) |
			   X86_64_PAGING_ENTRY_PRESENT |
			   X86_64_PAGING_ENTRY_WRITE;

	failures += test_expect_bool(
		"build physmap existing range",
		x86_64_pmap_build_physmap_in_owned_root(
			test_paddr(test_page_phys(0)),
			test_vaddr(X86_64_PHYSMAP_BASE),
			ARCH_HUGE_PAGE_SIZE,
			X86_64_PHYSMAP_BOOTSTRAP_SIZE),
		false);
	failures += test_expect_u64("build physmap preserves existing",
				    pml4[pml4_index],
				    test_page_phys(1) |
				    X86_64_PAGING_ENTRY_PRESENT |
				    X86_64_PAGING_ENTRY_WRITE);
	failures += test_expect_u64("build physmap existing no alloc",
				    allocated_page_count(), 0);

	return failures;
}

static int test_physmap_builder_rejects_non_final_base(void)
{
	int failures = 0;

	failures += test_expect_bool(
		"build physmap rejects non-final base",
		x86_64_pmap_build_physmap_in_owned_root(
			test_paddr(test_page_phys(0)),
			test_vaddr(X86_64_PHYSMAP_BASE +
				   X86_64_PHYSMAP_WINDOW_SIZE),
			ARCH_HUGE_PAGE_SIZE,
			X86_64_PHYSMAP_BOOTSTRAP_SIZE),
		false);
	failures += test_expect_u64("build non-final base no alloc",
				    allocated_page_count(), 0);

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

	pml4[1] = test_page_phys(1) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pdpt[2] = test_page_phys(2) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pd[3] = test_page_phys(3) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pt[4] = 0x12345000ull | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	alloc_fail_after = 2;

	failures += test_expect_bool("clone allocation failure",
				     x86_64_pmap_clone_kernel_page_tables(
					     test_page_phys(0), &new_pml4_phys),
				     false);
	failures += test_expect_u64("clone allocation rollback",
				    allocated_page_count(), 0);

	return failures;
}

static int test_clone_physmap_failure_releases_allocated_tables(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t new_pml4_phys = UINT64_MAX;
	int failures = 0;

	pml4[0] = test_page_phys(1) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	physmap_blocked_phys = test_page_phys(1);

	failures += test_expect_bool("clone physmap failure",
				     x86_64_pmap_clone_kernel_page_tables(
					     test_page_phys(0), &new_pml4_phys),
				     false);
	failures += test_expect_u64("clone physmap rollback",
				    allocated_page_count(), 0);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_map_page_allocates_missing_path),
		TEST_CASE(test_map_page_reuses_existing_tables),
		TEST_CASE(test_active_kernel_map_invalidates),
		TEST_CASE(test_active_kernel_protect_updates_writable_bit),
		TEST_CASE(test_mapping_attrs_encode_pte_cache_bits),
		TEST_CASE(test_mapping_attrs_validate_and_protect_preserves_cache_bits),
		TEST_CASE(test_hal_kernel_page_wrappers),
		TEST_CASE(test_protect_page_rejects_invalid_paths),
		TEST_CASE(test_map_page_rejects_invalid_inputs),
		TEST_CASE(test_map_page_rejects_existing_leaf),
		TEST_CASE(test_map_page_rejects_huge_intermediate),
		TEST_CASE(test_map_page_rolls_back_on_allocation_failure),
		TEST_CASE(test_translate_handles_leaf_sizes),
		TEST_CASE(test_unmap_page_clears_leaf),
		TEST_CASE(test_active_kernel_unmap_invalidates),
		TEST_CASE(test_unmap_keeps_shared_tables_until_empty),
		TEST_CASE(test_unmap_page_rejects_invalid_paths),
		TEST_CASE(test_clone_copies_4k_leaf_path),
		TEST_CASE(test_clone_preserves_huge_leaf_entries),
		TEST_CASE(test_clone_skips_physmap_pml4_ranges),
		TEST_CASE(test_physmap_builder_uses_2m_leaves),
		TEST_CASE(test_physmap_builder_crosses_pml4_slots),
		TEST_CASE(test_physmap_builder_failure_rolls_back),
		TEST_CASE(test_physmap_builder_rejects_existing_range),
		TEST_CASE(test_physmap_builder_rejects_non_final_base),
		TEST_CASE(test_clone_failure_releases_allocated_tables),
		TEST_CASE(test_clone_physmap_failure_releases_allocated_tables),
	};

	return test_run_cases_with_fixture("x86_64_pmap_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_pmap_test, NULL);
}
