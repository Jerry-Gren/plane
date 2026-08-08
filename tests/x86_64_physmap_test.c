#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <hal/x86_64/address_space.h>
#include <plane/memmap.h>

#include "support/test.h"
#include <x86_64/physmap_internal.h>

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

static void test_set_mb2_bootstrap_physmap_window(void)
{
	x86_64_physmap_set_bootstrap_window(
		test_vaddr(X86_64_PHYSMAP_BASE),
		X86_64_PHYSMAP_BOOTSTRAP_SIZE);
}

static uint64_t test_hhdm_base(void)
{
	return X86_64_KERNEL_MAP_BASE + X86_64_PAGING_PML4_SLOT_SIZE;
}

static void test_set_limine_bootstrap_physmap_window(uint64_t base)
{
	x86_64_physmap_set_bootstrap_window(test_vaddr(base),
					    x86_64_physmap_window_size());
}

static int test_physmap_roundtrip(void)
{
	struct plane_mem_info mem = {0};
	plane_vaddr_t vaddr;
	int failures = 0;

	test_set_mb2_bootstrap_physmap_window();

	mem.map[0].base = plane_paddr_make(0x1000);
	mem.map[0].length = 0x3000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.map[1].base = plane_paddr_make(X86_64_PHYSMAP_WINDOW_SIZE);
	mem.map[1].length = 0x1000;
	mem.map[1].type = PLANE_MEM_RESERVED;
	mem.entry_count = 2;

	failures += test_expect_bool("physmap enable",
				     hal_mmu_enable_physmap(&mem), true);

	vaddr = hal_mmu_physmap_phys_to_virt(test_paddr(0x2000));
	failures += test_expect_u64("physmap phys to virt",
				    plane_vaddr_raw(vaddr),
				    X86_64_PHYSMAP_BASE + 0x2000);
	failures += test_expect_u64(
		"physmap phys range to virt",
		plane_vaddr_raw(hal_mmu_physmap_phys_range_to_virt(
			test_paddr(0x2000), 0x1000)),
		X86_64_PHYSMAP_BASE + 0x2000);
	failures += test_expect_u64("physmap virt to phys",
				    test_paddr_raw(
					    hal_mmu_physmap_virt_to_phys(vaddr)),
				    0x2000);

	failures += test_expect_bool(
		"physmap reject outside runtime coverage",
		plane_vaddr_is_null(hal_mmu_physmap_phys_to_virt(
			test_paddr(ARCH_LARGE_PAGE_SIZE))),
		true);
	failures += test_expect_u64("physmap reject kernel vma",
				    test_paddr_raw(hal_mmu_physmap_virt_to_phys(
					    test_vaddr(KERNEL_VMA_BASE))),
				    HAL_MMU_INVALID_PHYS);

	return failures;
}

static int test_physmap_rejects_invalid_ranges(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	test_set_mb2_bootstrap_physmap_window();

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("range reject init",
				     hal_mmu_enable_physmap(&mem), true);
	failures += test_expect_bool("range reject zero size",
				     plane_vaddr_is_null(
					     hal_mmu_physmap_phys_range_to_virt(
						     test_paddr(0), 0)),
				     true);
	failures += test_expect_bool(
		"range reject start out of range",
		plane_vaddr_is_null(hal_mmu_physmap_phys_range_to_virt(
			test_paddr(ARCH_LARGE_PAGE_SIZE), 1)),
		true);
	failures += test_expect_bool(
		"range reject end past physmap",
		plane_vaddr_is_null(hal_mmu_physmap_phys_range_to_virt(
			test_paddr(ARCH_LARGE_PAGE_SIZE - 1), 2)),
		true);
	failures += test_expect_bool(
		"range reject phys overflow",
		plane_vaddr_is_null(hal_mmu_physmap_phys_range_to_virt(
			test_paddr(UINT64_MAX), 2)),
		true);

	return failures;
}

static int test_bootstrap_physmap_window_base(void)
{
	struct plane_mem_info mem = {0};
	uint64_t bootstrap_base =
		X86_64_PHYSMAP_BASE + X86_64_PHYSMAP_BOOTSTRAP_SIZE;
	plane_vaddr_t vaddr;
	int failures = 0;

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	x86_64_physmap_set_bootstrap_window(
		test_vaddr(bootstrap_base),
		X86_64_PHYSMAP_BOOTSTRAP_SIZE);
	failures += test_expect_bool("bootstrap physmap window enable",
				     hal_mmu_enable_physmap(&mem), true);
	vaddr = hal_mmu_physmap_phys_to_virt(test_paddr(0x1000));
	failures += test_expect_u64("bootstrap physmap window phys to virt",
				    plane_vaddr_raw(vaddr),
				    bootstrap_base + 0x1000);
	failures += test_expect_u64("bootstrap physmap window virt to phys",
				    test_paddr_raw(
					    hal_mmu_physmap_virt_to_phys(vaddr)),
				    0x1000);

	return failures;
}

