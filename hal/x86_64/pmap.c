#include <stddef.h>

#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>
#include <hal/x86_64/pat.h>
#include <hal/x86_64/pmap.h>
#include <plane/mm.h>
#include <plane/printk.h>
#include <plane/pmm.h>

static plane_paddr_t pmap_current_root_phys(void)
{
	return x86_64_pmap_active_root_phys();
}

static void write_cr3_phys(plane_paddr_t phys_addr)
{
	/*
	 * CR3 holds the physical base of the active top-level paging
	 * structure. Plane switches here after cloning PMM-owned kernel tables.
	 */
	__asm__ volatile ("mov %0, %%cr3" : : "r" (plane_paddr_raw(phys_addr)) : "memory");
}

static uint64_t *direct_map_page_table(plane_paddr_t phys_addr)
{
	plane_vaddr_t vaddr =
		hal_mmu_direct_phys_range_to_virt(phys_addr, PAGE_SIZE);

	if (plane_vaddr_is_null(vaddr)) {
		return NULL;
	}

	return plane_vaddr_to_ptr(vaddr);
}

static bool free_cloned_page_table(plane_paddr_t table_phys, uint8_t level)
{
	uint64_t *table = direct_map_page_table(table_phys);

	if (table == NULL) {
		return false;
	}

	for (uint64_t i = 0; i < X86_64_PAGING_TABLE_ENTRIES; i++) {
		uint64_t entry = table[i];

		if (!x86_64_paging_entry_present(entry) ||
		    x86_64_paging_entry_leaf(entry, level)) {
			continue;
		}

		if (!free_cloned_page_table(plane_paddr_make(x86_64_paging_entry_phys(entry)),
					    level - 1)) {
			return false;
		}
	}

	BUG_ON_MSG(!plane_pmm_free_page_phys(table_phys),
		   "failed to free cloned page table");
	return true;
}

static bool clone_page_table(plane_paddr_t source_phys,
			     uint8_t level,
			     plane_paddr_t *clone_phys)
{
	plane_paddr_t new_phys;
	uint64_t *source;
	uint64_t *clone;

	source = direct_map_page_table(source_phys);
	if (source == NULL) {
		return false;
	}

	if (!plane_pmm_alloc_pages_phys_flags(1, 1, PLANE_PMM_ALLOC_ZERO,
					      &new_phys)) {
		return false;
	}

	clone = direct_map_page_table(new_phys);
	if (clone == NULL) {
		BUG_ON_MSG(!plane_pmm_free_page_phys(new_phys),
			   "failed to free unreachable page table");
		BUG_ON_MSG(true,
			   "allocated page table is not direct-map reachable: phys=0x%016llx",
			   (unsigned long long)plane_paddr_raw(new_phys));
		return false;
	}

	for (uint64_t i = 0; i < X86_64_PAGING_TABLE_ENTRIES; i++) {
		uint64_t entry = source[i];
		plane_paddr_t child_clone_phys;

		if (!x86_64_paging_entry_present(entry)) {
			continue;
		}

		if (x86_64_paging_entry_leaf(entry, level)) {
			clone[i] = entry;
			continue;
		}

		if (!clone_page_table(plane_paddr_make(x86_64_paging_entry_phys(entry)),
				      level - 1,
				      &child_clone_phys)) {
			if (!free_cloned_page_table(new_phys, level)) {
				return false;
			}
			return false;
		}

		clone[i] = x86_64_paging_entry_flags(entry) |
			   plane_paddr_raw(child_clone_phys);
	}

	*clone_phys = new_phys;
	return true;
}

bool x86_64_pmap_clone_kernel_page_tables(plane_paddr_t source_pml4_phys,
					  plane_paddr_t *new_pml4_phys)
{
	if (new_pml4_phys == NULL ||
	    !plane_paddr_is_page_aligned(source_pml4_phys)) {
		return false;
	}

	return clone_page_table(source_pml4_phys, 4, new_pml4_phys);
}

struct pmap_allocated_table {
	uint64_t *parent;
	uint64_t index;
	plane_paddr_t phys_addr;
};

struct pmap_walk_entry {
	uint64_t *table;
	uint64_t index;
	plane_paddr_t phys_addr;
};

static uint64_t pmap_leaf_size(uint8_t level)
{
	switch (level) {
	case 3:
		return ARCH_HUGE_PAGE_SIZE;
	case 2:
		return ARCH_LARGE_PAGE_SIZE;
	default:
		return PAGE_SIZE;
	}
}

