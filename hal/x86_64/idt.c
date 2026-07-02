#include <hal/x86_64/gdt.h>
#include <hal/x86_64/idt.h>
#include <hal/x86_64/pio.h>

#define IDT_NR_DESCRIPTORS  256
_Static_assert(IDT_NR_DESCRIPTORS <= IDT_MAX_DESCRIPTORS, "idt descriptors exceed hardware limit!");

/* in idt_flush.S */
extern void x86_64_idt_flush(uint64_t idtr_addr);

static void legacy_pic_disable(void)
{
	outb(0x21, 0xff);  /* 0x21 pic1 data */
	outb(0xa1, 0xff);  /* 0xa1 pic2 data */
}

static struct idt_descriptor idt[IDT_NR_DESCRIPTORS];
static struct idt_ptr idtr;

#define DECL_ISR(n) extern void x86_64_isr##n(void);
DECL_ISR(0)  DECL_ISR(1)  DECL_ISR(2)  DECL_ISR(3)  DECL_ISR(4)  DECL_ISR(5)  DECL_ISR(6)  DECL_ISR(7)
DECL_ISR(8)  DECL_ISR(9)  DECL_ISR(10) DECL_ISR(11) DECL_ISR(12) DECL_ISR(13) DECL_ISR(14) DECL_ISR(15)
DECL_ISR(16) DECL_ISR(17) DECL_ISR(18) DECL_ISR(19) DECL_ISR(20) DECL_ISR(21) DECL_ISR(22) DECL_ISR(23)
DECL_ISR(24) DECL_ISR(25) DECL_ISR(26) DECL_ISR(27) DECL_ISR(28) DECL_ISR(29) DECL_ISR(30) DECL_ISR(31)

/* in interrupts.S */
extern void x86_64_isr_default(void);

static void *x86_64_isr_stub_table[32] = {
	x86_64_isr0, x86_64_isr1, x86_64_isr2, x86_64_isr3,
	x86_64_isr4, x86_64_isr5, x86_64_isr6, x86_64_isr7,
	x86_64_isr8, x86_64_isr9, x86_64_isr10, x86_64_isr11,
	x86_64_isr12, x86_64_isr13, x86_64_isr14, x86_64_isr15,
	x86_64_isr16, x86_64_isr17, x86_64_isr18, x86_64_isr19,
	x86_64_isr20, x86_64_isr21, x86_64_isr22, x86_64_isr23,
	x86_64_isr24, x86_64_isr25, x86_64_isr26, x86_64_isr27,
	x86_64_isr28, x86_64_isr29, x86_64_isr30, x86_64_isr31
};

static void set_idt_descriptor(uint8_t vector, uintptr_t isr, uint8_t attributes)
{
	idt[vector].offset_low    = (uint16_t)(isr & 0xffff);
	idt[vector].selector      = SELECTOR_KERNEL_CS;
	idt[vector].ist           = 0;
	idt[vector].attributes    = attributes;
	idt[vector].offset_middle = (uint16_t)((isr >> 16) & 0xffff);
	idt[vector].offset_high   = (uint32_t)(isr >> 32);
	idt[vector].reserved      = 0;
}

void x86_64_idt_init(void)
{
	/* set idtr */
	idtr.limit = sizeof(idt) - 1;
	idtr.base  = (uint64_t)&idt;

	/* fill all entries with default isr handler */
	for (int i = 0; i < IDT_NR_DESCRIPTORS; i++) {
		set_idt_descriptor(i, (uintptr_t)x86_64_isr_default, IDT_ATTR(1, DPL_KERNEL, IDT_TYPE_INTR64));
	}

	/* override entries for first 32 vectors */
	for (int i = 0; i < 32; i++) {
		set_idt_descriptor(i, (uintptr_t)x86_64_isr_stub_table[i], IDT_ATTR(1, DPL_KERNEL, IDT_TYPE_INTR64));
	}

	/* disabling 8259 pic */
	legacy_pic_disable();

	/* do flush */
	x86_64_idt_flush((uint64_t)&idtr);
}
