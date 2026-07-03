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

static uint64_t page_table_replace_phys(uint64_t entry, uint64_t phys_addr)
{
	return (entry & ~X86_64_PAGE_ENTRY_ADDR_MASK) | phys_addr;
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
