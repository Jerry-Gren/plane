#include <hal/mmu.h>

#include <stddef.h>

#include <plane/vm_fault.h>
#include <plane/vm_map.h>
#include <plane/vm_object.h>
#include <plane/printk.h>
#include <plane/vm_page.h>

struct plane_vm_fault_state {
	struct plane_vm_map_page_info info;
	struct plane_page *page;
	uint64_t wired_done;
	bool new_zero_page;
	bool wired_resident_hit;
};

static bool page_phys_valid(plane_paddr_t phys_addr)
{
	return !plane_paddr_equal(phys_addr, PLANE_VM_PAGE_NO_PHYS) &&
	       !plane_paddr_equal(phys_addr, PLANE_VM_PAGE_GUARD_PHYS);
}

static bool wire_page_count(struct plane_page *page,
			    uint64_t wired_count,
			    uint64_t *wired_done)
{
	*wired_done = 0;
	for (uint64_t i = 0; i < wired_count; i++) {
		if (!plane_vm_page_wire(page)) {
			return false;
		}
		(*wired_done)++;
	}
	return true;
}

static bool unwire_page_count(struct plane_page *page, uint64_t wired_count)
{
	for (uint64_t i = 0; i < wired_count; i++) {
		if (!plane_vm_page_unwire(page)) {
			return false;
		}
	}
	return true;
}

static void fault_release_uninserted_page(struct plane_vm_fault_state *state)
{
	BUG_ON_MSG(!plane_vm_page_release(state->page),
		   "failed to release fault page");
	state->page = NULL;
	state->wired_done = 0;
}

static void fault_unwire_uninserted_page(struct plane_vm_fault_state *state)
{
	BUG_ON_MSG(!unwire_page_count(state->page, state->wired_done),
		   "failed to rollback fault page wiring");
	fault_release_uninserted_page(state);
}

static bool fault_lookup(struct plane_vm_map *map,
			 plane_vaddr_t vaddr,
			 uint32_t fault_type,
			 struct plane_vm_fault_state *state)
{
	if (state == NULL ||
	    !plane_vm_prot_valid(fault_type) ||
	    !plane_vm_map_lookup_page(map, vaddr, &state->info) ||
	    (fault_type & ~state->info.prot) != 0) {
		return false;
	}

	state->page = NULL;
	state->wired_done = 0;
	state->new_zero_page = false;
	state->wired_resident_hit = false;
	return true;
}

static bool fault_resolve_page(struct plane_vm_fault_state *state,
			       bool wire_resident_hit)
{
	plane_paddr_t mapped_phys;

	state->page = plane_vm_object_lookup_page(state->info.object,
						  state->info.object_offset);
	if (state->page != NULL) {
		if (wire_resident_hit) {
			if (!plane_vm_page_wire(state->page)) {
				return false;
			}
			state->wired_resident_hit = true;
		}
		return true;
	}

	if (hal_mmu_translate_kernel_page(state->info.page_vaddr,
					  &mapped_phys)) {
		return false;
	}

	if (!plane_vm_page_grab(PLANE_VM_PAGE_GRAB_ZERO, &state->page) ||
	    state->page == NULL) {
		return false;
	}
	if (!page_phys_valid(plane_vm_page_phys(state->page))) {
		fault_release_uninserted_page(state);
		return false;
	}

	if (!wire_page_count(state->page, state->info.wired_count,
			     &state->wired_done)) {
		fault_unwire_uninserted_page(state);
		return false;
	}

	if (!plane_vm_object_insert_page(state->info.object,
					 state->info.object_offset,
					 state->page)) {
		fault_unwire_uninserted_page(state);
		return false;
	}

	state->new_zero_page = true;
	return true;
}

