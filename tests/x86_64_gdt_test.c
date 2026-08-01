#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <plane/smp.h>

#include "support/test.h"

static uint64_t last_gdtr;
static uint32_t gdt_flush_count;
static uint32_t tss_flush_count;
static uint32_t idt_load_count;
static enum plane_cpu_boot_state test_boot_states[PLANE_MAX_CPUS];

#include "../hal/x86_64/gdt.c"

void x86_64_gdt_flush(uint64_t gdtr_addr)
{
	gdt_flush_count++;
	last_gdtr = gdtr_addr;
}

void x86_64_tss_flush(void)
{
	tss_flush_count++;
}

void x86_64_idt_load_current(void)
{
	idt_load_count++;
}

enum plane_cpu_boot_state plane_cpu_boot_state(uint32_t logical_id)
{
	if (logical_id >= PLANE_MAX_CPUS) {
		return PLANE_CPU_BOOT_FAILED;
	}

	return test_boot_states[logical_id];
}

static void reset_gdt_test(void)
{
	memset(cpu_desc_contexts, 0, sizeof(cpu_desc_contexts));
	memset(test_boot_states, 0, sizeof(test_boot_states));
	last_gdtr = 0;
	gdt_flush_count = 0;
	tss_flush_count = 0;
	idt_load_count = 0;
}

static uint64_t tss_descriptor_base(const struct x86_64_cpu_desc_context *ctx)
{
	const struct tss_descriptor *desc =
		(const struct tss_descriptor *)&ctx->gdt[5];

	return (uint64_t)desc->base_low |
	       ((uint64_t)desc->base_middle << 16) |
	       ((uint64_t)desc->base_high << 24) |
	       ((uint64_t)desc->base_upper32 << 32);
}

static int test_bsp_gdt_init_builds_and_loads_cpu0_context(void)
{
	int failures = 0;
	struct x86_64_cpu_desc_context *ctx = &cpu_desc_contexts[0];

	reset_gdt_test();
	x86_64_gdt_init();

	failures += test_expect_bool("cpu0 prepared", ctx->prepared, true);
	failures += test_expect_u64("gdtr base",
				    ctx->gdtr.base, (uint64_t)&ctx->gdt);
	failures += test_expect_u64("gdtr limit",
				    ctx->gdtr.limit, sizeof(ctx->gdt) - 1);
	failures += test_expect_u64("bsp rsp0", ctx->tss.rsp0, 0);
	failures += test_expect_u32("bsp iopb",
				    ctx->tss.iopb_offset, sizeof(ctx->tss));
	failures += test_expect_u64("tss descriptor base",
				    tss_descriptor_base(ctx),
				    (uint64_t)(uintptr_t)&ctx->tss);
	failures += test_expect_u32("gdt loaded once", gdt_flush_count, 1);
	failures += test_expect_u64("loaded gdtr", last_gdtr,
				    (uint64_t)(uintptr_t)&ctx->gdtr);
	failures += test_expect_u32("tss loaded once", tss_flush_count, 1);
	return failures;
}

static int test_prepare_ap_context_builds_per_cpu_tss(void)
{
	int failures = 0;
	struct plane_cpu_data data = {
		.self = &data,
		.logical_id = 3,
		.physical_id = 17,
		.is_bsp = false,
		.present = true,
		.ap_stack_top = plane_vaddr_make(0xffff800000804000ull),
	};
	struct x86_64_cpu_desc_context *ctx = &cpu_desc_contexts[3];

	reset_gdt_test();
	failures += test_expect_bool("prepare ap context",
				     hal_cpu_prepare_ap_startup_context(&data),
				     true);
	failures += test_expect_bool("ap ctx prepared", ctx->prepared, true);
	failures += test_expect_u64("ap rsp0",
				    ctx->tss.rsp0,
				    plane_vaddr_raw(data.ap_stack_top));
	failures += test_expect_u32("ap iopb",
				    ctx->tss.iopb_offset, sizeof(ctx->tss));
	failures += test_expect_u64("ap gdtr base",
				    ctx->gdtr.base, (uint64_t)&ctx->gdt);
	failures += test_expect_u64("ap tss descriptor base",
				    tss_descriptor_base(ctx),
				    (uint64_t)(uintptr_t)&ctx->tss);
	failures += test_expect_u32("prepare does not load gdt",
				    gdt_flush_count, 0);
	failures += test_expect_u32("prepare does not load tss",
				    tss_flush_count, 0);
	return failures;
}

