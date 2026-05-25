#include <hal/x86_64/gdt.h>
#include <klib/string.h>

/* 1 Null + 4 Segments + 2 for TSS */
#define GDT_ENTRIES 7

static struct gdt_descriptor gdt[GDT_ENTRIES];
static struct gdt_ptr gdtr;
static struct tss64 tss;

extern void hal_gdt_flush(uint64_t gdtr_addr);
extern void hal_tss_flush(void);

static void set_gdt_descriptor(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
	gdt[index].base_low    = (base & 0xffff);
	gdt[index].base_middle = (base >> 16) & 0xff;
	gdt[index].base_high   = (base >> 24) & 0xff;

	gdt[index].limit_low   = (limit & 0xffff);
	gdt[index].flags_limit = (flags & 0xf0) | ((limit >> 16) & 0x0f);
	
	gdt[index].access      = access;
}

static void set_tss_descriptor(int index, uintptr_t base, uint32_t limit) {
	set_gdt_descriptor(index, (uint32_t)base, limit, 
			GDT_ACCESS(1, DPL_KERNEL, 0, TYPE_TSS_AVAILABLE), 
			GDT_FLAGS(0, 0, 0, 0));

	struct tss_descriptor *tss_desc = (struct tss_descriptor *)&gdt[index];
	tss_desc->base_upper32 = (uint32_t)(base >> 32);
	tss_desc->reserved     = 0;
}

void hal_gdt_init(void) {
	/* set gdtr */
	gdtr.limit = sizeof(gdt) - 1;
	gdtr.base  = (uint64_t)&gdt;

	/* build gdt */
	/* 64 bit flat, base=0, limit=0xfffff */
	// [0] null descriptor
	set_gdt_descriptor(0, 0, 0, 0, 0);

	/* [1] kernel code (cs): dpl 0, code, 64 bit (l=1) */
	set_gdt_descriptor(1, 0, 0xfffff, 
			GDT_ACCESS(1, DPL_KERNEL, 1, TYPE_CODE_XR), 
			GDT_FLAGS(1, 0, 1, 0));

	/* [2] kernel data (ds): dpl 0, data */
	set_gdt_descriptor(2, 0, 0xfffff, 
			GDT_ACCESS(1, DPL_KERNEL, 1, TYPE_DATA_RW), 
			GDT_FLAGS(1, 1, 0, 0));

	/* [3] user data (ds): dpl 3, data */
	/* must before user code to supprt SYSRET */
	set_gdt_descriptor(3, 0, 0xfffff, 
			GDT_ACCESS(1, DPL_USER, 1, TYPE_DATA_RW), 
			GDT_FLAGS(1, 1, 0, 0));

	// [4] user code (cs): dpl 3, code, 64 bit (l=1)
	set_gdt_descriptor(4, 0, 0xfffff, 
			GDT_ACCESS(1, DPL_USER, 1, TYPE_CODE_XR), 
			GDT_FLAGS(1, 0, 1, 0));

	/* init tss */
	memset(&tss, 0, sizeof(tss));
	/* no i/o privilege */
	tss.iopb_offset = sizeof(tss); 
	
	/* tss occupies gdt[5] and gdt[6] */
	set_tss_descriptor(5, (uintptr_t)&tss, sizeof(tss) - 1);

	/* do flush */
	hal_gdt_flush((uint64_t)&gdtr);
	hal_tss_flush();
}

void hal_tss_set_kernel_stack(uintptr_t stack) {
	tss.rsp0 = stack;
}