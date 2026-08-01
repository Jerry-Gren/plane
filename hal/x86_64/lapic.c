#include <stddef.h>

#include <hal/cpu.h>
#include <hal/irq.h>
#include <hal/local_interrupt.h>
#include <hal/mmu.h>
#include <hal/page.h>
#include <hal/x86_64/arch_mmu.h>
#include <hal/x86_64/cpu_features.h>
#include <plane/address.h>
#include <plane/bits.h>
#include <plane/smp.h>

#include "msr_internal.h"

/*
 * XNU-like local APIC foundation, narrowed to xAPIC MMIO setup and fixed IPI
 * primitives. Timer/error/IPI dispatch and x2APIC are intentionally left for
 * later SMP milestones.
 */

#define X86_64_MSR_APIC_BASE 0x1bu

#define X86_64_APIC_BASE_X2APIC BIT_ULL(10)
#define X86_64_APIC_BASE_ENABLE BIT_ULL(11)
#define X86_64_APIC_BASE_ADDR   X86_64_PAGE_ENTRY_ADDR_MASK

#define X86_64_LAPIC_ID              0x020u
#define X86_64_LAPIC_TPR             0x080u
#define X86_64_LAPIC_EOI             0x0b0u
#define X86_64_LAPIC_SVR             0x0f0u
#define X86_64_LAPIC_ICR_LOW         0x300u
#define X86_64_LAPIC_ICR_HIGH        0x310u
#define X86_64_LAPIC_LVT_TIMER       0x320u
#define X86_64_LAPIC_LVT_THERMAL     0x330u
#define X86_64_LAPIC_LVT_PERFCNT     0x340u
#define X86_64_LAPIC_LVT_LINT0       0x350u
#define X86_64_LAPIC_LVT_LINT1       0x360u
#define X86_64_LAPIC_LVT_ERROR       0x370u

#define X86_64_LAPIC_ID_SHIFT        24u
#define X86_64_LAPIC_ICR_PENDING     BIT(12)
#define X86_64_LAPIC_ICR_DM_FIXED    0x000u
#define X86_64_LAPIC_ICR_DEST_SHIFT  24u
#define X86_64_LAPIC_LVT_MASKED      BIT(16)
#define X86_64_LAPIC_SVR_ENABLE      BIT(8)
#define X86_64_LAPIC_SPURIOUS_VECTOR 0xffu
#define X86_64_LAPIC_VECTOR_MIN      32u

static plane_vaddr_t lapic_mmio_base;
static uint32_t lapic_id_by_logical_id[PLANE_MAX_CPUS];
static uint32_t lapic_cpu_count;
static bool lapic_initialized;

static bool lapic_id_valid(uint32_t lapic_id)
{
	return lapic_id <= UINT8_MAX;
}

static volatile uint32_t *lapic_reg(uint32_t offset)
{
	uint64_t base = plane_vaddr_raw(lapic_mmio_base);

	return (volatile uint32_t *)(uintptr_t)(base + offset);
}

static uint32_t lapic_read(uint32_t offset)
{
	return *lapic_reg(offset);
}

static void lapic_write(uint32_t offset, uint32_t value)
{
	*lapic_reg(offset) = value;
	lapic_read(X86_64_LAPIC_ID);
}

