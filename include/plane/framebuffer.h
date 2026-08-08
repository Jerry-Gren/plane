#ifndef PLANE_FRAMEBUFFER_H
#define PLANE_FRAMEBUFFER_H

#include <stdbool.h>

#include <plane/boot_info.h>

/*
 * Framebuffer helper for bootloader-provided handoff data. The runtime keeps
 * this narrow: remap the framebuffer through IO-map and draw a test pattern.
 * Mode setting, probing, console lifetime, and driver ownership are separate
 * future concerns.
 */
bool plane_framebuffer_format_supported(const struct plane_framebuffer_info *info);
bool plane_framebuffer_remap(struct plane_framebuffer_info *info);
bool plane_framebuffer_draw_test_pattern(struct plane_framebuffer_info *info);

#endif /* PLANE_FRAMEBUFFER_H */
