#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <machine/pmap.h>
#include <x86_64/address_space.h>
#include <plane/compiler.h>
#include <plane/mm.h>
#include <plane/pmm.h>
#include <plane/smp.h>
#include <plane/vm_prot.h>
#include <x86_64/proc_reg.h>

#include "support/spinlock_stubs.h"
#include "support/test.h"
#include <x86_64/physmap_internal.h>
#include <x86_64/pmap_internal.h>

#define TEST_PAGE_COUNT 720
#define TEST_ALLOC_START_PAGE 16
#define TEST_PHYS_SIZE (TEST_PAGE_COUNT * PAGE_SIZE)
#define TEST_CPU_TLB_INVALID_LOCAL  (1u << 0)
#define TEST_CPU_TLB_INVALID_GLOBAL (1u << 1)

static uint8_t phys_storage[TEST_PHYS_SIZE] __aligned(PAGE_SIZE);
static bool page_allocated[TEST_PAGE_COUNT];
static uint64_t alloc_attempts;
static uint64_t alloc_fail_after;
static uint64_t physmap_blocked_phys;
static uintptr_t invalidated_vaddr;
static uint64_t invalidate_count;
static uint64_t invalidate_lock_depth;
static uint64_t flush_count;
static uint64_t cr3_raw = 0x12345000;
static uint32_t test_cpu_count;
static uint32_t test_current_cpu_id;
static bool test_cpu_running[PLANE_MAX_CPUS];
static uint32_t test_cpu_tlb_invalid[PLANE_MAX_CPUS];
static uint64_t cpu_tlb_invalid_mark_lock_depth;
static bool cpu_signal_should_fail;
static uint32_t cpu_signal_count;
static uint32_t cpu_signal_last_logical_id;
static enum plane_smp_event cpu_signal_last_event;
static enum plane_smp_signal_mode cpu_signal_last_mode;
static uint64_t cpu_signal_lock_depth;
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

plane_paddr_t x86_64_pmap_current_root_phys(void)
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

static uint64_t pmm_allocated_page_count(void)
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
	invalidate_lock_depth = 0;
	flush_count = 0;
	cr3_raw = 0x12345000;
	test_cpu_count = 4;
	test_current_cpu_id = 0;
	memset(test_cpu_running, 0, sizeof(test_cpu_running));
	memset(test_cpu_tlb_invalid, 0, sizeof(test_cpu_tlb_invalid));
	cpu_tlb_invalid_mark_lock_depth = UINT64_MAX;
	test_cpu_running[0] = true;
	cpu_signal_should_fail = false;
	cpu_signal_count = 0;
	cpu_signal_last_logical_id = UINT32_MAX;
	cpu_signal_last_event = PLANE_SMP_EVENT_COUNT;
	cpu_signal_last_mode = PLANE_SMP_SIGNAL_SYNC;
	cpu_signal_lock_depth = UINT64_MAX;
	test_pat_wc_ready = true;
	test_physmap_base = test_vaddr(X86_64_PHYSMAP_BASE);
	test_physmap_size = ARCH_HUGE_PAGE_SIZE;
	test_physmap_initialized = true;
	test_spinlock_stub_reset_counts();
}

