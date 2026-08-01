#include <stdint.h>
#include <setjmp.h>

#include <hal/cpu.h>
#include <hal/irq.h>
#include <plane/smp.h>

#include "../kernel/smp_internal.h"
#include "support/test.h"

static bool hal_install_should_fail;
static bool hal_prepare_context_should_fail;
static bool hal_install_context_should_fail;
static uint32_t hal_install_count;
static uint32_t hal_prepare_context_count;
static uint32_t hal_install_context_count;
static uint32_t hal_hang_count;
static struct plane_cpu_data *hal_last_current_data;
static struct plane_cpu_data *hal_last_prepare_context_data;
static struct plane_cpu_data *hal_last_install_context_data;
static jmp_buf hal_hang_env;
static bool hal_hang_trap_enabled;

bool hal_cpu_set_current_data(struct plane_cpu_data *data)
{
	hal_install_count++;
	hal_last_current_data = data;
	return !hal_install_should_fail;
}

bool hal_cpu_prepare_ap_startup_context(struct plane_cpu_data *data)
{
	hal_prepare_context_count++;
	hal_last_prepare_context_data = data;
	return !hal_prepare_context_should_fail;
}

bool hal_cpu_install_ap_startup_context(struct plane_cpu_data *data)
{
	hal_install_context_count++;
	hal_last_install_context_data = data;
	return !hal_install_context_should_fail;
}

void hal_irq_disable(void)
{
}

void hal_cpu_hang(void)
{
	hal_hang_count++;
	if (hal_hang_trap_enabled) {
		longjmp(hal_hang_env, 1);
	}

	for (;;) {
	}
}

static void call_ap_park_entry(struct plane_cpu_data *data)
{
	hal_hang_trap_enabled = true;
	if (setjmp(hal_hang_env) == 0) {
		plane_smp_ap_park_entry(data);
	}
	hal_hang_trap_enabled = false;
}

static int test_builder_bsp_only(void)
{
	int failures = 0;
	struct plane_smp_info info;

	failures += test_expect_bool("init bsp",
				     plane_smp_info_init_bsp(&info, 9), true);
	failures += test_expect_u32("cpu count", info.cpu_count, 1);
	failures += test_expect_u32("discovered count",
				    info.discovered_cpu_count, 1);
	failures += test_expect_u32("bsp logical", info.bsp_logical_id, 0);
	failures += test_expect_u32("bsp lapic", info.cpus[0].lapic_id, 9);
	failures += test_expect_bool("bsp present", info.cpus[0].present, true);
	failures += test_expect_bool("bsp marked", info.cpus[0].is_bsp, true);
	failures += test_expect_bool("bsp not online before init",
				     info.cpus[0].online, false);
	return failures;
}

static int test_builder_truncates_extra_cpus(void)
{
	int failures = 0;
	struct plane_smp_info info;

	failures += test_expect_bool("init bsp",
				     plane_smp_info_init_bsp(&info, 0), true);
	for (uint32_t i = 1; i <= PLANE_MAX_CPUS; i++) {
		plane_smp_info_record_cpu(&info, i, false);
	}

	failures += test_expect_u32("stored max cpus",
				    info.cpu_count, PLANE_MAX_CPUS);
	failures += test_expect_u32("discovered includes truncated cpu",
				    info.discovered_cpu_count,
				    PLANE_MAX_CPUS + 1);
	failures += test_expect_bool("truncated", info.truncated, true);
	failures += test_expect_u32("last stored logical id",
				    info.cpus[PLANE_MAX_CPUS - 1].logical_id,
				    PLANE_MAX_CPUS - 1);
	return failures;
}

