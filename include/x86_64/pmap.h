#ifndef X86_64_PMAP_H
#define X86_64_PMAP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>

struct plane_mem_info;

enum pmap_mapping_attr {
	PMAP_MAPPING_ATTR_DEFAULT,
	PMAP_MAPPING_ATTR_DEVICE,
	PMAP_MAPPING_ATTR_WRITE_COMBINE,
};

struct pmap_map_options {
	uint32_t prot;
	enum pmap_mapping_attr attr;
};

static inline struct pmap_map_options pmap_default_map_options(uint32_t prot)
{
	return (struct pmap_map_options){
		.prot = prot,
		.attr = PMAP_MAPPING_ATTR_DEFAULT,
	};
}

#define PHYSMAP_INVALID_PHYS UINT64_MAX

/*
 * Physmap is a pmap-owned RAM physical mapping subfacility, matching XNU's
 * pmap/physmap split. It is not an IO-map or a separate machine selector.
 */
bool physmap_enable(const struct plane_mem_info *mem);
plane_vaddr_t physmap_phys_to_virt(plane_paddr_t phys_addr);
plane_vaddr_t physmap_phys_range_to_virt(plane_paddr_t phys_addr,
					 uint64_t size);
plane_paddr_t physmap_virt_to_phys(plane_vaddr_t vaddr);

/*
 * Active kernel pmap operations. Root cloning and caller-owned root mutation
 * stay in x86_64 pmap internal headers.
 */
bool pmap_take_kernel_page_table_ownership(void);
bool pmap_kernel_vma_range(plane_vaddr_t *base, uint64_t *size);
bool pmap_map_kernel_page(plane_vaddr_t vaddr, plane_paddr_t phys_addr,
			  struct pmap_map_options options);
bool pmap_unmap_kernel_page(plane_vaddr_t vaddr);
bool pmap_translate_kernel_page(plane_vaddr_t vaddr,
				plane_paddr_t *phys_addr);
bool pmap_protect_kernel_page(plane_vaddr_t vaddr, uint32_t prot);
void pmap_invalidate_tlb(plane_vaddr_t vaddr);
void pmap_flush_tlb_all(void);

/*
 * XNU-like pmap update interrupt landing pad for SMP TLB_FLUSH signals.
 * The pmap-owned sender side will attach range/cpumask rendezvous later.
 */
void pmap_update_interrupt(void);

#endif /* X86_64_PMAP_H */