static bool fault_enter_pmap(const struct plane_vm_fault_state *state)
{
	plane_paddr_t page_phys = plane_vm_page_phys(state->page);
	plane_paddr_t mapped_phys;
	struct hal_mmu_map_options options =
		hal_mmu_default_map_options(state->info.prot);

	if (!page_phys_valid(page_phys)) {
		return false;
	}

	if (hal_mmu_translate_kernel_page(state->info.page_vaddr,
					  &mapped_phys)) {
		return plane_paddr_equal(mapped_phys, page_phys) &&
		       hal_mmu_protect_kernel_page(state->info.page_vaddr,
						   state->info.prot);
	}

	return hal_mmu_map_kernel_page(state->info.page_vaddr, page_phys,
				       options);
}

static void fault_cleanup_new_page(struct plane_vm_fault_state *state)
{
	if (!state->new_zero_page || state->page == NULL) {
		return;
	}

	BUG_ON_MSG(plane_vm_object_remove_page(state->info.object,
					       state->info.object_offset) !=
		   state->page,
		   "failed to remove fault page from object");
	BUG_ON_MSG(!unwire_page_count(state->page, state->wired_done),
		   "failed to rollback fault page wiring");
	BUG_ON_MSG(!plane_vm_page_release(state->page),
		   "failed to release fault page");
	state->page = NULL;
	state->wired_done = 0;
	state->new_zero_page = false;
}

static void fault_cleanup_resident_hit_wire(struct plane_vm_fault_state *state)
{
	if (!state->wired_resident_hit || state->page == NULL) {
		return;
	}

	BUG_ON_MSG(!plane_vm_page_unwire(state->page),
		   "failed to rollback resident fault wiring");
	state->wired_resident_hit = false;
}

static bool fault_page_internal(struct plane_vm_map *map,
				plane_vaddr_t vaddr,
				uint32_t fault_type,
				bool wire_resident_hit)
{
	struct plane_vm_fault_state state;

	if (!fault_lookup(map, vaddr, fault_type, &state) ||
	    !fault_resolve_page(&state, wire_resident_hit)) {
		return false;
	}
	if (!fault_enter_pmap(&state)) {
		fault_cleanup_new_page(&state);
		fault_cleanup_resident_hit_wire(&state);
		return false;
	}

	return true;
}

bool plane_vm_fault_page(struct plane_vm_map *map,
			 plane_vaddr_t vaddr,
			 uint32_t fault_type)
{
	return fault_page_internal(map, vaddr, fault_type, false);
}

static bool fault_range_valid(struct plane_vm_map *map,
			      plane_vaddr_t vaddr,
			      uint64_t page_count)
{
	plane_vaddr_t last_vaddr;

	return map != NULL &&
	       !plane_vaddr_is_null(vaddr) &&
	       plane_vaddr_is_page_aligned(vaddr) &&
	       page_count != 0 &&
	       plane_vaddr_add_pages(vaddr, page_count - 1, &last_vaddr);
}

static bool fault_range_prot_valid(struct plane_vm_map *map,
				   plane_vaddr_t vaddr,
				   uint64_t page_count,
				   uint32_t fault_type)
{
	return plane_vm_prot_valid(fault_type) &&
	       fault_range_valid(map, vaddr, page_count);
}

bool plane_vm_fault_pages(struct plane_vm_map *map,
			  plane_vaddr_t vaddr,
			  uint64_t page_count,
			  uint32_t fault_type)
{
	if (!fault_range_prot_valid(map, vaddr, page_count, fault_type)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		plane_vaddr_t page_vaddr;

		BUG_ON_MSG(!plane_vaddr_add_pages(vaddr, i, &page_vaddr),
			   "failed to advance fault range");
		if (!fault_page_internal(map, page_vaddr, fault_type, false)) {
			return false;
		}
	}

	return true;
}

