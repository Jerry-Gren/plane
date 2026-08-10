#include <stdint.h>
#include <setjmp.h>

#include <machine/machine_routines.h>
#include <machine/pmap.h>
#include <plane/smp.h>

#include "../kernel/smp_internal.h"
#include "support/test.h"

static bool machine_current_data_should_fail;
static bool machine_prepare_context_should_fail;
static bool machine_install_context_should_fail;
static bool cpu_interrupts_should_fail;
static bool cpu_signal_should_fail;
static uint32_t machine_current_data_count;
static uint32_t machine_prepare_context_count;
static uint32_t machine_install_context_count;
static uint32_t cpu_interrupts_count;
static uint32_t cpu_signal_count;
static uint32_t cpu_interrupt_last_signal_logical_id;
static uint32_t machine_halt_count;
static uint32_t pmap_update_interrupt_count;
static struct plane_cpu_data *machine_last_current_data;
static struct plane_cpu_data *machine_last_prepare_context_data;
static struct plane_cpu_data *machine_last_install_context_data;
static struct plane_cpu_data *cpu_interrupts_last_data;
static jmp_buf machine_halt_env;
static bool machine_halt_trap_enabled;

bool ml_cpu_set_current_data(struct plane_cpu_data *data)
{
	machine_current_data_count++;
	machine_last_current_data = data;
	return !machine_current_data_should_fail;
}

bool ml_cpu_prepare_ap_startup_context(struct plane_cpu_data *data)
{
	machine_prepare_context_count++;
	machine_last_prepare_context_data = data;
	return !machine_prepare_context_should_fail;
}

bool ml_cpu_install_ap_startup_context(struct plane_cpu_data *data)
{
	machine_install_context_count++;
	machine_last_install_context_data = data;
	return !machine_install_context_should_fail;
}

bool ml_cpu_interrupt_init_bsp(const struct plane_smp_info *info)
{
	return info != NULL;
}

bool ml_cpu_interrupt_init_ap(struct plane_cpu_data *data)
{
	cpu_interrupts_count++;
	cpu_interrupts_last_data = data;
	return !cpu_interrupts_should_fail;
}

bool ml_cpu_signal(uint32_t logical_id)
{
	cpu_signal_count++;
	cpu_interrupt_last_signal_logical_id = logical_id;
	return !cpu_signal_should_fail;
}

void pmap_update_interrupt(void)
{
	pmap_update_interrupt_count++;
}

void ml_interrupts_disable(void)
{
}

void ml_cpu_halt(void)
{
	machine_halt_count++;
	if (machine_halt_trap_enabled) {
		longjmp(machine_halt_env, 1);
	}

	for (;;) {
	}
}

void ml_cpu_enter_on_stack(plane_vaddr_t stack_top,
			    void (*entry)(struct plane_cpu_data *data),
			    struct plane_cpu_data *data)
{
	(void)stack_top;
	entry(data);
	ml_cpu_halt();
}

