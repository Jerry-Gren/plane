#ifndef PLANE_MEMMAP_H
#define PLANE_MEMMAP_H

#include <stdbool.h>
#include <stdint.h>

#define PLANE_MAX_MEMMAP_ENTRIES 128

enum plane_mem_type {
	PLANE_MEM_INVALID = 0,
	PLANE_MEM_USABLE,
	PLANE_MEM_RESERVED,
	PLANE_MEM_ACPI_RECLAIMABLE,
	PLANE_MEM_ACPI_NVS,
	PLANE_MEM_BAD_MEMORY,
	PLANE_MEM_BOOTLOADER_RECLAIMABLE,
	PLANE_MEM_EXECUTABLE_AND_MODULES,
	PLANE_MEM_FRAMEBUFFER,
	PLANE_MEM_RESERVED_MAPPED
};

struct plane_mem_region {
	uint64_t base;
	uint64_t length;
	uint32_t type;
};

struct plane_mem_info {
	uint64_t entry_count;
	struct plane_mem_region map[PLANE_MAX_MEMMAP_ENTRIES];
};

void plane_sanitize_memory_map(struct plane_mem_info *mem);
bool plane_memmap_reserve(struct plane_mem_info *mem, uint64_t base,
			  uint64_t length, uint32_t type);

#endif /* PLANE_MEMMAP_H */
