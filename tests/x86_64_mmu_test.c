#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>
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
	mem.map[1].base = plane_paddr_make(X86_64_DIRECT_MAP_SIZE);
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
		"direct reject out of range phys",
		plane_vaddr_is_null(hal_mmu_direct_phys_to_virt(
			test_paddr(X86_64_DIRECT_MAP_SIZE))),
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
			test_paddr(X86_64_DIRECT_MAP_SIZE), 1)),
		true);
	failures += test_expect_bool(
		"range reject end past direct map",
		plane_vaddr_is_null(hal_mmu_direct_phys_range_to_virt(
			test_paddr(X86_64_DIRECT_MAP_SIZE - 1), 2)),
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
	uint64_t bootloader_base = X86_64_DIRECT_MAP_BASE + X86_64_DIRECT_MAP_SIZE;
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
		X86_64_KERNEL_MAP_BASE >= X86_64_DIRECT_MAP_END ||
		X86_64_KERNEL_MAP_END <= X86_64_DIRECT_MAP_BASE,
		true);
	failures += test_expect_bool(
		"kernel range avoids kernel image",
		KERNEL_VMA_BASE < X86_64_KERNEL_MAP_BASE ||
		KERNEL_VMA_BASE >= X86_64_KERNEL_MAP_END,
		true);

	return failures;
}

static int test_direct_map_rejects_uncovered_usable_memory(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	hal_mmu_set_direct_map_base(test_vaddr(X86_64_DIRECT_MAP_BASE));

	mem.map[0].base = plane_paddr_make(X86_64_DIRECT_MAP_SIZE - 0x1000);
	mem.map[0].length = 0x2000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("direct reject uncovered usable",
				     hal_mmu_enable_direct_map(&mem), false);
	failures += test_expect_bool("direct reject leaves phys unavailable",
				     plane_vaddr_is_null(
					     hal_mmu_direct_phys_to_virt(
						     test_paddr(0))),
				     true);
	failures += test_expect_u64("direct reject leaves virt unavailable",
				    test_paddr_raw(hal_mmu_direct_virt_to_phys(
					    test_vaddr(X86_64_DIRECT_MAP_BASE))),
				    HAL_MMU_INVALID_PHYS);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_direct_map_rejects_uncovered_usable_memory),
		TEST_CASE(test_direct_map_roundtrip),
		TEST_CASE(test_direct_map_rejects_invalid_ranges),
		TEST_CASE(test_bootloader_direct_map_base),
		TEST_CASE(test_kernel_vma_range),
	};

	return test_run_cases("x86_64_mmu_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
