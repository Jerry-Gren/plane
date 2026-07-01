#include <stdint.h>
#include <stddef.h>
#include <plane/boot_info.h>
#include <plane/kernel.h>
#include <hal/cpu.h>
#include <hal/serial.h>
#include <hal/hal.h>

static uint32_t scale_color(uint8_t value, uint8_t mask_size) {
	if (mask_size == 0) {
		return 0;
	}

	uint64_t max = (mask_size >= 32) ? UINT32_MAX : ((1ull << mask_size) - 1);
	return (uint32_t)(((uint64_t)value * max + 127) / 255);
}

static uint32_t pack_rgb(const struct plane_video_info *video,
			 uint8_t red, uint8_t green, uint8_t blue) {
	return (scale_color(red, video->red_mask_size) << video->red_mask_shift) |
	       (scale_color(green, video->green_mask_size) << video->green_mask_shift) |
	       (scale_color(blue, video->blue_mask_size) << video->blue_mask_shift);
}

static void write_pixel(uint8_t *dst, uint8_t bytes_per_pixel, uint32_t pixel) {
	for (uint8_t i = 0; i < bytes_per_pixel; i++) {
		dst[i] = (uint8_t)(pixel >> (i * 8));
	}
}

static int framebuffer_format_supported(const struct plane_video_info *video) {
	if (video->bpp != 16 && video->bpp != 24 && video->bpp != 32) {
		return 0;
	}

	if (video->red_mask_size == 0 ||
	    video->green_mask_size == 0 ||
	    video->blue_mask_size == 0) {
		return 0;
	}

	if (video->red_mask_shift + video->red_mask_size > video->bpp ||
	    video->green_mask_shift + video->green_mask_size > video->bpp ||
	    video->blue_mask_shift + video->blue_mask_size > video->bpp) {
		return 0;
	}

	return 1;
}

void kmain(struct boot_info *info) {
	hal_serial_init();
	hal_arch_early_init();

	plane_sanitize_memory_map(&info->mem);
	
	/* 
	 * TODO: 
	 * pmm_init(&info->mem); 
	 * vmm_init();
	 */
	if (!framebuffer_format_supported(&info->video)) {
		hal_cpu_hang();
	}

	uint8_t *fb_ptr = (uint8_t *)info->video.framebuffer_addr;
	uint8_t bytes_per_pixel = info->video.bpp / 8;

	for (size_t y = 0; y < info->video.height; y++) {
		for (size_t x = 0; x < info->video.width; x++) {
		
			uint8_t nX = (uint8_t)(x * 255 / info->video.width);
			uint8_t nY = (uint8_t)(y * 255 / info->video.height);
			
			size_t pixel_offset = (y * info->video.pitch) + (x * bytes_per_pixel);
			
			uint8_t blue  = nX;
			uint8_t green = nY;
			uint8_t red   = 0;
			uint32_t pixel = pack_rgb(&info->video, red, green, blue);

			write_pixel(&fb_ptr[pixel_offset], bytes_per_pixel, pixel);
		}
	}

	pr_info("Kernel initialization completed. System halted.\n");
}
