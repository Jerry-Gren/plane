#ifndef PLANE_EARLY_VIDEO_H
#define PLANE_EARLY_VIDEO_H

#include <stdbool.h>

#include <plane/boot_info.h>

/*
 * early boot framebuffer helper.
 * this consumes bootloader-provided video info only; it is not a video driver
 * and does not do device probing, mode setting, or lifetime management.
 */
bool plane_early_video_format_supported(const struct plane_video_info *video);
bool plane_early_video_draw_test_pattern(struct plane_video_info *video);

#endif /* PLANE_EARLY_VIDEO_H */
