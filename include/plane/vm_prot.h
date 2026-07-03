#ifndef PLANE_VM_PROT_H
#define PLANE_VM_PROT_H

#include <plane/bits.h>

/*
 * XNU-like VM protection bitset foundation.
 *
 * READ and WRITE are independent metadata bits. Plane currently has no
 * EXEC/NX, no no-access user mapping API, and no fault-time enforcement.
 */
enum plane_vm_prot {
	PLANE_VM_PROT_NONE = 0,
	PLANE_VM_PROT_READ = BIT(0),
	PLANE_VM_PROT_WRITE = BIT(1),
	PLANE_VM_PROT_DEFAULT = PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE,
	PLANE_VM_PROT_ALL = PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE,
};

#endif /* PLANE_VM_PROT_H */
