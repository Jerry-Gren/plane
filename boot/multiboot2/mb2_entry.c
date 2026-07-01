#include <stdint.h>
#include <stddef.h>
#include <multiboot2.h>

#include <hal/mmu.h>
#include <hal/cpu.h>
#include <hal/x86_64/linkage.h>

#include <plane/boot_info.h>
#include <plane/kernel.h>

/* in main.c */
extern void kmain(struct boot_info *info);

/* This struct is not present in official multiboot2.h */
struct multiboot_info_base {
    uint32_t total_size;
    uint32_t reserved;
};

static void boot_mb2_collect_framebuffer(struct plane_video_info *video,
					 struct multiboot_tag_framebuffer *fb_tag,
					 uint64_t *framebuffer_phys_addr,
					 uint64_t *framebuffer_size) {
	struct multiboot_tag_framebuffer_common *fb_common = &fb_tag->common;
	
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
	 */
	video->width  = fb_common->framebuffer_width;
	video->height = fb_common->framebuffer_height;
	video->pitch  = fb_common->framebuffer_pitch;
	video->bpp    = fb_common->framebuffer_bpp;

	uint64_t phys_addr = fb_common->framebuffer_addr;
	uint64_t fb_size = (uint64_t)video->pitch * video->height;

	*framebuffer_phys_addr = phys_addr;
	*framebuffer_size = fb_size;
	video->framebuffer_addr = (uint32_t *)hal_mmu_map_early_framebuffer(phys_addr, fb_size);
}

static void boot_mb2_collect_mmap(struct plane_mem_info *mem, struct multiboot_tag_mmap *mmap_tag) {
	multiboot_memory_map_t *entry = mmap_tag->entries;
	uint32_t entry_size = mmap_tag->entry_size;
	uint32_t data_size = mmap_tag->size - sizeof(struct multiboot_tag_mmap);
	uint32_t entry_count = data_size / entry_size;

	for (uint32_t i = 0; i < entry_count && mem->entry_count < PLANE_MAX_MEMMAP_ENTRIES; i++) {
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
		mem->map[index].base = entry->addr;
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

static void boot_mb2_add_reservations(struct boot_info *info,
				      uint64_t mb2_info_addr,
				      uint64_t mb2_info_size,
				      uint64_t framebuffer_phys_addr,
				      uint64_t framebuffer_size) {
	uint64_t kernel_phys_start = (uint64_t)__kernel_phys_start;
	uint64_t kernel_phys_end = (uint64_t)__kernel_phys_end;

	if (!plane_memmap_reserve(&info->mem, kernel_phys_start,
				  kernel_phys_end - kernel_phys_start,
				  PLANE_MEM_EXECUTABLE_AND_MODULES)) {
		hal_cpu_hang();
	}

	if (!plane_memmap_reserve(&info->mem, mb2_info_addr, mb2_info_size,
				  PLANE_MEM_BOOTLOADER_RECLAIMABLE)) {
		hal_cpu_hang();
	}

	if (!plane_memmap_reserve(&info->mem, framebuffer_phys_addr,
				  framebuffer_size,
				  PLANE_MEM_FRAMEBUFFER)) {
		hal_cpu_hang();
	}
}

void mb2_entry(uint64_t magic, uint64_t info_addr) {
	
	/* check magic number passed by multiboot2 */
	if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
		hal_cpu_hang();
	}

	struct boot_info b_info = {0};
	uint64_t framebuffer_phys_addr = 0;
	uint64_t framebuffer_size = 0;

	void *info_vaddr = hal_mmu_phys_to_virt(info_addr);
	struct multiboot_info_base *info_base = info_vaddr;
	struct multiboot_tag *tag = (struct multiboot_tag *)((uint8_t *)info_vaddr + sizeof(struct multiboot_info_base));

	while (tag->type != MULTIBOOT_TAG_TYPE_END) {
		
		if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
			struct multiboot_tag_framebuffer *fb_tag = (struct multiboot_tag_framebuffer *)tag;
			boot_mb2_collect_framebuffer(&b_info.video, fb_tag,
						     &framebuffer_phys_addr,
						     &framebuffer_size);
		}

		else if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
			struct multiboot_tag_mmap *mmap_tag = (struct multiboot_tag_mmap *)tag;
			boot_mb2_collect_mmap(&b_info.mem, mmap_tag);
		}

		tag = (struct multiboot_tag *)((uint8_t *)tag + ALIGN(tag->size, MULTIBOOT_TAG_ALIGN));
	}

	/* ensure we got a framebuffer */
	if (b_info.video.framebuffer_addr == NULL) {
		hal_cpu_hang();
	}

	boot_mb2_add_reservations(&b_info, info_addr, info_base->total_size,
				  framebuffer_phys_addr, framebuffer_size);

	hal_mmu_remove_identity_mapping();

	kmain(&b_info);

	hal_cpu_hang();
}
