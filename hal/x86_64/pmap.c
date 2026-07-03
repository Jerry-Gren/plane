#include <stddef.h>

#include <hal/mmu.h>
#include <hal/x86_64/arch_mmu.h>
#include <hal/x86_64/pmap.h>
#include <plane/mm.h>
#include <plane/pmm.h>

static uint64_t read_cr3_phys(void)
{
	uint64_t cr3;

	__asm__ volatile ("mov %%cr3, %0" : "=r" (cr3));
	return cr3 & X86_64_PAGE_ENTRY_ADDR_MASK;
}

static void write_cr3_phys(uint64_t phys_addr)
{
	__asm__ volatile ("mov %0, %%cr3" : : "r" (phys_addr) : "memory");
}

static bool page_table_entry_present(uint64_t entry)
{
	return (entry & PAGE_PRESENT) != 0;
}

static bool page_table_entry_is_leaf(uint64_t entry, uint8_t level)
{
	return level == 1 || (level < 4 && (entry & PAGE_PS) != 0);
}

static uint64_t page_table_entry_phys(uint64_t entry)
{
	return entry & X86_64_PAGE_ENTRY_ADDR_MASK;
}

static uint64_t page_table_entry_flags(uint64_t entry)
{
	return entry & ~X86_64_PAGE_ENTRY_ADDR_MASK;
}

static uint64_t page_table_replace_phys(uint64_t entry, uint64_t phys_addr)
{
	return page_table_entry_flags(entry) | phys_addr;
}

static uint64_t page_table_entry_make(uint64_t phys_addr, uint64_t flags)
{
	return (phys_addr & X86_64_PAGE_ENTRY_ADDR_MASK) | flags;
}

static uint64_t *direct_map_page_table(uint64_t phys_addr)
{
	return hal_mmu_direct_phys_range_to_virt(phys_addr, PAGE_SIZE);
}

static bool free_cloned_page_table(uint64_t table_phys, uint8_t level)
{
	uint64_t *table = direct_map_page_table(table_phys);

	if (table == NULL) {
		return false;
	}

	for (uint64_t i = 0; i < X86_64_PAGE_TABLE_ENTRIES; i++) {
		uint64_t entry = table[i];

		if (!page_table_entry_present(entry) ||
		    page_table_entry_is_leaf(entry, level)) {
			continue;
		}

		if (!free_cloned_page_table(page_table_entry_phys(entry),
					    level - 1)) {
			return false;
		}
	}

	return plane_pmm_free_page_phys(table_phys);
}

static bool clone_page_table(uint64_t source_phys,
			     uint8_t level,
			     uint64_t *clone_phys)
{
	uint64_t new_phys;
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
		if (!plane_pmm_free_page_phys(new_phys)) {
			return false;
		}
		return false;
	}

	for (uint64_t i = 0; i < X86_64_PAGE_TABLE_ENTRIES; i++) {
		uint64_t entry = source[i];
		uint64_t child_clone_phys;

		if (!page_table_entry_present(entry)) {
			continue;
		}

		if (page_table_entry_is_leaf(entry, level)) {
			clone[i] = entry;
			continue;
		}

		if (!clone_page_table(page_table_entry_phys(entry), level - 1,
				      &child_clone_phys)) {
			if (!free_cloned_page_table(new_phys, level)) {
				return false;
			}
			return false;
		}

		clone[i] = page_table_replace_phys(entry, child_clone_phys);
	}

	*clone_phys = new_phys;
	return true;
}

bool x86_64_pmap_clone_kernel_page_tables(uint64_t source_pml4_phys,
					  uint64_t *new_pml4_phys)
{
	if (new_pml4_phys == NULL ||
	    (source_pml4_phys & (PAGE_SIZE - 1)) != 0) {
		return false;
	}

	return clone_page_table(source_pml4_phys, 4, new_pml4_phys);
}

struct pmap_allocated_table {
	uint64_t *parent;
	uint64_t index;
	uint64_t phys_addr;
};

static bool pmap_vaddr_aligned(uint64_t vaddr)
{
	return (vaddr & (PAGE_SIZE - 1)) == 0;
}

static bool pmap_phys_aligned(uint64_t phys_addr)
{
	return (phys_addr & (PAGE_SIZE - 1)) == 0;
}

static uint64_t pmap_level_index(uint64_t vaddr, uint8_t level)
{
	switch (level) {
	case 4:
		return PML4_INDEX(vaddr);
	case 3:
		return PDPT_INDEX(vaddr);
	case 2:
		return PD_INDEX(vaddr);
	default:
		return PT_INDEX(vaddr);
	}
}

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

	return page_table_entry_phys(entry) & ~(leaf_size - 1);
}

static uint64_t pmap_flags_to_entry_flags(uint32_t flags)
{
	uint64_t entry_flags = PAGE_PRESENT;

	if ((flags & X86_64_PMAP_WRITE) != 0) {
		entry_flags |= PAGE_RW;
	}

	return entry_flags;
}

static bool pmap_flags_valid(uint32_t flags)
{
	return (flags & ~X86_64_PMAP_WRITE) == 0;
}

static bool pmap_free_allocated_tables(struct pmap_allocated_table *tables,
				       uint64_t count)
{
	while (count > 0) {
		struct pmap_allocated_table *table = &tables[count - 1];

		table->parent[table->index] = 0;
		if (!plane_pmm_free_page_phys(table->phys_addr)) {
			return false;
		}
		count--;
	}

	return true;
}

