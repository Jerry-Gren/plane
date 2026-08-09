#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <machine/machine_routines.h>
#include <machine/local_interrupt.h>
#include <machine/pmap.h>
#include <x86_64/cpu_features.h>
#include <plane/io_map.h>
#include <plane/smp.h>

#include "support/test.h"

#define TEST_APIC_PHYS 0xfee00000ull

static bool test_has_apic;
static bool test_has_msr;
static bool test_msr_write_should_fail;
static bool test_io_map_should_fail;
static bool test_irq_enabled = true;
static uint64_t test_apic_base_msr;
static uint32_t test_regs[ARCH_PAGE_SIZE / sizeof(uint32_t)];
static uint32_t test_io_map_count;
static plane_paddr_t test_last_io_map_phys;
static uint64_t test_last_io_map_size;
static enum plane_io_map_cache test_last_io_map_cache;
static uint32_t test_last_io_map_prot;
static uint32_t test_io_unmap_count;
static plane_vaddr_t test_last_io_unmap_vaddr;
static uint64_t test_last_io_unmap_size;
static uint32_t test_msr_write_count;
static uint32_t test_msr_write_msr;
static uint64_t test_msr_write_value;
static uint32_t test_irq_save_count;
static uint32_t test_irq_restore_count;
static uint32_t test_relax_count;
static bool test_smp_signal_handler_result;
static uint32_t test_smp_signal_handler_count;
static uint8_t test_smp_last_signal_vector;

#include <x86_64/lapic.c>

static uint32_t reg_index(enum x86_64_lapic_reg reg)
{
	return x86_64_lapic_mmio_offset(reg) / sizeof(uint32_t);
}

bool x86_64_cpu_has_feature(enum x86_64_cpu_feature feature)
{
	if (feature == X86_64_CPU_FEATURE_APIC) {
		return test_has_apic;
	}
	if (feature == X86_64_CPU_FEATURE_MSR) {
		return test_has_msr;
	}

	return false;
}

uint64_t x86_64_msr_read(uint32_t msr)
{
	if (msr == X86_64_MSR_IA32_APIC_BASE) {
		return test_apic_base_msr;
	}

	return 0;
}

bool x86_64_msr_write(uint32_t msr, uint64_t value)
{
	test_msr_write_count++;
	test_msr_write_msr = msr;
	test_msr_write_value = value;
	return !test_msr_write_should_fail;
}

bool plane_io_map(plane_paddr_t phys_addr,
		  uint64_t size,
		  enum plane_io_map_cache cache,
		  uint32_t prot,
		  plane_vaddr_t *vaddr)
{
	test_io_map_count++;
	test_last_io_map_phys = phys_addr;
	test_last_io_map_size = size;
	test_last_io_map_cache = cache;
	test_last_io_map_prot = prot;
	if (test_io_map_should_fail ||
	    plane_paddr_raw(phys_addr) != TEST_APIC_PHYS ||
	    size != ARCH_PAGE_SIZE ||
	    vaddr == NULL) {
		return false;
	}

	*vaddr = plane_vaddr_make((uint64_t)(uintptr_t)test_regs);
	return true;
}

bool plane_io_unmap(plane_vaddr_t vaddr, uint64_t size)
{
	test_io_unmap_count++;
	test_last_io_unmap_vaddr = vaddr;
	test_last_io_unmap_size = size;
	return !plane_vaddr_is_null(vaddr) && size != 0;
}

plane_irq_state_t ml_irq_save(void)
{
	plane_irq_state_t state = { .enabled = test_irq_enabled };

	test_irq_save_count++;
	test_irq_enabled = false;
	return state;
}

void ml_irq_restore(plane_irq_state_t state)
{
	test_irq_restore_count++;
	test_irq_enabled = state.enabled;
}

void cpu_pause(void)
{
	test_relax_count++;
	test_regs[reg_index(X86_64_LAPIC_REG_ICR_LOW)] &=
		~X86_64_LAPIC_ICR_PENDING;
}

