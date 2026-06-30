#ifndef PLANE_BOOT_INFO_H
#define PLANE_BOOT_INFO_H

#include <stdint.h>
#include <plane/memmap.h>

/* Video */
struct plane_video_info {
	void *framebuffer_addr;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint8_t  bpp;
};

/* hand off to kmain() */
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
