#ifndef PLANE_VM_OBJECT_INTERNAL_H
#define PLANE_VM_OBJECT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

struct plane_page;
struct plane_vm_object;
struct plane_vm_zone_segment;

/*
 * VM object wired-accounting notifications.
 *
 * VM page wire/unwire uses these to keep resident object accounting in sync,
 * mirroring XNU's object-side wired page accounting boundary.
 */
bool plane_vm_object_page_became_wired(struct plane_vm_object *object);
bool plane_vm_object_page_became_unwired(struct plane_vm_object *object);
bool plane_vm_object_add_zone_storage(struct plane_vm_object *storage,
				      uint64_t count,
				      struct plane_vm_zone_segment *segment);
bool plane_vm_object_rehome_resident_hash(struct plane_page **buckets,
					  uint64_t bucket_count);
void plane_vm_object_reset_bootstrap_for_tests(void);

#endif /* PLANE_VM_OBJECT_INTERNAL_H */