static int test_runtime_rejects_invalid_before_init(void)
{
	int failures = 0;
	struct plane_smp_info invalid = {0};
	struct plane_smp_info duplicate;

	failures += test_expect_bool("invalid init rejected",
				     plane_smp_init_bsp(&invalid), false);
	failures += test_expect_u32("invalid init does not install cpu data",
				    hal_install_count, 0);
	failures += test_expect_bool("duplicate init bsp",
				     plane_smp_info_init_bsp(&duplicate, 1),
				     true);
	failures += test_expect_bool("duplicate record ap",
				     plane_smp_info_record_cpu(&duplicate, 2,
							       false),
				     true);
	duplicate.cpus[1].lapic_id = duplicate.cpus[0].lapic_id;
	failures += test_expect_bool("duplicate lapic init rejected",
				     plane_smp_init_bsp(&duplicate), false);
	failures += test_expect_u32("duplicate init does not install cpu data",
				    hal_install_count, 0);

	struct plane_smp_info valid;

	failures += test_expect_bool("valid init bsp",
				     plane_smp_info_init_bsp(&valid, 5),
				     true);
	hal_install_should_fail = true;
	failures += test_expect_bool("hal install failure rejects init",
				     plane_smp_init_bsp(&valid), false);
	hal_install_should_fail = false;
	failures += test_expect_u32("hal install failure called once",
				    hal_install_count, 1);
	failures += test_expect_not_null("hal failure saw candidate data",
					 hal_last_current_data);
	failures += test_expect_bool("still uninitialized",
				     plane_smp_is_initialized(), false);
	failures += test_expect_u32("uninitialized cpu count",
				    plane_cpu_count(), 1);
	failures += test_expect_ptr("uninitialized current cpu data",
				    plane_cpu_current_data(), NULL);
	failures += test_expect_ptr("uninitialized cpu data get",
				    plane_cpu_data_get(0), NULL);
	failures += test_expect_bool("uninitialized prepare rejected",
				     plane_smp_prepare_ap_stack(
					     1, plane_vaddr_make(0x1000), 1),
				     false);
	return failures;
}

static int test_runtime_accepts_multi_cpu_bsp_topology(void)
{
	int failures = 0;
	struct plane_smp_info info;

	failures += test_expect_bool("init bsp",
				     plane_smp_info_init_bsp(&info, 7), true);
	failures += test_expect_bool("record ap 8",
				     plane_smp_info_record_cpu(&info, 8, false),
				     true);
	failures += test_expect_bool("record ap 11",
				     plane_smp_info_record_cpu(&info, 11, false),
				     true);
	failures += test_expect_bool("record ap 13",
				     plane_smp_info_record_cpu(&info, 13, false),
				     true);

	failures += test_expect_bool("init succeeds",
				     plane_smp_init_bsp(&info), true);
	failures += test_expect_u32("successful init installs cpu data",
				    hal_install_count, 2);
	failures += test_expect_bool("initialized",
				     plane_smp_is_initialized(), true);
	failures += test_expect_u32("runtime cpu count",
				    plane_cpu_count(), 4);
	failures += test_expect_u32("current cpu id",
				    plane_cpu_current_id(), 0);
	failures += test_expect_bool("current is bsp",
				     plane_cpu_is_bsp(), true);

	const struct plane_cpu_data *current = plane_cpu_current_data();
	const struct plane_cpu_data *bsp = plane_cpu_data_get(0);
	const struct plane_cpu_data *ap = plane_cpu_data_get(1);

	failures += test_expect_ptr("current is bsp data", current, bsp);
	failures += test_expect_ptr("hal saw current data",
				    hal_last_current_data, (void *)current);
	failures += test_expect_not_null("bsp info", bsp);
	failures += test_expect_not_null("ap info", ap);
	if (bsp != NULL) {
		failures += test_expect_ptr("bsp self", bsp->self, bsp);
		failures += test_expect_u32("bsp lapic", bsp->lapic_id, 7);
		failures += test_expect_u32("bsp logical id", bsp->logical_id, 0);
		failures += test_expect_bool("bsp marked", bsp->is_bsp, true);
		failures += test_expect_bool("bsp present", bsp->present, true);
		failures += test_expect_bool("bsp online", bsp->online, true);
	}
	if (ap != NULL) {
		failures += test_expect_ptr("ap self", ap->self, ap);
		failures += test_expect_u32("ap lapic", ap->lapic_id, 8);
		failures += test_expect_u32("ap logical id", ap->logical_id, 1);
		failures += test_expect_bool("ap not bsp", ap->is_bsp, false);
		failures += test_expect_bool("ap present", ap->present, true);
		failures += test_expect_bool("ap offline", ap->online, false);
	}
	failures += test_expect_ptr("out of range cpu data get",
				    plane_cpu_data_get(4), NULL);
	return failures;
}

