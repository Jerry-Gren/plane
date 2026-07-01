#ifndef PLANE_EARLY_VIDEO_H
#define PLANE_EARLY_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/boot_info.h>

bool plane_early_video_format_supported(const struct plane_video_info *video);
uint32_t plane_early_video_pack_rgb(const struct plane_video_info *video,
				    uint8_t red, uint8_t green, uint8_t blue);
bool plane_early_video_draw_test_pattern(struct plane_video_info *video);

#endif /* PLANE_EARLY_VIDEO_H */
