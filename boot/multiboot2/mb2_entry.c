#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <multiboot2.h>

#include <boot/multiboot2/mb2_arch.h>

#include <machine/machine_routines.h>
#include <machine/serial.h>

#include <plane/boot_info.h>
#include <plane/entry.h>
#include <plane/overflow.h>
#include <plane/printk.h>
#include <plane/util.h>

/* This struct is not present in official multiboot2.h */
struct multiboot_info_base {
    uint32_t total_size;
    uint32_t reserved;
};

static void boot_mb2_collect_framebuffer(struct plane_framebuffer_info *framebuffer_info,
					 struct multiboot_tag_framebuffer *fb_tag,
					 plane_paddr_t *framebuffer_phys_addr,
					 uint64_t *framebuffer_size)
{
	/* struct multiboot_tag_framebuffer_common
	 * {
	 *     multiboot_uint32_t type;
	 *     multiboot_uint32_t size;
	 *
	 *     multiboot_uint64_t framebuffer_addr;
	 *     multiboot_uint32_t framebuffer_pitch;
	 *     multiboot_uint32_t framebuffer_width;
	 *     multiboot_uint32_t framebuffer_height;
	 *     multiboot_uint8_t framebuffer_bpp;
	 *     multiboot_uint8_t framebuffer_type;
	 *     multiboot_uint16_t reserved;
	 * }
	 *
	 * struct multiboot_tag_framebuffer
	 * {
	 *     struct multiboot_tag_framebuffer_common common;

	 *     union
	 *     {
	 *         struct
	 *         {
	 *             multiboot_uint16_t framebuffer_palette_num_colors;
	 *             struct multiboot_color framebuffer_palette[0];
	 *         };
	 *         struct
	 *         {
	 *             multiboot_uint8_t framebuffer_red_field_position;
	 *             multiboot_uint8_t framebuffer_red_mask_size;
	 *             multiboot_uint8_t framebuffer_green_field_position;
	 *             multiboot_uint8_t framebuffer_green_mask_size;
	 *             multiboot_uint8_t framebuffer_blue_field_position;
	 *             multiboot_uint8_t framebuffer_blue_mask_size;
	 *         };
	 *     };
	 * };
	 */

	struct multiboot_tag_framebuffer_common *fb_common = &fb_tag->common;

	framebuffer_info->width  = fb_common->framebuffer_width;
	framebuffer_info->height = fb_common->framebuffer_height;
	framebuffer_info->pitch  = fb_common->framebuffer_pitch;
	framebuffer_info->bpp    = fb_common->framebuffer_bpp;

	BUG_ON_MSG(fb_common->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB,
		   "unsupported multiboot2 framebuffer type %u",
		   fb_common->framebuffer_type);

	framebuffer_info->red_mask_size    = fb_tag->framebuffer_red_mask_size;
	framebuffer_info->red_mask_shift   = fb_tag->framebuffer_red_field_position;
	framebuffer_info->green_mask_size  = fb_tag->framebuffer_green_mask_size;
	framebuffer_info->green_mask_shift = fb_tag->framebuffer_green_field_position;
	framebuffer_info->blue_mask_size   = fb_tag->framebuffer_blue_mask_size;
	framebuffer_info->blue_mask_shift  = fb_tag->framebuffer_blue_field_position;

	plane_paddr_t phys_addr = plane_paddr_make(fb_common->framebuffer_addr);
	plane_vaddr_t framebuffer_vaddr;
	uint64_t fb_size;

	BUG_ON_MSG(!plane_checked_mul_u64(framebuffer_info->pitch, framebuffer_info->height,
					  &fb_size),
		   "multiboot2 framebuffer size overflow: pitch=%u height=%u",
		   framebuffer_info->pitch, framebuffer_info->height);
	BUG_ON_MSG(fb_size == 0, "multiboot2 framebuffer size is zero");

	*framebuffer_phys_addr = phys_addr;
	*framebuffer_size = fb_size;
	framebuffer_info->framebuffer_phys_addr = phys_addr;
	framebuffer_info->framebuffer_size = fb_size;
	BUG_ON_MSG(!boot_mb2_arch_map_bootstrap_framebuffer(phys_addr, fb_size,
						  &framebuffer_vaddr),
		   "failed to map multiboot2 framebuffer: phys=0x%016llx size=0x%016llx",
		   (unsigned long long)plane_paddr_raw(phys_addr),
		   (unsigned long long)fb_size);
	framebuffer_info->framebuffer_addr = framebuffer_vaddr;
}