static void call_ap_park_entry(struct plane_cpu_data *data)
{
	machine_halt_trap_enabled = true;
	if (setjmp(machine_halt_env) == 0) {
		plane_smp_ap_park_entry(data);
	}
	machine_halt_trap_enabled = false;
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
	failures += test_expect_u32("bsp physical id", info.cpus[0].physical_id, 9);
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
				    machine_current_data_count, 0);
	failures += test_expect_bool("duplicate init bsp",
				     plane_smp_info_init_bsp(&duplicate, 1),
				     true);
	failures += test_expect_bool("duplicate record ap",
				     plane_smp_info_record_cpu(&duplicate, 2,
							       false),
				     true);
	duplicate.cpus[1].physical_id = duplicate.cpus[0].physical_id;
	failures += test_expect_bool("duplicate physical id init rejected",
				     plane_smp_init_bsp(&duplicate), false);
	failures += test_expect_u32("duplicate init does not install cpu data",
				    machine_current_data_count, 0);

	struct plane_smp_info valid;

	failures += test_expect_bool("valid init bsp",
				     plane_smp_info_init_bsp(&valid, 5),
				     true);
	machine_current_data_should_fail = true;
	failures += test_expect_bool("machine install failure rejects init",
				     plane_smp_init_bsp(&valid), false);
	machine_current_data_should_fail = false;
	failures += test_expect_u32("machine install failure called once",
				    machine_current_data_count, 1);
	failures += test_expect_not_null("machine failure saw candidate data",
					 machine_last_current_data);
	failures += test_expect_bool("still uninitialized",
				     plane_smp_is_initialized(), false);
	failures += test_expect_u32("uninitialized cpu count",
				    plane_cpu_count(), 1);
	failures += test_expect_ptr("uninitialized current cpu data",
				    plane_cpu_current_data(), NULL);
	failures += test_expect_ptr("uninitialized cpu data get",
				    plane_cpu_get_data(0), NULL);
	failures += test_expect_bool("uninitialized signal rejected",
				     plane_smp_signal_handler(),
				     false);
	failures += test_expect_u64("uninitialized event count",
				    plane_smp_event_count(
					    PLANE_SMP_EVENT_TLB_FLUSH),
				    0);
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
	failures += test_expect_bool("record ap 15",
				     plane_smp_info_record_cpu(&info, 15, false),
				     true);

	failures += test_expect_bool("init succeeds",
				     plane_smp_init_bsp(&info), true);
	failures += test_expect_u32("successful init installs cpu data",
				    machine_current_data_count, 2);
	failures += test_expect_bool("initialized",
				     plane_smp_is_initialized(), true);
	failures += test_expect_u32("runtime cpu count",
				    plane_cpu_count(), 5);
	failures += test_expect_u32("current cpu id",
				    plane_cpu_current_id(), 0);
	failures += test_expect_bool("current is bsp",
				     plane_cpu_is_bsp(), true);
	failures += test_expect_bool("bsp is running",
				     plane_cpu_is_running(0), true);
	failures += test_expect_bool("present ap is not running",
				     plane_cpu_is_running(1), false);

	const struct plane_cpu_data *current = plane_cpu_current_data();
	const struct plane_cpu_data *bsp = plane_cpu_get_data(0);
	const struct plane_cpu_data *ap = plane_cpu_get_data(1);

	failures += test_expect_ptr("current is bsp data", current, bsp);
	failures += test_expect_ptr("machine saw current data",
				    machine_last_current_data, (void *)current);
	failures += test_expect_not_null("bsp info", bsp);
	failures += test_expect_not_null("ap info", ap);
	if (bsp != NULL) {
		failures += test_expect_ptr("bsp self", bsp->self, bsp);
		failures += test_expect_u32("bsp physical id", bsp->physical_id, 7);
		failures += test_expect_u32("bsp logical id", bsp->logical_id, 0);
		failures += test_expect_bool("bsp marked", bsp->is_bsp, true);
		failures += test_expect_bool("bsp present", bsp->present, true);
		failures += test_expect_bool("bsp online", bsp->online, true);
		failures += test_expect_u32("bsp tlb invalid init",
					    bsp->cpu_tlb_invalid, 0);
	}
	if (ap != NULL) {
		failures += test_expect_ptr("ap self", ap->self, ap);
		failures += test_expect_u32("ap physical id", ap->physical_id, 8);
		failures += test_expect_u32("ap logical id", ap->logical_id, 1);
		failures += test_expect_bool("ap not bsp", ap->is_bsp, false);
		failures += test_expect_bool("ap present", ap->present, true);
		failures += test_expect_bool("ap offline", ap->online, false);
		failures += test_expect_u32("ap tlb invalid init",
					    ap->cpu_tlb_invalid, 0);
	}
	failures += test_expect_ptr("out of range cpu data get",
				    plane_cpu_get_data(5), NULL);
	return failures;
}

static int test_cpu_tlb_invalid_primitives(void)
{
	int failures = 0;
	bool was_invalid = true;

	failures += test_expect_bool("bsp tlb starts valid",
				     plane_cpu_is_tlb_invalid(0), false);
	failures += test_expect_bool("mark bsp tlb invalid",
				     plane_cpu_mark_tlb_invalid(0,
								&was_invalid),
				     true);
	failures += test_expect_bool("first mark reports not invalid",
				     was_invalid, false);
	failures += test_expect_bool("bsp tlb is invalid",
				     plane_cpu_is_tlb_invalid(0), true);

	was_invalid = false;
	failures += test_expect_bool("mark bsp tlb invalid twice",
				     plane_cpu_mark_tlb_invalid(0,
								&was_invalid),
				     true);
	failures += test_expect_bool("second mark reports invalid",
				     was_invalid, true);
	failures += test_expect_bool("clear bsp tlb invalid",
				     plane_cpu_clear_tlb_invalid(0), true);
	failures += test_expect_bool("bsp tlb invalid cleared",
				     plane_cpu_is_tlb_invalid(0), false);
	failures += test_expect_bool("clear already valid tlb rejected",
				     plane_cpu_clear_tlb_invalid(0), false);

	failures += test_expect_bool("mark ap tlb invalid",
				     plane_cpu_mark_tlb_invalid(1, NULL),
				     true);
	failures += test_expect_bool("ap tlb invalid set",
				     plane_cpu_is_tlb_invalid(1), true);
	failures += test_expect_bool("clear ap tlb invalid",
				     plane_cpu_clear_tlb_invalid(1), true);

	failures += test_expect_bool("mark invalid cpu rejected",
				     plane_cpu_mark_tlb_invalid(PLANE_MAX_CPUS,
								NULL),
				     false);
	failures += test_expect_bool("clear invalid cpu rejected",
				     plane_cpu_clear_tlb_invalid(PLANE_MAX_CPUS),
				     false);
	failures += test_expect_bool("invalid cpu tlb is not invalid",
				     plane_cpu_is_tlb_invalid(PLANE_MAX_CPUS),
				     false);
	return failures;
}