static uint64_t pmap_leaf_phys(uint64_t entry, uint8_t level)
{
	uint64_t leaf_size = pmap_leaf_size(level);

	return x86_64_paging_entry_phys(entry) & ~(leaf_size - 1);
}

static uint64_t pmap_flags_to_entry_flags(uint32_t flags)
{
	uint64_t entry_flags = X86_64_PAGING_ENTRY_PRESENT;

	if ((flags & X86_64_PMAP_WRITE) != 0) {
		entry_flags |= X86_64_PAGING_ENTRY_WRITE;
	}
	if ((flags & X86_64_PMAP_DEVICE) != 0) {
		entry_flags |= X86_64_PAGING_ENTRY_PCD |
			       X86_64_PAGING_ENTRY_PWT;
	}
	if ((flags & X86_64_PMAP_WRITE_COMBINE) != 0) {
		entry_flags |= X86_64_PAGING_ENTRY_PWT;
	}

	return entry_flags;
}

static bool pmap_map_flags_valid(uint32_t flags)
{
	const uint32_t known = X86_64_PMAP_WRITE |
			       X86_64_PMAP_DEVICE |
			       X86_64_PMAP_WRITE_COMBINE;

	if ((flags & ~known) != 0) {
		return false;
	}
	if ((flags & X86_64_PMAP_WRITE_COMBINE) != 0 &&
	    !x86_64_pat_write_combine_ready()) {
		return false;
	}

	return (flags & (X86_64_PMAP_DEVICE |
			 X86_64_PMAP_WRITE_COMBINE)) !=
	       (X86_64_PMAP_DEVICE | X86_64_PMAP_WRITE_COMBINE);
}

static bool pmap_protect_flags_valid(uint32_t flags)
{
	return (flags & ~X86_64_PMAP_WRITE) == 0;
}

static void pmap_free_allocated_tables(struct pmap_allocated_table *tables,
				       uint64_t count)
{
	while (count > 0) {
		struct pmap_allocated_table *table = &tables[count - 1];

		BUG_ON_MSG(!plane_pmm_free_page_phys(table->phys_addr),
			   "failed to rollback page table allocation");
		table->parent[table->index] = 0;
		count--;
	}
}

static bool pmap_table_empty(uint64_t *table)
{
	for (uint64_t i = 0; i < X86_64_PAGING_TABLE_ENTRIES; i++) {
		if (x86_64_paging_entry_present(table[i])) {
			return false;
		}
	}

	return true;
}

static bool pmap_alloc_child_table(uint64_t *parent,
				   uint64_t index,
				   struct pmap_allocated_table *allocated,
				   uint64_t *allocated_count,
				   plane_paddr_t *child_phys)
{
	plane_paddr_t phys_addr;

	if (!plane_pmm_alloc_pages_phys_flags(1, 1, PLANE_PMM_ALLOC_ZERO,
					      &phys_addr)) {
		return false;
	}

	if (direct_map_page_table(phys_addr) == NULL) {
		BUG_ON_MSG(!plane_pmm_free_page_phys(phys_addr),
			   "failed to free unreachable child page table");
		BUG_ON_MSG(true,
			   "allocated child page table is not direct-map reachable: phys=0x%016llx",
			   (unsigned long long)plane_paddr_raw(phys_addr));
		return false;
	}

	parent[index] = x86_64_paging_entry_make(
		plane_paddr_raw(phys_addr),
		X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE);
	allocated[*allocated_count].parent = parent;
	allocated[*allocated_count].index = index;
	allocated[*allocated_count].phys_addr = phys_addr;
	(*allocated_count)++;
	*child_phys = phys_addr;
	return true;
}