static int test_install_ap_context_validates_state_and_loads_context(void)
{
	int failures = 0;
	struct plane_cpu_data data = {
		.self = &data,
		.logical_id = 2,
		.physical_id = 9,
		.is_bsp = false,
		.present = true,
		.ap_stack_top = plane_vaddr_make(0xffff800000904000ull),
	};
	struct x86_64_cpu_desc_context *ctx = &cpu_desc_contexts[2];

	reset_gdt_test();
	failures += test_expect_bool("install before prepare rejected",
				     hal_cpu_install_ap_startup_context(&data),
				     false);
	failures += test_expect_bool("prepare ap",
				     hal_cpu_prepare_ap_startup_context(&data),
				     true);
	failures += test_expect_bool("install before starting rejected",
				     hal_cpu_install_ap_startup_context(&data),
				     false);
	test_boot_states[2] = PLANE_CPU_BOOT_STARTING;
	failures += test_expect_bool("install starting ap",
				     hal_cpu_install_ap_startup_context(&data),
				     true);
	failures += test_expect_u32("gdt loaded", gdt_flush_count, 1);
	failures += test_expect_u64("loaded ap gdtr", last_gdtr,
				    (uint64_t)(uintptr_t)&ctx->gdtr);
	failures += test_expect_u32("tss loaded", tss_flush_count, 1);
	failures += test_expect_u32("idt reloaded", idt_load_count, 1);
	return failures;
}

static int test_rejects_invalid_ap_context_inputs(void)
{
	int failures = 0;
	struct plane_cpu_data bsp = {
		.self = &bsp,
		.logical_id = 0,
		.is_bsp = true,
		.present = true,
		.ap_stack_top = plane_vaddr_make(0x1000),
	};
	struct plane_cpu_data absent = {
		.self = &absent,
		.logical_id = 1,
		.is_bsp = false,
		.present = false,
		.ap_stack_top = plane_vaddr_make(0x2000),
	};
	struct plane_cpu_data no_stack = {
		.self = &no_stack,
		.logical_id = 2,
		.is_bsp = false,
		.present = true,
	};
	struct plane_cpu_data bad_self = {
		.logical_id = 3,
		.is_bsp = false,
		.present = true,
		.ap_stack_top = plane_vaddr_make(0x3000),
	};
	struct plane_cpu_data out_of_range = {
		.self = &out_of_range,
		.logical_id = PLANE_MAX_CPUS,
		.is_bsp = false,
		.present = true,
		.ap_stack_top = plane_vaddr_make(0x4000),
	};

	reset_gdt_test();
	failures += test_expect_bool("prepare null rejected",
				     hal_cpu_prepare_ap_startup_context(NULL),
				     false);
	failures += test_expect_bool("prepare bsp rejected",
				     hal_cpu_prepare_ap_startup_context(&bsp),
				     false);
	failures += test_expect_bool("prepare absent rejected",
				     hal_cpu_prepare_ap_startup_context(&absent),
				     false);
	failures += test_expect_bool("prepare no stack rejected",
				     hal_cpu_prepare_ap_startup_context(&no_stack),
				     false);
	failures += test_expect_bool("prepare bad self rejected",
				     hal_cpu_prepare_ap_startup_context(&bad_self),
				     false);
	failures += test_expect_bool("prepare out of range rejected",
				     hal_cpu_prepare_ap_startup_context(&out_of_range),
				     false);
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_bsp_gdt_init_builds_and_loads_cpu0_context),
		TEST_CASE(test_prepare_ap_context_builds_per_cpu_tss),
		TEST_CASE(test_install_ap_context_validates_state_and_loads_context),
		TEST_CASE(test_rejects_invalid_ap_context_inputs),
	};

	return test_run_cases("x86_64_gdt_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
