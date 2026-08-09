#include <stddef.h>

#include <hal/mmu.h>
#include <hal/x86_64/address_space.h>
#include <hal/x86_64/pat.h>
#include <hal/x86_64/pmap.h>
#include <plane/compiler.h>
#include <plane/mm.h>
#include <plane/overflow.h>
#include <plane/printk.h>
#include <plane/pmm.h>
#include <plane/spinlock.h>
#include <plane/util.h>
#include <plane/vm_prot.h>

#include <x86_64/physmap_internal.h>
#include <x86_64/pmap_internal.h>

/*
 * Protects active kernel page-table mutation and active-root translation
 * snapshots. Owned-root helpers below are caller-owned/startup-only and do not
 * take this lock. Current lock order: pmap -> PMM -> VM page.
 */
static struct plane_spinlock kernel_pmap_lock = PLANE_SPINLOCK_INIT;

static plane_irq_state_t lock_pmap(void)
{
	return plane_spin_lock_irqsave(&kernel_pmap_lock);
}

static void unlock_pmap(plane_irq_state_t state)
{
	plane_spin_unlock_irqrestore(&kernel_pmap_lock, state);
}

plane_paddr_t __weak x86_64_pmap_current_root_phys(void)
{
	uint64_t cr3;

	__asm__ volatile ("mov %%cr3, %0" : "=r" (cr3));
	return plane_paddr_make(cr3 & X86_64_PAGING_ENTRY_ADDR_MASK);
}

bool hal_mmu_kernel_vma_range(plane_vaddr_t *base, uint64_t *size)
{
	if (base == NULL || size == NULL) {
		return false;
	}

	*base = plane_vaddr_make(X86_64_KERNEL_MAP_BASE);
	*size = X86_64_KERNEL_MAP_SIZE;
	return true;
}

void __weak hal_mmu_invalidate_tlb(plane_vaddr_t vaddr)
{
	/*
	 * INVLPG invalidates cached translations for one linear address on the
	 * current CPU. Cross-CPU shootdown comes with the later SMP pmap path.
	 */
	__asm__ volatile ("invlpg (%0)" : : "r" (plane_vaddr_raw(vaddr)) : "memory");
}

void __weak hal_mmu_flush_tlb_all(void)
{
	__asm__ volatile (
		"mov %%cr3, %%rax\n\t"
		"mov %%rax, %%cr3\n\t"
		: /* no input */
		: /* no output */
		: "rax", "memory"
	);
}

static void pmap_assert_page_table_phys(plane_paddr_t phys_addr)
{
	BUG_ON_MSG(plane_paddr_is_null(phys_addr),
		   "PMM allocated null physical page for page table");
}

static void write_cr3_phys(plane_paddr_t phys_addr)
{
	/*
	 * CR3 holds the physical base of the active top-level paging
	 * structure. Plane treats physical zero as reserved/null, so reaching
	 * this point with a null paddr means PMM ownership policy was violated.
	 */
	pmap_assert_page_table_phys(phys_addr);
	__asm__ volatile ("mov %0, %%cr3" : : "r" (plane_paddr_raw(phys_addr)) : "memory");
}

static uint64_t *pmap_table_from_phys(plane_paddr_t phys_addr)
{
	plane_vaddr_t vaddr =
		hal_mmu_physmap_phys_range_to_virt(phys_addr, PAGE_SIZE);

	if (plane_vaddr_is_null(vaddr)) {
		return NULL;
	}

	return plane_vaddr_to_ptr(vaddr);
}

