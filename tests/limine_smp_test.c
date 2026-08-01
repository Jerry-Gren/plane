#include <stdbool.h>
#include <stdint.h>

#include <hal/cpu.h>
#include <hal/irq.h>
#include <plane/smp.h>

#include "../boot/limine/limine_smp_internal.h"
#include "../kernel/smp_internal.h"
#include "support/test.h"

bool hal_cpu_set_current_data(struct plane_cpu_data *data)
{
	return data != NULL;
}

bool hal_cpu_prepare_ap_startup_context(struct plane_cpu_data *data)
{
	return data != NULL;
}

bool hal_cpu_install_ap_startup_context(struct plane_cpu_data *data)
{
	return data != NULL;
}

bool hal_cpu_init_bsp_local_interrupts(const struct plane_smp_info *info)
{
	return info != NULL;
}

bool hal_cpu_init_ap_local_interrupts(struct plane_cpu_data *data)
{
	return data != NULL;
}

void hal_irq_disable(void)
{
}

void hal_cpu_hang(void)
{
	for (;;) {
	}
}

void hal_cpu_enter_on_stack(plane_vaddr_t stack_top,
			    void (*entry)(struct plane_cpu_data *data),
			    struct plane_cpu_data *data)
{
	(void)stack_top;
	(void)entry;
	(void)data;
	for (;;) {
	}
}

static int test_limine_start_preflights_and_sets_ap_entries(void)
{
	int failures = 0;
	struct plane_smp_info info;
	struct limine_mp_info ap1 = {0};
	struct limine_mp_info ap2 = {0};

	failures += test_expect_bool("init bsp",
				     plane_smp_info_init_bsp(&info, 1), true);
	failures += test_expect_bool("record ap1",
				     plane_smp_info_record_cpu(&info, 2, false),
				     true);
	failures += test_expect_bool("record ap2",
				     plane_smp_info_record_cpu(&info, 3, false),
				     true);
	failures += test_expect_bool("init smp",
				     plane_smp_init_bsp(&info), true);
	failures += test_expect_bool("prepare ap1",
				     plane_smp_prepare_ap_stack(
					     1, plane_vaddr_make(0x800000), 1),
				     true);

	boot_limine_smp_reset_cpu_handles();
	failures += test_expect_bool("set ap1 handle",
				     boot_limine_smp_set_cpu_handle(1, &ap1),
				     true);
	failures += test_expect_bool("set ap2 handle",
				     boot_limine_smp_set_cpu_handle(2, &ap2),
				     true);
	failures += test_expect_bool("unprepared ap blocks start",
				     boot_limine_smp_start_aps(), false);
	failures += test_expect_u32("ap1 still prepared",
				    plane_cpu_boot_state(1),
				    PLANE_CPU_BOOT_PREPARED);
	failures += test_expect_ptr("ap1 goto unchanged",
				    ap1.goto_address, NULL);
	failures += test_expect_u64("ap1 extra unchanged",
				    ap1.extra_argument, 0);

	failures += test_expect_bool("prepare ap2",
				     plane_smp_prepare_ap_stack(
					     2, plane_vaddr_make(0x900000), 1),
				     true);
	boot_limine_smp_reset_cpu_handles();
	failures += test_expect_bool("set only ap1 handle",
				     boot_limine_smp_set_cpu_handle(1, &ap1),
				     true);
	failures += test_expect_bool("missing ap2 handle blocks start",
				     boot_limine_smp_start_aps(), false);
	failures += test_expect_u32("ap1 still prepared after missing handle",
				    plane_cpu_boot_state(1),
				    PLANE_CPU_BOOT_PREPARED);
	failures += test_expect_ptr("ap1 goto still unchanged",
				    ap1.goto_address, NULL);

	failures += test_expect_bool("set ap2 handle",
				     boot_limine_smp_set_cpu_handle(2, &ap2),
				     true);
	failures += test_expect_bool("start aps succeeds",
				     boot_limine_smp_start_aps(), true);
	failures += test_expect_u32("ap1 starting",
				    plane_cpu_boot_state(1),
				    PLANE_CPU_BOOT_STARTING);
	failures += test_expect_u32("ap2 starting",
				    plane_cpu_boot_state(2),
				    PLANE_CPU_BOOT_STARTING);
	failures += test_expect_u64("ap1 extra points at cpu data",
				    ap1.extra_argument,
				    (uint64_t)(uintptr_t)
					    plane_cpu_boot_data_get(1));
	failures += test_expect_u64("ap2 extra points at cpu data",
				    ap2.extra_argument,
				    (uint64_t)(uintptr_t)
					    plane_cpu_boot_data_get(2));
	failures += test_expect_not_null("ap1 goto set", ap1.goto_address);
	failures += test_expect_ptr("ap2 uses same ap entry",
				    ap2.goto_address, ap1.goto_address);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_limine_start_preflights_and_sets_ap_entries),
	};

	return test_run_cases("limine_smp_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