static void boot_mb2_collect_memmap(struct plane_mem_info *mem, struct multiboot_tag_mmap *mmap_tag)
{
	multiboot_memory_map_t *entry = mmap_tag->entries;
	uint32_t entry_size = mmap_tag->entry_size;

	BUG_ON_MSG(mmap_tag->size < sizeof(struct multiboot_tag_mmap),
		   "multiboot2 mmap tag too small: size=%u min=%zu",
		   mmap_tag->size, sizeof(struct multiboot_tag_mmap));

	BUG_ON_MSG(entry_size < sizeof(multiboot_memory_map_t),
		   "multiboot2 mmap entry too small: entry_size=%u min=%zu",
		   entry_size, sizeof(multiboot_memory_map_t));

	uint32_t data_size = mmap_tag->size - sizeof(struct multiboot_tag_mmap);
	BUG_ON_MSG((data_size % entry_size) != 0,
		   "multiboot2 mmap data is not entry-aligned: data_size=%u entry_size=%u",
		   data_size, entry_size);
	uint32_t entry_count = data_size / entry_size;

	BUG_ON_MSG(entry_count > PLANE_MAX_MEMMAP_ENTRIES,
		   "multiboot2 mmap has too many entries: count=%u max=%u",
		   entry_count, PLANE_MAX_MEMMAP_ENTRIES);

	for (uint32_t i = 0; i < entry_count; i++) {
		uint64_t index = mem->entry_count;

		/*
		 * struct multiboot_mmap_entry
		 * {
		 *     multiboot_uint64_t addr;
		 *     multiboot_uint64_t len;
		 *     multiboot_uint32_t type;
		 *     multiboot_uint32_t zero;
		 * }
		 * typedef struct multiboot_mmap_entry multiboot_memory_map_t;
		 */
		mem->map[index].base = plane_paddr_make(entry->addr);
		mem->map[index].length = entry->len;

		switch (entry->type) {
			case MULTIBOOT_MEMORY_AVAILABLE:
				mem->map[index].type = PLANE_MEM_USABLE; break;
			case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
				mem->map[index].type = PLANE_MEM_ACPI_RECLAIMABLE; break;
			case MULTIBOOT_MEMORY_NVS:
				mem->map[index].type = PLANE_MEM_ACPI_NVS; break;
			case MULTIBOOT_MEMORY_BADRAM:
				mem->map[index].type = PLANE_MEM_BAD_MEMORY; break;
			case MULTIBOOT_MEMORY_RESERVED:
			default:
				mem->map[index].type = PLANE_MEM_RESERVED; break;
		}

		/* the zero field is discarded */

		mem->entry_count++;

		entry = (multiboot_memory_map_t *)((uint8_t *)entry + entry_size);
	}
}

static void boot_mb2_add_reservations(struct plane_boot_info *info,
				      plane_paddr_t mb2_info_addr,
				      uint64_t mb2_info_size,
				      plane_paddr_t framebuffer_phys_addr,
				      uint64_t framebuffer_size)
{
	BUG_ON_MSG(!plane_memmap_reserve(&info->mem,
					 mb2_info_addr,
					 mb2_info_size,
					 PLANE_MEM_BOOTLOADER_RECLAIMABLE),
		   "failed to reserve multiboot2 info: base=0x%016llx size=0x%016llx",
		   (unsigned long long)plane_paddr_raw(mb2_info_addr),
		   (unsigned long long)mb2_info_size);

	BUG_ON_MSG(!plane_memmap_reserve(&info->mem,
					 framebuffer_phys_addr,
					 framebuffer_size,
					 PLANE_MEM_FRAMEBUFFER),
		   "failed to reserve framebuffer: base=0x%016llx size=0x%016llx",
		   (unsigned long long)plane_paddr_raw(framebuffer_phys_addr),
		   (unsigned long long)framebuffer_size);
}

