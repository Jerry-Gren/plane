#ifndef HAL_X86_64_INTERRUPT_DEFS_H
#define HAL_X86_64_INTERRUPT_DEFS_H

#include <plane/bits.h>

/*
 * x86-64 interrupt, exception, and IDT definitions used by Plane today.
 *
 * Intel SDM Vol.3 Chapters 7 and 8 plus AMD APM Vol.2 Chapter 8 define the
 * IDT gate format, exception vector assignments, and page-fault error-code
 * bits. Keep this header scoped to the current minimal exception path: IST
 * routing, syscall/user traps, and interrupt dispatch are later milestones.
 */
#define X86_64_INTR_IDT_ENTRIES 256
#define X86_64_INTR_EXTERNAL_DEFAULT_VECTOR 256

#define X86_64_INTR_GATE_TYPE_INTERRUPT64 0x0e
#define X86_64_INTR_GATE_TYPE_TRAP64      0x0f

#define X86_64_INTR_GATE_ATTR_PRESENT BIT(7)
#define X86_64_INTR_GATE_ATTR_DPL_SHIFT 5
#define X86_64_INTR_GATE_ATTR_TYPE_MASK 0x0f

#define X86_64_INTR_VECTOR_DIVIDE_ERROR 0
#define X86_64_INTR_VECTOR_DEBUG 1
#define X86_64_INTR_VECTOR_NMI 2
#define X86_64_INTR_VECTOR_BREAKPOINT 3
#define X86_64_INTR_VECTOR_OVERFLOW 4
#define X86_64_INTR_VECTOR_BOUND_RANGE 5
#define X86_64_INTR_VECTOR_INVALID_OPCODE 6
#define X86_64_INTR_VECTOR_DEVICE_NOT_AVAILABLE 7
#define X86_64_INTR_VECTOR_DOUBLE_FAULT 8
#define X86_64_INTR_VECTOR_COPROCESSOR_SEGMENT_OVERRUN 9
#define X86_64_INTR_VECTOR_INVALID_TSS 10
#define X86_64_INTR_VECTOR_SEGMENT_NOT_PRESENT 11
#define X86_64_INTR_VECTOR_STACK_SEGMENT_FAULT 12
#define X86_64_INTR_VECTOR_GENERAL_PROTECTION 13
#define X86_64_INTR_VECTOR_PAGE_FAULT 14
#define X86_64_INTR_VECTOR_X87_FP_ERROR 16
#define X86_64_INTR_VECTOR_ALIGNMENT_CHECK 17
#define X86_64_INTR_VECTOR_MACHINE_CHECK 18
#define X86_64_INTR_VECTOR_SIMD_FP_EXCEPTION 19
#define X86_64_INTR_VECTOR_VIRTUALIZATION_EXCEPTION 20
#define X86_64_INTR_VECTOR_CONTROL_PROTECTION 21
#define X86_64_INTR_VECTOR_HYPERVISOR_INJECTION 28
#define X86_64_INTR_VECTOR_VMM_COMMUNICATION 29
#define X86_64_INTR_VECTOR_SECURITY_EXCEPTION 30

#define X86_64_INTR_PF_ERROR_PRESENT BIT(0)
#define X86_64_INTR_PF_ERROR_WRITE BIT(1)
#define X86_64_INTR_PF_ERROR_USER BIT(2)
#define X86_64_INTR_PF_ERROR_RSVD BIT(3)
#define X86_64_INTR_PF_ERROR_EXECUTE BIT(4)
#define X86_64_INTR_PF_ERROR_KNOWN \
	(X86_64_INTR_PF_ERROR_PRESENT | X86_64_INTR_PF_ERROR_WRITE | \
	 X86_64_INTR_PF_ERROR_USER | X86_64_INTR_PF_ERROR_RSVD | \
	 X86_64_INTR_PF_ERROR_EXECUTE)
#define X86_64_INTR_PF_ERROR_PLANE_REJECT \
	(X86_64_INTR_PF_ERROR_USER | X86_64_INTR_PF_ERROR_RSVD | \
	 X86_64_INTR_PF_ERROR_EXECUTE)

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stdint.h>

#include <plane/compiler.h>
#include <plane/vm_prot.h>

struct x86_64_intr_idt_entry {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t attributes;
	uint16_t offset_middle;
	uint32_t offset_high;
	uint32_t reserved;
} __packed;

struct x86_64_intr_idt_ptr {
	uint16_t limit;
	uint64_t base;
} __packed;

struct x86_64_intr_frame {
	/* Saved by interrupts.S before calling the C exception handler. */
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rbp;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;

	/* Pushed by the ISR stub. */
	uint64_t int_no;
	uint64_t error_code;

	/* Pushed by hardware. */
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
} __packed;

static inline uint8_t x86_64_intr_idt_attr(bool present,
					   uint8_t dpl,
					   uint8_t gate_type)
{
	uint8_t attr = gate_type & X86_64_INTR_GATE_ATTR_TYPE_MASK;

	if (present) {
		attr |= X86_64_INTR_GATE_ATTR_PRESENT;
	}

	attr |= (dpl & 0x03) << X86_64_INTR_GATE_ATTR_DPL_SHIFT;
	return attr;
}

static inline void
x86_64_intr_set_idt_entry(struct x86_64_intr_idt_entry *entry,
			  uintptr_t isr,
			  uint16_t selector,
			  uint8_t ist,
			  uint8_t attributes)
{
	entry->offset_low = (uint16_t)(isr & 0xffff);
	entry->selector = selector;
	entry->ist = ist & 0x07;
	entry->attributes = attributes;
	entry->offset_middle = (uint16_t)((isr >> 16) & 0xffff);
	entry->offset_high = (uint32_t)(isr >> 32);
	entry->reserved = 0;
}

static inline bool x86_64_intr_vector_is_exception(uint64_t vector)
{
	return vector < 32;
}

static inline bool x86_64_intr_pf_error_known(uint64_t error_code)
{
	return (error_code & ~X86_64_INTR_PF_ERROR_KNOWN) == 0;
}

static inline bool x86_64_intr_pf_error_plane_supported(uint64_t error_code)
{
	return x86_64_intr_pf_error_known(error_code) &&
	       (error_code & X86_64_INTR_PF_ERROR_PLANE_REJECT) == 0;
}

static inline uint32_t x86_64_intr_pf_error_fault_type(uint64_t error_code)
{
	uint32_t fault_type = PLANE_VM_PROT_READ;

	if ((error_code & X86_64_INTR_PF_ERROR_WRITE) != 0) {
		fault_type |= PLANE_VM_PROT_WRITE;
	}

	return fault_type;
}

#endif /* !__ASSEMBLER__ */

#endif /* HAL_X86_64_INTERRUPT_DEFS_H */
