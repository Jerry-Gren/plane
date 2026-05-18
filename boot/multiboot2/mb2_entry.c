#include <stdint.h>
#include <stddef.h>
#include <multiboot2.h>

#include <hal/mmu.h>
#include <hal/cpu.h>

#include <plane/boot_info.h>
#include <plane/kernel.h>

/* TODO: MUST BE REMOVED IN THE FUTURE */
#define KERNEL_VMA_BASE      0xffffffff80000000ULL
#define FRAMEBUFFER_VMA_BASE 0xffffffffC0000000ULL

#define PAGE_PRESENT         (1ULL << 0)
#define PAGE_RW              (1ULL << 1)
#define PAGE_PWT             (1ULL << 3)
#define PAGE_HUGE            (1ULL << 7)
#define FB_PAGE_FLAGS        (PAGE_PRESENT | PAGE_RW | PAGE_PWT | PAGE_HUGE)

#define HUGE_PAGE_SIZE       0x200000ULL  /* 2MB */

/* in main.c */
extern void kmain(struct boot_info *info);

/* in string.c */
extern void *memset(void *s, int c, size_t n);

/* in linker_grub.lds */
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

/* This struct is not present in official multiboot2.h */
struct multiboot_info_base {
    uint32_t total_size;
    uint32_t reserved;
};

static void boot_mb2_collect_framebuffer(struct plane_video_info *video, struct multiboot_tag_framebuffer *fb_tag, uint64_t *boot_fb_pd) {
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
	uint64_t phys_addr = fb_common->framebuffer_addr;
	
	video->width  = fb_common->framebuffer_width;
	video->height = fb_common->framebuffer_height;
	video->pitch  = fb_common->framebuffer_pitch;
	video->bpp    = fb_common->framebuffer_bpp;

	uint64_t phys_base = ALIGN_DOWN(phys_addr, HUGE_PAGE_SIZE);
	uint64_t page_offset = phys_addr - phys_base;

	uint64_t fb_size = (uint64_t)video->pitch * video->height;
	uint64_t fb_aligned_size = ALIGN(fb_size + page_offset, HUGE_PAGE_SIZE);
	uint64_t pages_needed = fb_aligned_size / HUGE_PAGE_SIZE;

	for (uint64_t i = 0; i < pages_needed; i++) {
		uint64_t offset = i * HUGE_PAGE_SIZE;
		uint64_t current_vaddr = FRAMEBUFFER_VMA_BASE + offset;
		
		boot_fb_pd[i] = (phys_base + offset) | FB_PAGE_FLAGS;
		hal_mmu_invalidate_tlb(current_vaddr);
	}

	video->framebuffer_addr = (uint32_t *)(FRAMEBUFFER_VMA_BASE + page_offset);
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

void mb2_entry(uint64_t magic, uint64_t info_addr, uint64_t *boot_fb_pd, uint64_t *boot_pml4) {

	/* clear .bss */
	memset(__bss_start, 0, __bss_end - __bss_start);

	/* check magic number passed by multiboot2 */
	if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
		hal_cpu_hang();
	}

	struct boot_info b_info = {0};

	uint64_t info_vaddr = info_addr + KERNEL_VMA_BASE;
	struct multiboot_tag *tag = (struct multiboot_tag *)(info_vaddr + sizeof(struct multiboot_info_base));

	while (tag->type != MULTIBOOT_TAG_TYPE_END) {
		
		if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
			struct multiboot_tag_framebuffer *fb_tag = (struct multiboot_tag_framebuffer *)tag;
			boot_mb2_collect_framebuffer(&b_info.video, fb_tag, boot_fb_pd);
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

	/* delete 0~1GB equal mapping */
	boot_pml4[0] = 0;
	hal_mmu_reload_cr3();

	kmain(&b_info);

	hal_cpu_hang();
}