void mb2_entry(uint64_t magic, uint64_t info_addr)
{
	serial_init();

	/* check magic number passed by multiboot2 */
	BUG_ON_MSG(magic != MULTIBOOT2_BOOTLOADER_MAGIC,
		   "bad multiboot2 magic: 0x%016llx",
		   (unsigned long long)magic);

	struct plane_boot_info b_info = {0};
	plane_paddr_t mb2_info_phys = plane_paddr_make(info_addr);
	plane_paddr_t framebuffer_phys_addr = plane_paddr_make(0);
	uint64_t framebuffer_size = 0;

	BUG_ON_MSG(!plane_smp_info_init_bsp(&b_info.smp, 0),
		   "failed to initialize multiboot2 BSP-only SMP info");

	plane_vaddr_t info_vaddr = boot_mb2_arch_phys_to_virt(mb2_info_phys);
	void *info_ptr = plane_vaddr_to_ptr(info_vaddr);
	struct multiboot_info_base *info_base = info_ptr;

	BUG_ON_MSG(info_base->total_size < sizeof(struct multiboot_info_base) +
		   sizeof(struct multiboot_tag),
		   "multiboot2 info too small: total_size=%u",
		   info_base->total_size);

	uint8_t *tag_cursor = (uint8_t *)info_ptr + sizeof(struct multiboot_info_base);
	uint8_t *info_end = (uint8_t *)info_ptr + info_base->total_size;
	struct multiboot_tag *tag = (struct multiboot_tag *)tag_cursor;

	while (true) {
		BUG_ON_MSG(tag_cursor + sizeof(struct multiboot_tag) > info_end,
			   "multiboot2 tag header exceeds info size");
		BUG_ON_MSG(tag->size < sizeof(struct multiboot_tag),
			   "multiboot2 tag too small: type=%u size=%u",
			   tag->type, tag->size);

		uint64_t aligned_size = ALIGN(tag->size, MULTIBOOT_TAG_ALIGN);
		BUG_ON_MSG(aligned_size < tag->size ||
			   aligned_size > (uint64_t)(info_end - tag_cursor),
			   "multiboot2 tag exceeds info size: type=%u size=%u total_size=%u",
			   tag->type, tag->size, info_base->total_size);

		if (tag->type == MULTIBOOT_TAG_TYPE_END) {
			break;
		}

		if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
			struct multiboot_tag_framebuffer *fb_tag = (struct multiboot_tag_framebuffer *)tag;
			boot_mb2_collect_framebuffer(&b_info.framebuffer, fb_tag,
						     &framebuffer_phys_addr,
						     &framebuffer_size);
		} else if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
			struct multiboot_tag_mmap *mmap_tag = (struct multiboot_tag_mmap *)tag;
			boot_mb2_collect_memmap(&b_info.mem, mmap_tag);
		}

		tag_cursor += aligned_size;
		tag = (struct multiboot_tag *)tag_cursor;
	}

	/* ensure we got a framebuffer */
	BUG_ON_MSG(plane_vaddr_is_null(b_info.framebuffer.framebuffer_addr),
		   "multiboot2 framebuffer tag missing");

	boot_mb2_arch_reserve_kernel_image(&b_info.mem);
	boot_mb2_add_reservations(&b_info, mb2_info_phys, info_base->total_size,
				  framebuffer_phys_addr, framebuffer_size);

	boot_mb2_arch_finish_handoff();
	b_info.release_framebuffer_bootstrap_mapping =
		boot_mb2_arch_release_bootstrap_framebuffer_mapping;

	kmain(&b_info);

	ml_cpu_halt();
}
