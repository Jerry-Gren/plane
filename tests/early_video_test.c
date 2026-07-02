#include <stdint.h>
#include <string.h>

#include <plane/early_video.h>

#include "support/test.h"

static struct plane_video_info rgb_video(uint8_t bpp,
					 uint8_t red_shift,
					 uint8_t green_shift,
					 uint8_t blue_shift,
					 uint8_t red_size,
					 uint8_t green_size,
					 uint8_t blue_size) {
	struct plane_video_info video = {0};

	video.bpp = bpp;
	video.red_mask_shift = red_shift;
	video.green_mask_shift = green_shift;
	video.blue_mask_shift = blue_shift;
	video.red_mask_size = red_size;
	video.green_mask_size = green_size;
	video.blue_mask_size = blue_size;
	return video;
}

static uint32_t read_le_pixel(const uint8_t *src, uint8_t bytes_per_pixel) {
	uint32_t pixel = 0;

	for (uint8_t i = 0; i < bytes_per_pixel; i++) {
		pixel |= (uint32_t)src[i] << (i * 8);
	}

	return pixel;
}

static int test_draw_pattern_packs_rgb_formats(void) {
	int failures = 0;

	struct {
		const char *name;
		struct plane_video_info video;
		uint32_t expected;
	} cases[] = {
		{
			.name = "draw packs rgb565",
			.video = rgb_video(16, 11, 5, 0, 5, 6, 5),
			.expected = 0x000003ef,
		},
		{
			.name = "draw packs rgb888",
			.video = rgb_video(24, 16, 8, 0, 8, 8, 8),
			.expected = 0x00007f7f,
		},
		{
			.name = "draw packs xrgb8888",
			.video = rgb_video(32, 16, 8, 0, 8, 8, 8),
			.expected = 0x00007f7f,
		},
	};

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(cases); i++) {
		uint8_t framebuffer[16] = {0};
		struct plane_video_info video = cases[i].video;
		uint8_t bytes_per_pixel = video.bpp / 8;

		video.framebuffer_addr = framebuffer;
		video.width = 2;
		video.height = 2;
		video.pitch = video.width * bytes_per_pixel;

		if (!plane_early_video_draw_test_pattern(&video)) {
			test_fail("%s returned false", cases[i].name);
			failures++;
			continue;
		}

		uint64_t offset = video.pitch + bytes_per_pixel;
		failures += test_expect_u32(cases[i].name,
					    read_le_pixel(&framebuffer[offset],
							  bytes_per_pixel),
					    cases[i].expected);
	}

	return failures;
}

static int test_format_supported(void) {
	int failures = 0;

	struct plane_video_info valid = rgb_video(32, 16, 8, 0, 8, 8, 8);
	struct plane_video_info bad_bpp = valid;
	struct plane_video_info empty_mask = valid;
	struct plane_video_info mask_overflow = valid;

	bad_bpp.bpp = 15;
	empty_mask.red_mask_size = 0;
	mask_overflow.red_mask_shift = 28;
	mask_overflow.red_mask_size = 8;

	failures += test_expect_bool("valid format",
				     plane_early_video_format_supported(&valid),
				     true);
	failures += test_expect_bool("bad bpp",
				     plane_early_video_format_supported(&bad_bpp),
				     false);
	failures += test_expect_bool("empty mask",
				     plane_early_video_format_supported(&empty_mask),
				     false);
	failures += test_expect_bool("mask overflow",
				     plane_early_video_format_supported(&mask_overflow),
				     false);

	return failures;
}

static int test_draw_pattern_honors_pitch(void) {
	uint8_t framebuffer[24];
	memset(framebuffer, 0x5a, sizeof(framebuffer));

	struct plane_video_info video = rgb_video(32, 16, 8, 0, 8, 8, 8);
	video.framebuffer_addr = framebuffer;
	video.width = 2;
	video.height = 2;
	video.pitch = 12;

	if (!plane_early_video_draw_test_pattern(&video)) {
		test_fail("draw test pattern returned false");
		return 1;
	}

	uint32_t bottom_right = (uint32_t)framebuffer[12 + 4] |
				((uint32_t)framebuffer[12 + 5] << 8) |
				((uint32_t)framebuffer[12 + 6] << 16) |
				((uint32_t)framebuffer[12 + 7] << 24);
	if (bottom_right == 0) {
		test_fail("draw test pattern did not write bottom-right pixel");
		return 1;
	}

	for (uint64_t i = 8; i < 12; i++) {
		if (framebuffer[i] != 0x5a) {
			test_fail("first row padding overwritten at %llu",
				  (unsigned long long)i);
			return 1;
		}
	}
	for (uint64_t i = 20; i < 24; i++) {
		if (framebuffer[i] != 0x5a) {
			test_fail("second row padding overwritten at %llu",
				  (unsigned long long)i);
			return 1;
		}
	}

	return 0;
}

static int test_draw_rejects_invalid_inputs(void) {
	uint8_t framebuffer[16] = {0};
	struct plane_video_info video = rgb_video(32, 16, 8, 0, 8, 8, 8);
	int failures = 0;

	video.framebuffer_addr = framebuffer;
	video.width = 1;
	video.height = 1;
	video.pitch = 4;

	video.bpp = 15;
	failures += test_expect_bool("draw rejects bad bpp",
				     plane_early_video_draw_test_pattern(&video),
				     false);
	video.bpp = 32;

	video.framebuffer_addr = NULL;
	failures += test_expect_bool("draw rejects null framebuffer",
				     plane_early_video_draw_test_pattern(&video),
				     false);

	return failures;
}

static int test_draw_rejects_short_pitch(void) {
	uint8_t framebuffer[16];
	int failures;
	memset(framebuffer, 0x5a, sizeof(framebuffer));

	struct plane_video_info video = rgb_video(32, 16, 8, 0, 8, 8, 8);
	video.framebuffer_addr = framebuffer;
	video.width = 2;
	video.height = 2;
	video.pitch = 4;

	failures = test_expect_bool("draw rejects short pitch",
				    plane_early_video_draw_test_pattern(&video),
				    false);
	if (failures != 0) {
		return failures;
	}

	for (uint64_t i = 0; i < sizeof(framebuffer); i++) {
		if (framebuffer[i] != 0x5a) {
			test_fail("short pitch rejection wrote byte %llu",
				  (unsigned long long)i);
			return 1;
		}
	}

	return 0;
}

int main(void) {
	static const struct test_case cases[] = {
		TEST_CASE(test_draw_pattern_packs_rgb_formats),
		TEST_CASE(test_format_supported),
		TEST_CASE(test_draw_pattern_honors_pitch),
		TEST_CASE(test_draw_rejects_invalid_inputs),
		TEST_CASE(test_draw_rejects_short_pitch),
	};

	return test_run_cases("early_video_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