plane_vaddr_t physmap_phys_range_to_virt(plane_paddr_t phys_addr,
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

plane_vaddr_t physmap_phys_to_virt(plane_paddr_t phys_addr)
{
	return physmap_phys_range_to_virt(phys_addr, 1);
}

uint64_t get_cr3_raw(void)
{
	return cr3_raw;
}

void set_cr3_raw(uint64_t value)
{
	cr3_raw = value;
	flush_count++;
}

void invlpg(plane_vaddr_t vaddr)
{
	invalidated_vaddr = plane_vaddr_raw(vaddr);
	invalidate_lock_depth = test_spinlock_stub_irqsave_depth();
	invalidate_count++;
}

uint32_t plane_cpu_count(void)
{
	return test_cpu_count;
}

uint32_t plane_cpu_current_id(void)
{
	return test_current_cpu_id;
}

bool plane_cpu_is_running(uint32_t logical_id)
{
	return logical_id < test_cpu_count && logical_id < PLANE_MAX_CPUS &&
	       test_cpu_running[logical_id];
}

static bool test_cpu_mark_tlb_invalid_bits(uint32_t logical_id,
					   uint32_t bits,
					   bool *was_invalid)
{
	if (logical_id >= test_cpu_count || logical_id >= PLANE_MAX_CPUS) {
		return false;
	}
	if ((test_cpu_tlb_invalid[logical_id] & bits) == bits) {
		if (was_invalid != NULL) {
			*was_invalid = true;
		}
		return true;
	}

	cpu_tlb_invalid_mark_lock_depth = test_spinlock_stub_irqsave_depth();
	test_cpu_tlb_invalid[logical_id] |= bits;
	if (was_invalid != NULL) {
		*was_invalid = false;
	}
	return true;
}

bool plane_cpu_mark_tlb_invalid_local(uint32_t logical_id, bool *was_invalid)
{
	return test_cpu_mark_tlb_invalid_bits(logical_id,
					      TEST_CPU_TLB_INVALID_LOCAL,
					      was_invalid);
}

bool plane_cpu_mark_tlb_invalid_global(uint32_t logical_id, bool *was_invalid)
{
	return test_cpu_mark_tlb_invalid_bits(logical_id,
					      TEST_CPU_TLB_INVALID_GLOBAL,
					      was_invalid);
}

bool plane_cpu_clear_tlb_invalid(uint32_t logical_id)
{
	if (logical_id >= test_cpu_count || logical_id >= PLANE_MAX_CPUS ||
	    test_cpu_tlb_invalid[logical_id] == 0) {
		return false;
	}

	test_cpu_tlb_invalid[logical_id] = 0;
	return true;
}

uint32_t plane_cpu_tlb_invalid_snapshot(uint32_t logical_id)
{
	if (logical_id >= test_cpu_count || logical_id >= PLANE_MAX_CPUS) {
		return 0;
	}

	return test_cpu_tlb_invalid[logical_id];
}

bool plane_cpu_tlb_is_invalid(uint32_t logical_id)
{
	return plane_cpu_tlb_invalid_snapshot(logical_id) != 0;
}

bool plane_smp_signal_cpu(uint32_t logical_id,
			  enum plane_smp_event event,
			  enum plane_smp_signal_mode mode)
{
	cpu_signal_count++;
	cpu_signal_last_logical_id = logical_id;
	cpu_signal_last_event = event;
	cpu_signal_last_mode = mode;
	cpu_signal_lock_depth = test_spinlock_stub_irqsave_depth();
	return !cpu_signal_should_fail;
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

bool x86_64_pat_write_combine_is_ready(void)
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

static int test_kernel_vma_range(void)
{
	plane_vaddr_t base = {0};
	uint64_t size = 0;
	int failures = 0;

	failures += test_expect_bool("kernel range",
				     pmap_kernel_vma_range(&base, &size),
				     true);
	failures += test_expect_u64("kernel range base",
				    plane_vaddr_raw(base),
				    X86_64_KERNEL_MAP_BASE);
	failures += test_expect_u64("kernel range size",
				    size, X86_64_KERNEL_MAP_SIZE);
	failures += test_expect_bool("kernel range null base",
				     pmap_kernel_vma_range(NULL, &size),
				     false);
	failures += test_expect_bool("kernel range null size",
				     pmap_kernel_vma_range(&base, NULL),
				     false);
	failures += test_expect_bool(
		"kernel range avoids physmap",
		X86_64_KERNEL_MAP_BASE >= X86_64_PHYSMAP_WINDOW_END ||
		X86_64_KERNEL_MAP_END <= X86_64_PHYSMAP_BASE,
		true);
	failures += test_expect_bool(
		"kernel range avoids kernel image",
		KERNEL_VMA_BASE < X86_64_KERNEL_MAP_BASE ||
		KERNEL_VMA_BASE >= X86_64_KERNEL_MAP_END,
		true);

	return failures;
}

static void *test_physmap_phys_to_virt(uint64_t phys_addr)
{
	plane_vaddr_t vaddr = physmap_phys_to_virt(
		test_paddr(phys_addr));

	if (plane_vaddr_is_null(vaddr)) {
		return NULL;
	}

	return plane_vaddr_to_ptr(vaddr);
}

static struct pmap_map_options test_map_options(
	uint32_t prot,
	enum pmap_mapping_attr attr)
{
	return (struct pmap_map_options){
		.prot = prot,
		.attr = attr,
	};
}

static struct pmap_map_options test_default_options(uint32_t prot)
{
	return pmap_default_map_options(prot);
}

static bool test_pmap_map_in_root(uint64_t root,
				  uint64_t vaddr,
				  uint64_t phys_addr,
				  struct pmap_map_options options)
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

static bool test_pmap_protect_in_root(uint64_t root,
				      uint64_t vaddr,
				      uint32_t prot)
{
	return x86_64_pmap_protect_page_in_owned_root(test_paddr(root),
						      test_vaddr(vaddr),
						      prot);
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
				     struct pmap_map_options options)
{
	return pmap_map_kernel_page(test_vaddr(vaddr), test_paddr(phys_addr),
				       options);
}

static bool test_pmap_translate_kernel_page(uint64_t vaddr, uint64_t *phys_addr)
{
	plane_paddr_t out;

	if (!pmap_translate_kernel_page(test_vaddr(vaddr),
					   phys_addr == NULL ? NULL : &out)) {
		return false;
	}

	*phys_addr = test_paddr_raw(out);
	return true;
}

static bool test_pmap_unmap_kernel_page(uint64_t vaddr)
{
	return pmap_unmap_kernel_page(test_vaddr(vaddr));
}

static bool test_pmap_protect_kernel_page(uint64_t vaddr, uint32_t prot)
{
	return pmap_protect_kernel_page(test_vaddr(vaddr), prot);
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

static void reset_active_pmap_observation(void)
{
	invalidated_vaddr = UINTPTR_MAX;
	invalidate_count = 0;
	invalidate_lock_depth = 0;
	cpu_tlb_invalid_mark_lock_depth = UINT64_MAX;
	cpu_signal_count = 0;
	cpu_signal_last_logical_id = UINT32_MAX;
	cpu_signal_last_event = PLANE_SMP_EVENT_COUNT;
	cpu_signal_last_mode = PLANE_SMP_SIGNAL_SYNC;
	cpu_signal_lock_depth = UINT64_MAX;
	test_spinlock_stub_reset_counts();
}

#define physmap_phys_to_virt(phys_addr) \
	test_physmap_phys_to_virt((phys_addr))
#define x86_64_pmap_map_page_in_owned_root(root, vaddr, phys_addr, options) \
	test_pmap_map_in_root((root), (vaddr), (phys_addr), (options))
#define x86_64_pmap_unmap_page_in_owned_root(root, vaddr) \
	test_pmap_unmap_in_root((root), (vaddr))
#define x86_64_pmap_translate_in_root(root, vaddr, phys_addr) \
	test_pmap_translate_in_root((root), (vaddr), (phys_addr))
#define x86_64_pmap_clone_kernel_page_tables(source, out) \
	test_pmap_clone_kernel_page_tables((source), (out))
#define pmap_map_kernel_page(vaddr, phys_addr, options) \
	test_pmap_map_kernel_page((vaddr), (phys_addr), (options))
#define pmap_translate_kernel_page(vaddr, phys_addr) \
	test_pmap_translate_kernel_page((vaddr), (phys_addr))
#define pmap_unmap_kernel_page(vaddr) \
	test_pmap_unmap_kernel_page((vaddr))
#define pmap_protect_kernel_page(vaddr, prot) \
	test_pmap_protect_kernel_page((vaddr), (prot))

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
				    pmm_allocated_page_count(), 3);
	failures += test_expect_u64("root map does not invalidate",
				    invalidate_count, 0);
	failures += test_expect_u64("root map leaves invalidated vaddr",
				    invalidated_vaddr, UINTPTR_MAX);

	pdpt = physmap_phys_to_virt(pte_phys(pml4[X86_64_PAGING_PML4_INDEX(vaddr)]));
	pd = physmap_phys_to_virt(pte_phys(pdpt[X86_64_PAGING_PDPT_INDEX(vaddr)]));
	pt = physmap_phys_to_virt(pte_phys(pd[X86_64_PAGING_PD_INDEX(vaddr)]));
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
				    pmm_allocated_page_count(), 3);
	failures += test_expect_u64("root map reuse invalidate count",
				    invalidate_count, 0);

	return failures;
}

