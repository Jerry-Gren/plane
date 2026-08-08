#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>
#include <hal/x86_64/mmu_internal.h>
#include <plane/memmap.h>

#include "support/test.h"

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

static int test_direct_map_roundtrip(void)
{
	struct plane_mem_info mem = {0};
	plane_vaddr_t vaddr;
	int failures = 0;

	hal_mmu_set_direct_map_base(test_vaddr(X86_64_DIRECT_MAP_BASE));

	mem.map[0].base = plane_paddr_make(0x1000);
	mem.map[0].length = 0x3000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.map[1].base = plane_paddr_make(X86_64_DIRECT_MAP_WINDOW_SIZE);
	mem.map[1].length = 0x1000;
	mem.map[1].type = PLANE_MEM_RESERVED;
	mem.entry_count = 2;

	failures += test_expect_bool("direct map enable",
				     hal_mmu_enable_direct_map(&mem), true);

	vaddr = hal_mmu_direct_phys_to_virt(test_paddr(0x2000));
	failures += test_expect_u64("direct phys to virt",
				    plane_vaddr_raw(vaddr),
				    X86_64_DIRECT_MAP_BASE + 0x2000);
	failures += test_expect_u64(
		"direct phys range to virt",
		plane_vaddr_raw(hal_mmu_direct_phys_range_to_virt(
			test_paddr(0x2000), 0x1000)),
		X86_64_DIRECT_MAP_BASE + 0x2000);
	failures += test_expect_u64("direct virt to phys",
				    test_paddr_raw(
					    hal_mmu_direct_virt_to_phys(vaddr)),
				    0x2000);

	failures += test_expect_bool(
		"direct reject outside runtime coverage",
		plane_vaddr_is_null(hal_mmu_direct_phys_to_virt(
			test_paddr(ARCH_LARGE_PAGE_SIZE))),
		true);
	failures += test_expect_u64("direct reject kernel vma",
				    test_paddr_raw(hal_mmu_direct_virt_to_phys(
					    test_vaddr(KERNEL_VMA_BASE))),
				    HAL_MMU_INVALID_PHYS);

	return failures;
}

static int test_direct_map_rejects_invalid_ranges(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	hal_mmu_set_direct_map_base(test_vaddr(X86_64_DIRECT_MAP_BASE));

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("range reject init",
				     hal_mmu_enable_direct_map(&mem), true);
	failures += test_expect_bool("range reject zero size",
				     plane_vaddr_is_null(
					     hal_mmu_direct_phys_range_to_virt(
						     test_paddr(0), 0)),
				     true);
	failures += test_expect_bool(
		"range reject start out of range",
		plane_vaddr_is_null(hal_mmu_direct_phys_range_to_virt(
			test_paddr(ARCH_LARGE_PAGE_SIZE), 1)),
		true);
	failures += test_expect_bool(
		"range reject end past direct map",
		plane_vaddr_is_null(hal_mmu_direct_phys_range_to_virt(
			test_paddr(ARCH_LARGE_PAGE_SIZE - 1), 2)),
		true);
	failures += test_expect_bool(
		"range reject phys overflow",
		plane_vaddr_is_null(hal_mmu_direct_phys_range_to_virt(
			test_paddr(UINT64_MAX), 2)),
		true);

	return failures;
}

static int test_bootloader_direct_map_base(void)
{
	struct plane_mem_info mem = {0};
	uint64_t bootloader_base =
		X86_64_DIRECT_MAP_BASE + X86_64_DIRECT_MAP_WINDOW_SIZE;
	plane_vaddr_t vaddr;
	int failures = 0;

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	hal_mmu_set_direct_map_base(test_vaddr(bootloader_base));
	failures += test_expect_bool("bootloader direct map enable",
				     hal_mmu_enable_direct_map(&mem), true);
	vaddr = hal_mmu_direct_phys_to_virt(test_paddr(0x1000));
	failures += test_expect_u64("bootloader direct phys to virt",
				    plane_vaddr_raw(vaddr),
				    bootloader_base + 0x1000);
	failures += test_expect_u64("bootloader direct virt to phys",
				    test_paddr_raw(
					    hal_mmu_direct_virt_to_phys(vaddr)),
				    0x1000);

	return failures;
}

static int test_kernel_vma_range(void)
{
	plane_vaddr_t base = {0};
	uint64_t size = 0;
	int failures = 0;

	failures += test_expect_bool("kernel range",
				     hal_mmu_kernel_vma_range(&base, &size),
				     true);
	failures += test_expect_u64("kernel range base",
				    plane_vaddr_raw(base),
				    X86_64_KERNEL_MAP_BASE);
	failures += test_expect_u64("kernel range size",
				    size, X86_64_KERNEL_MAP_SIZE);
	failures += test_expect_bool("kernel range null base",
				     hal_mmu_kernel_vma_range(NULL, &size),
				     false);
	failures += test_expect_bool("kernel range null size",
				     hal_mmu_kernel_vma_range(&base, NULL),
				     false);
	failures += test_expect_bool(
		"kernel range avoids direct map",
		X86_64_KERNEL_MAP_BASE >= X86_64_DIRECT_MAP_WINDOW_END ||
		X86_64_KERNEL_MAP_END <= X86_64_DIRECT_MAP_BASE,
		true);
	failures += test_expect_bool(
		"kernel range avoids kernel image",
		KERNEL_VMA_BASE < X86_64_KERNEL_MAP_BASE ||
		KERNEL_VMA_BASE >= X86_64_KERNEL_MAP_END,
		true);

	return failures;
}