bool x86_64_pmap_map_page_in_owned_root(plane_paddr_t root_pml4_phys,
					plane_vaddr_t vaddr,
					plane_paddr_t phys_addr,
					uint32_t flags)
{
	struct pmap_allocated_table allocated[3];
	uint64_t allocated_count = 0;
	plane_paddr_t current_phys = root_pml4_phys;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t raw_phys = plane_paddr_raw(phys_addr);
	uint64_t *table;

	if (!plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_paddr_is_page_aligned(phys_addr) ||
	    !pmap_map_flags_valid(flags) ||
	    !plane_paddr_is_page_aligned(root_pml4_phys)) {
		return false;
	}

	for (uint8_t level = 4; level > 1; level--) {
		uint64_t index;
		uint64_t entry;
		plane_paddr_t child_phys;

		table = direct_map_page_table(current_phys);
		if (table == NULL) {
			pmap_free_allocated_tables(allocated, allocated_count);
			return false;
		}

		index = x86_64_paging_index(level, raw_vaddr);
		entry = table[index];
		if (x86_64_paging_entry_present(entry)) {
			if (x86_64_paging_entry_leaf(entry, level)) {
				pmap_free_allocated_tables(allocated,
							   allocated_count);
				return false;
			}
			current_phys = plane_paddr_make(
				x86_64_paging_entry_phys(entry));
			continue;
		}

		if (!pmap_alloc_child_table(table, index, allocated,
					    &allocated_count, &child_phys)) {
			pmap_free_allocated_tables(allocated, allocated_count);
			return false;
		}
		current_phys = child_phys;
	}

	table = direct_map_page_table(current_phys);
	if (table == NULL) {
		pmap_free_allocated_tables(allocated, allocated_count);
		return false;
	}

	uint64_t pt_index = x86_64_paging_index(1, raw_vaddr);

	if (x86_64_paging_entry_present(table[pt_index])) {
		pmap_free_allocated_tables(allocated, allocated_count);
		return false;
	}

	table[pt_index] =
		x86_64_paging_entry_make(raw_phys,
					 pmap_flags_to_entry_flags(flags));
	return true;
}

bool x86_64_pmap_unmap_page_in_owned_root(plane_paddr_t root_pml4_phys,
					  plane_vaddr_t vaddr)
{
	plane_paddr_t current_phys = root_pml4_phys;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	struct pmap_walk_entry path[4];
	uint64_t depth = 0;

	if (!plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_paddr_is_page_aligned(root_pml4_phys)) {
		return false;
	}

	for (uint8_t level = 4; level > 1; level--) {
		uint64_t *table = direct_map_page_table(current_phys);
		uint64_t index;
		uint64_t entry;

		if (table == NULL) {
			return false;
		}

		index = x86_64_paging_index(level, raw_vaddr);
		path[depth].table = table;
		path[depth].index = index;
		path[depth].phys_addr = current_phys;
		depth++;

		entry = table[index];
		if (!x86_64_paging_entry_present(entry) ||
		    x86_64_paging_entry_leaf(entry, level)) {
			return false;
		}

		current_phys = plane_paddr_make(
			x86_64_paging_entry_phys(entry));
	}

	uint64_t *table = direct_map_page_table(current_phys);
	uint64_t pt_index = x86_64_paging_index(1, raw_vaddr);

	if (table == NULL ||
	    !x86_64_paging_entry_present(table[pt_index])) {
		return false;
	}

	path[depth].table = table;
	path[depth].index = pt_index;
	path[depth].phys_addr = current_phys;
	depth++;

	table[pt_index] = 0;

	for (uint64_t i = depth - 1; i > 0; i--) {
		struct pmap_walk_entry *child = &path[i];
		struct pmap_walk_entry *parent = &path[i - 1];

		if (!pmap_table_empty(child->table)) {
			break;
		}

		BUG_ON_MSG(!plane_pmm_free_page_phys(child->phys_addr),
			   "failed to free empty page table");
		parent->table[parent->index] = 0;
	}

	return true;
}

bool x86_64_pmap_translate_in_root(plane_paddr_t root_pml4_phys,
				   plane_vaddr_t vaddr,
				   plane_paddr_t *phys_addr)
{
	plane_paddr_t current_phys = root_pml4_phys;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);

	if (phys_addr == NULL || !plane_paddr_is_page_aligned(root_pml4_phys)) {
		return false;
	}

	for (uint8_t level = 4; level > 0; level--) {
		uint64_t *table = direct_map_page_table(current_phys);
		uint64_t entry;

		if (table == NULL) {
			return false;
		}

		entry = table[x86_64_paging_index(level, raw_vaddr)];
		if (!x86_64_paging_entry_present(entry)) {
			return false;
		}

		if (x86_64_paging_entry_leaf(entry, level)) {
			uint64_t leaf_size = pmap_leaf_size(level);
			uint64_t offset = raw_vaddr & (leaf_size - 1);

			*phys_addr = plane_paddr_make(
				pmap_leaf_phys(entry, level) + offset);
			return true;
		}

		current_phys = plane_paddr_make(
			x86_64_paging_entry_phys(entry));
	}

	return false;
}

bool x86_64_pmap_map_kernel_page(plane_vaddr_t vaddr,
				 plane_paddr_t phys_addr,
				 uint32_t flags)
{
	if (!x86_64_pmap_map_page_in_owned_root(pmap_current_root_phys(),
						vaddr, phys_addr, flags)) {
		return false;
	}

	hal_mmu_invalidate_tlb(vaddr);
	return true;
}

