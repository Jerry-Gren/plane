#include <stdint.h>
#include <stddef.h>
#include <plane/boot_info.h>

void kmain(struct boot_info *info) {
	plane_sanitize_memory_map(&info->mem);
	/* 
	 * TODO: 
	 * pmm_init(&info->mem); 
	 * vmm_init();
	 */
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

			if (bytes_per_pixel == 4) {
				// 32 bit mode (bgra)
				fb_ptr[pixel_offset + 0] = blue;
				fb_ptr[pixel_offset + 1] = green;
				fb_ptr[pixel_offset + 2] = red;
				fb_ptr[pixel_offset + 3] = 0;    // alpha / reserved
			}
			else if (bytes_per_pixel == 3) {
				// 24 bit mode (bgr)
				fb_ptr[pixel_offset + 0] = blue;
				fb_ptr[pixel_offset + 1] = green;
				fb_ptr[pixel_offset + 2] = red;
			}
		}
	}
}