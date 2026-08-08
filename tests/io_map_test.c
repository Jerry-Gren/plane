#include <stdbool.h>
#include <stdint.h>

#include <hal/mmu.h>
#include <plane/io_map.h>
#include <plane/mm.h>

#include "support/test.h"

#define TEST_IO_BASE 0xffff900000100000ull
#define TEST_MAX_MAPPINGS 8

struct test_mapping {
	plane_vaddr_t vaddr;
	plane_paddr_t phys_addr;
	struct hal_mmu_map_options options;
	bool mapped;
};

static struct test_mapping mappings[TEST_MAX_MAPPINGS];
static uint64_t map_count;
static uint64_t unmap_count;
static uint64_t release_count;
static uint64_t reserve_page_count;
static uint64_t release_page_count;
static uint64_t map_fail_after;
static plane_vaddr_t last_reserve_base;
static plane_vaddr_t last_release_base;
static uint32_t reserve_prot;
static bool reserve_fail;
static bool reserved_va_present;

#include "../kernel/mm/io_map.c"

static void reset_io_map_test(void)
{
	for (uint64_t i = 0; i < TEST_MAX_MAPPINGS; i++) {
		mappings[i] = (struct test_mapping){0};
	}
	io_map_initialized = false;
	map_count = 0;
	unmap_count = 0;
	release_count = 0;
	reserve_page_count = 0;
	release_page_count = 0;
	map_fail_after = UINT64_MAX;
	last_reserve_base = plane_vaddr_make(0);
	last_release_base = plane_vaddr_make(0);
	reserve_prot = 0;
	reserve_fail = false;
	reserved_va_present = false;
}

bool plane_kmem_reserve_va_pages(uint64_t page_count,
				 uint32_t prot,
				 plane_vaddr_t *vaddr)
{
	if (reserve_fail || page_count == 0 || vaddr == NULL) {
		return false;
	}

	reserve_page_count = page_count;
	reserve_prot = prot;
	last_reserve_base = plane_vaddr_make(TEST_IO_BASE);
	*vaddr = last_reserve_base;
	reserved_va_present = true;
	return true;
}

bool plane_kmem_va_pages_reserved(plane_vaddr_t vaddr, uint64_t page_count)
{
	return reserved_va_present &&
	       plane_vaddr_raw(vaddr) == TEST_IO_BASE &&
	       page_count == reserve_page_count;
}

bool plane_kmem_release_va_pages(plane_vaddr_t vaddr, uint64_t page_count)
{
	release_count++;
	last_release_base = vaddr;
	release_page_count = page_count;
	if (!plane_kmem_va_pages_reserved(vaddr, page_count)) {
		return false;
	}

	reserved_va_present = false;
	return true;
}

bool hal_mmu_map_kernel_page(plane_vaddr_t vaddr,
			     plane_paddr_t phys_addr,
			     struct hal_mmu_map_options options)
{
	if (map_count >= map_fail_after || map_count >= TEST_MAX_MAPPINGS) {
		return false;
	}

	mappings[map_count] = (struct test_mapping){
		.vaddr = vaddr,
		.phys_addr = phys_addr,
		.options = options,
		.mapped = true,
	};
	map_count++;
	return true;
}

bool hal_mmu_unmap_kernel_page(plane_vaddr_t vaddr)
{
	for (uint64_t i = 0; i < map_count; i++) {
		if (mappings[i].mapped &&
		    plane_vaddr_raw(mappings[i].vaddr) ==
			    plane_vaddr_raw(vaddr)) {
			mappings[i].mapped = false;
			unmap_count++;
			return true;
		}
	}

	return false;
}

