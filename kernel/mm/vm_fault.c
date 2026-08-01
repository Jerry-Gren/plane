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
};

static uint32_t prot_to_map_flags(uint32_t prot)
{
	uint32_t flags = 0;

	if ((prot & PLANE_VM_PROT_WRITE) != 0) {
		flags |= HAL_MMU_MAP_WRITE;
	}

	return flags;
}

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
	return true;
}

static bool fault_resolve_page(struct plane_vm_fault_state *state)
{
	plane_paddr_t mapped_phys;

	state->page = plane_vm_object_lookup_page(state->info.object,
						  state->info.object_offset);
	if (state->page != NULL) {
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
	uint32_t map_flags = prot_to_map_flags(state->info.prot);

	if (!page_phys_valid(page_phys)) {
		return false;
	}

	if (hal_mmu_translate_kernel_page(state->info.page_vaddr,
					  &mapped_phys)) {
		return plane_paddr_equal(mapped_phys, page_phys) &&
		       hal_mmu_protect_kernel_page(state->info.page_vaddr,
						   map_flags);
	}

	return hal_mmu_map_kernel_page(state->info.page_vaddr, page_phys,
				       map_flags);
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

bool plane_vm_fault_page(struct plane_vm_map *map,
			 plane_vaddr_t vaddr,
			 uint32_t fault_type)
{
	struct plane_vm_fault_state state;

	if (!fault_lookup(map, vaddr, fault_type, &state) ||
	    !fault_resolve_page(&state)) {
		return false;
	}
	if (!fault_enter_pmap(&state)) {
		fault_cleanup_new_page(&state);
		return false;
	}

	return true;
}