static bool pmap_alloc_zero_table(plane_paddr_t *phys_addr, uint64_t **table)
{
	plane_paddr_t new_phys;
	uint64_t *new_table;

	if (phys_addr == NULL || table == NULL ||
	    !plane_pmm_alloc_pages_phys_flags(1, 1, PLANE_PMM_ALLOC_ZERO,
					      &new_phys)) {
		return false;
	}
	pmap_assert_page_table_phys(new_phys);

	new_table = pmap_table_from_phys(new_phys);
	if (new_table == NULL) {
		BUG_ON_MSG(!plane_pmm_free_page_phys(new_phys),
			   "failed to free unreachable page table");
		BUG_ON_MSG(true,
			   "allocated page table is not physmap reachable: phys=0x%016llx",
			   (unsigned long long)plane_paddr_raw(new_phys));
		return false;
	}

	*phys_addr = new_phys;
	*table = new_table;
	return true;
}

static bool pmap_skip_range_contains_pml4(const struct x86_64_pmap_skip_range *range,
					  uint64_t index)
{
	uint64_t start;
	uint64_t end;
	uint64_t start_index;
	uint64_t end_index;

	if (range == NULL || range->size == 0) {
		return false;
	}

	start = plane_vaddr_raw(range->base);
	if (!plane_checked_add_u64(start, range->size - 1, &end)) {
		return false;
	}

	start_index = X86_64_PAGING_PML4_INDEX(start);
	end_index = X86_64_PAGING_PML4_INDEX(end);
	if (start_index <= end_index) {
		return index >= start_index && index <= end_index;
	}

	return index >= start_index || index <= end_index;
}

static bool pmap_clone_should_skip_pml4(
	const struct x86_64_pmap_skip_range *skip,
	uint64_t skip_count,
	uint64_t index)
{
	for (uint64_t i = 0; i < skip_count; i++) {
		if (pmap_skip_range_contains_pml4(&skip[i], index)) {
			return true;
		}
	}

	return false;
}

static bool pmap_skip_ranges_are_valid(const struct x86_64_pmap_skip_range *skip,
				   uint64_t skip_count)
{
	for (uint64_t i = 0; i < skip_count; i++) {
		uint64_t end;

		if (skip[i].size == 0) {
			continue;
		}
		if (!plane_checked_add_u64(plane_vaddr_raw(skip[i].base),
					   skip[i].size - 1, &end)) {
			return false;
		}
	}

	return true;
}

