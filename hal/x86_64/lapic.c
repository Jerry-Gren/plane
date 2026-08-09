#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/cpu.h>
#include <hal/irq.h>
#include <hal/local_interrupt.h>
#include <hal/mmu.h>
#include <hal/page.h>
#include <hal/x86_64/cpu_features.h>
#include <hal/x86_64/msr_defs.h>
#include <plane/address.h>
#include <plane/io_map.h>
#include <plane/printk.h>
#include <plane/smp.h>

#include "lapic_regs.h"
#include <x86_64/msr.h>

/*
 * XNU-like local APIC foundation, narrowed to xAPIC MMIO setup and fixed IPI
 * primitives. Timer/error/IPI dispatch and x2APIC are intentionally left for
 * later SMP milestones.
 */

static plane_vaddr_t lapic_mmio_base;
static uint32_t lapic_id_by_logical_id[PLANE_MAX_CPUS];
static uint32_t lapic_cpu_count;
static bool lapic_initialized;
static volatile uint32_t lapic_write_flush_value;

static bool lapic_id_is_valid(uint32_t lapic_id)
{
	return lapic_id <= X86_64_LAPIC_ID_MASK;
}

static volatile uint32_t *lapic_reg(plane_vaddr_t base_addr,
				    enum x86_64_lapic_reg reg)
{
	uint64_t base = plane_vaddr_raw(base_addr);

	return (volatile uint32_t *)(uintptr_t)(
		base + x86_64_lapic_mmio_offset(reg));
}

static uint32_t lapic_read_at(plane_vaddr_t base,
			      enum x86_64_lapic_reg reg)
{
	return *lapic_reg(base, reg);
}

static uint32_t lapic_read(enum x86_64_lapic_reg reg)
{
	return lapic_read_at(lapic_mmio_base, reg);
}

static void lapic_write(enum x86_64_lapic_reg reg, uint32_t value)
{
	*lapic_reg(lapic_mmio_base, reg) = value;
	/*
	 * xAPIC registers are MMIO. Keep a conservative read-back after writes
	 * so the store is posted before subsequent local-APIC programming.
	 */
	lapic_write_flush_value = lapic_read(X86_64_LAPIC_REG_ID);
}

static bool lapic_probe_xapic(plane_vaddr_t *mmio_base)
{
	uint64_t apic_base;
	plane_paddr_t phys_base;
	plane_vaddr_t mapped_base;

	if (mmio_base == NULL ||
	    !x86_64_cpu_has_feature(X86_64_CPU_FEATURE_APIC) ||
	    !x86_64_cpu_has_feature(X86_64_CPU_FEATURE_MSR)) {
		return false;
	}

	apic_base = x86_64_msr_read(X86_64_MSR_IA32_APIC_BASE);
	if ((apic_base & X86_64_MSR_IA32_APIC_BASE_X2APIC) != 0) {
		return false;
	}

	phys_base = plane_paddr_make(apic_base &
				     X86_64_MSR_IA32_APIC_BASE_ADDR);
	if (plane_paddr_is_null(phys_base)) {
		return false;
	}

	if (!plane_paddr_is_page_aligned(phys_base) ||
	    !plane_io_map(phys_base, ARCH_PAGE_SIZE,
			  PLANE_IO_MAP_CACHE_DEVICE,
			  PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE,
			  &mapped_base)) {
		return false;
	}
	if (!x86_64_lapic_version_supported(lapic_read_at(
		    mapped_base, X86_64_LAPIC_REG_VERSION))) {
		BUG_ON_MSG(!plane_io_unmap(mapped_base, ARCH_PAGE_SIZE),
			   "failed to rollback LAPIC IO map");
		return false;
	}

	if ((apic_base & X86_64_MSR_IA32_APIC_BASE_ENABLE) == 0) {
		if (!x86_64_msr_write(X86_64_MSR_IA32_APIC_BASE,
				      apic_base |
					      X86_64_MSR_IA32_APIC_BASE_ENABLE)) {
			BUG_ON_MSG(!plane_io_unmap(mapped_base, ARCH_PAGE_SIZE),
				   "failed to rollback LAPIC IO map");
			return false;
		}
	}

	*mmio_base = mapped_base;
	return true;
}

