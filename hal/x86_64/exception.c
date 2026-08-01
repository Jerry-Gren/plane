#include <hal/x86_64/exception.h>
#include <hal/x86_64/linkage.h>
#include <plane/bits.h>
#include <plane/kmem.h>
#include <plane/printk.h>
#include <plane/vm_prot.h>

#define X86_EXCEPTION_BP 3
#define X86_EXCEPTION_PF 14
#define CODE_DUMP_BYTES 16
#define X86_PF_ERROR_PRESENT BIT(0)
#define X86_PF_ERROR_WRITE BIT(1)
#define X86_PF_ERROR_USER BIT(2)
#define X86_PF_ERROR_RSVD BIT(3)
#define X86_PF_ERROR_EXECUTE BIT(4)
#define X86_PF_ERROR_SUPPORTED \
	(X86_PF_ERROR_PRESENT | X86_PF_ERROR_WRITE | X86_PF_ERROR_USER | \
	 X86_PF_ERROR_RSVD | X86_PF_ERROR_EXECUTE)

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

static uint64_t read_cr2(void)
{
	uint64_t cr2;

	__asm__ volatile ("mov %%cr2, %0" : "=r" (cr2));
	return cr2;
}

static int range_in_kernel_text(uint64_t addr, uint64_t size)
{
	uint64_t text_start = (uint64_t)__kernel_text_start;
	uint64_t text_end = (uint64_t)__kernel_text_end;

	return addr >= text_start && addr <= text_end && size <= text_end - addr;
}

static void dump_code(uint64_t rip, uint64_t int_no)
{
	uint64_t code_addr = rip;

	if (int_no == X86_EXCEPTION_BP) {
		code_addr--;
	}
	uint64_t marker_addr = code_addr;

	if (!range_in_kernel_text(code_addr, CODE_DUMP_BYTES)) {
		printk("Code: unavailable, rip is outside kernel .text\n");
		return;
	}

	printk("Code: ");
	uint8_t *pc = (uint8_t *)code_addr;
	for (int i = 0; i < CODE_DUMP_BYTES; i++) {
		if ((uint64_t)&pc[i] == marker_addr) {
			printk("<%02x> ", pc[i]);
		} else {
			printk("%02x ", pc[i]);
		}
	}
	printk("\n");
}

bool x86_64_try_handle_page_fault(uint64_t int_no,
				  plane_vaddr_t fault_addr,
				  uint64_t error_code)
{
	uint32_t fault_type = PLANE_VM_PROT_READ;

	if (int_no != X86_EXCEPTION_PF ||
	    (error_code & ~X86_PF_ERROR_SUPPORTED) != 0 ||
	    (error_code & (X86_PF_ERROR_USER |
			   X86_PF_ERROR_RSVD |
			   X86_PF_ERROR_EXECUTE)) != 0) {
		return false;
	}

	/*
	 * Match XNU's early trap direction: the architecture error code is
	 * translated into VM protection bits, then VM decides whether the
	 * kernel-map fault can be satisfied.
	 */
	if ((error_code & X86_PF_ERROR_WRITE) != 0) {
		fault_type |= PLANE_VM_PROT_WRITE;
	}

	return plane_kmem_fault_page(fault_addr, fault_type);
}

void x86_64_exception_handler(struct interrupt_frame *frame)
{
	uint64_t cr2 = 0;

	if (frame->int_no >= 32) {
		pr_warn("Unhandled interrupt/irq triggered but ignored.\n");
		return;
	}

	if (frame->int_no == X86_EXCEPTION_PF) {
		cr2 = read_cr2();
		if (x86_64_try_handle_page_fault(frame->int_no,
						 plane_vaddr_make(cr2),
						 frame->error_code)) {
			return;
		}
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

	if (frame->int_no == X86_EXCEPTION_PF) {
		printk(" cr2: 0x%016llx\n", cr2);
	}

	dump_code(frame->rip, frame->int_no);

	panic("Unhandled exception: %s", exception_names[frame->int_no]);
}