static int test_signal_handler_counts_known_events(void)
{
	int failures = 0;
	struct plane_cpu_data *current = plane_cpu_get_startup_data(0);
	uint32_t pmap_updates = pmap_update_interrupt_count;

	if (current != NULL) {
		current->cpu_signals = (1u << PLANE_SMP_EVENT_AST) |
				       (1u << PLANE_SMP_EVENT_TLB_FLUSH);
	}
	failures += test_expect_bool("pending events handled",
				     plane_smp_signal_handler(), true);
	failures += test_expect_bool("empty signal rejected",
				     plane_smp_signal_handler(), false);
	if (current != NULL) {
		failures += test_expect_u32("signals consumed",
					    current->cpu_signals, 0);
	}
	failures += test_expect_u64("AST event count",
				    plane_smp_event_count(
					    PLANE_SMP_EVENT_AST),
				    1);
	failures += test_expect_u64("TLB flush event count",
				    plane_smp_event_count(
					    PLANE_SMP_EVENT_TLB_FLUSH),
				    1);
	failures += test_expect_u64("empty preserves AST event",
				    plane_smp_event_count(
					    PLANE_SMP_EVENT_AST),
				    1);
	failures += test_expect_u64("empty preserves TLB flush event",
				    plane_smp_event_count(
					    PLANE_SMP_EVENT_TLB_FLUSH),
				    1);
	failures += test_expect_u32("TLB flush calls pmap hook",
				    pmap_update_interrupt_count,
				    pmap_updates + 1);
	return failures;
}