bool plane_smp_signal_handler(uint8_t vector)
{
	test_smp_signal_handler_count++;
	test_smp_last_signal_vector = vector;
	return test_smp_signal_handler_result;
}

static void reset_lapic_test(void)
{
	memset(test_regs, 0, sizeof(test_regs));
	memset(lapic_id_by_logical_id, 0, sizeof(lapic_id_by_logical_id));
	test_regs[reg_index(X86_64_LAPIC_REG_VERSION)] =
		X86_64_LAPIC_MIN_VERSION;
	test_has_apic = true;
	test_has_msr = true;
	test_msr_write_should_fail = false;
	test_io_map_should_fail = false;
	test_irq_enabled = true;
	test_apic_base_msr = TEST_APIC_PHYS;
	test_io_map_count = 0;
	test_last_io_map_phys = plane_paddr_make(0);
	test_last_io_map_size = 0;
	test_last_io_map_cache = (enum plane_io_map_cache)0;
	test_last_io_map_prot = 0;
	test_io_unmap_count = 0;
	test_last_io_unmap_vaddr = plane_vaddr_make(0);
	test_last_io_unmap_size = 0;
	test_msr_write_count = 0;
	test_msr_write_msr = 0;
	test_msr_write_value = 0;
	test_irq_save_count = 0;
	test_irq_restore_count = 0;
	test_relax_count = 0;
	test_smp_signal_handler_result = true;
	test_smp_signal_handler_count = 0;
	test_smp_last_signal_vector = 0;
	lapic_mmio_base = plane_vaddr_make(0);
	lapic_cpu_count = 0;
	lapic_initialized = false;
	lapic_write_flush_value = 0;
}

static bool build_topology(struct plane_smp_info *info)
{
	if (info == NULL) {
		return false;
	}

	*info = (struct plane_smp_info){
		.cpu_count = 3,
		.discovered_cpu_count = 3,
		.bsp_logical_id = 0,
		.cpus = {
			{
				.logical_id = 0,
				.physical_id = 1,
				.is_bsp = true,
				.present = true,
			},
			{
				.logical_id = 1,
				.physical_id = 2,
				.is_bsp = false,
				.present = true,
			},
			{
				.logical_id = 2,
				.physical_id = 3,
				.is_bsp = false,
				.present = true,
			},
		},
	};
	return true;
}