static int test_ap_stack_prepare_and_state_transitions(void)
{
	int failures = 0;
	struct plane_cpu_data *ap1 = plane_cpu_boot_data_get(1);
	struct plane_cpu_data *ap2 = plane_cpu_boot_data_get(2);

	failures += test_expect_bool("prepare rejects bsp",
				     plane_smp_prepare_ap_stack(
					     0, plane_vaddr_make(0x800000), 1),
				     false);
	failures += test_expect_bool("prepare rejects out of range",
				     plane_smp_prepare_ap_stack(
					     4, plane_vaddr_make(0x800000), 1),
				     false);
	failures += test_expect_bool("prepare rejects null stack",
				     plane_smp_prepare_ap_stack(
					     1, plane_vaddr_make(0), 1),
				     false);
	failures += test_expect_bool("prepare rejects unaligned stack",
				     plane_smp_prepare_ap_stack(
					     1, plane_vaddr_make(0x800123), 1),
				     false);
	failures += test_expect_bool("prepare rejects zero pages",
				     plane_smp_prepare_ap_stack(
					     1, plane_vaddr_make(0x800000), 0),
				     false);
	failures += test_expect_bool("park before starting rejected",
				     plane_smp_mark_ap_parked(ap1), false);

	failures += test_expect_bool("prepare ap1",
				     plane_smp_prepare_ap_stack(
					     1, plane_vaddr_make(0x800000), 2),
				     true);
	failures += test_expect_u32("prepare context once",
				    hal_prepare_context_count, 1);
	failures += test_expect_ptr("prepare context sees ap1",
				    hal_last_prepare_context_data, ap1);
	failures += test_expect_bool("prepare ap1 again rejected",
				     plane_smp_prepare_ap_stack(
					     1, plane_vaddr_make(0x900000), 1),
				     false);
	failures += test_expect_u32("ap1 state prepared",
				    plane_cpu_boot_state(1),
				    PLANE_CPU_BOOT_PREPARED);
	if (ap1 != NULL) {
		failures += test_expect_u64("ap1 stack base",
					    plane_vaddr_raw(ap1->ap_stack_base),
					    0x800000);
		failures += test_expect_u64("ap1 stack top",
					    plane_vaddr_raw(ap1->ap_stack_top),
					    0x802000);
		failures += test_expect_u64("ap1 stack pages",
					    ap1->ap_stack_pages, 2);
	}

	failures += test_expect_bool("start ap1",
				     plane_smp_mark_ap_starting(1), true);
	failures += test_expect_bool("start ap1 again rejected",
				     plane_smp_mark_ap_starting(1), false);
	failures += test_expect_u32("ap1 state starting",
				    plane_cpu_boot_state(1),
				    PLANE_CPU_BOOT_STARTING);
	call_ap_park_entry(ap1);
	failures += test_expect_u32("park entry installs context",
				    hal_install_context_count, 1);
	failures += test_expect_ptr("install context sees ap1",
				    hal_last_install_context_data, ap1);
	failures += test_expect_u32("park entry installs current data",
				    hal_install_count, 3);
	failures += test_expect_ptr("current data sees ap1",
				    hal_last_current_data, ap1);
	failures += test_expect_u32("ap1 state parked",
				    plane_cpu_boot_state(1),
				    PLANE_CPU_BOOT_PARKED);
	failures += test_expect_u32("one parked ap",
				    plane_cpu_parked_count(), 1);
	failures += test_expect_bool("fail parked ap rejected",
				     plane_smp_mark_ap_failed(ap1), false);

	failures += test_expect_bool("prepare ap2",
				     plane_smp_prepare_ap_stack(
					     2, plane_vaddr_make(0xa00000), 1),
				     true);
	failures += test_expect_bool("start ap2",
				     plane_smp_mark_ap_starting(2), true);
	hal_install_context_should_fail = true;
	call_ap_park_entry(ap2);
	hal_install_context_should_fail = false;
	failures += test_expect_u32("ap2 state failed",
				    plane_cpu_boot_state(2),
				    PLANE_CPU_BOOT_FAILED);
	failures += test_expect_u32("parked count unchanged",
				    plane_cpu_parked_count(), 1);
	return failures;
}