static bool lapic_map_xapic_mmio(plane_paddr_t phys_base,
				 plane_vaddr_t *mmio_base)
{
	plane_vaddr_t vaddr;
	plane_paddr_t mapped_phys;

	if (mmio_base == NULL || !plane_paddr_is_page_aligned(phys_base)) {
		return false;
	}

	/*
	 * Transitional MMIO mapping.
	 *
	 * Do not treat this as proof that MMIO belongs in the direct map:
	 * bootloader HHDM/direct-map coverage is allowed to exclude reserved
	 * device pages, which is why Limine faulted on the LAPIC TPR write.
	 *
	 * XNU maps LAPIC through an IO mapping path with device cache
	 * attributes. Plane should grow a dedicated kernel IO-map API and move
	 * LAPIC there; for now we reuse the direct-map VA slot only after
	 * explicitly installing the missing kernel PTE.
	 */
	vaddr = hal_mmu_direct_phys_range_to_virt(phys_base, ARCH_PAGE_SIZE);
	if (plane_vaddr_is_null(vaddr)) {
		return false;
	}

	if (hal_mmu_translate_kernel_page(vaddr, &mapped_phys)) {
		if (!plane_paddr_equal(mapped_phys, phys_base)) {
			return false;
		}
		*mmio_base = vaddr;
		return true;
	}

	if (!hal_mmu_map_kernel_page(vaddr, phys_base, HAL_MMU_MAP_WRITE)) {
		return false;
	}

	*mmio_base = vaddr;
	return true;
}

static bool lapic_probe_xapic(plane_vaddr_t *mmio_base)
{
	uint64_t apic_base;
	plane_paddr_t phys_base;

	if (mmio_base == NULL ||
	    !x86_64_cpu_has_feature(X86_64_CPU_FEATURE_APIC) ||
	    !x86_64_cpu_has_feature(X86_64_CPU_FEATURE_MSR)) {
		return false;
	}

	apic_base = x86_64_msr_read(X86_64_MSR_APIC_BASE);
	if ((apic_base & X86_64_APIC_BASE_X2APIC) != 0) {
		return false;
	}

	phys_base = plane_paddr_make(apic_base & X86_64_APIC_BASE_ADDR);
	if (plane_paddr_is_null(phys_base)) {
		return false;
	}

	if (!lapic_map_xapic_mmio(phys_base, mmio_base)) {
		return false;
	}

	if ((apic_base & X86_64_APIC_BASE_ENABLE) == 0) {
		if (!x86_64_msr_write(X86_64_MSR_APIC_BASE,
				      apic_base | X86_64_APIC_BASE_ENABLE)) {
			return false;
		}
	}

	return true;
}

static void lapic_configure_current(void)
{
	lapic_write(X86_64_LAPIC_TPR, 0);
	lapic_write(X86_64_LAPIC_LVT_TIMER, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_LVT_THERMAL, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_LVT_PERFCNT, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_LVT_LINT0, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_LVT_LINT1, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_LVT_ERROR, X86_64_LAPIC_LVT_MASKED);
	lapic_write(X86_64_LAPIC_SVR,
		    X86_64_LAPIC_SPURIOUS_VECTOR | X86_64_LAPIC_SVR_ENABLE);
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
		    !lapic_id_valid(lapic_id)) {
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

	local_lapic_id = lapic_read(X86_64_LAPIC_ID) >> X86_64_LAPIC_ID_SHIFT;
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

	lapic_write(X86_64_LAPIC_EOI, 0);
	return true;
}

bool hal_local_interrupt_send_fixed_ipi(uint32_t logical_id, uint8_t vector)
{
	plane_irq_state_t irq_state;
	uint32_t lapic_id;

	if (!lapic_initialized || logical_id >= lapic_cpu_count ||
	    vector < X86_64_LAPIC_VECTOR_MIN) {
		return false;
	}

	lapic_id = lapic_id_by_logical_id[logical_id];
	irq_state = hal_irq_save();
	while ((lapic_read(X86_64_LAPIC_ICR_LOW) &
		X86_64_LAPIC_ICR_PENDING) != 0) {
		hal_cpu_relax();
	}

	lapic_write(X86_64_LAPIC_ICR_HIGH,
		    lapic_id << X86_64_LAPIC_ICR_DEST_SHIFT);
	lapic_write(X86_64_LAPIC_ICR_LOW,
		    vector | X86_64_LAPIC_ICR_DM_FIXED);
	hal_irq_restore(irq_state);
	return true;
}