static int test_active_kernel_map_invalidates(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("active map",
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE)),
				     true);
	failures += test_expect_u64("active map invalidates",
				    invalidate_count, 1);
	failures += test_expect_u64("active map invalidated vaddr",
				    invalidated_vaddr, vaddr);
	failures += test_expect_u64("active map locks",
				    test_spinlock_stub_irqsave_count(), 1);
	failures += test_expect_u64("active map unlocks",
				    test_spinlock_stub_irqrestore_count(), 1);
	failures += test_expect_u64("active map releases lock",
				    test_spinlock_stub_irqsave_depth(), 0);
	failures += test_expect_u64("active map invalidate under lock",
				    invalidate_lock_depth, 1);
	failures += test_expect_u32("active map no remote signal by default",
				    cpu_signal_count, 0);

	return failures;
}

static int test_active_kernel_map_signals_running_remote_tlb_flush(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	test_cpu_running[2] = true;
	failures += test_expect_bool("active map with remote cpu",
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ)),
				     true);
	failures += test_expect_u64("active map local invalidate",
				    invalidate_count, 1);
	failures += test_expect_u32("remote signal sent once",
				    cpu_signal_count, 1);
	failures += test_expect_u32("remote signal target",
				    cpu_signal_last_logical_id, 2);
	failures += test_expect_u32("remote signal event",
				    cpu_signal_last_event,
				    PLANE_SMP_EVENT_TLB_FLUSH);
	failures += test_expect_u32("remote signal mode",
				    cpu_signal_last_mode,
				    PLANE_SMP_SIGNAL_ASYNC);
	failures += test_expect_u64("remote signal after pmap unlock",
				    cpu_signal_lock_depth, 0);
	failures += test_expect_u64("remote invalid mark under pmap lock",
				    cpu_tlb_invalid_mark_lock_depth, 1);
	failures += test_expect_bool("remote cpu marked tlb invalid",
				     plane_cpu_tlb_is_invalid(2), true);
	failures += test_expect_u32("remote cpu marked global invalid",
				    plane_cpu_tlb_invalid_snapshot(2),
				    TEST_CPU_TLB_INVALID_GLOBAL);

	test_current_cpu_id = 2;
	pmap_update_interrupt();
	failures += test_expect_u64("remote invalid flushes on target cpu",
				    flush_count, 1);
	failures += test_expect_bool("remote cpu invalid clears",
				     plane_cpu_tlb_is_invalid(2), false);
	pmap_update_interrupt();
	failures += test_expect_u64("remote invalid does not flush twice",
				    flush_count, 1);
	return failures;
}

