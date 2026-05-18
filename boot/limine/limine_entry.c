#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include <hal/cpu.h>

#include <plane/boot_info.h>

// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID,
	.revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

/* in main.c */
extern void kmain(struct boot_info *info);

void boot_limine_collect_framebuffer(struct boot_info *b_info) {
	if (framebuffer_request.response == NULL ||
	    framebuffer_request.response->framebuffer_count < 1) {
		hal_cpu_hang();
	}
	/* struct limine_framebuffer {
	 *     LIMINE_PTR(void *) address;
	 *     uint64_t width;
	 *     uint64_t height;
	 *     uint64_t pitch;
	 *     uint16_t bpp;
	 *     uint8_t memory_model;
	 *     uint8_t red_mask_size;
	 *     uint8_t red_mask_shift;
	 *     uint8_t green_mask_size;
	 *     uint8_t green_mask_shift;
	 *     uint8_t blue_mask_size;
	 *     uint8_t blue_mask_shift;
	 *     uint8_t unused[7];
	 *     uint64_t edid_size;
	 *     LIMINE_PTR(void *) edid;
	 *     // Response revision 1
	 *     uint64_t mode_count;
	 *     LIMINE_PTR(struct limine_video_mode **) modes;
	 * };
	 */
	/* fetch the first framebuffer */
   	struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
	
	b_info->framebuffer_addr   = (uint32_t *)fb->address;
	b_info->framebuffer_width  = fb->width;
	b_info->framebuffer_height = fb->height;
	b_info->framebuffer_pitch  = fb->pitch;
	b_info->framebuffer_bpp    = fb->bpp;
}

void boot_limine_collect_memmap(struct boot_info *b_info) {
	if (memmap_request.response == NULL) {
		hal_cpu_hang();
	}
	/*
	 * struct limine_memmap_entry {
	 *     uint64_t base;
	 *     uint64_t length;
	 *     uint64_t type;
	 * };
	 */
	uint64_t count = memmap_request.response->entry_count;
	for (uint64_t i = 0; i < count && b_info->memmap_entries_count < PLANE_MAX_MEMMAP_ENTRIES; i++) {
		struct limine_memmap_entry *entry = memmap_request.response->entries[i];
		uint64_t index = b_info->memmap_entries_count;

		b_info->memmap[index].base = entry->base;
		b_info->memmap[index].length = entry->length;

		switch (entry->type) {
			case LIMINE_MEMMAP_USABLE:
				b_info->memmap[index].type = PLANE_MEM_USABLE; break;
			case LIMINE_MEMMAP_RESERVED:
				b_info->memmap[index].type = PLANE_MEM_RESERVED; break;
			case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
				b_info->memmap[index].type = PLANE_MEM_ACPI_RECLAIMABLE; break;
			case LIMINE_MEMMAP_ACPI_NVS:
				b_info->memmap[index].type = PLANE_MEM_ACPI_NVS; break;
			case LIMINE_MEMMAP_BAD_MEMORY:
				b_info->memmap[index].type = PLANE_MEM_BAD_MEMORY; break;
			case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
				b_info->memmap[index].type = PLANE_MEM_BOOTLOADER_RECLAIMABLE; break;
			case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
				b_info->memmap[index].type = PLANE_MEM_EXECUTABLE_AND_MODULES; break;
			case LIMINE_MEMMAP_FRAMEBUFFER:
				b_info->memmap[index].type = PLANE_MEM_FRAMEBUFFER; break;
			case LIMINE_MEMMAP_RESERVED_MAPPED:
				b_info->memmap[index].type = PLANE_MEM_RESERVED_MAPPED; break;
			default:
				b_info->memmap[index].type = PLANE_MEM_RESERVED; break;
		}
		b_info->memmap_entries_count++;
	}
}

void _start(void) {
	/* Ensure the bootloader actually understands our base revision */
	if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
		hal_cpu_hang();
	}

	struct boot_info b_info = {0};
	
	boot_limine_collect_framebuffer(&b_info);

	boot_limine_collect_memmap(&b_info);
	
	kmain(&b_info);

	hal_cpu_hang();
}