static int test_bsp_init_rejects_invalid_inputs(void)
{
	int failures = 0;
	struct plane_smp_info info;

	reset_lapic_test();
	failures += test_expect_bool("null topology rejected",
				     ml_local_interrupt_init_bsp(NULL),
				     false);
	failures += test_expect_bool("null topology leaves uninitialized",
				     lapic_initialized, false);

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	test_has_apic = false;
	failures += test_expect_bool("missing apic rejected",
				     ml_local_interrupt_init_bsp(&info),
				     false);

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	test_has_msr = false;
	failures += test_expect_bool("missing msr rejected",
				     ml_local_interrupt_init_bsp(&info),
				     false);

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	test_io_map_should_fail = true;
	failures += test_expect_bool("io map failure rejected",
				     ml_local_interrupt_init_bsp(&info),
				     false);

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	test_apic_base_msr =
		TEST_APIC_PHYS | X86_64_MSR_IA32_APIC_BASE_X2APIC;
	failures += test_expect_bool("x2apic rejected",
				     ml_local_interrupt_init_bsp(&info),
				     false);

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	info.cpus[2].physical_id = UINT8_MAX + 1u;
	failures += test_expect_bool("large xapic id rejected",
				     ml_local_interrupt_init_bsp(&info),
				     false);
	failures += test_expect_u32("large id does not write msr",
				    test_msr_write_count, 0);

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	test_msr_write_should_fail = true;
	failures += test_expect_bool("apic enable write failure rejected",
				     ml_local_interrupt_init_bsp(&info),
				     false);
	failures += test_expect_bool("write failure leaves uninitialized",
				     lapic_initialized, false);
	failures += test_expect_u32("write failure unmaps mmio",
				    test_io_unmap_count, 1);

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	test_io_map_should_fail = true;
	failures += test_expect_bool("mmio map failure rejected",
				     ml_local_interrupt_init_bsp(&info),
				     false);
	failures += test_expect_bool("map failure leaves uninitialized",
				     lapic_initialized, false);

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	test_regs[reg_index(X86_64_LAPIC_REG_VERSION)] =
		X86_64_LAPIC_MIN_VERSION - 1;
	failures += test_expect_bool("unsupported lapic version rejected",
				     ml_local_interrupt_init_bsp(&info),
				     false);
	failures += test_expect_bool("unsupported version leaves uninitialized",
				     lapic_initialized, false);
	failures += test_expect_u32("unsupported version does not write msr",
				    test_msr_write_count, 0);
	failures += test_expect_u32("unsupported version unmaps mmio",
				    test_io_unmap_count, 1);
	failures += test_expect_u64("unsupported version unmap vaddr",
				    plane_vaddr_raw(test_last_io_unmap_vaddr),
				    (uint64_t)(uintptr_t)test_regs);
	failures += test_expect_u64("unsupported version unmap size",
				    test_last_io_unmap_size, ARCH_PAGE_SIZE);
	return failures;
}

static int test_bsp_init_configures_xapic_and_cpu_map(void)
{
	int failures = 0;
	struct plane_smp_info info;

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	test_apic_base_msr = TEST_APIC_PHYS;
	failures += test_expect_bool("bsp lapic init",
				     ml_local_interrupt_init_bsp(&info),
				     true);
	failures += test_expect_bool("lapic initialized",
				     lapic_initialized, true);
	failures += test_expect_u32("io map requested",
				    test_io_map_count, 1);
	failures += test_expect_u64("io map phys",
				    plane_paddr_raw(test_last_io_map_phys),
				    TEST_APIC_PHYS);
	failures += test_expect_u64("io map size",
				    test_last_io_map_size, ARCH_PAGE_SIZE);
	failures += test_expect_u32("io map cache",
				    test_last_io_map_cache,
				    PLANE_IO_MAP_CACHE_DEVICE);
	failures += test_expect_u32("io map prot",
				    test_last_io_map_prot,
				    PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE);
	failures += test_expect_u32("cpu count recorded",
				    lapic_cpu_count, 3);
	failures += test_expect_u32("logical 0 lapic",
				    lapic_id_by_logical_id[0], 1);
	failures += test_expect_u32("logical 2 lapic",
				    lapic_id_by_logical_id[2], 3);
	failures += test_expect_u32("apic base msr write once",
				    test_msr_write_count, 1);
	failures += test_expect_u32("apic base msr",
				    test_msr_write_msr,
				    X86_64_MSR_IA32_APIC_BASE);
	failures += test_expect_u64("apic base enabled",
				    test_msr_write_value,
				    TEST_APIC_PHYS |
					    X86_64_MSR_IA32_APIC_BASE_ENABLE);
	failures += test_expect_u32("tpr accepts all",
				    test_regs[reg_index(X86_64_LAPIC_REG_TPR)],
				    0);
	failures += test_expect_u32("svr enabled",
				    test_regs[reg_index(X86_64_LAPIC_REG_SVR)],
				    X86_64_LAPIC_SPURIOUS_VECTOR |
					    X86_64_LAPIC_SVR_ENABLE);
	failures += test_expect_u32("timer masked",
				    test_regs[reg_index(X86_64_LAPIC_REG_LVT_TIMER)],
				    X86_64_LAPIC_LVT_MASKED);
	failures += test_expect_u32("lint0 masked",
				    test_regs[reg_index(X86_64_LAPIC_REG_LVT_LINT0)],
				    X86_64_LAPIC_LVT_MASKED);
	return failures;
}