static bool free_cloned_page_table(plane_paddr_t table_phys, uint8_t level)
{
	uint64_t *table = pmap_table_from_phys(table_phys);

	if (table == NULL) {
		return false;
	}

	for (uint64_t i = 0; i < X86_64_PAGING_TABLE_ENTRIES; i++) {
		uint64_t entry = table[i];

		if (!x86_64_paging_entry_is_present(entry) ||
		    x86_64_paging_entry_is_leaf(entry, level)) {
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
			     const struct x86_64_pmap_skip_range *skip,
			     uint64_t skip_count,
			     plane_paddr_t *clone_phys)
{
	plane_paddr_t new_phys;
	uint64_t *source;
	uint64_t *clone;

	source = pmap_table_from_phys(source_phys);
	if (source == NULL) {
		return false;
	}

	if (!pmap_alloc_zero_table(&new_phys, &clone)) {
		return false;
	}

	for (uint64_t i = 0; i < X86_64_PAGING_TABLE_ENTRIES; i++) {
		uint64_t entry = source[i];
		plane_paddr_t child_clone_phys;

		if (level == 4 &&
		    pmap_clone_should_skip_pml4(skip, skip_count, i)) {
			continue;
		}

		if (!x86_64_paging_entry_is_present(entry)) {
			continue;
		}

		if (x86_64_paging_entry_is_leaf(entry, level)) {
			clone[i] = entry;
			continue;
		}

		if (!clone_page_table(plane_paddr_make(x86_64_paging_entry_phys(entry)),
				      level - 1, skip, skip_count,
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
					  const struct x86_64_pmap_skip_range *skip,
					  uint64_t skip_count,
					  plane_paddr_t *new_pml4_phys)
{
	if (new_pml4_phys == NULL ||
	    (skip_count > 0 && skip == NULL) ||
	    !pmap_skip_ranges_are_valid(skip, skip_count) ||
	    !plane_paddr_is_page_aligned(source_pml4_phys)) {
		return false;
	}

	return clone_page_table(source_pml4_phys, 4, skip, skip_count,
				new_pml4_phys);
}

static bool pmap_pml4_index_range(plane_vaddr_t base,
				  uint64_t size,
				  uint64_t *start_index,
				  uint64_t *end_index)
{
	uint64_t raw_base;
	uint64_t raw_end;

	if (start_index == NULL || end_index == NULL || size == 0) {
		return false;
	}

	raw_base = plane_vaddr_raw(base);
	if (!plane_checked_add_u64(raw_base, size - 1, &raw_end)) {
		return false;
	}

	*start_index = X86_64_PAGING_PML4_INDEX(raw_base);
	*end_index = X86_64_PAGING_PML4_INDEX(raw_end);
	return *start_index <= *end_index;
}

static bool pmap_physmap_pml4_range_is_available(uint64_t *root,
					      plane_vaddr_t base,
					      uint64_t size)
{
	uint64_t start_index;
	uint64_t end_index;

	if (!pmap_pml4_index_range(base, size, &start_index, &end_index)) {
		return false;
	}

	for (uint64_t i = start_index; i <= end_index; i++) {
		if (x86_64_paging_entry_is_present(root[i])) {
			return false;
		}
	}

	return true;
}

static bool pmap_mapping_attr_is_valid(enum hal_mmu_mapping_attr attr)
{
	return attr == HAL_MMU_MAPPING_DEFAULT ||
	       attr == HAL_MMU_MAPPING_DEVICE ||
	       attr == HAL_MMU_MAPPING_WRITE_COMBINE;
}

static bool pmap_map_options_are_valid(struct hal_mmu_map_options options)
{
	if (!plane_vm_prot_is_valid(options.prot) ||
	    !pmap_mapping_attr_is_valid(options.attr)) {
		return false;
	}
	return options.attr != HAL_MMU_MAPPING_WRITE_COMBINE ||
	       x86_64_pat_write_combine_is_ready();
}

static uint64_t pmap_options_to_entry_flags(
	struct hal_mmu_map_options options)
{
	uint64_t entry_flags = X86_64_PAGING_ENTRY_PRESENT;

	if ((options.prot & PLANE_VM_PROT_WRITE) != 0) {
		entry_flags |= X86_64_PAGING_ENTRY_WRITE;
	}
	if (options.attr == HAL_MMU_MAPPING_DEVICE) {
		entry_flags |= X86_64_PAGING_ENTRY_PCD |
			       X86_64_PAGING_ENTRY_PWT;
	}
	if (options.attr == HAL_MMU_MAPPING_WRITE_COMBINE) {
		entry_flags |= X86_64_PAGING_ENTRY_PWT;
	}

	return entry_flags;
}

static void pmap_physmap_rollback(uint64_t *root,
				  plane_vaddr_t base,
				  uint64_t size)
{
	uint64_t start_index;
	uint64_t end_index;

	if (!pmap_pml4_index_range(base, size, &start_index, &end_index)) {
		return;
	}

	for (uint64_t i = start_index; i <= end_index; i++) {
		uint64_t entry = root[i];

		if (!x86_64_paging_entry_is_present(entry)) {
			continue;
		}

		BUG_ON_MSG(x86_64_paging_entry_is_leaf(entry, 4),
			   "physmap rollback found leaf PML4 entry");
		BUG_ON_MSG(!free_cloned_page_table(
				   plane_paddr_make(x86_64_paging_entry_phys(entry)),
				   3),
			   "failed to rollback physmap page tables");
		root[i] = 0;
	}
}

bool x86_64_pmap_build_physmap_in_owned_root(plane_paddr_t root_pml4_phys,
					     plane_vaddr_t base,
					     uint64_t required_size,
					     uint64_t window_size)
{
	uint64_t *root;
	uint64_t *pdpt = NULL;
	uint64_t current_pml4_index = UINT64_MAX;
	uint64_t mapped = 0;
	struct hal_mmu_map_options options = hal_mmu_default_map_options(
		PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE);

	if (!plane_paddr_is_page_aligned(root_pml4_phys) ||
	    !plane_vaddr_is_page_aligned(base) ||
	    plane_vaddr_raw(base) != X86_64_PHYSMAP_BASE ||
	    required_size > window_size ||
	    window_size > X86_64_PHYSMAP_WINDOW_SIZE ||
	    (required_size & (ARCH_LARGE_PAGE_SIZE - 1)) != 0 ||
	    (window_size & (X86_64_PAGING_PML4_SLOT_SIZE - 1)) != 0) {
		return false;
	}
	if (required_size == 0) {
		return true;
	}

	root = pmap_table_from_phys(root_pml4_phys);
	if (root == NULL ||
	    !pmap_physmap_pml4_range_is_available(root, base, window_size)) {
		return false;
	}

	while (mapped < required_size) {
		uint64_t raw_vaddr;
		uint64_t pml4_index;
		uint64_t pdpt_index;
		uint64_t *pd;
		plane_paddr_t pd_phys;

		if (!plane_checked_add_u64(plane_vaddr_raw(base), mapped,
					   &raw_vaddr)) {
			pmap_physmap_rollback(root, base, window_size);
			return false;
		}

		pml4_index = X86_64_PAGING_PML4_INDEX(raw_vaddr);
		if (pml4_index != current_pml4_index) {
			plane_paddr_t pdpt_phys;

			if (!pmap_alloc_zero_table(&pdpt_phys, &pdpt)) {
				pmap_physmap_rollback(root, base, window_size);
				return false;
			}
			root[pml4_index] = x86_64_paging_entry_make(
				plane_paddr_raw(pdpt_phys),
				X86_64_PAGING_ENTRY_PRESENT |
				X86_64_PAGING_ENTRY_WRITE);
			current_pml4_index = pml4_index;
		}

		pdpt_index = X86_64_PAGING_PDPT_INDEX(raw_vaddr);
		if (!pmap_alloc_zero_table(&pd_phys, &pd)) {
			pmap_physmap_rollback(root, base, window_size);
			return false;
		}
		pdpt[pdpt_index] = x86_64_paging_entry_make(
			plane_paddr_raw(pd_phys),
			X86_64_PAGING_ENTRY_PRESENT |
			X86_64_PAGING_ENTRY_WRITE);

		for (uint64_t pd_index = X86_64_PAGING_PD_INDEX(raw_vaddr);
		     pd_index < X86_64_PAGING_TABLE_ENTRIES &&
		     mapped < required_size;
		     pd_index++) {
			pd[pd_index] = x86_64_paging_entry_make(
				mapped,
				pmap_options_to_entry_flags(options) |
				X86_64_PAGING_ENTRY_PS);
			mapped += ARCH_LARGE_PAGE_SIZE;
		}
	}

	return true;
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

static bool pmap_table_is_empty(uint64_t *table)
{
	for (uint64_t i = 0; i < X86_64_PAGING_TABLE_ENTRIES; i++) {
		if (x86_64_paging_entry_is_present(table[i])) {
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
	uint64_t *table;

	if (!pmap_alloc_zero_table(&phys_addr, &table)) {
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
					struct hal_mmu_map_options options)
{
	struct pmap_allocated_table allocated[3];
	uint64_t allocated_count = 0;
	plane_paddr_t current_phys = root_pml4_phys;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t raw_phys = plane_paddr_raw(phys_addr);
	uint64_t *table;

	if (!plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_paddr_is_page_aligned(phys_addr) ||
	    !pmap_map_options_are_valid(options) ||
	    !plane_paddr_is_page_aligned(root_pml4_phys)) {
		return false;
	}

	for (uint8_t level = 4; level > 1; level--) {
		uint64_t index;
		uint64_t entry;
		plane_paddr_t child_phys;

		table = pmap_table_from_phys(current_phys);
		if (table == NULL) {
			pmap_free_allocated_tables(allocated, allocated_count);
			return false;
		}

		index = x86_64_paging_index(level, raw_vaddr);
		entry = table[index];
		if (x86_64_paging_entry_is_present(entry)) {
			if (x86_64_paging_entry_is_leaf(entry, level)) {
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

	table = pmap_table_from_phys(current_phys);
	if (table == NULL) {
		pmap_free_allocated_tables(allocated, allocated_count);
		return false;
	}

	uint64_t pt_index = x86_64_paging_index(1, raw_vaddr);

	if (x86_64_paging_entry_is_present(table[pt_index])) {
		pmap_free_allocated_tables(allocated, allocated_count);
		return false;
	}

	table[pt_index] =
		x86_64_paging_entry_make(raw_phys,
					 pmap_options_to_entry_flags(options));
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
		uint64_t *table = pmap_table_from_phys(current_phys);
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
		if (!x86_64_paging_entry_is_present(entry) ||
		    x86_64_paging_entry_is_leaf(entry, level)) {
			return false;
		}

		current_phys = plane_paddr_make(
			x86_64_paging_entry_phys(entry));
	}

	uint64_t *table = pmap_table_from_phys(current_phys);
	uint64_t pt_index = x86_64_paging_index(1, raw_vaddr);

	if (table == NULL ||
	    !x86_64_paging_entry_is_present(table[pt_index])) {
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

		if (!pmap_table_is_empty(child->table)) {
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
		uint64_t *table = pmap_table_from_phys(current_phys);
		uint64_t entry;

		if (table == NULL) {
			return false;
		}

		entry = table[x86_64_paging_index(level, raw_vaddr)];
		if (!x86_64_paging_entry_is_present(entry)) {
			return false;
		}

		if (x86_64_paging_entry_is_leaf(entry, level)) {
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

bool x86_64_pmap_protect_page_in_owned_root(plane_paddr_t root_pml4_phys,
					    plane_vaddr_t vaddr,
					    uint32_t prot)
{
	plane_paddr_t current_phys = root_pml4_phys;
	uint64_t raw_vaddr = plane_vaddr_raw(vaddr);
	uint64_t *table;
	uint64_t entry;
	uint64_t index;

	if (!plane_vaddr_is_page_aligned(vaddr) ||
	    !plane_paddr_is_page_aligned(root_pml4_phys) ||
	    !plane_vm_prot_is_valid(prot)) {
		return false;
	}

	for (uint8_t level = 4; level > 1; level--) {
		table = pmap_table_from_phys(current_phys);
		if (table == NULL) {
			return false;
		}

		index = x86_64_paging_index(level, raw_vaddr);
		entry = table[index];
		if (!x86_64_paging_entry_is_present(entry) ||
		    x86_64_paging_entry_is_leaf(entry, level)) {
			return false;
		}

		current_phys = plane_paddr_make(
			x86_64_paging_entry_phys(entry));
	}

	table = pmap_table_from_phys(current_phys);
	if (table == NULL) {
		return false;
	}

	index = x86_64_paging_index(1, raw_vaddr);
	entry = table[index];
	if (!x86_64_paging_entry_is_present(entry)) {
		return false;
	}

	entry &= ~X86_64_PAGING_ENTRY_WRITE;
	if ((prot & PLANE_VM_PROT_WRITE) != 0) {
		entry |= X86_64_PAGING_ENTRY_WRITE;
	}
	table[index] = entry;
	return true;
}

bool x86_64_pmap_map_kernel_page(plane_vaddr_t vaddr,
				 plane_paddr_t phys_addr,
				 struct hal_mmu_map_options options)
{
	plane_irq_state_t state;
	bool mapped;

	state = lock_pmap();
	mapped = x86_64_pmap_map_page_in_owned_root(
		x86_64_pmap_current_root_phys(), vaddr, phys_addr, options);
	if (mapped) {
		hal_mmu_invalidate_tlb(vaddr);
	}
	unlock_pmap(state);
	return mapped;
}

bool x86_64_pmap_unmap_kernel_page(plane_vaddr_t vaddr)
{
	plane_irq_state_t state;
	bool unmapped;

	state = lock_pmap();
	unmapped = x86_64_pmap_unmap_page_in_owned_root(
		x86_64_pmap_current_root_phys(), vaddr);
	if (unmapped) {
		hal_mmu_invalidate_tlb(vaddr);
	}
	unlock_pmap(state);
	return unmapped;
}

bool x86_64_pmap_translate_kernel_page(plane_vaddr_t vaddr,
				       plane_paddr_t *phys_addr)
{
	plane_irq_state_t state;
	bool translated;

	state = lock_pmap();
	translated = x86_64_pmap_translate_in_root(
		x86_64_pmap_current_root_phys(), vaddr, phys_addr);
	unlock_pmap(state);
	return translated;
}

bool x86_64_pmap_protect_kernel_page(plane_vaddr_t vaddr, uint32_t prot)
{
	plane_irq_state_t state;
	bool protected;

	state = lock_pmap();
	protected = x86_64_pmap_protect_page_in_owned_root(
		x86_64_pmap_current_root_phys(), vaddr, prot);
	if (protected) {
		hal_mmu_invalidate_tlb(vaddr);
	}
	unlock_pmap(state);
	return protected;
}

bool hal_mmu_map_kernel_page(plane_vaddr_t vaddr,
			     plane_paddr_t phys_addr,
			     struct hal_mmu_map_options options)
{
	return x86_64_pmap_map_kernel_page(vaddr, phys_addr, options);
}

bool hal_mmu_unmap_kernel_page(plane_vaddr_t vaddr)
{
	return x86_64_pmap_unmap_kernel_page(vaddr);
}

bool hal_mmu_translate_kernel_page(plane_vaddr_t vaddr, plane_paddr_t *phys_addr)
{
	return x86_64_pmap_translate_kernel_page(vaddr, phys_addr);
}

bool hal_mmu_protect_kernel_page(plane_vaddr_t vaddr, uint32_t prot)
{
	return x86_64_pmap_protect_kernel_page(vaddr, prot);
}

bool hal_mmu_take_kernel_page_table_ownership(void)
{
	struct x86_64_pmap_skip_range skip[2];
	struct x86_64_physmap_runtime physmap;
	plane_paddr_t new_pml4_phys;

	if (!x86_64_physmap_get_runtime(&physmap)) {
		return false;
	}

	/*
	 * Startup ownership handoff runs before APs enter the general kernel
	 * path. The cloned root is not active until write_cr3_phys(), so
	 * owned-root builders below run without the active pmap lock.
	 */
	skip[0].base = physmap.bootstrap_base;
	skip[0].size = physmap.bootstrap_size;
	skip[1].base = plane_vaddr_make(X86_64_PHYSMAP_BASE);
	skip[1].size = physmap.owned_window_size;

	if (!x86_64_pmap_clone_kernel_page_tables(x86_64_pmap_current_root_phys(),
						  skip,
						  ARRAY_SIZE(skip),
						  &new_pml4_phys)) {
		return false;
	}

	if (!x86_64_pmap_build_physmap_in_owned_root(
		    new_pml4_phys, plane_vaddr_make(X86_64_PHYSMAP_BASE),
		    physmap.required_size, physmap.owned_window_size)) {
		if (!free_cloned_page_table(new_pml4_phys, 4)) {
			return false;
		}
		return false;
	}

	write_cr3_phys(new_pml4_phys);
	x86_64_physmap_commit_owned();
	hal_mmu_flush_tlb_all();
	return true;
}
