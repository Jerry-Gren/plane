#ifndef PLANE_BOOT_INFO_H
#define PLANE_BOOT_INFO_H

#include <stdint.h>

/* Video */
struct plane_video_info {
	uint32_t *framebuffer_addr;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint8_t  bpp;
};

/* Memory Map */
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

struct boot_info {
	struct plane_video_info video;
	struct plane_mem_info   mem;
	
	/*
	 * and more ...
	 * struct plane_acpi_info  acpi;
	 * struct plane_smp_info   smp;
	 * char cmdline[256];
	 */
};

#endif /* PLANE_BOOT_INFO_H */