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
		pr_warn("Unhandled interrupt/irq triggered and ignored.\n");
		return;
	}

	printk(" Error Code : 0x%016llx\n", frame->error_code);
	printk("--------------------------------------------------\n");
	printk(" RIP: 0x%016llx   CS : 0x%04llx   RFLAGS: 0x%016llx\n", 
		frame->rip, frame->cs, frame->rflags);
	printk(" RSP: 0x%016llx   SS : 0x%04llx\n", 
		frame->rsp, frame->ss);
	printk("--------------------------------------------------\n");
	printk(" RAX: 0x%016llx   RBX: 0x%016llx\n", frame->rax, frame->rbx);
	printk(" RCX: 0x%016llx   RDX: 0x%016llx\n", frame->rcx, frame->rdx);
	printk(" RSI: 0x%016llx   RDI: 0x%016llx\n", frame->rsi, frame->rdi);
	printk(" RBP: 0x%016llx   R8 : 0x%016llx\n", frame->rbp, frame->r8);
	printk(" R9 : 0x%016llx   R10: 0x%016llx\n", frame->r9,  frame->r10);
	printk(" R11: 0x%016llx   R12: 0x%016llx\n", frame->r11, frame->r12);
	printk(" R13: 0x%016llx   R14: 0x%016llx\n", frame->r13, frame->r14);
	printk(" R15: 0x%016llx\n", frame->r15);

	panic("Unhandled exception: %s", exception_names[frame->int_no]);
}