bool x86_64_pmap_unmap_kernel_page(plane_vaddr_t vaddr)
{
	if (!x86_64_pmap_unmap_page_in_owned_root(pmap_current_root_phys(),
						  vaddr)) {
		return false;
	}

	hal_mmu_invalidate_tlb(vaddr);
	return true;
}

bool x86_64_pmap_translate_kernel_page(plane_vaddr_t vaddr,
				       plane_paddr_t *phys_addr)
{
	return x86_64_pmap_translate_in_root(pmap_current_root_phys(),
					     vaddr, phys_addr);
}

bool x86_64_pmap_protect_kernel_page(plane_vaddr_t vaddr, uint32_t flags)
{
	plane_paddr_t current_phys = pmap_current_root_phys();
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t *table;
	uint64_t entry;
	uint64_t index;

	if (!plane_vaddr_is_page_aligned(vaddr) ||
	    !pmap_protect_flags_valid(flags)) {
		return false;
	}

	for (uint8_t level = 4; level > 1; level--) {
		table = direct_map_page_table(current_phys);
		if (table == NULL) {
			return false;
		}

		index = x86_64_paging_index(level, raw_vaddr);
		entry = table[index];
		if (!x86_64_paging_entry_present(entry) ||
		    x86_64_paging_entry_leaf(entry, level)) {
			return false;
		}

		current_phys = plane_paddr_make(
			x86_64_paging_entry_phys(entry));
	}

	table = direct_map_page_table(current_phys);
	if (table == NULL) {
		return false;
	}

	index = x86_64_paging_index(1, raw_vaddr);
	entry = table[index];
	if (!x86_64_paging_entry_present(entry)) {
		return false;
	}

	entry &= ~X86_64_PAGING_ENTRY_WRITE;
	if ((flags & X86_64_PMAP_WRITE) != 0) {
		entry |= X86_64_PAGING_ENTRY_WRITE;
	}
	table[index] = entry;
	hal_mmu_invalidate_tlb(vaddr);
	return true;
}

bool hal_mmu_map_kernel_page(plane_vaddr_t vaddr,
			     plane_paddr_t phys_addr,
			     uint32_t flags)
{
	uint32_t pmap_flags = 0;

	if ((flags & ~(HAL_MMU_MAP_WRITE |
		       HAL_MMU_MAP_DEVICE |
		       HAL_MMU_MAP_WRITE_COMBINE)) != 0 ||
	    (flags & (HAL_MMU_MAP_DEVICE |
		      HAL_MMU_MAP_WRITE_COMBINE)) ==
		    (HAL_MMU_MAP_DEVICE | HAL_MMU_MAP_WRITE_COMBINE)) {
		return false;
	}
	if ((flags & HAL_MMU_MAP_WRITE) != 0) {
		pmap_flags |= X86_64_PMAP_WRITE;
	}
	if ((flags & HAL_MMU_MAP_DEVICE) != 0) {
		pmap_flags |= X86_64_PMAP_DEVICE;
	}
	if ((flags & HAL_MMU_MAP_WRITE_COMBINE) != 0) {
		pmap_flags |= X86_64_PMAP_WRITE_COMBINE;
	}

	return x86_64_pmap_map_kernel_page(vaddr, phys_addr, pmap_flags);
}

bool hal_mmu_unmap_kernel_page(plane_vaddr_t vaddr)
{
	return x86_64_pmap_unmap_kernel_page(vaddr);
}

bool hal_mmu_translate_kernel_page(plane_vaddr_t vaddr, plane_paddr_t *phys_addr)
{
	return x86_64_pmap_translate_kernel_page(vaddr, phys_addr);
}

bool hal_mmu_protect_kernel_page(plane_vaddr_t vaddr, uint32_t flags)
{
	uint32_t pmap_flags = 0;

	if ((flags & ~HAL_MMU_MAP_WRITE) != 0) {
		return false;
	}
	if ((flags & HAL_MMU_MAP_WRITE) != 0) {
		pmap_flags |= X86_64_PMAP_WRITE;
	}

	return x86_64_pmap_protect_kernel_page(vaddr, pmap_flags);
}

bool hal_mmu_take_kernel_page_table_ownership(void)
{
	plane_paddr_t new_pml4_phys;

	if (!x86_64_pmap_clone_kernel_page_tables(pmap_current_root_phys(),
						  &new_pml4_phys)) {
		return false;
	}

	write_cr3_phys(new_pml4_phys);
	hal_mmu_flush_tlb_all();
	return true;
}
