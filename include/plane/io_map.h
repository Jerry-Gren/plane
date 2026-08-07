#ifndef PLANE_IO_MAP_H
#define PLANE_IO_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <plane/vm_prot.h>

enum plane_io_map_cache {
	PLANE_IO_MAP_CACHE_DEVICE,
	PLANE_IO_MAP_CACHE_WRITE_COMBINE,
};

/*
 * Kernel-only IO mapping.
 *
 * IO-map reserves kernel VA and installs pmap mappings with explicit cache
 * attributes. It does not create vm_object backing; faults into this range
 * fail instead of zero-filling anonymous pages.
 */
bool plane_io_map_init(void);
bool plane_io_map(plane_paddr_t phys_addr,
		  uint64_t size,
		  enum plane_io_map_cache cache,
		  uint32_t prot,
		  plane_vaddr_t *vaddr);
bool plane_io_unmap(plane_vaddr_t vaddr, uint64_t size);

#endif /* PLANE_IO_MAP_H */
