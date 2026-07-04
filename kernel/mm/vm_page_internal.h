#ifndef PLANE_VM_PAGE_INTERNAL_H
#define PLANE_VM_PAGE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

struct plane_page;
struct plane_vm_object;

/*
 * VM resident-page helpers.
 *
 * These are the private mutation side of plane_page resident metadata.
 * Public callers should use vm_object insert/remove instead.
 */
bool plane_vm_page_attach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset);
bool plane_vm_page_detach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset);

#endif /* PLANE_VM_PAGE_INTERNAL_H */
