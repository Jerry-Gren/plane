#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>
#include <plane/memmap.h>

static int expect_bool(const char *name, bool actual, bool expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=%d actual=%d\n", name, expected, actual);
	return 1;
}

static int expect_ptr(const char *name, const void *actual, const void *expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=%p actual=%p\n", name, expected, actual);
	return 1;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
	if (actual == expected) {
		return 0;
	}

	printf("FAIL: %s expected=0x%016llx actual=0x%016llx\n",
	       name, (unsigned long long)expected, (unsigned long long)actual);
	return 1;
}

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

	failures += expect_bool("direct map enable",
				hal_mmu_enable_direct_map(&mem), true);

	vaddr = hal_mmu_direct_phys_to_virt(0x2000);
	failures += expect_ptr("direct phys to virt",
			       vaddr,
			       (void *)(X86_64_DIRECT_MAP_BASE + 0x2000));
	failures += expect_u64("direct virt to phys",
			       hal_mmu_direct_virt_to_phys(vaddr), 0x2000);

	failures += expect_ptr("direct reject out of range phys",
			       hal_mmu_direct_phys_to_virt(X86_64_DIRECT_MAP_SIZE),
			       NULL);
	failures += expect_u64("direct reject kernel vma",
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
	failures += expect_bool("bootloader direct map enable",
				hal_mmu_enable_direct_map(&mem), true);
	vaddr = hal_mmu_direct_phys_to_virt(0x1000);
	failures += expect_ptr("bootloader direct phys to virt",
			       vaddr, (void *)(bootloader_base + 0x1000));
	failures += expect_u64("bootloader direct virt to phys",
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

	failures += expect_bool("direct reject uncovered usable",
				hal_mmu_enable_direct_map(&mem), false);
	failures += expect_ptr("direct reject leaves phys unavailable",
			       hal_mmu_direct_phys_to_virt(0), NULL);
	failures += expect_u64("direct reject leaves virt unavailable",
			       hal_mmu_direct_virt_to_phys(
				       (void *)X86_64_DIRECT_MAP_BASE),
			       UINT64_MAX);

	return failures;
}

int main(void)
{
	int failures = 0;

	failures += test_direct_map_rejects_uncovered_usable_memory();
	failures += test_direct_map_roundtrip();
	failures += test_bootloader_direct_map_base();

	if (failures != 0) {
		return 1;
	}

	printf("x86_64_mmu_test: ok\n");
	return 0;
}
