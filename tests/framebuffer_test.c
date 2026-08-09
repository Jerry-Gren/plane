#include <stdint.h>
#include <string.h>

#include <plane/framebuffer.h>
#include <plane/io_map.h>

#include "support/test.h"

static bool io_map_should_fail;
static uint64_t io_map_call_count;
static plane_paddr_t io_map_last_phys_addr;
static uint64_t io_map_last_size;
static enum plane_io_map_cache io_map_last_cache;
static uint32_t io_map_last_prot;
static plane_vaddr_t io_map_return_vaddr;

bool plane_io_map(plane_paddr_t phys_addr,
		  uint64_t size,
		  enum plane_io_map_cache cache,
		  uint32_t prot,
		  plane_vaddr_t *vaddr)
{
	io_map_call_count++;
	io_map_last_phys_addr = phys_addr;
	io_map_last_size = size;
	io_map_last_cache = cache;
	io_map_last_prot = prot;

	if (io_map_should_fail) {
		return false;
	}

	*vaddr = io_map_return_vaddr;
	return true;
}

static void reset_io_map_stub(void)
{
	io_map_should_fail = false;
	io_map_call_count = 0;
	io_map_last_phys_addr = plane_paddr_make(0);
	io_map_last_size = 0;
	io_map_last_cache = PLANE_IO_MAP_CACHE_DEVICE;
	io_map_last_prot = 0;
	io_map_return_vaddr = plane_vaddr_make(0xffff900000200000ull);
}

static struct plane_framebuffer_info rgb_video(uint8_t bpp,
					 uint8_t red_shift,
					 uint8_t green_shift,
					 uint8_t blue_shift,
					 uint8_t red_size,
					 uint8_t green_size,
					 uint8_t blue_size) {
	struct plane_framebuffer_info framebuffer_info = {0};

	framebuffer_info.bpp = bpp;
	framebuffer_info.red_mask_shift = red_shift;
	framebuffer_info.green_mask_shift = green_shift;
	framebuffer_info.blue_mask_shift = blue_shift;
	framebuffer_info.red_mask_size = red_size;
	framebuffer_info.green_mask_size = green_size;
	framebuffer_info.blue_mask_size = blue_size;
	return framebuffer_info;
}

static uint32_t read_le_pixel(const uint8_t *src, uint8_t bytes_per_pixel)
{
	uint32_t pixel = 0;

	for (uint8_t i = 0; i < bytes_per_pixel; i++) {
		pixel |= (uint32_t)src[i] << (i * 8);
	}

	return pixel;
}

static int test_draw_pattern_packs_rgb_formats(void)
{
	int failures = 0;

	struct {
		const char *name;
		struct plane_framebuffer_info framebuffer;
		uint32_t expected;
	} cases[] = {
		{
			.name = "draw packs rgb565",
			.framebuffer = rgb_video(16, 11, 5, 0, 5, 6, 5),
			.expected = 0x000003ef,
		},
		{
			.name = "draw packs rgb888",
			.framebuffer = rgb_video(24, 16, 8, 0, 8, 8, 8),
			.expected = 0x00007f7f,
		},
		{
			.name = "draw packs xrgb8888",
			.framebuffer = rgb_video(32, 16, 8, 0, 8, 8, 8),
			.expected = 0x00007f7f,
		},
	};

	for (uint64_t i = 0; i < TEST_ARRAY_SIZE(cases); i++) {
		uint8_t framebuffer[16] = {0};
		struct plane_framebuffer_info framebuffer_info = cases[i].framebuffer;
		uint8_t bytes_per_pixel = framebuffer_info.bpp / 8;

		framebuffer_info.framebuffer_addr = plane_vaddr_from_ptr(framebuffer);
		framebuffer_info.width = 2;
		framebuffer_info.height = 2;
		framebuffer_info.pitch = framebuffer_info.width * bytes_per_pixel;

		if (!plane_framebuffer_draw_test_pattern(&framebuffer_info)) {
			test_fail("%s returned false", cases[i].name);
			failures++;
			continue;
		}

		uint64_t offset = framebuffer_info.pitch + bytes_per_pixel;
		failures += test_expect_u32(cases[i].name,
					    read_le_pixel(&framebuffer[offset],
							  bytes_per_pixel),
					    cases[i].expected);
	}

	return failures;
}

