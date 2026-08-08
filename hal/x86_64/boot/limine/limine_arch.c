#include <boot/limine/limine_arch.h>

#include <x86_64/physmap_internal.h>

bool boot_limine_arch_init_handoff(
	const struct boot_limine_arch_handoff *handoff)
{
	if (handoff == NULL || plane_vaddr_is_null(handoff->hhdm_base)) {
		return false;
	}

	x86_64_physmap_set_bootstrap(handoff->hhdm_base,
				     x86_64_physmap_window_size());
	return true;
}

bool boot_limine_arch_hhdm_virt_to_phys(
	const struct boot_limine_arch_handoff *handoff,
	plane_vaddr_t vaddr,
	plane_paddr_t *phys_addr)
{
	uint64_t raw_vaddr;
	uint64_t raw_base;

	if (handoff == NULL || phys_addr == NULL ||
	    plane_vaddr_is_null(handoff->hhdm_base)) {
		return false;
	}

	raw_vaddr = plane_vaddr_raw(vaddr);
	raw_base = plane_vaddr_raw(handoff->hhdm_base);
	if (raw_vaddr < raw_base) {
		return false;
	}

	*phys_addr = plane_paddr_make(raw_vaddr - raw_base);
	return true;
}
