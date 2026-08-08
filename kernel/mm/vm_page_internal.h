#ifndef PLANE_VM_PAGE_INTERNAL_H
#define PLANE_VM_PAGE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/vm_page.h>

struct plane_vm_object;
struct plane_vm_zone_segment;

enum plane_vm_page_queue_state {
	PLANE_VM_PAGE_QUEUE_NONE = 0,
	PLANE_VM_PAGE_QUEUE_FREE,
};

struct plane_vm_page_queue {
	struct plane_page *head;
	struct plane_page *tail;
	uint64_t count;
	enum plane_vm_page_queue_state state;
};

struct plane_page {
	uint64_t phys_addr;
	uint64_t wire_count;
	struct plane_vm_object *vm_object;
	uint64_t vm_object_offset;
	struct plane_page *object_prev;
	struct plane_page *object_next;
	struct plane_page *object_hash_next;
	bool object_tabled;
	bool object_hashed;
	enum plane_vm_page_state state;
	struct plane_page *queue_prev;
	struct plane_page *queue_next;
	enum plane_vm_page_queue_state queue_state;
};

struct plane_vm_page_managed_range {
	uint64_t base;
	uint64_t page_count;
	uint64_t page_index;
};

/*
 * VM page resident metadata mutation helpers.
 * Public callers should use vm_object insert/remove instead.
 * Tabled/hashed state is VM-resident membership state, not public page API.
 * Queue links are owned by plane_vm_page_queue_* helpers; PMM owns the free
 * allocator policy, but not the queue linkage mechanics.
 */
void plane_vm_page_reset_runtime(void);
bool plane_vm_page_install_pool(
	struct plane_page *pool,
	uint64_t page_count,
	const struct plane_vm_page_managed_range *ranges,
	uint64_t range_count);
void plane_vm_page_init(struct plane_page *page,
			plane_paddr_t phys_addr,
			enum plane_vm_page_state state);
void plane_vm_page_reset_resident_links(struct plane_page *page);
bool plane_vm_page_set_state(struct plane_page *page,
			     enum plane_vm_page_state state);
bool plane_vm_page_allocated_unwired_no_object(const struct plane_page *page);
bool plane_vm_page_queue_init(struct plane_vm_page_queue *queue,
			      enum plane_vm_page_queue_state state);
bool plane_vm_page_queue_insert_ordered(struct plane_vm_page_queue *queue,
					struct plane_page *page);
bool plane_vm_page_queue_remove(struct plane_vm_page_queue *queue,
				struct plane_page *page);
struct plane_page *plane_vm_page_queue_pop_head(
	struct plane_vm_page_queue *queue);
uint64_t plane_vm_page_queue_count(const struct plane_vm_page_queue *queue);
enum plane_vm_page_queue_state plane_vm_page_queue_state(
	const struct plane_page *page);
struct plane_page *plane_vm_page_create_guard(void);
bool plane_vm_page_release_guard(struct plane_page *page);
bool plane_vm_page_guard_storage_size(uint64_t count, uint64_t *size);
bool plane_vm_page_add_guard_storage(void *storage,
				     uint64_t count,
				     struct plane_vm_zone_segment *segment);
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