static int test_active_kernel_map_skips_non_running_remote_cpus(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	test_cpu_running[1] = false;
	test_cpu_running[2] = false;
	failures += test_expect_bool("active map skips parked/offline cpus",
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ)),
				     true);
	failures += test_expect_u32("no remote signal",
				    cpu_signal_count, 0);
	failures += test_expect_u64("non-running cpus not marked under lock",
				    cpu_tlb_invalid_mark_lock_depth,
				    UINT64_MAX);

	test_current_cpu_id = 1;
	pmap_update_interrupt();
	test_current_cpu_id = 2;
	pmap_update_interrupt();
	failures += test_expect_u64("non-running cpus not marked invalid",
				    flush_count, 0);
	return failures;
}

static int test_active_kernel_protect_updates_writable_bit(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t *pte;
	int failures = 0;

	failures += test_expect_bool("protect setup map",
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE)),
				     true);
	pte = test_kernel_pte(vaddr);
	failures += test_expect_u64("protect setup writable",
				    *pte & X86_64_PAGING_ENTRY_WRITE, X86_64_PAGING_ENTRY_WRITE);

	reset_active_pmap_observation();
	failures += test_expect_bool("protect readonly",
				     pmap_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_u64("protect readonly clears write",
				    *pte & X86_64_PAGING_ENTRY_WRITE, 0);
	failures += test_expect_u64("protect readonly invalidates",
				    invalidate_count, 1);
	failures += test_expect_u64("protect readonly vaddr",
				    invalidated_vaddr, vaddr);
	failures += test_expect_u64("protect readonly locks",
				    test_spinlock_stub_irqsave_count(), 1);
	failures += test_expect_u64("protect readonly unlocks",
				    test_spinlock_stub_irqrestore_count(), 1);
	failures += test_expect_u64("protect readonly invalidate under lock",
				    invalidate_lock_depth, 1);

	reset_active_pmap_observation();
	failures += test_expect_bool("protect writable",
				     pmap_protect_kernel_page(
					     vaddr,
					     PLANE_VM_PROT_READ |
					     PLANE_VM_PROT_WRITE),
				     true);
	failures += test_expect_u64("protect writable sets write",
				    *pte & X86_64_PAGING_ENTRY_WRITE, X86_64_PAGING_ENTRY_WRITE);
	failures += test_expect_u64("protect writable invalidates",
				    invalidate_count, 1);
	failures += test_expect_u64("protect writable locks",
				    test_spinlock_stub_irqsave_count(), 1);
	return failures;
}

