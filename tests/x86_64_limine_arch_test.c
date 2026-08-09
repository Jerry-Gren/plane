#include <stdint.h>

#include <boot/limine/limine_arch.h>
#include <machine/pmap.h>
#include <x86_64/address_space.h>
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

static uint64_t test_hhdm_base(void)
{
	return X86_64_KERNEL_MAP_BASE + X86_64_PAGING_PML4_SLOT_SIZE;
}

static int test_limine_arch_rejects_invalid_handoff(void)
{
	struct boot_limine_arch_handoff null_hhdm = {0};
	plane_paddr_t phys_addr = test_paddr(UINT64_MAX);
	int failures = 0;

	failures += test_expect_bool("limine arch install null hhdm physmap",
				     boot_limine_arch_install_hhdm_physmap(
					     NULL),
				     false);
	failures += test_expect_bool("limine arch install null hhdm",
				     boot_limine_arch_install_hhdm_physmap(
					     &null_hhdm),
				     false);
	failures += test_expect_bool("limine arch convert null handoff",
				     boot_limine_arch_hhdm_virt_to_phys(
					     NULL, test_vaddr(test_hhdm_base()),
					     &phys_addr),
				     false);
	failures += test_expect_bool("limine arch convert null output",
				     boot_limine_arch_hhdm_virt_to_phys(
					     &null_hhdm,
					     test_vaddr(test_hhdm_base()),
					     NULL),
				     false);
	failures += test_expect_bool("limine arch convert null hhdm",
				     boot_limine_arch_hhdm_virt_to_phys(
					     &null_hhdm,
					     test_vaddr(test_hhdm_base()),
					     &phys_addr),
				     false);

	return failures;
}

static int test_limine_arch_hhdm_virt_to_phys(void)
{
	struct boot_limine_arch_handoff handoff = {
		.hhdm_base = test_vaddr(test_hhdm_base()),
	};
	plane_paddr_t phys_addr = test_paddr(UINT64_MAX);
	int failures = 0;

	failures += test_expect_bool("limine arch convert hhdm va",
				     boot_limine_arch_hhdm_virt_to_phys(
					     &handoff,
					     test_vaddr(test_hhdm_base() +
							0x1234),
					     &phys_addr),
				     true);
	failures += test_expect_u64("limine arch converted phys",
				    test_paddr_raw(phys_addr), 0x1234);
	failures += test_expect_bool("limine arch reject below hhdm",
				     boot_limine_arch_hhdm_virt_to_phys(
					     &handoff,
					     test_vaddr(test_hhdm_base() - 1),
					     &phys_addr),
				     false);

	return failures;
}

static int test_limine_arch_installs_hhdm_physmap(void)
{
	struct boot_limine_arch_handoff handoff = {
		.hhdm_base = test_vaddr(test_hhdm_base()),
	};
	struct plane_mem_info mem = {0};
	plane_vaddr_t vaddr;
	int failures = 0;

	mem.map[0].base = test_paddr(0x1000);
	mem.map[0].length = 0x3000;
	mem.map[0].type = PLANE_MEM_USABLE;
	mem.entry_count = 1;

	failures += test_expect_bool("limine arch install hhdm physmap",
				     boot_limine_arch_install_hhdm_physmap(
					     &handoff),
				     true);
	failures += test_expect_bool("limine arch enable physmap",
				     physmap_enable(&mem), true);
	vaddr = physmap_phys_to_virt(test_paddr(0x2000));
	failures += test_expect_u64("limine arch physmap base",
				    plane_vaddr_raw(vaddr),
				    test_hhdm_base() + 0x2000);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_limine_arch_rejects_invalid_handoff),
		TEST_CASE(test_limine_arch_hhdm_virt_to_phys),
		TEST_CASE(test_limine_arch_installs_hhdm_physmap),
	};

	return test_run_cases("x86_64_limine_arch_test", cases,
			      TEST_ARRAY_SIZE(cases));
}