static int test_ap_init_validates_data_and_configures_current_lapic(void)
{
	int failures = 0;
	struct plane_smp_info info;
	struct plane_cpu_data ap = {
		.self = &ap,
		.logical_id = 1,
		.physical_id = 2,
		.is_bsp = false,
		.present = true,
	};
	struct plane_cpu_data bad_self = ap;

	reset_lapic_test();
	failures += test_expect_bool("ap init before runtime rejected",
				     ml_local_interrupt_init_ap(&ap),
				     false);
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	failures += test_expect_bool("bsp lapic init",
				     ml_local_interrupt_init_bsp(&info),
				     true);
	test_regs[reg_index(X86_64_LAPIC_REG_ID)] =
		(ap.physical_id << X86_64_LAPIC_ID_SHIFT) | 0x00ffffffu;
	failures += test_expect_bool("ap lapic init",
				     ml_local_interrupt_init_ap(&ap),
				     true);
	failures += test_expect_u32("ap svr configured",
				    test_regs[reg_index(X86_64_LAPIC_REG_SVR)],
				    X86_64_LAPIC_SPURIOUS_VECTOR |
					    X86_64_LAPIC_SVR_ENABLE);

	bad_self.self = NULL;
	failures += test_expect_bool("bad self rejected",
				     ml_local_interrupt_init_ap(&bad_self),
				     false);
	ap.physical_id = 7;
	failures += test_expect_bool("wrong mapped lapic rejected",
				     ml_local_interrupt_init_ap(&ap),
				     false);
	return failures;
}

static int test_end_of_interrupt_requires_initialization(void)
{
	int failures = 0;
	struct plane_smp_info info;

	reset_lapic_test();
	failures += test_expect_bool("end-of-interrupt before init rejected",
				     ml_local_interrupt_end_of_interrupt(), false);
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	failures += test_expect_bool("bsp lapic init",
				     ml_local_interrupt_init_bsp(&info),
				     true);
	test_regs[reg_index(X86_64_LAPIC_REG_ID)] =
		0x22u << X86_64_LAPIC_ID_SHIFT;
	test_regs[reg_index(X86_64_LAPIC_REG_EOI)] = 0xfeedface;
	failures += test_expect_bool("end-of-interrupt succeeds", ml_local_interrupt_end_of_interrupt(), true);
	failures += test_expect_u32("end-of-interrupt written",
				    test_regs[reg_index(X86_64_LAPIC_REG_EOI)],
				    0);
	failures += test_expect_u32("write flush read-back",
				    lapic_write_flush_value,
				    0x22u << X86_64_LAPIC_ID_SHIFT);
	return failures;
}

static int test_dispatch_forwards_to_smp_and_ends_interrupt(void)
{
	int failures = 0;
	struct plane_smp_info info;

	reset_lapic_test();
	failures += test_expect_bool("dispatch before init rejected",
				     ml_local_interrupt_dispatch(0xf0),
				     false);
	failures += test_expect_u32("dispatch before init no smp",
				    test_smp_signal_handler_count, 0);
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	failures += test_expect_bool("bsp lapic init",
				     ml_local_interrupt_init_bsp(&info),
				     true);
	test_regs[reg_index(X86_64_LAPIC_REG_ID)] =
		0x33u << X86_64_LAPIC_ID_SHIFT;
	test_regs[reg_index(X86_64_LAPIC_REG_EOI)] = 0xfeedface;

	failures += test_expect_bool("dispatch handled",
				     ml_local_interrupt_dispatch(0xf0),
				     true);
	failures += test_expect_u32("dispatch calls smp",
				    test_smp_signal_handler_count, 1);
	failures += test_expect_u32("dispatch vector",
				    test_smp_last_signal_vector, 0xf0);
	failures += test_expect_u32("dispatch ends interrupt",
				    test_regs[reg_index(X86_64_LAPIC_REG_EOI)],
				    0);
	failures += test_expect_u32("dispatch end-of-interrupt flush",
				    lapic_write_flush_value,
				    0x33u << X86_64_LAPIC_ID_SHIFT);
	return failures;
}

