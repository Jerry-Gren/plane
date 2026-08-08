#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

#include <boot/limine/limine_arch.h>

#include <hal/cpu.h>
#include <hal/serial.h>

#include <plane/boot_info.h>
#include <plane/compiler.h>
#include <plane/entry.h>
#include <plane/overflow.h>
#include <plane/printk.h>

#include "limine_smp_internal.h"

/*
 * Set the recommended Limine base revision.
 * See the Limine boot protocol specification for details.
 */

__used __section(".limine_requests")
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

/*
 * Limine requests must not be optimized away, so keep them volatile and
 * explicitly marked as used.
 */

__used __section(".limine_requests")
static volatile struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID,
	.revision = 0
};

__used __section(".limine_requests")
static volatile struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST_ID,
	.revision = 0
};

__used __section(".limine_requests")
static volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST_ID,
	.revision = 0
};

__used __section(".limine_requests")
static volatile struct limine_mp_request mp_request = {
	.id = LIMINE_MP_REQUEST_ID,
	.revision = 0,
	.flags = 0
};

/* Mark the start and end of the Limine request list. */

__used __section(".limine_requests_start")
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__used __section(".limine_requests_end")
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

static plane_paddr_t boot_limine_framebuffer_phys_addr(
	const struct boot_limine_arch_handoff *handoff,
	plane_vaddr_t vaddr)
{
	plane_paddr_t phys_addr;
	uint64_t hhdm_base = 0;

	if (handoff != NULL) {
		hhdm_base = plane_vaddr_raw(handoff->hhdm_base);
	}

	/*
	 * Runtime framebuffer access is handed off to plane_io_map(), but
	 * Limine only gives Plane a framebuffer VA. The current handoff assumes
	 * that VA lives in Limine's HHDM bootstrap mapping, making phys =
	 * va - HHDM.
	 * If a future boot environment violates that, use the memmap framebuffer
	 * entry to resolve the physical address instead of silently guessing.
	 */
	BUG_ON_MSG(!boot_limine_arch_hhdm_virt_to_phys(handoff, vaddr,
						       &phys_addr),
		   "limine framebuffer VA is not in HHDM: vaddr=0x%016llx hhdm=0x%016llx",
		   (unsigned long long)plane_vaddr_raw(vaddr),
		   (unsigned long long)hhdm_base);

	return phys_addr;
}

static void boot_limine_collect_framebuffer(
	struct plane_framebuffer_info *framebuffer_info,
	const struct boot_limine_arch_handoff *handoff)
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
	 *     Response revision 1
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
		   "limine framebuffer fields exceed plane_framebuffer_info limits");

	uint64_t fb_size;
	BUG_ON_MSG(!plane_checked_mul_u64(fb->pitch, fb->height, &fb_size),
		   "limine framebuffer size overflow: pitch=%llu height=%llu",
		   (unsigned long long)fb->pitch,
		   (unsigned long long)fb->height);
	BUG_ON_MSG(fb_size == 0, "limine framebuffer size is zero");

	framebuffer_info->framebuffer_addr = plane_vaddr_make((uint64_t)fb->address);
	framebuffer_info->framebuffer_phys_addr =
		boot_limine_framebuffer_phys_addr(handoff,
						  framebuffer_info->framebuffer_addr);
	framebuffer_info->framebuffer_size = fb_size;
	framebuffer_info->width            = fb->width;
	framebuffer_info->height           = fb->height;
	framebuffer_info->pitch            = fb->pitch;
	framebuffer_info->bpp              = fb->bpp;
	framebuffer_info->red_mask_size    = fb->red_mask_size;
	framebuffer_info->red_mask_shift   = fb->red_mask_shift;
	framebuffer_info->green_mask_size  = fb->green_mask_size;
	framebuffer_info->green_mask_shift = fb->green_mask_shift;
	framebuffer_info->blue_mask_size   = fb->blue_mask_size;
	framebuffer_info->blue_mask_shift  = fb->blue_mask_shift;
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

		mem->map[index].base = plane_paddr_make(entry->base);
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

static void boot_limine_collect_hhdm(
	struct boot_limine_arch_handoff *handoff)
{
	BUG_ON_MSG(hhdm_request.response == NULL,
		   "limine HHDM response missing");
	handoff->hhdm_base = plane_vaddr_make(hhdm_request.response->offset);
}

static void boot_limine_collect_smp(struct plane_smp_info *smp)
{
	struct limine_mp_response *response = mp_request.response;

	boot_limine_smp_reset_cpu_handles();
	BUG_ON_MSG(!plane_smp_info_init(smp),
		   "failed to initialize limine SMP info");
	if (response == NULL || response->cpu_count == 0 ||
	    response->cpus == NULL) {
		BUG_ON_MSG(!plane_smp_info_init_bsp(smp, 0),
			   "failed to initialize limine BSP-only SMP info");
		return;
	}

	uint32_t bsp_lapic_id = response->bsp_lapic_id;
	bool found_bsp = false;

	for (uint64_t i = 0; i < response->cpu_count; i++) {
		struct limine_mp_info *cpu = response->cpus[i];

		BUG_ON_MSG(cpu == NULL, "limine MP CPU %llu is null",
			   (unsigned long long)i);
		if (cpu->lapic_id == bsp_lapic_id) {
			BUG_ON_MSG(!plane_smp_info_record_cpu(smp,
							      cpu->lapic_id,
							      true),
				   "failed to record limine BSP CPU");
			BUG_ON_MSG(!boot_limine_smp_set_cpu_handle(
					   smp->bsp_logical_id, cpu),
				   "failed to record limine BSP handle");
			found_bsp = true;
			break;
		}
	}

	if (!found_bsp) {
		BUG_ON_MSG(!plane_smp_info_record_cpu(smp, bsp_lapic_id, true),
			   "failed to record limine fallback BSP CPU");
	}

	for (uint64_t i = 0; i < response->cpu_count; i++) {
		struct limine_mp_info *cpu = response->cpus[i];

		BUG_ON_MSG(cpu == NULL, "limine MP CPU %llu is null",
			   (unsigned long long)i);
		if (cpu->lapic_id == bsp_lapic_id) {
			continue;
		}

		if (plane_smp_info_record_cpu(smp, cpu->lapic_id, false)) {
			BUG_ON_MSG(!boot_limine_smp_set_cpu_handle(
					   smp->cpu_count - 1, cpu),
				   "failed to record limine AP handle");
		}
	}
}

void _start(void)
{
	hal_serial_init();

	/* Ensure the bootloader actually understands our base revision */
	BUG_ON_MSG(LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false,
		   "limine base revision is not supported");

	struct plane_boot_info b_info = {0};
	struct boot_limine_arch_handoff arch_handoff = {0};

	boot_limine_collect_hhdm(&arch_handoff);
	BUG_ON_MSG(!boot_limine_arch_install_hhdm_physmap(&arch_handoff),
		   "failed to install limine HHDM physmap");
	boot_limine_collect_framebuffer(&b_info.framebuffer, &arch_handoff);
	boot_limine_collect_memmap(&b_info.mem);
	boot_limine_collect_smp(&b_info.smp);
	b_info.start_aps = boot_limine_smp_start_aps;

	kmain(&b_info);

	hal_cpu_hang();
}
