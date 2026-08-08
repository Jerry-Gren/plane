#include <hal/hal.h>
#include <hal/local_interrupt.h>
#include <hal/mmu.h>
#include <hal/serial.h>

#include <plane/boot_info.h>
#include <plane/early_video.h>
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

void hal_serial_init(void) {}

bool hal_arch_early_init(void)
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

bool plane_sanitize_memory_map(struct plane_mem_info *mem)
{
	return mem != NULL;
}

bool hal_mmu_enable_direct_map(const struct plane_mem_info *mem)
{
	return mem != NULL;
}

bool plane_pmm_init(const struct plane_mem_info *mem)
{
	return mem != NULL;
}

bool hal_mmu_take_kernel_page_table_ownership(void)
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

bool plane_early_video_remap_framebuffer(struct plane_video_info *video)
{
	remap_step = ++current_step;
	return video != NULL;
}

bool hal_local_interrupt_init_bsp(const struct plane_smp_info *info)
{
	return info != NULL;
}

void plane_pmm_log_stats(void) {}

bool plane_smp_prepare_ap_stacks(void)
{
	return true;
}

uint32_t plane_cpu_parked_count(void)
{
	return 0;
}

bool plane_early_video_draw_test_pattern(struct plane_video_info *video)
{
	return video != NULL;
}

static bool release_framebuffer_boot_mapping(plane_vaddr_t vaddr,
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

static struct boot_info boot_info_fixture(void)
{
	struct boot_info info = {0};

	info.video.framebuffer_addr = plane_vaddr_make(0xffffffffc0001234ull);
	info.video.framebuffer_phys_addr = plane_paddr_make(0xe0001234ull);
	info.video.framebuffer_size = 0x300000;
	return info;
}

static int test_release_callback_runs_before_page_table_ownership(void)
{
	struct boot_info info = boot_info_fixture();
	int failures = 0;

	reset_state();
	info.release_framebuffer_boot_mapping =
		release_framebuffer_boot_mapping;

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
	struct boot_info info = boot_info_fixture();
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

	return test_run_cases("main_boot_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