static int test_active_kernel_protect_and_unmap_signal_remote_tlb_flush(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("remote signal setup map",
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE)),
				     true);

	test_cpu_running[2] = true;
	reset_active_pmap_observation();
	failures += test_expect_bool("protect signals remote",
				     pmap_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_u32("protect signal count",
				    cpu_signal_count, 1);
	failures += test_expect_u32("protect signal target",
				    cpu_signal_last_logical_id, 2);
	failures += test_expect_u64("protect invalid mark under pmap lock",
				    cpu_tlb_invalid_mark_lock_depth, 1);
	failures += test_expect_bool("protect marks remote invalid",
				     plane_cpu_tlb_is_invalid(2), true);
	failures += test_expect_u32("protect marks remote global invalid",
				    plane_cpu_tlb_invalid_snapshot(2),
				    TEST_CPU_TLB_INVALID_GLOBAL);

	test_current_cpu_id = 2;
	pmap_update_interrupt();
	failures += test_expect_u64("protect remote invalid flushes",
				    flush_count, 1);
	failures += test_expect_bool("protect remote invalid clears",
				     plane_cpu_tlb_is_invalid(2), false);

	test_current_cpu_id = 0;
	reset_active_pmap_observation();
	failures += test_expect_bool("unmap signals remote",
				     pmap_unmap_kernel_page(vaddr),
				     true);
	failures += test_expect_u32("unmap signal count",
				    cpu_signal_count, 1);
	failures += test_expect_u32("unmap signal target",
				    cpu_signal_last_logical_id, 2);
	failures += test_expect_u64("unmap invalid mark under pmap lock",
				    cpu_tlb_invalid_mark_lock_depth, 1);
	failures += test_expect_bool("unmap marks remote invalid",
				     plane_cpu_tlb_is_invalid(2), true);
	failures += test_expect_u32("unmap marks remote global invalid",
				    plane_cpu_tlb_invalid_snapshot(2),
				    TEST_CPU_TLB_INVALID_GLOBAL);

	test_current_cpu_id = 2;
	pmap_update_interrupt();
	failures += test_expect_u64("unmap remote invalid flushes",
				    flush_count, 2);
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
				     pmap_map_kernel_page(
					     device_vaddr, 0x12345000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE,
						     PMAP_MAPPING_ATTR_DEVICE)),
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
				     pmap_map_kernel_page(
					     wc_vaddr, 0x12346000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ,
						     PMAP_MAPPING_ATTR_WRITE_COMBINE)),
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
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ,
						     (enum pmap_mapping_attr)99)),
				     false);
	test_pat_wc_ready = false;
	failures += test_expect_bool("reject wc before pat init",
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ,
						     PMAP_MAPPING_ATTR_WRITE_COMBINE)),
				     false);
	test_pat_wc_ready = true;
	failures += test_expect_bool("map device writable",
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_map_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE,
						     PMAP_MAPPING_ATTR_DEVICE)),
				     true);
	pte = test_kernel_pte(vaddr);
	failures += test_expect_bool("protect readonly",
				     pmap_protect_kernel_page(
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

static int test_owned_root_protect_preserves_cache_bits_without_active_lock(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t *pte;
	int failures = 0;

	failures += test_expect_bool(
		"owned protect setup map",
		x86_64_pmap_map_page_in_owned_root(
			test_page_phys(0), vaddr, 0x12345000ull,
			test_map_options(PLANE_VM_PROT_READ |
					 PLANE_VM_PROT_WRITE,
					 PMAP_MAPPING_ATTR_DEVICE)),
		true);
	pte = test_kernel_pte(vaddr);
	reset_active_pmap_observation();
	failures += test_expect_bool("owned protect readonly",
				     test_pmap_protect_in_root(
					     test_page_phys(0), vaddr,
					     PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_u64("owned protect preserves pcd",
				    *pte & X86_64_PAGING_ENTRY_PCD,
				    X86_64_PAGING_ENTRY_PCD);
	failures += test_expect_u64("owned protect preserves pwt",
				    *pte & X86_64_PAGING_ENTRY_PWT,
				    X86_64_PAGING_ENTRY_PWT);
	failures += test_expect_u64("owned protect clears write",
				    *pte & X86_64_PAGING_ENTRY_WRITE, 0);
	failures += test_expect_u64("owned protect does not lock active pmap",
				    test_spinlock_stub_irqsave_count(), 0);
	failures += test_expect_u64("owned protect does not invalidate",
				    invalidate_count, 0);

	return failures;
}

static int test_pmap_kernel_page_wrappers(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t phys = UINT64_MAX;
	int failures = 0;

	failures += test_expect_bool("pmap map",
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ |
						     PLANE_VM_PROT_WRITE)),
				     true);
	failures += test_expect_u64("pmap map invalidates",
				    invalidate_count, 1);
	failures += test_expect_bool("pmap translate",
				     pmap_translate_kernel_page(vaddr, &phys),
				     true);
	failures += test_expect_u64("pmap translate phys", phys,
				    0x12345000ull);
	failures += test_expect_bool("pmap protect readonly",
				     pmap_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     true);
	failures += test_expect_bool("pmap unmap",
				     pmap_unmap_kernel_page(vaddr), true);
	failures += test_expect_u64("pmap wrappers invalidate",
				    invalidate_count, 3);
	failures += test_expect_bool("pmap reject invalid attr",
				     pmap_map_kernel_page(vaddr,
							     0x12345000ull,
							     test_map_options(
								     PLANE_VM_PROT_READ,
								     (enum pmap_mapping_attr)99)),
				     false);
	failures += test_expect_bool("pmap reject invalid protect prot",
				     pmap_protect_kernel_page(vaddr,
								 BIT(8)),
				     false);

	return failures;
}

static int test_active_kernel_translate_locks_snapshot(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t phys = UINT64_MAX;
	int failures = 0;

	failures += test_expect_bool("translate lock setup map",
				     pmap_map_kernel_page(
					     vaddr, 0x12345000ull,
					     test_default_options(
						     PLANE_VM_PROT_READ)),
				     true);
	reset_active_pmap_observation();
	failures += test_expect_bool("active translate",
				     pmap_translate_kernel_page(vaddr,
								   &phys),
				     true);
	failures += test_expect_u64("active translate phys",
				    phys, 0x12345000ull);
	failures += test_expect_u64("active translate locks",
				    test_spinlock_stub_irqsave_count(), 1);
	failures += test_expect_u64("active translate unlocks",
				    test_spinlock_stub_irqrestore_count(), 1);
	failures += test_expect_u64("active translate no invalidate",
				    invalidate_count, 0);

	return failures;
}

static int test_pmap_update_interrupt_flushes_only_invalid_cpu(void)
{
	int failures = 0;

	pmap_update_interrupt();
	failures += test_expect_u64("no invalid update no flush",
				    flush_count, 0);

	failures += test_expect_bool("mark current cpu invalid",
				     x86_64_pmap_mark_tlb_invalid_global(0),
				     true);
	failures += test_expect_bool("mark current cpu invalid twice",
				     x86_64_pmap_mark_tlb_invalid_global(0),
				     true);
	failures += test_expect_u64("mark does not flush directly",
				    flush_count, 0);

	pmap_update_interrupt();
	failures += test_expect_u64("invalid update flushes once",
				    flush_count, 1);
	pmap_update_interrupt();
	failures += test_expect_u64("cleared invalid no repeat flush",
				    flush_count, 1);
	return failures;
}

static int test_pmap_update_interrupt_uses_current_cpu_invalid_state(void)
{
	int failures = 0;

	failures += test_expect_bool("mark remote cpu invalid",
				     x86_64_pmap_mark_tlb_invalid_local(2),
				     true);
	pmap_update_interrupt();
	failures += test_expect_u64("other cpu invalid does not flush current",
				    flush_count, 0);

	test_current_cpu_id = 2;
	failures += test_expect_bool("add remote global invalid",
				     x86_64_pmap_mark_tlb_invalid_global(2),
				     true);
	failures += test_expect_u32(
		"remote local and global pending",
		plane_cpu_tlb_invalid_snapshot(2),
		TEST_CPU_TLB_INVALID_LOCAL | TEST_CPU_TLB_INVALID_GLOBAL);
	pmap_update_interrupt();
	failures += test_expect_u64("marked cpu interrupt flushes",
				    flush_count, 1);
	failures += test_expect_bool("marked cpu invalid clears",
				     plane_cpu_tlb_is_invalid(2), false);
	pmap_update_interrupt();
	failures += test_expect_u64("marked cpu invalid does not flush twice",
				    flush_count, 1);
	return failures;
}

static int test_pmap_mark_tlb_invalid_global_rejects_invalid_cpu(void)
{
	int failures = 0;

	test_cpu_count = 2;
	failures += test_expect_bool("mark out of runtime cpu range",
				     x86_64_pmap_mark_tlb_invalid_global(2),
				     false);
	failures += test_expect_bool("mark out of max cpu range",
				     x86_64_pmap_mark_tlb_invalid_global(
					     PLANE_MAX_CPUS),
				     false);
	failures += test_expect_u64("invalid cpu does not flush",
				    flush_count, 0);
	return failures;
}

static int test_protect_page_rejects_invalid_paths(void)
{
	uint64_t *pml4 = test_table(0);
	uint64_t *pdpt = test_table(1);
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	failures += test_expect_bool("protect reject unaligned",
				     pmap_protect_kernel_page(vaddr + 1,
								     PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_bool("protect reject bad prot",
				     pmap_protect_kernel_page(vaddr,
								     BIT(31)),
				     false);
	failures += test_expect_bool("protect reject absent",
				     pmap_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     false);

	pml4[X86_64_PAGING_PML4_INDEX(vaddr)] = test_page_phys(1) | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE;
	pdpt[X86_64_PAGING_PDPT_INDEX(vaddr)] = 0x40000000ull | X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE |
				  X86_64_PAGING_ENTRY_PS;
	failures += test_expect_bool("protect reject huge",
				     pmap_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("protect reject no invalidate",
				    invalidate_count, 0);
	return failures;
}

static int test_active_kernel_failures_release_lock_without_invalidate(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	uint64_t phys = UINT64_MAX;
	int failures = 0;

	failures += test_expect_bool(
		"active map failure",
		pmap_map_kernel_page(
			vaddr + 1, 0x12345000ull,
			test_default_options(PLANE_VM_PROT_READ)),
		false);
	failures += test_expect_u64("active map failure locks",
				    test_spinlock_stub_irqsave_count(), 1);
	failures += test_expect_u64("active map failure unlocks",
				    test_spinlock_stub_irqrestore_count(), 1);
	failures += test_expect_u64("active map failure no invalidate",
				    invalidate_count, 0);
	failures += test_expect_u64("active map failure no remote invalid",
				    cpu_tlb_invalid_mark_lock_depth,
				    UINT64_MAX);
	failures += test_expect_u64("active map failure releases lock",
				    test_spinlock_stub_irqsave_depth(), 0);

	reset_active_pmap_observation();
	failures += test_expect_bool("active unmap failure",
				     pmap_unmap_kernel_page(vaddr),
				     false);
	failures += test_expect_u64("active unmap failure locks",
				    test_spinlock_stub_irqsave_count(), 1);
	failures += test_expect_u64("active unmap failure unlocks",
				    test_spinlock_stub_irqrestore_count(), 1);
	failures += test_expect_u64("active unmap failure no invalidate",
				    invalidate_count, 0);
	failures += test_expect_u64("active unmap failure no remote invalid",
				    cpu_tlb_invalid_mark_lock_depth,
				    UINT64_MAX);

	reset_active_pmap_observation();
	failures += test_expect_bool("active protect failure",
				     pmap_protect_kernel_page(
					     vaddr, PLANE_VM_PROT_READ),
				     false);
	failures += test_expect_u64("active protect failure locks",
				    test_spinlock_stub_irqsave_count(), 1);
	failures += test_expect_u64("active protect failure unlocks",
				    test_spinlock_stub_irqrestore_count(), 1);
	failures += test_expect_u64("active protect failure no invalidate",
				    invalidate_count, 0);
	failures += test_expect_u64("active protect failure no remote invalid",
				    cpu_tlb_invalid_mark_lock_depth,
				    UINT64_MAX);

	reset_active_pmap_observation();
	failures += test_expect_bool("active translate failure",
				     pmap_translate_kernel_page(vaddr,
								   &phys),
				     false);
	failures += test_expect_u64("active translate failure locks",
				    test_spinlock_stub_irqsave_count(), 1);
	failures += test_expect_u64("active translate failure unlocks",
				    test_spinlock_stub_irqrestore_count(), 1);
	failures += test_expect_u64("active translate failure no invalidate",
				    invalidate_count, 0);

	return failures;
}

static int test_active_kernel_map_failure_rolls_back_and_unlocks(void)
{
	uint64_t vaddr = 0xffff800000402000ull;
	int failures = 0;

	alloc_fail_after = 1;
	failures += test_expect_bool(
		"active map allocation failure",
		pmap_map_kernel_page(
			vaddr, 0x12345000ull,
			test_default_options(PLANE_VM_PROT_READ)),
		false);
	failures += test_expect_u64("active map failure rollback",
				    pmm_allocated_page_count(), 0);
	failures += test_expect_u64("active map alloc failure locks",
				    test_spinlock_stub_irqsave_count(), 1);
	failures += test_expect_u64("active map alloc failure unlocks",
				    test_spinlock_stub_irqrestore_count(), 1);
	failures += test_expect_u64("active map alloc failure no invalidate",
				    invalidate_count, 0);
	failures += test_expect_u64("active map alloc failure releases lock",
				    test_spinlock_stub_irqsave_depth(), 0);

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
				    pmm_allocated_page_count(), 0);

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
				    pmm_allocated_page_count(), 3);

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
				    pmm_allocated_page_count(), 0);

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
				    pmm_allocated_page_count(), 0);

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
				    pmm_allocated_page_count(), 0);
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
				     pmap_unmap_kernel_page(vaddr),
				     true);
	failures += test_expect_u64("active unmap invalidates",
				    invalidate_count, 1);
	failures += test_expect_u64("active unmap invalidated vaddr",
				    invalidated_vaddr, vaddr);
	failures += test_expect_u64("active unmap locks",
				    test_spinlock_stub_irqsave_count(), 1);
	failures += test_expect_u64("active unmap unlocks",
				    test_spinlock_stub_irqrestore_count(), 1);
	failures += test_expect_u64("active unmap invalidate under lock",
				    invalidate_lock_depth, 1);

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
				    pmm_allocated_page_count(), 3);
	failures += test_expect_bool("shared unmap first",
				     x86_64_pmap_unmap_page_in_owned_root(
					     test_page_phys(0), vaddr),
				     true);
	failures += test_expect_u64("shared tables remain",
				    pmm_allocated_page_count(), 3);
	failures += test_expect_bool("shared unmap second",
				     x86_64_pmap_unmap_page_in_owned_root(
					     test_page_phys(0), next_vaddr),
				     true);
	failures += test_expect_u64("shared tables freed",
				    pmm_allocated_page_count(), 0);

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
				    pmm_allocated_page_count(), 4);

	new_pml4 = physmap_phys_to_virt(new_pml4_phys);
	new_pdpt_phys = pte_phys(new_pml4[1]);
	new_pdpt = physmap_phys_to_virt(new_pdpt_phys);
	new_pd_phys = pte_phys(new_pdpt[2]);
	new_pd = physmap_phys_to_virt(new_pd_phys);
	new_pt_phys = pte_phys(new_pd[3]);
	new_pt = physmap_phys_to_virt(new_pt_phys);

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
				    pmm_allocated_page_count(), 3);

	new_pml4 = physmap_phys_to_virt(new_pml4_phys);
	new_pdpt = physmap_phys_to_virt(pte_phys(new_pml4[0]));
	new_pd = physmap_phys_to_virt(pte_phys(new_pdpt[2]));

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
	uint64_t bootstrap_index = X86_64_PAGING_PML4_INDEX(
		X86_64_PHYSMAP_BASE + X86_64_PHYSMAP_WINDOW_SIZE);
	int failures = 0;

	pml4[final_index] = test_page_phys(1) |
			    X86_64_PAGING_ENTRY_PRESENT |
			    X86_64_PAGING_ENTRY_WRITE;
	pml4[bootstrap_index] = test_page_phys(2) |
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
	new_pml4 = physmap_phys_to_virt(new_pml4_phys);

	failures += test_expect_u64("clone skips final physmap",
				    new_pml4[final_index], 0);
	failures += test_expect_u64("clone skips bootstrap physmap",
				    new_pml4[bootstrap_index], 0);
	failures += test_expect_bool("clone keeps ordinary pml4",
				     x86_64_paging_entry_is_present(new_pml4[0]),
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
				    pmm_allocated_page_count(), 3);

	pdpt = physmap_phys_to_virt(pte_phys(pml4[pml4_index]));
	pd0 = physmap_phys_to_virt(pte_phys(pdpt[0]));
	pd1 = physmap_phys_to_virt(pte_phys(pdpt[1]));

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
				     x86_64_paging_entry_is_present(
					     pml4[first_index]),
				     true);
	failures += test_expect_bool("second pml4 present",
				     x86_64_paging_entry_is_present(
					     pml4[second_index]),
				     true);

	second_pdpt = physmap_phys_to_virt(
		pte_phys(pml4[second_index]));
	second_pd = physmap_phys_to_virt(pte_phys(second_pdpt[0]));
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
				    pmm_allocated_page_count(), 0);

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
				    pmm_allocated_page_count(), 0);

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
				    pmm_allocated_page_count(), 0);

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
				    pmm_allocated_page_count(), 0);

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
				    pmm_allocated_page_count(), 0);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_kernel_vma_range),
		TEST_CASE(test_map_page_allocates_missing_path),
		TEST_CASE(test_map_page_reuses_existing_tables),
		TEST_CASE(test_active_kernel_map_invalidates),
		TEST_CASE(test_active_kernel_map_signals_running_remote_tlb_flush),
		TEST_CASE(test_active_kernel_map_skips_non_running_remote_cpus),
		TEST_CASE(test_active_kernel_protect_updates_writable_bit),
		TEST_CASE(test_active_kernel_protect_and_unmap_signal_remote_tlb_flush),
		TEST_CASE(test_mapping_attrs_encode_pte_cache_bits),
		TEST_CASE(test_mapping_attrs_validate_and_protect_preserves_cache_bits),
		TEST_CASE(test_owned_root_protect_preserves_cache_bits_without_active_lock),
		TEST_CASE(test_pmap_kernel_page_wrappers),
		TEST_CASE(test_active_kernel_translate_locks_snapshot),
		TEST_CASE(test_pmap_update_interrupt_flushes_only_invalid_cpu),
		TEST_CASE(test_pmap_update_interrupt_uses_current_cpu_invalid_state),
		TEST_CASE(test_pmap_mark_tlb_invalid_global_rejects_invalid_cpu),
		TEST_CASE(test_protect_page_rejects_invalid_paths),
		TEST_CASE(test_active_kernel_failures_release_lock_without_invalidate),
		TEST_CASE(test_active_kernel_map_failure_rolls_back_and_unlocks),
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