static int test_physmap_runtime_counts_owned_pml4_slots(void)
{
	struct plane_mem_info mem = {0};
	struct x86_64_physmap_runtime runtime = {0};
	uint64_t hhdm_base = test_hhdm_base();
	uint64_t high_phys = X86_64_PHYSMAP_BOOTSTRAP_SIZE + 0x2000;
	int failures = 0;

	test_set_limine_bootstrap_physmap_window(hhdm_base);

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.map[1].base = plane_paddr_make(high_phys);
	mem.map[1].length = 0x1000;
	mem.map[1].type = PLANE_MEM_USABLE;
	mem.entry_count = 2;

	failures += test_expect_bool("physmap multi-slot enable",
				     hal_mmu_enable_physmap(&mem), true);
	failures += test_expect_bool(
		"physmap multi-slot runtime",
		x86_64_physmap_get_runtime(&runtime), true);
	failures += test_expect_u64("physmap multi-slot required",
				    runtime.required_size,
				    X86_64_PHYSMAP_BOOTSTRAP_SIZE +
					    ARCH_LARGE_PAGE_SIZE);
	failures += test_expect_u64("physmap multi-slot owned pml4 count",
				    runtime.owned_pml4_count, 2);
	failures += test_expect_u64("physmap multi-slot owned window",
				    runtime.owned_window_size,
				    2 * X86_64_PAGING_PML4_SLOT_SIZE);
	failures += test_expect_u64("physmap multi-slot bootstrap size",
				    runtime.bootstrap_size,
				    X86_64_PHYSMAP_WINDOW_SIZE);

	return failures;
}

static int test_physmap_supports_runtime_coverage_above_64g(void)
{
	struct plane_mem_info mem = {0};
	uint64_t high_phys = 0x1000000000ull;
	plane_vaddr_t vaddr;
	int failures = 0;

	test_set_mb2_bootstrap_physmap_window();

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x2000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.map[1].base = plane_paddr_make(high_phys);
	mem.map[1].length = 0x3000;
	mem.map[1].type = PLANE_MEM_USABLE;
	mem.entry_count = 2;

	failures += test_expect_bool("physmap high enable",
				     hal_mmu_enable_physmap(&mem), true);
	vaddr = hal_mmu_physmap_phys_to_virt(test_paddr(high_phys + 0x2000));
	failures += test_expect_u64("physmap high phys to virt",
				    plane_vaddr_raw(vaddr),
				    X86_64_PHYSMAP_BASE + high_phys + 0x2000);
	failures += test_expect_bool(
		"physmap high rejects beyond runtime coverage",
		plane_vaddr_is_null(hal_mmu_physmap_phys_to_virt(
			test_paddr(high_phys + ARCH_LARGE_PAGE_SIZE))),
		true);

	return failures;
}

static int test_physmap_reserved_high_memory_does_not_extend_coverage(void)
{
	struct plane_mem_info mem = {0};
	uint64_t high_phys = 0x100000000ull;
	int failures = 0;

	test_set_mb2_bootstrap_physmap_window();

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.map[1].base = plane_paddr_make(high_phys);
	mem.map[1].length = 0x1000;
	mem.map[1].type = PLANE_MEM_FRAMEBUFFER;
	mem.entry_count = 2;

	failures += test_expect_bool("physmap reserved high enable",
				     hal_mmu_enable_physmap(&mem), true);
	failures += test_expect_bool(
		"physmap reserved high not covered",
		plane_vaddr_is_null(hal_mmu_physmap_phys_to_virt(
			test_paddr(high_phys))),
		true);

	return failures;
}

