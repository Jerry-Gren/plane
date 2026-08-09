#include <machine/machine_routines.h>
#include <machine/pmap.h>
#include <machine/serial.h>

#include <plane/boot_info.h>
#include <plane/framebuffer.h>
#include <plane/entry.h>
#include <plane/io_map.h>
#include <plane/kmem.h>
#include <plane/pmm.h>
#include <plane/smp.h>

#include "support/test.h"

static uint64_t current_step;
static uint64_t release_step;
static uint64_t ownership_step;
static uint64_t remap_step;
static uint64_t release_vaddr;
static uint64_t release_size;

void serial_init(void) {}

bool ml_startup_init(void)
{
	return true;
}

bool plane_smp_init_bsp(const struct plane_smp_info *info)
{
	return info != NULL;
}

uint32_t plane_cpu_count(void)
{
	return 1;
}

uint32_t plane_cpu_current_id(void)
{
	return 0;
}

bool plane_memmap_sanitize(struct plane_mem_info *mem)
{
	return mem != NULL;
}

bool physmap_enable(const struct plane_mem_info *mem)
{
	return mem != NULL;
}

bool plane_pmm_init(const struct plane_mem_info *mem)
{
	return mem != NULL;
}

bool pmap_take_kernel_page_table_ownership(void)
{
	ownership_step = ++current_step;
	return true;
}

bool plane_kmem_init(void)
{
	return true;
}

bool plane_io_map_init(void)
{
	return true;
}

bool plane_framebuffer_remap(struct plane_framebuffer_info *framebuffer_info)
{
	remap_step = ++current_step;
	return framebuffer_info != NULL;
}

bool ml_local_interrupt_init_bsp(const struct plane_smp_info *info)
{
	return info != NULL;
}

void plane_pmm_log_stats(void) {}

bool plane_smp_startup_prepare_ap_stacks(void)
{
	return true;
}

uint32_t plane_cpu_parked_count(void)
{
	return 0;
}

bool plane_framebuffer_draw_test_pattern(struct plane_framebuffer_info *framebuffer_info)
{
	return framebuffer_info != NULL;
}

static bool release_framebuffer_bootstrap_mapping(plane_vaddr_t vaddr,
					     uint64_t size)
{
	release_step = ++current_step;
	release_vaddr = plane_vaddr_raw(vaddr);
	release_size = size;
	return true;
}

static void reset_state(void)
{
	current_step = 0;
	release_step = 0;
	ownership_step = 0;
	remap_step = 0;
	release_vaddr = 0;
	release_size = 0;
}

static struct plane_boot_info boot_info_fixture(void)
{
	struct plane_boot_info info = {0};

	info.framebuffer.framebuffer_addr = plane_vaddr_make(0xffffffffc0001234ull);
	info.framebuffer.framebuffer_phys_addr = plane_paddr_make(0xe0001234ull);
	info.framebuffer.framebuffer_size = 0x300000;
	return info;
}

static int test_release_callback_runs_before_page_table_ownership(void)
{
	struct plane_boot_info info = boot_info_fixture();
	int failures = 0;

	reset_state();
	info.release_framebuffer_bootstrap_mapping =
		release_framebuffer_bootstrap_mapping;

	kmain(&info);
	failures += test_expect_bool("release callback called",
				     release_step != 0, true);
	failures += test_expect_bool("ownership called",
				     ownership_step != 0, true);
	failures += test_expect_bool("release before ownership",
				     release_step < ownership_step, true);
	failures += test_expect_bool("remap after ownership",
				     ownership_step < remap_step, true);
	failures += test_expect_u64("release vaddr",
				    release_vaddr,
				    0xffffffffc0001234ull);
	failures += test_expect_u64("release size",
				    release_size, 0x300000);

	return failures;
}

static int test_null_release_callback_is_skipped(void)
{
	struct plane_boot_info info = boot_info_fixture();
	int failures = 0;

	reset_state();

	kmain(&info);
	failures += test_expect_u64("no release callback",
				    release_step, 0);
	failures += test_expect_bool("ownership still called",
				     ownership_step != 0, true);
	failures += test_expect_bool("remap still after ownership",
				     ownership_step < remap_step, true);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_release_callback_runs_before_page_table_ownership),
		TEST_CASE(test_null_release_callback_is_skipped),
	};

	return test_run_cases("kernel_startup_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