static int test_direct_map_supports_runtime_coverage_above_64g(void)
{
	struct plane_mem_info mem = {0};
	uint64_t high_phys = 0x1000000000ull;
	plane_vaddr_t vaddr;
	int failures = 0;

	hal_mmu_set_direct_map_base(test_vaddr(X86_64_DIRECT_MAP_BASE));

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x2000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.map[1].base = plane_paddr_make(high_phys);
	mem.map[1].length = 0x3000;
	mem.map[1].type = PLANE_MEM_USABLE;
	mem.entry_count = 2;

	failures += test_expect_bool("direct high enable",
				     hal_mmu_enable_direct_map(&mem), true);
	vaddr = hal_mmu_direct_phys_to_virt(test_paddr(high_phys + 0x2000));
	failures += test_expect_u64("direct high phys to virt",
				    plane_vaddr_raw(vaddr),
				    X86_64_DIRECT_MAP_BASE + high_phys + 0x2000);
	failures += test_expect_bool(
		"direct high rejects beyond runtime coverage",
		plane_vaddr_is_null(hal_mmu_direct_phys_to_virt(
			test_paddr(high_phys + ARCH_LARGE_PAGE_SIZE))),
		true);

	return failures;
}

static int test_direct_map_reserved_high_memory_does_not_extend_coverage(void)
{
	struct plane_mem_info mem = {0};
	uint64_t high_phys = 0x100000000ull;
	int failures = 0;

	hal_mmu_set_direct_map_base(test_vaddr(X86_64_DIRECT_MAP_BASE));

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.map[1].base = plane_paddr_make(high_phys);
	mem.map[1].length = 0x1000;
	mem.map[1].type = PLANE_MEM_FRAMEBUFFER;
	mem.entry_count = 2;

	failures += test_expect_bool("direct reserved high enable",
				     hal_mmu_enable_direct_map(&mem), true);
	failures += test_expect_bool(
		"direct reserved high not covered",
		plane_vaddr_is_null(hal_mmu_direct_phys_to_virt(
			test_paddr(high_phys))),
		true);

	return failures;
}

static int test_direct_map_covers_ram_like_boot_regions(void)
{
	struct plane_mem_info mem = {0};
	uint64_t high_bootloader = 0x200000000ull;
	uint64_t high_kernel = 0x300000000ull;
	int failures = 0;

	hal_mmu_set_direct_map_base(test_vaddr(X86_64_DIRECT_MAP_BASE));

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

	failures += test_expect_bool("direct ram-like enable",
				     hal_mmu_enable_direct_map(&mem), true);
	failures += test_expect_u64(
		"direct bootloader covered",
		plane_vaddr_raw(hal_mmu_direct_phys_to_virt(
			test_paddr(high_bootloader))),
		X86_64_DIRECT_MAP_BASE + high_bootloader);
	failures += test_expect_u64(
		"direct kernel/modules covered",
		plane_vaddr_raw(hal_mmu_direct_phys_to_virt(
			test_paddr(high_kernel))),
		X86_64_DIRECT_MAP_BASE + high_kernel);

	return failures;
}

static int test_direct_map_commit_converges_to_plane_base(void)
{
	struct plane_mem_info mem = {0};
	uint64_t bootloader_base =
		X86_64_DIRECT_MAP_BASE + X86_64_DIRECT_MAP_WINDOW_SIZE;
	int failures = 0;

	hal_mmu_set_direct_map_base(test_vaddr(bootloader_base));

	mem.map[0].base = plane_paddr_make(0);
	mem.map[0].length = 0x2000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("direct commit enable",
				     hal_mmu_enable_direct_map(&mem), true);
	failures += test_expect_u64(
		"direct commit boot base before ownership",
		plane_vaddr_raw(hal_mmu_direct_phys_to_virt(test_paddr(0x1000))),
		bootloader_base + 0x1000);

	x86_64_mmu_commit_owned_direct_map();

	failures += test_expect_u64(
		"direct commit plane base after ownership",
		plane_vaddr_raw(hal_mmu_direct_phys_to_virt(test_paddr(0x1000))),
		X86_64_DIRECT_MAP_BASE + 0x1000);

	return failures;
}

static int test_direct_map_rejects_ram_like_memory_above_window(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	hal_mmu_set_direct_map_base(test_vaddr(X86_64_DIRECT_MAP_BASE));

	mem.map[0].base = plane_paddr_make(X86_64_DIRECT_MAP_WINDOW_SIZE);
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("direct window reject",
				     hal_mmu_enable_direct_map(&mem), false);
	failures += test_expect_bool("direct window reject disables phys",
				     plane_vaddr_is_null(
					     hal_mmu_direct_phys_to_virt(
						     test_paddr(0))),
				     true);
	failures += test_expect_u64("direct window reject disables virt",
				    test_paddr_raw(hal_mmu_direct_virt_to_phys(
					    test_vaddr(X86_64_DIRECT_MAP_BASE))),
				    HAL_MMU_INVALID_PHYS);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_direct_map_supports_runtime_coverage_above_64g),
		TEST_CASE(test_direct_map_reserved_high_memory_does_not_extend_coverage),
		TEST_CASE(test_direct_map_covers_ram_like_boot_regions),
		TEST_CASE(test_direct_map_commit_converges_to_plane_base),
		TEST_CASE(test_direct_map_rejects_ram_like_memory_above_window),
		TEST_CASE(test_direct_map_roundtrip),
		TEST_CASE(test_direct_map_rejects_invalid_ranges),
		TEST_CASE(test_bootloader_direct_map_base),
		TEST_CASE(test_kernel_vma_range),
	};

	return test_run_cases("x86_64_mmu_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
