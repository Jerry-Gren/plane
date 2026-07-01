#ifndef PLANE_EARLY_VIDEO_H
#define PLANE_EARLY_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/boot_info.h>

/*
 * early boot framebuffer helper.
 * this consumes bootloader-provided video info only; it is not a video driver
 * and does not do device probing, mode setting, or lifetime management.
 */
bool plane_early_video_format_supported(const struct plane_video_info *video);

/*
 * call with rgb framebuffer info already accepted by
 * plane_early_video_format_supported().
 *
 * kept public for tests and early reuse; can become internal once no external
 * caller needs the raw pixel packing helper.
 */
uint32_t plane_early_video_pack_rgb(const struct plane_video_info *video,
				    uint8_t red, uint8_t green, uint8_t blue);
bool plane_early_video_draw_test_pattern(struct plane_video_info *video);

#endif /* PLANE_EARLY_VIDEO_H */