static int test_ap_stack_prepare_rejects_after_context_failure_path(void)
{
	int failures = 0;
	struct plane_cpu_data *ap3 = plane_cpu_boot_data_get(3);

	hal_prepare_context_should_fail = true;
	failures += test_expect_bool("prepare ap3 context failure rejected",
				     plane_smp_prepare_ap_stack(
					     3, plane_vaddr_make(0xb00000), 1),
				     false);
	hal_prepare_context_should_fail = false;
	failures += test_expect_u32("ap3 still offline",
				    plane_cpu_boot_state(3),
				    PLANE_CPU_BOOT_OFFLINE);
	if (ap3 != NULL) {
		failures += test_expect_u64("ap3 stack base cleared",
					    plane_vaddr_raw(ap3->ap_stack_base),
					    0);
		failures += test_expect_u64("ap3 stack top cleared",
					    plane_vaddr_raw(ap3->ap_stack_top),
					    0);
		failures += test_expect_u64("ap3 stack pages cleared",
					    ap3->ap_stack_pages, 0);
	}
	return failures;
}

static int test_runtime_rejects_reinit_without_state_change(void)
{
	int failures = 0;
	struct plane_smp_info info;

	failures += test_expect_bool("init replacement candidate",
				     plane_smp_info_init_bsp(&info, 99), true);
	failures += test_expect_bool("reinit rejected",
				     plane_smp_init_bsp(&info), false);
	failures += test_expect_u32("reinit does not reinstall cpu data",
				    hal_install_count, 3);
	failures += test_expect_u32("old cpu count kept",
				    plane_cpu_count(), 4);

	const struct plane_cpu_data *bsp = plane_cpu_data_get(0);

	failures += test_expect_not_null("old bsp still present", bsp);
	if (bsp != NULL) {
		failures += test_expect_u32("old bsp lapic kept",
					    bsp->lapic_id, 7);
	}
	return failures;
}

static int test_builder_rejects_duplicates_and_null(void)
{
	int failures = 0;
	struct plane_smp_info info;

	failures += test_expect_bool("null init rejected",
				     plane_smp_info_init(NULL), false);
	failures += test_expect_bool("null bsp init rejected",
				     plane_smp_info_init_bsp(NULL, 0), false);
	failures += test_expect_bool("null record rejected",
				     plane_smp_info_record_cpu(NULL, 0, false),
				     false);
	failures += test_expect_bool("init bsp",
				     plane_smp_info_init_bsp(&info, 3), true);
	failures += test_expect_bool("duplicate lapic rejected",
				     plane_smp_info_record_cpu(&info, 3, false),
				     false);
	failures += test_expect_bool("second bsp rejected",
				     plane_smp_info_record_cpu(&info, 4, true),
				     false);
	failures += test_expect_u32("only bsp stored", info.cpu_count, 1);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_builder_bsp_only),
		TEST_CASE(test_builder_truncates_extra_cpus),
		TEST_CASE(test_runtime_rejects_invalid_before_init),
		TEST_CASE(test_runtime_accepts_multi_cpu_bsp_topology),
		TEST_CASE(test_ap_stack_prepare_and_state_transitions),
		TEST_CASE(test_ap_stack_prepare_rejects_after_context_failure_path),
		TEST_CASE(test_runtime_rejects_reinit_without_state_change),
		TEST_CASE(test_builder_rejects_duplicates_and_null),
	};

	return test_run_cases("smp_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
