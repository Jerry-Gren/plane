#include <hal/x86_64/exception.h>
#include <plane/kernel.h>

static const char *exception_names[32] = {
    "Divide Error (#DE)",                       /* 0 */
    "Debug Exception (#DB)",                    /* 1 */
    "NMI Interrupt",                            /* 2 */
    "Breakpoint (#BP)",                         /* 3 */
    "Overflow (#OF)",                           /* 4 */
    "BOUND Range Exceeded (#BR)",               /* 5 */
    "Invalid Opcode (#UD)",                     /* 6 */
    "Device Not Available (#NM)",               /* 7 */
    "Double Fault (#DF)",                       /* 8 */
    "Coprocessor Segment Overrun",              /* 9 */
    "Invalid TSS (#TS)",                        /* 10 */
    "Segment Not Present (#NP)",                /* 11 */
    "Stack-Segment Fault (#SS)",                /* 12 */
    "General Protection Fault (#GP)",           /* 13 */
    "Page Fault (#PF)",                         /* 14 */
    "Reserved",                                 /* 15 */
    "x87 FPU Floating-Point Error (#MF)",       /* 16 */
    "Alignment Check (#AC)",                    /* 17 */
    "Machine Check (#MC)",                      /* 18 */
    "SIMD Floating-Point Exception (#XM)",      /* 19 */
    "Virtualization Exception (#VE)",           /* 20 */
    "Control Protection Exception (#CP)",       /* 21 */
    "Reserved",                                 /* 22 */
    "Reserved",                                 /* 23 */
    "Reserved",                                 /* 24 */
    "Reserved",                                 /* 25 */
    "Reserved",                                 /* 26 */
    "Reserved",                                 /* 27 */
    "Hypervisor Injection Exception (#HV)",     /* 28 (AMD) */
    "VMM Communication Exception (#VC)",        /* 29 (AMD) */
    "Security Exception (#SX)",                 /* 30 (AMD) */
    "Reserved"                                  /* 31 */
};

void x86_64_exception_handler(struct interrupt_frame *frame) {
	if (frame->int_no >= 32) {
		pr_warn("Unhandled interrupt/irq triggered but ignored.\n");
		return;
	}

	printk(" error code : 0x%016llx\n", frame->error_code);
	printk(" rip: 0x%016llx   cs : 0x%04llx   rflags: 0x%016llx\n", 
		frame->rip, frame->cs, frame->rflags);
	printk(" rsp: 0x%016llx   ss : 0x%04llx\n", 
		frame->rsp, frame->ss);
	printk(" rax: 0x%016llx   rbx: 0x%016llx\n", frame->rax, frame->rbx);
	printk(" rcx: 0x%016llx   rdx: 0x%016llx\n", frame->rcx, frame->rdx);
	printk(" rsi: 0x%016llx   rdi: 0x%016llx\n", frame->rsi, frame->rdi);
	printk(" rbp: 0x%016llx   r8 : 0x%016llx\n", frame->rbp, frame->r8);
	printk(" r9 : 0x%016llx   r10: 0x%016llx\n", frame->r9,  frame->r10);
	printk(" r11: 0x%016llx   r12: 0x%016llx\n", frame->r11, frame->r12);
	printk(" r13: 0x%016llx   r14: 0x%016llx\n", frame->r13, frame->r14);
	printk(" r15: 0x%016llx\n", frame->r15);

	/* TODO: implemement page fault handler and print something before pc */
	printk("Code: ");
	uint8_t *pc = (uint8_t *)frame->rip;
	for (int i = 0; i < 16; i++) {
		if (i == 0) {
			printk("<%02x> ", pc[i]);
		} else {
			printk("%02x ", pc[i]);
		}
	}
	printk("\n");

	panic("Unhandled exception: %s", exception_names[frame->int_no]);
}