static int test_init_and_invalid_inputs(void)
{
	plane_vaddr_t vaddr;
	int failures = 0;

	failures += test_expect_bool("map before init",
				     plane_io_map(plane_paddr_make(0x1000),
						  PAGE_SIZE,
						  PLANE_IO_MAP_CACHE_DEVICE,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     false);
	failures += test_expect_bool("init succeeds", plane_io_map_init(),
				     true);
	failures += test_expect_bool("repeat init fails", plane_io_map_init(),
				     false);
	failures += test_expect_bool("null out fails",
				     plane_io_map(plane_paddr_make(0x1000),
						  PAGE_SIZE,
						  PLANE_IO_MAP_CACHE_DEVICE,
						  PLANE_VM_PROT_READ,
						  NULL),
				     false);
	failures += test_expect_bool("zero size fails",
				     plane_io_map(plane_paddr_make(0x1000),
						  0,
						  PLANE_IO_MAP_CACHE_DEVICE,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     false);
	failures += test_expect_bool("bad cache fails",
				     plane_io_map(plane_paddr_make(0x1000),
						  PAGE_SIZE,
						  (enum plane_io_map_cache)99,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     false);

	return failures;
}

static int test_unaligned_device_map_and_unmap(void)
{
	plane_vaddr_t vaddr;
	int failures = 0;

	failures += test_expect_bool("init succeeds", plane_io_map_init(),
				     true);
	failures += test_expect_bool("device map succeeds",
				     plane_io_map(plane_paddr_make(0x12345),
						  5000,
						  PLANE_IO_MAP_CACHE_DEVICE,
						  PLANE_VM_PROT_READ |
							  PLANE_VM_PROT_WRITE,
						  &vaddr),
				     true);
	failures += test_expect_u64("returned offset vaddr",
				    plane_vaddr_raw(vaddr),
				    TEST_IO_BASE + 0x345);
	failures += test_expect_u64("reserved pages", reserve_page_count, 2);
	failures += test_expect_u32("reserved prot", reserve_prot,
				    PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE);
	failures += test_expect_u64("mapped pages", map_count, 2);
	failures += test_expect_u64("first map vaddr",
				    plane_vaddr_raw(mappings[0].vaddr),
				    TEST_IO_BASE);
	failures += test_expect_u64("first map phys",
				    plane_paddr_raw(mappings[0].phys_addr),
				    0x12000);
	failures += test_expect_u32("first map prot", mappings[0].options.prot,
				    PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE);
	failures += test_expect_u32("first map attr",
				    mappings[0].options.attr,
				    HAL_MMU_MAPPING_DEVICE);
	failures += test_expect_u64("second map phys",
				    plane_paddr_raw(mappings[1].phys_addr),
				    0x13000);

	failures += test_expect_bool("device unmap succeeds",
				     plane_io_unmap(vaddr, 5000), true);
	failures += test_expect_u64("unmapped pages", unmap_count, 2);
	failures += test_expect_u64("release count", release_count, 1);
	failures += test_expect_u64("release base",
				    plane_vaddr_raw(last_release_base),
				    TEST_IO_BASE);
	failures += test_expect_u64("release pages", release_page_count, 2);

	return failures;
}

static int test_write_combine_map_uses_cache_flag(void)
{
	plane_vaddr_t vaddr;
	int failures = 0;

	failures += test_expect_bool("init succeeds", plane_io_map_init(),
				     true);
	failures += test_expect_bool("wc map succeeds",
				     plane_io_map(plane_paddr_make(0x4000),
						  PAGE_SIZE,
						  PLANE_IO_MAP_CACHE_WRITE_COMBINE,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     true);
	failures += test_expect_u32("wc map prot", mappings[0].options.prot,
				    PLANE_VM_PROT_READ);
	failures += test_expect_u32("wc map attr", mappings[0].options.attr,
				    HAL_MMU_MAPPING_WRITE_COMBINE);

	return failures;
}

static int test_map_failure_rolls_back_prefix(void)
{
	plane_vaddr_t vaddr;
	int failures = 0;

	failures += test_expect_bool("init succeeds", plane_io_map_init(),
				     true);
	map_fail_after = 1;
	failures += test_expect_bool("map fails",
				     plane_io_map(plane_paddr_make(0x2000),
						  PAGE_SIZE * 2,
						  PLANE_IO_MAP_CACHE_DEVICE,
						  PLANE_VM_PROT_READ,
						  &vaddr),
				     false);
	failures += test_expect_u64("one map attempted", map_count, 1);
	failures += test_expect_u64("prefix unmapped", unmap_count, 1);
	failures += test_expect_u64("reservation released", release_count, 1);
	failures += test_expect_u64("release pages", release_page_count, 2);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_init_and_invalid_inputs),
		TEST_CASE(test_unaligned_device_map_and_unmap),
		TEST_CASE(test_write_combine_map_uses_cache_flag),
		TEST_CASE(test_map_failure_rolls_back_prefix),
	};

	return test_run_cases_with_fixture("io_map_test", cases,
					   TEST_ARRAY_SIZE(cases),
					   reset_io_map_test, NULL);
}
