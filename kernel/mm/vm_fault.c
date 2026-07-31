#include <hal/mmu.h>

#include <stddef.h>

#include <plane/vm_fault.h>
#include <plane/vm_map.h>
#include <plane/vm_object.h>
#include <plane/vm_page.h>

static uint32_t prot_to_map_flags(uint32_t prot)
{
	uint32_t flags = 0;

	if ((prot & PLANE_VM_PROT_WRITE) != 0) {
		flags |= HAL_MMU_MAP_WRITE;
	}

	return flags;
}

static bool page_phys_valid(uint64_t phys_addr)
{
	return phys_addr != PLANE_VM_PAGE_NO_PHYS &&
	       phys_addr != PLANE_VM_PAGE_GUARD_PHYS;
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

static bool repair_resident_mapping(const struct plane_vm_map_page_info *info,
				    struct plane_page *page)
{
	uint64_t page_phys = plane_vm_page_phys(page);
	uint64_t mapped_phys;
	uint32_t map_flags = prot_to_map_flags(info->prot);

	if (!page_phys_valid(page_phys)) {
		return false;
	}

	if (hal_mmu_translate_kernel_page(info->page_vaddr, &mapped_phys)) {
		return mapped_phys == page_phys &&
		       hal_mmu_protect_kernel_page(info->page_vaddr, map_flags);
	}

	return hal_mmu_map_kernel_page(info->page_vaddr, page_phys, map_flags);
}

bool plane_vm_fault_page(struct plane_vm_map *map,
			 uint64_t vaddr,
			 uint32_t fault_type)
{
	struct plane_vm_map_page_info info;
	struct plane_page *page;
	uint64_t mapped_phys;
	uint64_t wired_done;

	if (!plane_vm_prot_valid(fault_type) ||
	    !plane_vm_map_lookup_page(map, vaddr, &info) ||
	    (fault_type & ~info.prot) != 0) {
		return false;
	}

	page = plane_vm_object_lookup_page(info.object, info.object_offset);
	if (page != NULL) {
		return repair_resident_mapping(&info, page);
	}

	if (hal_mmu_translate_kernel_page(info.page_vaddr, &mapped_phys)) {
		return false;
	}

	if (!plane_vm_page_grab(PLANE_VM_PAGE_GRAB_ZERO, &page) ||
	    page == NULL) {
		return false;
	}
	if (!page_phys_valid(plane_vm_page_phys(page))) {
		(void)plane_vm_page_release(page);
		return false;
	}

	if (!wire_page_count(page, info.wired_count, &wired_done)) {
		(void)unwire_page_count(page, wired_done);
		(void)plane_vm_page_release(page);
		return false;
	}

	if (!plane_vm_object_insert_page(info.object, info.object_offset, page)) {
		(void)unwire_page_count(page, wired_done);
		(void)plane_vm_page_release(page);
		return false;
	}

	if (!hal_mmu_map_kernel_page(info.page_vaddr,
				     plane_vm_page_phys(page),
				     prot_to_map_flags(info.prot))) {
		(void)plane_vm_object_remove_page(info.object,
						 info.object_offset);
		(void)unwire_page_count(page, wired_done);
		(void)plane_vm_page_release(page);
		return false;
	}

	return true;
}