static int test_dispatch_unknown_vector_still_ends_interrupt(void)
{
	int failures = 0;
	struct plane_smp_info info;

	reset_lapic_test();
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	failures += test_expect_bool("bsp lapic init",
				     ml_local_interrupt_init_bsp(&info),
				     true);
	test_smp_signal_handler_result = false;
	test_regs[reg_index(X86_64_LAPIC_REG_EOI)] = 0xfeedface;

	failures += test_expect_bool("unknown dispatch unhandled",
				     ml_local_interrupt_dispatch(0xf2),
				     false);
	failures += test_expect_u32("unknown calls smp",
				    test_smp_signal_handler_count, 1);
	failures += test_expect_u32("unknown ends interrupt",
				    test_regs[reg_index(X86_64_LAPIC_REG_EOI)],
				    0);
	failures += test_expect_bool("low external rejected",
				     ml_local_interrupt_dispatch(31), false);
	failures += test_expect_bool("past idt rejected",
				     ml_local_interrupt_dispatch(256), false);
	failures += test_expect_u32("invalid vectors no extra smp",
				    test_smp_signal_handler_count, 1);
	return failures;
}

static int test_fixed_ipi_validates_and_writes_icr(void)
{
	int failures = 0;
	struct plane_smp_info info;

	reset_lapic_test();
	failures += test_expect_bool("ipi before init rejected",
				     ml_local_interrupt_send_ipi(1, 0xf0),
				     false);
	failures += test_expect_bool("build topology",
				     build_topology(&info), true);
	failures += test_expect_bool("bsp lapic init",
				     ml_local_interrupt_init_bsp(&info),
				     true);
	failures += test_expect_bool("low vector rejected",
				     ml_local_interrupt_send_ipi(1, 31),
				     false);
	failures += test_expect_bool("bad cpu rejected",
				     ml_local_interrupt_send_ipi(3, 0xf0),
				     false);

	test_regs[reg_index(X86_64_LAPIC_REG_ICR_LOW)] =
		X86_64_LAPIC_ICR_PENDING;
	failures += test_expect_bool("send fixed ipi",
				     ml_local_interrupt_send_ipi(2, 0xf0),
				     true);
	failures += test_expect_u32("waits while pending", test_relax_count, 1);
	failures += test_expect_u32("irq saved", test_irq_save_count, 1);
	failures += test_expect_u32("irq restored", test_irq_restore_count, 1);
	failures += test_expect_bool("irq state restored", test_irq_enabled, true);
	failures += test_expect_u32("icr high destination",
				    test_regs[reg_index(X86_64_LAPIC_REG_ICR_HIGH)],
				    x86_64_lapic_icr_dest_high(3));
	failures += test_expect_u32("icr low vector",
				    test_regs[reg_index(X86_64_LAPIC_REG_ICR_LOW)],
				    x86_64_lapic_icr_fixed_low(0xf0));
	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_bsp_init_rejects_invalid_inputs),
		TEST_CASE(test_bsp_init_configures_xapic_and_cpu_map),
		TEST_CASE(test_ap_init_validates_data_and_configures_current_lapic),
		TEST_CASE(test_end_of_interrupt_requires_initialization),
		TEST_CASE(test_dispatch_forwards_to_smp_and_ends_interrupt),
		TEST_CASE(test_dispatch_unknown_vector_still_ends_interrupt),
		TEST_CASE(test_fixed_ipi_validates_and_writes_icr),
	};

	return test_run_cases("x86_64_lapic_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
