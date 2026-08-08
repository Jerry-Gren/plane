#ifndef BOOT_LIMINE_LIMINE_ARCH_H
#define BOOT_LIMINE_LIMINE_ARCH_H

#include <stdbool.h>

#include <plane/address.h>

struct boot_limine_arch_handoff {
	plane_vaddr_t hhdm_base;
};

bool boot_limine_arch_install_hhdm_physmap(
	const struct boot_limine_arch_handoff *handoff);
bool boot_limine_arch_hhdm_virt_to_phys(
	const struct boot_limine_arch_handoff *handoff,
	plane_vaddr_t vaddr,
	plane_paddr_t *phys_addr);

#endif /* BOOT_LIMINE_LIMINE_ARCH_H */
