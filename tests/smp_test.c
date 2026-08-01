#include <stdint.h>

#include <plane/smp.h>

#include "support/test.h"

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
	failures += test_expect_bool("still uninitialized",
				     plane_smp_is_initialized(), false);
	failures += test_expect_u32("uninitialized cpu count",
				    plane_cpu_count(), 1);
	failures += test_expect_ptr("uninitialized current cpu data",
				    plane_cpu_current_data(), NULL);
	failures += test_expect_ptr("uninitialized cpu data get",
				    plane_cpu_data_get(0), NULL);
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

	failures += test_expect_bool("init succeeds",
				     plane_smp_init_bsp(&info), true);
	failures += test_expect_bool("initialized",
				     plane_smp_is_initialized(), true);
	failures += test_expect_u32("runtime cpu count",
				    plane_cpu_count(), 3);
	failures += test_expect_u32("current cpu id",
				    plane_cpu_current_id(), 0);
	failures += test_expect_bool("current is bsp",
				     plane_cpu_is_bsp(), true);

	const struct plane_cpu_data *current = plane_cpu_current_data();
	const struct plane_cpu_data *bsp = plane_cpu_data_get(0);
	const struct plane_cpu_data *ap = plane_cpu_data_get(1);

	failures += test_expect_ptr("current is bsp data", current, bsp);
	failures += test_expect_not_null("bsp info", bsp);
	failures += test_expect_not_null("ap info", ap);
	if (bsp != NULL) {
		failures += test_expect_u32("bsp lapic", bsp->lapic_id, 7);
		failures += test_expect_u32("bsp logical id", bsp->logical_id, 0);
		failures += test_expect_bool("bsp marked", bsp->is_bsp, true);
		failures += test_expect_bool("bsp present", bsp->present, true);
		failures += test_expect_bool("bsp online", bsp->online, true);
	}
	if (ap != NULL) {
		failures += test_expect_u32("ap lapic", ap->lapic_id, 8);
		failures += test_expect_u32("ap logical id", ap->logical_id, 1);
		failures += test_expect_bool("ap not bsp", ap->is_bsp, false);
		failures += test_expect_bool("ap present", ap->present, true);
		failures += test_expect_bool("ap offline", ap->online, false);
	}
	failures += test_expect_ptr("out of range cpu data get",
				    plane_cpu_data_get(3), NULL);
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
	failures += test_expect_u32("old cpu count kept",
				    plane_cpu_count(), 3);

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
		TEST_CASE(test_runtime_rejects_reinit_without_state_change),
		TEST_CASE(test_builder_rejects_duplicates_and_null),
	};

	return test_run_cases("smp_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
