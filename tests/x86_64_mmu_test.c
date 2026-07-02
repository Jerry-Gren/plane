#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>
#include <plane/memmap.h>

#include "support/test.h"

static int test_direct_map_roundtrip(void)
{
	struct plane_mem_info mem = {0};
	void *vaddr;
	int failures = 0;

	hal_mmu_set_direct_map_base(X86_64_DIRECT_MAP_BASE);

	mem.map[0].base = 0x1000;
	mem.map[0].length = 0x3000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.map[1].base = X86_64_DIRECT_MAP_SIZE;
	mem.map[1].length = 0x1000;
	mem.map[1].type = PLANE_MEM_RESERVED;
	mem.entry_count = 2;

	failures += test_expect_bool("direct map enable",
				     hal_mmu_enable_direct_map(&mem), true);

	vaddr = hal_mmu_direct_phys_to_virt(0x2000);
	failures += test_expect_ptr("direct phys to virt",
				    vaddr,
				    (void *)(X86_64_DIRECT_MAP_BASE + 0x2000));
	failures += test_expect_u64("direct virt to phys",
				    hal_mmu_direct_virt_to_phys(vaddr), 0x2000);

	failures += test_expect_ptr(
		"direct reject out of range phys",
		hal_mmu_direct_phys_to_virt(X86_64_DIRECT_MAP_SIZE),
		NULL);
	failures += test_expect_u64("direct reject kernel vma",
				    hal_mmu_direct_virt_to_phys(
					    (void *)KERNEL_VMA_BASE),
				    UINT64_MAX);

	return failures;
}

static int test_bootloader_direct_map_base(void)
{
	struct plane_mem_info mem = {0};
	uint64_t bootloader_base = X86_64_DIRECT_MAP_BASE + X86_64_DIRECT_MAP_SIZE;
	void *vaddr;
	int failures = 0;

	mem.map[0].base = 0;
	mem.map[0].length = 0x1000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	hal_mmu_set_direct_map_base(bootloader_base);
	failures += test_expect_bool("bootloader direct map enable",
				     hal_mmu_enable_direct_map(&mem), true);
	vaddr = hal_mmu_direct_phys_to_virt(0x1000);
	failures += test_expect_ptr("bootloader direct phys to virt",
				    vaddr, (void *)(bootloader_base + 0x1000));
	failures += test_expect_u64("bootloader direct virt to phys",
				    hal_mmu_direct_virt_to_phys(vaddr), 0x1000);

	return failures;
}

static int test_direct_map_rejects_uncovered_usable_memory(void)
{
	struct plane_mem_info mem = {0};
	int failures = 0;

	hal_mmu_set_direct_map_base(X86_64_DIRECT_MAP_BASE);

	mem.map[0].base = X86_64_DIRECT_MAP_SIZE - 0x1000;
	mem.map[0].length = 0x2000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("direct reject uncovered usable",
				     hal_mmu_enable_direct_map(&mem), false);
	failures += test_expect_ptr("direct reject leaves phys unavailable",
				    hal_mmu_direct_phys_to_virt(0), NULL);
	failures += test_expect_u64("direct reject leaves virt unavailable",
				    hal_mmu_direct_virt_to_phys(
					    (void *)X86_64_DIRECT_MAP_BASE),
				    UINT64_MAX);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_direct_map_rejects_uncovered_usable_memory),
		TEST_CASE(test_direct_map_roundtrip),
		TEST_CASE(test_bootloader_direct_map_base),
	};

	return test_run_cases("x86_64_mmu_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