static bool fault_wire_range_preflight(struct plane_vm_map *map,
				       plane_vaddr_t vaddr,
				       uint64_t page_count,
				       uint32_t fault_type)
{
	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_vm_map_page_info info;
		struct plane_page *page;
		uint64_t wire_count;
		plane_vaddr_t page_vaddr;

		BUG_ON_MSG(!plane_vaddr_add_pages(vaddr, i, &page_vaddr),
			   "failed to advance fault wire range");
		if (!plane_vm_map_lookup_page(map, page_vaddr, &info) ||
		    (fault_type & ~info.prot) != 0) {
			return false;
		}

		page = plane_vm_object_lookup_page(info.object,
						   info.object_offset);
		if (page == NULL) {
			continue;
		}
		if (!plane_vm_page_wire_count(page, &wire_count) ||
		    wire_count == UINT64_MAX) {
			return false;
		}
	}

	return true;
}

static bool fault_unwire_range_preflight(struct plane_vm_map *map,
					 plane_vaddr_t vaddr,
					 uint64_t page_count)
{
	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_vm_map_page_info info;
		struct plane_page *page;
		uint64_t wire_count;
		plane_vaddr_t page_vaddr;

		BUG_ON_MSG(!plane_vaddr_add_pages(vaddr, i, &page_vaddr),
			   "failed to advance fault unwire range");
		if (!plane_vm_map_lookup_page(map, page_vaddr, &info) ||
		    info.wired_count == 0) {
			return false;
		}

		page = plane_vm_object_lookup_page(info.object,
						   info.object_offset);
		if (page == NULL) {
			continue;
		}
		if (!plane_vm_page_wire_count(page, &wire_count) ||
		    wire_count == 0) {
			return false;
		}
	}

	return true;
}

static bool fault_unwire_resident_pages(struct plane_vm_map *map,
					plane_vaddr_t vaddr,
					uint64_t page_count)
{
	for (uint64_t i = 0; i < page_count; i++) {
		struct plane_vm_map_page_info info;
		struct plane_page *page;
		plane_vaddr_t page_vaddr;

		BUG_ON_MSG(!plane_vaddr_add_pages(vaddr, i, &page_vaddr),
			   "failed to advance fault unwire range");
		if (!plane_vm_map_lookup_page(map, page_vaddr, &info)) {
			return false;
		}

		page = plane_vm_object_lookup_page(info.object,
						   info.object_offset);
		if (page != NULL && !plane_vm_page_unwire(page)) {
			return false;
		}
	}

	return true;
}

bool plane_vm_fault_wire_pages(struct plane_vm_map *map,
			       plane_vaddr_t vaddr,
			       uint64_t page_count,
			       uint32_t fault_type)
{
	uint64_t faulted_pages = 0;

	if (!fault_range_prot_valid(map, vaddr, page_count, fault_type) ||
	    !fault_wire_range_preflight(map, vaddr, page_count, fault_type) ||
	    !plane_vm_map_wire_pages(map, vaddr, page_count)) {
		return false;
	}

	for (uint64_t i = 0; i < page_count; i++) {
		plane_vaddr_t page_vaddr;

		BUG_ON_MSG(!plane_vaddr_add_pages(vaddr, i, &page_vaddr),
			   "failed to advance fault wire range");
		if (!fault_page_internal(map, page_vaddr, fault_type, true)) {
			BUG_ON_MSG(!fault_unwire_resident_pages(
					   map, vaddr, faulted_pages),
				   "failed to rollback fault wiring");
			BUG_ON_MSG(!plane_vm_map_unwire_pages(map, vaddr,
							      page_count),
				   "failed to rollback map wiring");
			return false;
		}
		faulted_pages++;
	}

	return true;
}

bool plane_vm_fault_unwire_pages(struct plane_vm_map *map,
				 plane_vaddr_t vaddr,
				 uint64_t page_count)
{
	if (!fault_range_valid(map, vaddr, page_count) ||
	    !fault_unwire_range_preflight(map, vaddr, page_count) ||
	    !plane_vm_map_unwire_pages(map, vaddr, page_count)) {
		return false;
	}

	BUG_ON_MSG(!fault_unwire_resident_pages(map, vaddr, page_count),
		   "failed to unwire resident pages");
	return true;
}