static int test_physmap_covers_ram_like_boot_regions(void)
{
	struct plane_mem_info mem = {0};
	uint64_t high_bootloader = 0x200000000ull;
	uint64_t high_kernel = 0x300000000ull;
	int failures = 0;

	test_set_mb2_bootstrap_physmap_window();

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.map[1].base = plane_paddr_make(high_bootloader);
	mem.map[1].length = 0x1000;
	mem.map[1].type = PLANE_MEM_BOOTLOADER_RECLAIMABLE;
	mem.map[2].base = plane_paddr_make(high_kernel);
	mem.map[2].length = 0x1000;
	mem.map[2].type = PLANE_MEM_EXECUTABLE_AND_MODULES;
	mem.entry_count = 3;

	failures += test_expect_bool("physmap ram-like enable",
				     hal_mmu_enable_physmap(&mem), true);
	failures += test_expect_u64(
		"physmap bootloader covered",
		plane_vaddr_raw(hal_mmu_physmap_phys_to_virt(
			test_paddr(high_bootloader))),
		X86_64_PHYSMAP_BASE + high_bootloader);
	failures += test_expect_u64(
		"physmap kernel/modules covered",
		plane_vaddr_raw(hal_mmu_physmap_phys_to_virt(
			test_paddr(high_kernel))),
		X86_64_PHYSMAP_BASE + high_kernel);

	return failures;
}

static int test_physmap_commit_converges_to_plane_base(void)
{
	struct plane_mem_info mem = {0};
	uint64_t bootstrap_base =
		X86_64_PHYSMAP_BASE + X86_64_PHYSMAP_BOOTSTRAP_SIZE;
	int failures = 0;

	x86_64_physmap_set_bootstrap_window(
		test_vaddr(bootstrap_base),
		X86_64_PHYSMAP_BOOTSTRAP_SIZE);

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x2000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("physmap commit enable",
				     hal_mmu_enable_physmap(&mem), true);
	failures += test_expect_u64(
		"physmap commit bootstrap base before ownership",
		plane_vaddr_raw(hal_mmu_physmap_phys_to_virt(test_paddr(0x1000))),
		bootstrap_base + 0x1000);

	x86_64_physmap_commit_owned();

	failures += test_expect_u64(
		"physmap commit plane base after ownership",
		plane_vaddr_raw(hal_mmu_physmap_phys_to_virt(test_paddr(0x1000))),
		X86_64_PHYSMAP_BASE + 0x1000);

	return failures;
}

static int test_physmap_rejects_mb2_bootstrap_shortfall(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	test_set_mb2_bootstrap_physmap_window();

	mem.map[0].base = plane_paddr_make(
		X86_64_PHYSMAP_BOOTSTRAP_SIZE);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("physmap bootstrap shortfall",
				     hal_mmu_enable_physmap(&mem), false);
	failures += test_expect_bool("physmap bootstrap shortfall disables",
				     plane_vaddr_is_null(
					     hal_mmu_physmap_phys_to_virt(
						     test_paddr(0))),
				     true);

	return failures;
}

static int test_physmap_rejects_bootstrap_kernel_map_overlap(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	x86_64_physmap_set_bootstrap_window(
		test_vaddr(X86_64_KERNEL_MAP_BASE - ARCH_LARGE_PAGE_SIZE),
		2 * ARCH_LARGE_PAGE_SIZE);

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("physmap bootstrap kernel overlap",
				     hal_mmu_enable_physmap(&mem), false);

	return failures;
}

static int test_physmap_rejects_ram_like_memory_above_window(void)
{
	struct plane_mem_info mem = {0};
	uint64_t hhdm_base = test_hhdm_base();
	int failures = 0;

	test_set_limine_bootstrap_physmap_window(hhdm_base);

	mem.map[0].base = plane_paddr_make(X86_64_PHYSMAP_WINDOW_SIZE);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("physmap window reject",
				     hal_mmu_enable_physmap(&mem), false);
	failures += test_expect_bool("physmap window reject disables phys",
				     plane_vaddr_is_null(
					     hal_mmu_physmap_phys_to_virt(
						     test_paddr(0))),
				     true);
	failures += test_expect_u64("physmap window reject disables virt",
				    test_paddr_raw(hal_mmu_physmap_virt_to_phys(
					    test_vaddr(hhdm_base))),
				    HAL_MMU_INVALID_PHYS);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_physmap_supports_runtime_coverage_above_64g),
		TEST_CASE(test_physmap_reserved_high_memory_does_not_extend_coverage),
		TEST_CASE(test_physmap_covers_ram_like_boot_regions),
		TEST_CASE(test_physmap_runtime_counts_owned_pml4_slots),
		TEST_CASE(test_physmap_commit_converges_to_plane_base),
		TEST_CASE(test_physmap_rejects_mb2_bootstrap_shortfall),
		TEST_CASE(test_physmap_rejects_bootstrap_kernel_map_overlap),
		TEST_CASE(test_physmap_rejects_ram_like_memory_above_window),
		TEST_CASE(test_physmap_roundtrip),
		TEST_CASE(test_physmap_rejects_invalid_ranges),
		TEST_CASE(test_bootstrap_physmap_window_base),
	};

	return test_run_cases("x86_64_physmap_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
