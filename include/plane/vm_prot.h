#ifndef PLANE_VM_PROT_H
#define PLANE_VM_PROT_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/bits.h>

/*
 * XNU-like VM protection bitset foundation.
 *
 * READ and WRITE are independent metadata bits. Plane currently has no
 * EXEC/NX and no no-access user mapping API. The early soft fault core and
 * hardware fault dispatch consume these bits.
 */
enum plane_vm_prot {
	PLANE_VM_PROT_NONE = 0,
	PLANE_VM_PROT_READ = BIT(0),
	PLANE_VM_PROT_WRITE = BIT(1),
	PLANE_VM_PROT_DEFAULT = PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE,
	PLANE_VM_PROT_ALL = PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE,
};

static inline bool plane_vm_prot_valid(uint32_t prot)
{
	return prot != PLANE_VM_PROT_NONE &&
	       (prot & ~PLANE_VM_PROT_ALL) == 0;
}

static inline bool plane_vm_prot_allowed(uint32_t prot, uint32_t max_prot)
{
	return plane_vm_prot_valid(prot) &&
	       plane_vm_prot_valid(max_prot) &&
	       (prot & ~max_prot) == 0;
}

#endif /* PLANE_VM_PROT_H */
