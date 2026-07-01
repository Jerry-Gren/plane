#include <stddef.h>
#include <stdint.h>

#include <plane/early_video.h>

static uint32_t scale_color(uint8_t value, uint8_t mask_size) {
	if (mask_size == 0) {
		return 0;
	}

	uint64_t max = (mask_size >= 32) ? UINT32_MAX : ((1ull << mask_size) - 1);
	return (uint32_t)(((uint64_t)value * max + 127) / 255);
}

uint32_t plane_early_video_pack_rgb(const struct plane_video_info *video,
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

bool plane_early_video_format_supported(const struct plane_video_info *video) {
	if (video->bpp != 16 && video->bpp != 24 && video->bpp != 32) {
		return false;
	}

	if (video->red_mask_size == 0 ||
	    video->green_mask_size == 0 ||
	    video->blue_mask_size == 0) {
		return false;
	}

	if (video->red_mask_shift + video->red_mask_size > video->bpp ||
	    video->green_mask_shift + video->green_mask_size > video->bpp ||
	    video->blue_mask_shift + video->blue_mask_size > video->bpp) {
		return false;
	}

	return true;
}

bool plane_early_video_draw_test_pattern(struct plane_video_info *video) {
	if (!plane_early_video_format_supported(video) ||
	    video->framebuffer_addr == NULL ||
	    video->width == 0 ||
	    video->height == 0) {
		return false;
	}

	uint8_t *fb_ptr = (uint8_t *)video->framebuffer_addr;
	uint8_t bytes_per_pixel = video->bpp / 8;

	for (size_t y = 0; y < video->height; y++) {
		for (size_t x = 0; x < video->width; x++) {
			uint8_t nX = (uint8_t)(x * 255 / video->width);
			uint8_t nY = (uint8_t)(y * 255 / video->height);
			size_t pixel_offset = (y * video->pitch) + (x * bytes_per_pixel);
			uint32_t pixel = plane_early_video_pack_rgb(video, 0, nY, nX);

			write_pixel(&fb_ptr[pixel_offset], bytes_per_pixel, pixel);
		}
	}

	return true;
}
