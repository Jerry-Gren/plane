#include <stddef.h>
#include <stdint.h>

#include <plane/framebuffer.h>
#include <plane/io_map.h>

static uint32_t scale_color(uint8_t value, uint8_t mask_size)
{
	if (mask_size == 0) {
		return 0;
	}

	uint64_t max = (mask_size >= 32) ? UINT32_MAX : ((1ull << mask_size) - 1);
	return (uint32_t)(((uint64_t)value * max + 127) / 255);
}

static uint32_t pack_rgb(const struct plane_framebuffer_info *info,
			 uint8_t red, uint8_t green, uint8_t blue)
{
	return (scale_color(red, info->red_mask_size) << info->red_mask_shift) |
	       (scale_color(green, info->green_mask_size) << info->green_mask_shift) |
	       (scale_color(blue, info->blue_mask_size) << info->blue_mask_shift);
}

static void write_pixel(uint8_t *dst, uint8_t bytes_per_pixel, uint32_t pixel)
{
	/*
	 * current targets are little-endian.
	 * if big-endian support is added,
	 * make byte order explicit behind a hal helper.
	 */
	for (uint8_t i = 0; i < bytes_per_pixel; i++) {
		dst[i] = (uint8_t)(pixel >> (i * 8));
	}
}

bool plane_framebuffer_format_supported(const struct plane_framebuffer_info *info)
{
	if (info->bpp != 16 && info->bpp != 24 && info->bpp != 32) {
		return false;
	}

	if (info->red_mask_size == 0 ||
	    info->green_mask_size == 0 ||
	    info->blue_mask_size == 0) {
		return false;
	}

	if (info->red_mask_shift + info->red_mask_size > info->bpp ||
	    info->green_mask_shift + info->green_mask_size > info->bpp ||
	    info->blue_mask_shift + info->blue_mask_size > info->bpp) {
		return false;
	}

	return true;
}

bool plane_framebuffer_remap(struct plane_framebuffer_info *info)
{
	plane_vaddr_t mapped_addr;

	if (info == NULL ||
	    plane_vaddr_is_null(info->framebuffer_addr) ||
	    plane_paddr_is_null(info->framebuffer_phys_addr) ||
	    info->framebuffer_size == 0) {
		return false;
	}

	if (!plane_io_map(info->framebuffer_phys_addr,
			  info->framebuffer_size,
			  PLANE_IO_MAP_CACHE_WRITE_COMBINE,
			  PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE,
			  &mapped_addr)) {
		return false;
	}

	info->framebuffer_addr = mapped_addr;
	return true;
}

bool plane_framebuffer_draw_test_pattern(struct plane_framebuffer_info *info)
{
	if (!plane_framebuffer_format_supported(info) ||
	    plane_vaddr_is_null(info->framebuffer_addr) ||
	    info->width == 0 ||
	    info->height == 0) {
		return false;
	}

	uint8_t *fb_ptr = plane_vaddr_to_ptr(info->framebuffer_addr);
	uint8_t bytes_per_pixel = info->bpp / 8;

	/* reject pitch values that cannot hold one full framebuffer row */
	if (info->width > SIZE_MAX / bytes_per_pixel ||
	    info->pitch < info->width * bytes_per_pixel) {
		return false;
	}

	for (size_t y = 0; y < info->height; y++) {
		for (size_t x = 0; x < info->width; x++) {
			uint8_t nx = (uint8_t)(x * 255 / info->width);
			uint8_t ny = (uint8_t)(y * 255 / info->height);
			size_t pixel_offset = (y * info->pitch) + (x * bytes_per_pixel);
			uint32_t pixel = pack_rgb(info, 0, ny, nx);

			write_pixel(&fb_ptr[pixel_offset], bytes_per_pixel, pixel);
		}
	}

	return true;
}