static void lapic_configure_current(void)
{
	lapic_write(X86_64_LAPIC_REG_TPR, 0);
	lapic_write(X86_64_LAPIC_REG_LVT_TIMER, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_REG_LVT_THERMAL, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_REG_LVT_PERFCNT, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_REG_LVT_LINT0, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_REG_LVT_LINT1, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_REG_LVT_ERROR, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_REG_SVR,
		    x86_64_lapic_svr_enable(X86_64_LAPIC_SPURIOUS_VECTOR));
}

bool hal_local_interrupt_init_bsp(const struct plane_smp_info *info)
{
	plane_vaddr_t mmio_base;

	if (lapic_initialized || info == NULL || info->cpu_count == 0 ||
	    info->cpu_count > PLANE_MAX_CPUS || info->bsp_logical_id != 0) {
		return false;
	}

	for (uint32_t i = 0; i < info->cpu_count; i++) {
		const struct plane_cpu_info *cpu = &info->cpus[i];
		uint32_t lapic_id = cpu->physical_id;

		if (!cpu->present || cpu->logical_id != i ||
		    !lapic_id_is_valid(lapic_id)) {
			return false;
		}
		lapic_id_by_logical_id[i] = lapic_id;
	}

	if (!lapic_probe_xapic(&mmio_base)) {
		return false;
	}

	lapic_mmio_base = mmio_base;
	lapic_cpu_count = info->cpu_count;
	lapic_configure_current();
	lapic_initialized = true;
	return true;
}

bool hal_local_interrupt_init_ap(struct plane_cpu_data *data)
{
	uint32_t expected_lapic_id;
	uint32_t local_lapic_id;

	if (!lapic_initialized || data == NULL || data->self != data ||
	    data->is_bsp || !data->present ||
	    data->logical_id >= lapic_cpu_count) {
		return false;
	}

	expected_lapic_id = data->physical_id;
	if (expected_lapic_id != lapic_id_by_logical_id[data->logical_id]) {
		return false;
	}

	local_lapic_id =
		x86_64_lapic_xapic_id(lapic_read(X86_64_LAPIC_REG_ID));
	if (local_lapic_id != expected_lapic_id) {
		return false;
	}

	lapic_configure_current();
	return true;
}

bool hal_local_interrupt_eoi(void)
{
	if (!lapic_initialized) {
		return false;
	}

	lapic_write(X86_64_LAPIC_REG_EOI, 0);
	return true;
}

bool hal_local_interrupt_send_ipi(uint32_t logical_id, uint8_t vector)
{
	plane_irq_state_t irq_state;
	uint32_t lapic_id;

	if (!lapic_initialized || logical_id >= lapic_cpu_count ||
	    !x86_64_lapic_external_vector_is_valid(vector)) {
		return false;
	}

	/*
	 * The generic HAL exposes "send local interrupt". The current x86_64
	 * backend implements that as a physical-destination fixed IPI.
	 */
	lapic_id = lapic_id_by_logical_id[logical_id];
	irq_state = hal_irq_save();
	while ((lapic_read(X86_64_LAPIC_REG_ICR_LOW) &
		X86_64_LAPIC_ICR_PENDING) != 0) {
		hal_cpu_relax();
	}

	lapic_write(X86_64_LAPIC_REG_ICR_HIGH,
		    x86_64_lapic_icr_dest_high(lapic_id));
	lapic_write(X86_64_LAPIC_REG_ICR_LOW,
		    x86_64_lapic_icr_fixed_low(vector));
	hal_irq_restore(irq_state);
	return true;
}
