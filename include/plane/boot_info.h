#ifndef PLANE_BOOT_INFO_H
#define PLANE_BOOT_INFO_H

#include <stdbool.h>
#include <stdint.h>
#include <plane/address.h>
#include <plane/memmap.h>
#include <plane/smp.h>

struct plane_framebuffer_info {
	plane_vaddr_t framebuffer_addr;
	plane_paddr_t framebuffer_phys_addr;
	uint64_t framebuffer_size;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint8_t  bpp;
	uint8_t  red_mask_size;
	uint8_t  red_mask_shift;
	uint8_t  green_mask_size;
	uint8_t  green_mask_shift;
	uint8_t  blue_mask_size;
	uint8_t  blue_mask_shift;
};

struct plane_boot_info {
	struct plane_framebuffer_info framebuffer;
	struct plane_mem_info mem;
	struct plane_smp_info smp;
	bool (*start_aps)(void);
	bool (*release_framebuffer_bootstrap_mapping)(plane_vaddr_t vaddr,
						 uint64_t size);

	/*
	 * and more ...
	 * struct plane_acpi_info  acpi;
	 * char cmdline[256];
	 */
};

#endif /* PLANE_BOOT_INFO_H */