static int test_signal_cpu_sets_pending_signal_and_sends_signal(void)
{
	int failures = 0;
	struct plane_cpu_data *bsp = plane_cpu_get_startup_data(0);
	uint64_t ast_count = plane_smp_event_count(PLANE_SMP_EVENT_AST);

	cpu_signal_should_fail = false;
	failures += test_expect_bool("signal cpu sends AST",
				     plane_smp_signal_cpu(
					     0, PLANE_SMP_EVENT_AST,
					     PLANE_SMP_SIGNAL_ASYNC),
				     true);
	failures += test_expect_u32("signal cpu sends once",
				    cpu_signal_count, 1);
	failures += test_expect_u32("signal cpu target",
				    cpu_interrupt_last_signal_logical_id, 0);
	if (bsp != NULL) {
		failures += test_expect_u32("signal cpu sets pending bit",
					    bsp->cpu_signals,
					    1u << PLANE_SMP_EVENT_AST);
	}

	failures += test_expect_bool("handler consumes pending AST",
				     plane_smp_signal_handler(),
				     true);
	failures += test_expect_u64("handler updates AST count",
				    plane_smp_event_count(PLANE_SMP_EVENT_AST),
				    ast_count + 1);
	if (bsp != NULL) {
		failures += test_expect_u32("handler clears pending bit",
					    bsp->cpu_signals, 0);
	}

	uint64_t next_ast_count = plane_smp_event_count(PLANE_SMP_EVENT_AST);
	uint64_t tlb_count = plane_smp_event_count(PLANE_SMP_EVENT_TLB_FLUSH);
	uint32_t pmap_updates = pmap_update_interrupt_count;

	failures += test_expect_bool("signal cpu sends TLB flush",
				     plane_smp_signal_cpu(
					     0, PLANE_SMP_EVENT_TLB_FLUSH,
					     PLANE_SMP_SIGNAL_ASYNC),
				     true);
	if (bsp != NULL) {
		failures += test_expect_u32("TLB flush pending bit set",
					    bsp->cpu_signals,
					    1u << PLANE_SMP_EVENT_TLB_FLUSH);
	}
	failures += test_expect_bool("handler drains pending signals",
				     plane_smp_signal_handler(),
				     true);
	failures += test_expect_u64("pending AST unchanged",
				    plane_smp_event_count(PLANE_SMP_EVENT_AST),
				    next_ast_count);
	failures += test_expect_u64("pending TLB flush handled",
				    plane_smp_event_count(
					    PLANE_SMP_EVENT_TLB_FLUSH),
				    tlb_count + 1);
	failures += test_expect_u32("pending TLB flush calls pmap hook",
				    pmap_update_interrupt_count,
				    pmap_updates + 1);
	if (bsp != NULL) {
		failures += test_expect_u32("pending signals drained",
					    bsp->cpu_signals, 0);
	}

	cpu_signal_should_fail = true;
	failures += test_expect_bool("send failure rejects signal",
				     plane_smp_signal_cpu(
					     0, PLANE_SMP_EVENT_TLB_FLUSH,
					     PLANE_SMP_SIGNAL_ASYNC),
				     false);
	cpu_signal_should_fail = false;
	if (bsp != NULL) {
		failures += test_expect_u32("send failure rolls back signal",
					    bsp->cpu_signals, 0);
	}
	uint32_t send_count = cpu_signal_count;

	failures += test_expect_bool("offline ap signal rejected",
				     plane_smp_signal_cpu(
					     1, PLANE_SMP_EVENT_AST,
					     PLANE_SMP_SIGNAL_ASYNC),
				     false);
	failures += test_expect_bool("bad event rejected",
				     plane_smp_signal_cpu(
					     0, PLANE_SMP_EVENT_COUNT,
					     PLANE_SMP_SIGNAL_ASYNC),
				     false);
	failures += test_expect_bool("bad cpu rejected",
				     plane_smp_signal_cpu(
					     PLANE_MAX_CPUS,
					     PLANE_SMP_EVENT_AST,
					     PLANE_SMP_SIGNAL_ASYNC),
				     false);
	failures += test_expect_bool("offline ap not running",
				     plane_cpu_is_running(1), false);
	failures += test_expect_bool("sync mode rejected",
				     plane_smp_signal_cpu(
					     0, PLANE_SMP_EVENT_AST,
					     PLANE_SMP_SIGNAL_SYNC),
				     false);
	failures += test_expect_bool("nosync mode rejected",
				     plane_smp_signal_cpu(
					     0, PLANE_SMP_EVENT_AST,
					     PLANE_SMP_SIGNAL_NOSYNC),
				     false);
	failures += test_expect_u32("invalid signal does not send",
				    cpu_signal_count, send_count);
	return failures;
}