static int test_format_supported(void)
{
	int failures = 0;

	struct plane_framebuffer_info valid = rgb_video(32, 16, 8, 0, 8, 8, 8);
	struct plane_framebuffer_info bad_bpp = valid;
	struct plane_framebuffer_info empty_mask = valid;
	struct plane_framebuffer_info mask_overflow = valid;

	bad_bpp.bpp = 15;
	empty_mask.red_mask_size = 0;
	mask_overflow.red_mask_shift = 28;
	mask_overflow.red_mask_size = 8;

	failures += test_expect_bool("valid format",
				     plane_framebuffer_is_format_supported(&valid),
				     true);
	failures += test_expect_bool("bad bpp",
				     plane_framebuffer_is_format_supported(&bad_bpp),
				     false);
	failures += test_expect_bool("empty mask",
				     plane_framebuffer_is_format_supported(&empty_mask),
				     false);
	failures += test_expect_bool("mask overflow",
				     plane_framebuffer_is_format_supported(&mask_overflow),
				     false);

	return failures;
}

static int test_draw_pattern_honors_pitch(void)
{
	uint8_t framebuffer[24];
	memset(framebuffer, 0x5a, sizeof(framebuffer));

	struct plane_framebuffer_info framebuffer_info = rgb_video(32, 16, 8, 0, 8, 8, 8);
	framebuffer_info.framebuffer_addr = plane_vaddr_from_ptr(framebuffer);
	framebuffer_info.width = 2;
	framebuffer_info.height = 2;
	framebuffer_info.pitch = 12;

	if (!plane_framebuffer_draw_test_pattern(&framebuffer_info)) {
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

static int test_draw_rejects_invalid_inputs(void)
{
	uint8_t framebuffer[16] = {0};
	struct plane_framebuffer_info framebuffer_info = rgb_video(32, 16, 8, 0, 8, 8, 8);
	int failures = 0;

	framebuffer_info.framebuffer_addr = plane_vaddr_from_ptr(framebuffer);
	framebuffer_info.width = 1;
	framebuffer_info.height = 1;
	framebuffer_info.pitch = 4;

	framebuffer_info.bpp = 15;
	failures += test_expect_bool("draw rejects bad bpp",
				     plane_framebuffer_draw_test_pattern(&framebuffer_info),
				     false);
	framebuffer_info.bpp = 32;

	framebuffer_info.framebuffer_addr = plane_vaddr_make(0);
	failures += test_expect_bool("draw rejects null framebuffer",
				     plane_framebuffer_draw_test_pattern(&framebuffer_info),
				     false);

	return failures;
}

static int test_draw_rejects_short_pitch(void)
{
	uint8_t framebuffer[16];
	int failures;
	memset(framebuffer, 0x5a, sizeof(framebuffer));

	struct plane_framebuffer_info framebuffer_info = rgb_video(32, 16, 8, 0, 8, 8, 8);
	framebuffer_info.framebuffer_addr = plane_vaddr_from_ptr(framebuffer);
	framebuffer_info.width = 2;
	framebuffer_info.height = 2;
	framebuffer_info.pitch = 4;

	failures = test_expect_bool("draw rejects short pitch",
				    plane_framebuffer_draw_test_pattern(&framebuffer_info),
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

static int test_framebuffer_remap_uses_write_combine(void)
{
	struct plane_framebuffer_info framebuffer_info = {
		.framebuffer_addr = plane_vaddr_make(0xffff800000100123ull),
		.framebuffer_phys_addr = plane_paddr_make(0x00000000e0000123ull),
		.framebuffer_size = 0x1000,
	};
	int failures = 0;

	reset_io_map_stub();
	io_map_return_vaddr = plane_vaddr_make(0xffff900000300123ull);

	failures += test_expect_bool("remap succeeds",
				     plane_framebuffer_remap(&framebuffer_info),
				     true);
	failures += test_expect_u64("remap calls io map",
				    io_map_call_count, 1);
	failures += test_expect_u64("remap phys",
				    plane_paddr_raw(io_map_last_phys_addr),
				    0x00000000e0000123ull);
	failures += test_expect_u64("remap size",
				    io_map_last_size, 0x1000);
	failures += test_expect_u32("remap cache",
				    io_map_last_cache,
				    PLANE_IO_MAP_CACHE_WRITE_COMBINE);
	failures += test_expect_u32("remap prot",
				    io_map_last_prot,
				    PLANE_VM_PROT_READ | PLANE_VM_PROT_WRITE);
	failures += test_expect_u64("remap updates vaddr",
				    plane_vaddr_raw(framebuffer_info.framebuffer_addr),
				    0xffff900000300123ull);

	return failures;
}

static int test_framebuffer_remap_rejects_missing_handoff(void)
{
	struct plane_framebuffer_info valid = {
		.framebuffer_addr = plane_vaddr_make(0xffff800000100000ull),
		.framebuffer_phys_addr = plane_paddr_make(0x00000000e0000000ull),
		.framebuffer_size = 0x1000,
	};
	struct plane_framebuffer_info missing_vaddr = valid;
	struct plane_framebuffer_info missing_phys = valid;
	struct plane_framebuffer_info missing_size = valid;
	int failures = 0;

	reset_io_map_stub();
	failures += test_expect_bool("remap rejects null framebuffer info",
				     plane_framebuffer_remap(NULL),
				     false);
	missing_vaddr.framebuffer_addr = plane_vaddr_make(0);
	failures += test_expect_bool("remap rejects missing va",
				     plane_framebuffer_remap(&missing_vaddr),
				     false);
	missing_phys.framebuffer_phys_addr = plane_paddr_make(0);
	failures += test_expect_bool("remap rejects missing phys",
				     plane_framebuffer_remap(&missing_phys),
				     false);
	missing_size.framebuffer_size = 0;
	failures += test_expect_bool("remap rejects missing size",
				     plane_framebuffer_remap(&missing_size),
				     false);
	failures += test_expect_u64("remap rejects before io map",
				    io_map_call_count, 0);

	return failures;
}

static int test_framebuffer_remap_preserves_va_on_failure(void)
{
	struct plane_framebuffer_info framebuffer_info = {
		.framebuffer_addr = plane_vaddr_make(0xffff800000100000ull),
		.framebuffer_phys_addr = plane_paddr_make(0x00000000e0000000ull),
		.framebuffer_size = 0x1000,
	};
	int failures = 0;

	reset_io_map_stub();
	io_map_should_fail = true;

	failures += test_expect_bool("remap io map failure",
				     plane_framebuffer_remap(&framebuffer_info),
				     false);
	failures += test_expect_u64("failed remap calls io map",
				    io_map_call_count, 1);
	failures += test_expect_u64("failed remap preserves vaddr",
				    plane_vaddr_raw(framebuffer_info.framebuffer_addr),
				    0xffff800000100000ull);

	return failures;
}

static int test_draw_pattern_uses_remapped_framebuffer(void)
{
	uint8_t old_framebuffer[16] = {0};
	uint8_t runtime_framebuffer[16] = {0};
	struct plane_framebuffer_info framebuffer_info = rgb_video(32, 16, 8, 0, 8, 8, 8);

	framebuffer_info.framebuffer_addr = plane_vaddr_from_ptr(old_framebuffer);
	framebuffer_info.framebuffer_phys_addr = plane_paddr_make(0x00000000e0000000ull);
	framebuffer_info.framebuffer_size = sizeof(runtime_framebuffer);
	framebuffer_info.width = 2;
	framebuffer_info.height = 2;
	framebuffer_info.pitch = 8;

	reset_io_map_stub();
	io_map_return_vaddr = plane_vaddr_from_ptr(runtime_framebuffer);

	if (!plane_framebuffer_remap(&framebuffer_info)) {
		test_fail("remap before draw returned false");
		return 1;
	}
	if (!plane_framebuffer_draw_test_pattern(&framebuffer_info)) {
		test_fail("draw after remap returned false");
		return 1;
	}

	if (runtime_framebuffer[12] == 0 && runtime_framebuffer[13] == 0 &&
	    runtime_framebuffer[14] == 0 && runtime_framebuffer[15] == 0) {
		test_fail("draw did not write remapped framebuffer");
		return 1;
	}

	for (uint64_t i = 0; i < sizeof(old_framebuffer); i++) {
		if (old_framebuffer[i] != 0) {
			test_fail("draw wrote old framebuffer byte %llu",
				  (unsigned long long)i);
			return 1;
		}
	}

	return 0;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_framebuffer_remap_uses_write_combine),
		TEST_CASE(test_framebuffer_remap_rejects_missing_handoff),
		TEST_CASE(test_framebuffer_remap_preserves_va_on_failure),
		TEST_CASE(test_draw_pattern_uses_remapped_framebuffer),
		TEST_CASE(test_draw_pattern_packs_rgb_formats),
		TEST_CASE(test_format_supported),
		TEST_CASE(test_draw_pattern_honors_pitch),
		TEST_CASE(test_draw_rejects_invalid_inputs),
		TEST_CASE(test_draw_rejects_short_pitch),
	};

	return test_run_cases("framebuffer_test",
			      cases, TEST_ARRAY_SIZE(cases));
}
