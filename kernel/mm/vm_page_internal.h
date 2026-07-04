#ifndef PLANE_VM_PAGE_INTERNAL_H
#define PLANE_VM_PAGE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

struct plane_page;
struct plane_vm_object;

/*
 * VM page resident metadata mutation helpers.
 * Public callers should use vm_object insert/remove instead.
 * Tabled/hashed state is VM-resident membership state, not public page API.
 */
struct plane_page *plane_vm_page_create_guard(void);
bool plane_vm_page_release_guard(struct plane_page *page);
bool plane_vm_page_attach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset);
bool plane_vm_page_detach_object(struct plane_page *page,
				 struct plane_vm_object *object,
				 uint64_t offset);
struct plane_page *plane_vm_page_object_prev(const struct plane_page *page);
struct plane_page *plane_vm_page_object_next(const struct plane_page *page);
struct plane_page *plane_vm_page_object_hash_next(const struct plane_page *page);
bool plane_vm_page_object_tabled(const struct plane_page *page);
bool plane_vm_page_object_hashed(const struct plane_page *page);
bool plane_vm_page_set_object_prev(struct plane_page *page,
				   struct plane_page *prev);
bool plane_vm_page_set_object_next(struct plane_page *page,
				   struct plane_page *next);
bool plane_vm_page_set_object_hash_next(struct plane_page *page,
					struct plane_page *next);
bool plane_vm_page_set_object_tabled(struct plane_page *page, bool tabled);
bool plane_vm_page_set_object_hashed(struct plane_page *page, bool hashed);

#endif /* PLANE_VM_PAGE_INTERNAL_H */