static int test_ap_stack_prepare_and_state_transitions(void)
{
	int failures = 0;
	struct plane_cpu_data *ap1 = plane_cpu_get_startup_data(1);
	struct plane_cpu_data *ap2 = plane_cpu_get_startup_data(2);

	failures += test_expect_bool("prepare rejects bsp",
				     plane_smp_prepare_ap_stack(
					     0, plane_vaddr_make(0x800000), 1),
				     false);
	failures += test_expect_bool("prepare rejects out of range",
				     plane_smp_prepare_ap_stack(
					     5, plane_vaddr_make(0x800000), 1),
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
				    machine_prepare_context_count, 1);
	failures += test_expect_ptr("prepare context sees ap1",
				    machine_last_prepare_context_data, ap1);
	failures += test_expect_bool("prepare ap1 again rejected",
				     plane_smp_prepare_ap_stack(
					     1, plane_vaddr_make(0x900000), 1),
				     false);
	failures += test_expect_u32("ap1 state prepared",
				    plane_cpu_startup_state(1),
				    PLANE_CPU_STARTUP_PREPARED);
	failures += test_expect_bool("prepared ap is not running",
				     plane_cpu_is_running(1), false);
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
				    plane_cpu_startup_state(1),
				    PLANE_CPU_STARTUP_STARTING);
	failures += test_expect_bool("starting ap is not running",
				     plane_cpu_is_running(1), false);
	call_ap_park_entry(ap1);
	failures += test_expect_u32("park entry installs context",
				    machine_install_context_count, 1);
	failures += test_expect_ptr("install context sees ap1",
				    machine_last_install_context_data, ap1);
	failures += test_expect_u32("park entry installs current data",
				    machine_current_data_count, 3);
	failures += test_expect_ptr("current data sees ap1",
				    machine_last_current_data, ap1);
	failures += test_expect_u32("park entry initializes CPU interrupts",
				    cpu_interrupts_count, 1);
	failures += test_expect_ptr("CPU interrupts sees ap1",
				    cpu_interrupts_last_data, ap1);
	failures += test_expect_u32("ap1 state parked",
				    plane_cpu_startup_state(1),
				    PLANE_CPU_STARTUP_PARKED);
	failures += test_expect_bool("parked ap is not running",
				     plane_cpu_is_running(1), false);
	failures += test_expect_u32("one parked ap",
				    plane_cpu_parked_count(), 1);
	failures += test_expect_bool("fail parked ap rejected",
				     plane_smp_mark_ap_failed(ap1), false);
	uint32_t signal_count = cpu_signal_count;
	failures += test_expect_bool("parked ap signal rejected",
				     plane_smp_signal_cpu(
					     1, PLANE_SMP_EVENT_AST,
					     PLANE_SMP_SIGNAL_ASYNC),
				     false);
	failures += test_expect_u32("parked ap signal does not send",
				    cpu_signal_count, signal_count);

	failures += test_expect_bool("prepare ap2",
				     plane_smp_prepare_ap_stack(
					     2, plane_vaddr_make(0xa00000), 1),
				     true);
	failures += test_expect_bool("start ap2",
				     plane_smp_mark_ap_starting(2), true);
	machine_install_context_should_fail = true;
	call_ap_park_entry(ap2);
	machine_install_context_should_fail = false;
	failures += test_expect_u32("ap2 state failed",
				    plane_cpu_startup_state(2),
				    PLANE_CPU_STARTUP_FAILED);
	failures += test_expect_u32("parked count unchanged",
				    plane_cpu_parked_count(), 1);

	struct plane_cpu_data *ap3 = plane_cpu_get_startup_data(3);

	failures += test_expect_bool("prepare ap3",
				     plane_smp_prepare_ap_stack(
					     3, plane_vaddr_make(0xb00000), 1),
				     true);
	failures += test_expect_bool("start ap3",
				     plane_smp_mark_ap_starting(3), true);
	cpu_interrupts_should_fail = true;
	call_ap_park_entry(ap3);
	cpu_interrupts_should_fail = false;
	failures += test_expect_u32("ap3 state failed after CPU interrupts",
				    plane_cpu_startup_state(3),
				    PLANE_CPU_STARTUP_FAILED);
	failures += test_expect_u32("CPU interrupts called for ap3",
				    cpu_interrupts_count, 2);
	failures += test_expect_ptr("CPU interrupts sees ap3",
				    cpu_interrupts_last_data, ap3);
	failures += test_expect_u32("parked count still unchanged",
				    plane_cpu_parked_count(), 1);
	return failures;
}

static int test_ap_stack_prepare_rejects_after_context_failure_path(void)
{
	int failures = 0;
	struct plane_cpu_data *ap4 = plane_cpu_get_startup_data(4);

	machine_prepare_context_should_fail = true;
	failures += test_expect_bool("prepare ap4 context failure rejected",
				     plane_smp_prepare_ap_stack(
					     4, plane_vaddr_make(0xc00000), 1),
				     false);
	machine_prepare_context_should_fail = false;
	failures += test_expect_u32("ap4 still offline",
				    plane_cpu_startup_state(4),
				    PLANE_CPU_STARTUP_OFFLINE);
	if (ap4 != NULL) {
		failures += test_expect_u64("ap4 stack base cleared",
					    plane_vaddr_raw(ap4->ap_stack_base),
					    0);
		failures += test_expect_u64("ap4 stack top cleared",
					    plane_vaddr_raw(ap4->ap_stack_top),
					    0);
		failures += test_expect_u64("ap4 stack pages cleared",
					    ap4->ap_stack_pages, 0);
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
				    machine_current_data_count, 4);
	failures += test_expect_u32("old cpu count kept",
				    plane_cpu_count(), 5);

	const struct plane_cpu_data *bsp = plane_cpu_get_data(0);

	failures += test_expect_not_null("old bsp still present", bsp);
	if (bsp != NULL) {
		failures += test_expect_u32("old bsp physical id kept",
					    bsp->physical_id, 7);
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
	failures += test_expect_bool("duplicate physical id rejected",
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
		TEST_CASE(test_cpu_tlb_invalid_primitives),
		TEST_CASE(test_signal_handler_counts_known_events),
		TEST_CASE(test_signal_cpu_sets_pending_signal_and_sends_signal),
		TEST_CASE(test_ap_stack_prepare_and_state_transitions),
		TEST_CASE(test_ap_stack_prepare_rejects_after_context_failure_path),
		TEST_CASE(test_runtime_rejects_reinit_without_state_change),
		TEST_CASE(test_builder_rejects_duplicates_and_null),
	};

	return test_run_cases("smp_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
