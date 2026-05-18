#ifndef PLANE_BOOT_INFO_H
#define PLANE_BOOT_INFO_H

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

struct boot_info {
	uint32_t *framebuffer_addr;
	uint64_t framebuffer_width;
	uint64_t framebuffer_height;
	uint64_t framebuffer_pitch;
	uint8_t  framebuffer_bpp;

	uint64_t memmap_entries_count;
	struct plane_mem_region memmap[PLANE_MAX_MEMMAP_ENTRIES];
};

#endif /* PLANE_BOOT_INFO_H */