static bool pmap_alloc_child_table(uint64_t *parent,
				   uint64_t index,
				   struct pmap_allocated_table *allocated,
				   uint64_t *allocated_count,
				   uint64_t *child_phys)
{
	uint64_t phys_addr;

	if (!plane_pmm_alloc_pages_phys_flags(1, 1, PLANE_PMM_ALLOC_ZERO,
					      &phys_addr)) {
		return false;
	}

	if (direct_map_page_table(phys_addr) == NULL) {
		if (!plane_pmm_free_page_phys(phys_addr)) {
			return false;
		}
		return false;
	}

	parent[index] = page_table_entry_make(phys_addr,
					      PAGE_PRESENT | PAGE_RW);
	allocated[*allocated_count].parent = parent;
	allocated[*allocated_count].index = index;
	allocated[*allocated_count].phys_addr = phys_addr;
	(*allocated_count)++;
	*child_phys = phys_addr;
	return true;
}

bool x86_64_pmap_map_page(uint64_t root_pml4_phys,
			  uint64_t vaddr,
			  uint64_t phys_addr,
			  uint32_t flags)
{
	struct pmap_allocated_table allocated[3];
	uint64_t allocated_count = 0;
	uint64_t current_phys = root_pml4_phys;
	uint64_t *table;

	if (!pmap_vaddr_aligned(vaddr) ||
	    !pmap_phys_aligned(phys_addr) ||
	    !pmap_flags_valid(flags) ||
	    !pmap_phys_aligned(root_pml4_phys)) {
		return false;
	}

	for (uint8_t level = 4; level > 1; level--) {
		uint64_t index;
		uint64_t entry;
		uint64_t child_phys;

		table = direct_map_page_table(current_phys);
		if (table == NULL) {
			if (!pmap_free_allocated_tables(allocated,
							allocated_count)) {
				return false;
			}
			return false;
		}

		index = pmap_level_index(vaddr, level);
		entry = table[index];
		if (page_table_entry_present(entry)) {
			if (page_table_entry_is_leaf(entry, level)) {
				if (!pmap_free_allocated_tables(allocated,
								allocated_count)) {
					return false;
				}
				return false;
			}
			current_phys = page_table_entry_phys(entry);
			continue;
		}

		if (!pmap_alloc_child_table(table, index, allocated,
					    &allocated_count, &child_phys)) {
			if (!pmap_free_allocated_tables(allocated,
							allocated_count)) {
				return false;
			}
			return false;
		}
		current_phys = child_phys;
	}

	table = direct_map_page_table(current_phys);
	if (table == NULL) {
		if (!pmap_free_allocated_tables(allocated, allocated_count)) {
			return false;
		}
		return false;
	}

	if (page_table_entry_present(table[PT_INDEX(vaddr)])) {
		if (!pmap_free_allocated_tables(allocated, allocated_count)) {
			return false;
		}
		return false;
	}

	table[PT_INDEX(vaddr)] =
		page_table_entry_make(phys_addr, pmap_flags_to_entry_flags(flags));
	hal_mmu_invalidate_tlb((uintptr_t)vaddr);
	return true;
}

bool x86_64_pmap_unmap_page(uint64_t root_pml4_phys, uint64_t vaddr)
{
	uint64_t current_phys = root_pml4_phys;

	if (!pmap_vaddr_aligned(vaddr) || !pmap_phys_aligned(root_pml4_phys)) {
		return false;
	}

	for (uint8_t level = 4; level > 1; level--) {
		uint64_t *table = direct_map_page_table(current_phys);
		uint64_t entry;

		if (table == NULL) {
			return false;
		}

		entry = table[pmap_level_index(vaddr, level)];
		if (!page_table_entry_present(entry) ||
		    page_table_entry_is_leaf(entry, level)) {
			return false;
		}

		current_phys = page_table_entry_phys(entry);
	}

	uint64_t *table = direct_map_page_table(current_phys);
	if (table == NULL ||
	    !page_table_entry_present(table[PT_INDEX(vaddr)])) {
		return false;
	}

	table[PT_INDEX(vaddr)] = 0;
	hal_mmu_invalidate_tlb((uintptr_t)vaddr);
	return true;
}

bool x86_64_pmap_translate(uint64_t root_pml4_phys,
			   uint64_t vaddr,
			   uint64_t *phys_addr)
{
	uint64_t current_phys = root_pml4_phys;

	if (phys_addr == NULL || !pmap_phys_aligned(root_pml4_phys)) {
		return false;
	}

	for (uint8_t level = 4; level > 0; level--) {
		uint64_t *table = direct_map_page_table(current_phys);
		uint64_t entry;

		if (table == NULL) {
			return false;
		}

		entry = table[pmap_level_index(vaddr, level)];
		if (!page_table_entry_present(entry)) {
			return false;
		}

		if (page_table_entry_is_leaf(entry, level)) {
			uint64_t leaf_size = pmap_leaf_size(level);
			uint64_t offset = vaddr & (leaf_size - 1);

			*phys_addr = pmap_leaf_phys(entry, level) + offset;
			return true;
		}

		current_phys = page_table_entry_phys(entry);
	}

	return false;
}

bool hal_mmu_take_kernel_page_table_ownership(void)
{
	uint64_t new_pml4_phys;

	if (!x86_64_pmap_clone_kernel_page_tables(read_cr3_phys(),
						  &new_pml4_phys)) {
		return false;
	}

	write_cr3_phys(new_pml4_phys);
	hal_mmu_flush_tlb_all();
	return true;
}
