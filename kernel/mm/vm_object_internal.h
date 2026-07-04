#ifndef PLANE_VM_OBJECT_INTERNAL_H
#define PLANE_VM_OBJECT_INTERNAL_H

#include <stdbool.h>

struct plane_vm_object;

/*
 * VM object wired-accounting notifications.
 *
 * VM page wire/unwire uses these to keep resident object accounting in sync,
 * mirroring XNU's object-side wired page accounting boundary.
 */
bool plane_vm_object_page_became_wired(struct plane_vm_object *object);
bool plane_vm_object_page_became_unwired(struct plane_vm_object *object);

#endif /* PLANE_VM_OBJECT_INTERNAL_H */
