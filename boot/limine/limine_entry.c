#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include <hal/cpu.h>
#include <hal/mmu.h>
#include <hal/serial.h>

#include <plane/boot_info.h>
#include <plane/entry.h>
#include <plane/printk.h>

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

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST_ID,
	.revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static void boot_limine_collect_framebuffer(struct plane_video_info *video)
{
	BUG_ON_MSG(framebuffer_request.response == NULL,
		   "limine framebuffer response missing");
	BUG_ON_MSG(framebuffer_request.response->framebuffer_count < 1,
		   "limine framebuffer response has no framebuffers");
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

	BUG_ON_MSG(fb == NULL, "limine framebuffer pointer is null");
	BUG_ON_MSG(fb->address == NULL, "limine framebuffer address is null");
	BUG_ON_MSG(fb->memory_model != LIMINE_FRAMEBUFFER_RGB,
		   "unsupported limine framebuffer memory model %u",
		   fb->memory_model);

	BUG_ON_MSG(fb->width > UINT32_MAX || fb->height > UINT32_MAX ||
		   fb->pitch > UINT32_MAX || fb->bpp > UINT8_MAX,
		   "limine framebuffer fields exceed plane_video_info limits");

	video->framebuffer_addr = (uint32_t *)fb->address;
	video->width            = fb->width;
	video->height           = fb->height;
	video->pitch            = fb->pitch;
	video->bpp              = fb->bpp;
	video->red_mask_size    = fb->red_mask_size;
	video->red_mask_shift   = fb->red_mask_shift;
	video->green_mask_size  = fb->green_mask_size;
	video->green_mask_shift = fb->green_mask_shift;
	video->blue_mask_size   = fb->blue_mask_size;
	video->blue_mask_shift  = fb->blue_mask_shift;
}

static void boot_limine_collect_memmap(struct plane_mem_info *mem)
{
	BUG_ON_MSG(memmap_request.response == NULL,
		   "limine memmap response missing");
	/*
	 * struct limine_memmap_entry {
	 *     uint64_t base;
	 *     uint64_t length;
	 *     uint64_t type;
	 * };
	 */
	uint64_t count = memmap_request.response->entry_count;
	BUG_ON_MSG(count > PLANE_MAX_MEMMAP_ENTRIES,
		   "limine memmap has too many entries: count=%llu max=%u",
		   (unsigned long long)count, PLANE_MAX_MEMMAP_ENTRIES);
	BUG_ON_MSG(count != 0 && memmap_request.response->entries == NULL,
		   "limine memmap entries pointer is null");

	for (uint64_t i = 0; i < count; i++) {
		struct limine_memmap_entry *entry = memmap_request.response->entries[i];
		uint64_t index = mem->entry_count;

		BUG_ON_MSG(entry == NULL, "limine memmap entry %llu is null",
			   (unsigned long long)i);

		mem->map[index].base = entry->base;
		mem->map[index].length = entry->length;

		switch (entry->type) {
			case LIMINE_MEMMAP_USABLE:
				mem->map[index].type = PLANE_MEM_USABLE; break;
			case LIMINE_MEMMAP_RESERVED:
				mem->map[index].type = PLANE_MEM_RESERVED; break;
			case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
				mem->map[index].type = PLANE_MEM_ACPI_RECLAIMABLE; break;
			case LIMINE_MEMMAP_ACPI_NVS:
				mem->map[index].type = PLANE_MEM_ACPI_NVS; break;
			case LIMINE_MEMMAP_BAD_MEMORY:
				mem->map[index].type = PLANE_MEM_BAD_MEMORY; break;
			case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
				mem->map[index].type = PLANE_MEM_BOOTLOADER_RECLAIMABLE; break;
			case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
				mem->map[index].type = PLANE_MEM_EXECUTABLE_AND_MODULES; break;
			case LIMINE_MEMMAP_FRAMEBUFFER:
				mem->map[index].type = PLANE_MEM_FRAMEBUFFER; break;
			case LIMINE_MEMMAP_RESERVED_MAPPED:
				mem->map[index].type = PLANE_MEM_RESERVED_MAPPED; break;
			default:
				mem->map[index].type = PLANE_MEM_RESERVED; break;
		}
		mem->entry_count++;
	}
}

static void boot_limine_collect_hhdm(void)
{
	BUG_ON_MSG(hhdm_request.response == NULL,
		   "limine HHDM response missing");
	hal_mmu_set_direct_map_base(hhdm_request.response->offset);
}

void _start(void)
{
	hal_serial_init();

	/* Ensure the bootloader actually understands our base revision */
	BUG_ON_MSG(LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false,
		   "limine base revision is not supported");

	struct boot_info b_info = {0};
	
	boot_limine_collect_hhdm();
	boot_limine_collect_framebuffer(&b_info.video);
	boot_limine_collect_memmap(&b_info.mem);
	
	kmain(&b_info);

	hal_cpu_hang();
}
