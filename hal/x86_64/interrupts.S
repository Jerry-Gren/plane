.section .text

.extern x86_64_exception_handler

/* hardware does not push an error code, manually push 0 */
.macro ISR_NOERRCODE vector
.global isr\vector
isr\vector:
    pushq $0            /* a dummy error code */
    pushq $\vector
    jmp isr_common_stub
.endm

/* hardware handles error code */
.macro ISR_ERRCODE vector
.global isr\vector
isr\vector:
    pushq $\vector
    jmp isr_common_stub
.endm

ISR_NOERRCODE 0     /* #DE: Divide Error
                     *      Intel: DIV and IDIV instructions.
                     *      AMD: DIV, IDIV, AAM instructions.
                     */
ISR_NOERRCODE 1     /* #DB: Debug Exception
                     *      Intel: Instruction, data, and I/O breakpoints; single-step; and others.
                     *      AMD: Instruction accesses and data accesses.
                     */
ISR_NOERRCODE 2     /* NMI Interrupt
                     *      Intel: Nonmaskable external interrupt.
                     *      AMD: External NMI signal.
                     */
ISR_NOERRCODE 3     /* #BP: Breakpoint
                     *      Intel: INT3 instruction.
                     *      AMD: INT3 instruction.
                     */
ISR_NOERRCODE 4     /* #OF: Overflow
                     *      Intel: INTO instruction.
                     *      AMD: INTO instruction.
                     */
ISR_NOERRCODE 5     /* #BR: BOUND Range Exceeded
                     *      Intel: BOUND instruction.
                     *      AMD: BOUND instruction.
                     */
ISR_NOERRCODE 6     /* #UD: Invalid Opcode
                     *      Intel: UD instruction or reserved opcode.
                     *      AMD: Invalid instructions.
                     */
ISR_NOERRCODE 7     /* #NM: Device Not Available
                     *      Intel: Floating-point or WAIT/FWAIT instruction
                     *      AMD: x87 instructions.
                     */
ISR_ERRCODE   8     /* #DF: Double Fault
                     *      Intel: Any instruction that can generate an exception, an NMI, or an INTR.
                     *      AMD: Exception during the handling of another exception or interrupt.
                     */
ISR_NOERRCODE 9     /* Coprocessor Segment Overrun (reserved)
                     *      Intel: Floating-point instruction.
                     *      AMD: Unsupported (Reserved).
                     */
ISR_ERRCODE   10    /* #TS: Invalid TSS
                     *      Intel: Task switch or TSS access.
                     *      AMD: Task-state segment access and task switch.
                     */
ISR_ERRCODE   11    /* #NP: Segment Not Present
                     *      Intel: Loading segment registers or accessing system segments.
                     *      AMD: Segment register loads.
                     */
ISR_ERRCODE   12    /* #SS: Stack-Segment Fault
                     *      Intel: Stack operations and SS register loads.
                     *      AMD: SS register loads and stack references.
                     */
ISR_ERRCODE   13    /* #GP: General Protection Fault
                     *      Intel: Any memory reference and other protection checks.
                     *      AMD: Memory accesses and protection checks.
                     */
ISR_ERRCODE   14    /* #PF: Page Fault
                     *      Intel: Any memory reference.
                     *      AMD: Memory accesses when paging enabled.
                     */
ISR_NOERRCODE 15    /* Reserved */
ISR_NOERRCODE 16    /* #MF: x87 FPU Floating-Point Error
                     *      Intel: x87 FPU floating-point or WAIT/FWAIT instruction.
                     *      AMD: x87 floating-point instructions.
                     */
ISR_ERRCODE   17    /* #AC: Alignment Check
                     *      Intel: Any data reference in memory.
                     *      AMD: Misaligned memory accesses.
                     */
ISR_NOERRCODE 18    /* #MC: Machine Check
                     *      Intel: Error codes (if any) and source are model dependent.
                     *      AMD: Model specific.
                     */
ISR_NOERRCODE 19    /* #XM: SIMD Floating-Point Exception
                     *      Intel: SSE/SSE2/SSE3 floating-point instructions.
                     *      AMD: SSE floating-point instructions.
                     */
ISR_NOERRCODE 20    /* #VE: Virtualization Exception
                     *      Intel: EPT violations.
                     *      AMD: Reserved.
                     */
ISR_ERRCODE   21    /* #CP: Control Protection Exception
                     *      Intel:
                     *      RET, IRET, RSTORSSP, and SETSSBSY instructions can generate this exception.
                     *      When CET indirect branch tracking is enabled,
                     *      this exception can be generated due to a missing ENDBRANCH instruction
                     *      at target of an indirect call or jump.
                     *      AMD:
                     *      RET/IRET or other control transfer.
                     */
ISR_NOERRCODE 22    /* Reserved */
ISR_NOERRCODE 23    /* Reserved */
ISR_NOERRCODE 24    /* Reserved */
ISR_NOERRCODE 25    /* Reserved */
ISR_NOERRCODE 26    /* Reserved */
ISR_NOERRCODE 27    /* Reserved */
ISR_NOERRCODE 28    /* #HV: Hypervisor Injection Exception
                     *      Intel: Reserved.
                     *      AMD: Event injection.
                     */
ISR_ERRCODE   29    /* #VC: VMM Communication Exception
                     *      Intel: Reserved.
                     *      AMD: Virtualization event.
                     */
ISR_ERRCODE   30    /* #SX: Security Exception
                     *      Intel: Reserved.
                     *      AMD: Security-sensitive event in host.
                     */
ISR_NOERRCODE 31    /* Reserved */

/*
 * for vector 32-255
 * manually push error code 0
 * and a fake vector number 256
 */
.global isr_default
isr_default:
    pushq $0            /* a dummy error code */
    pushq $256
    jmp isr_common_stub

/* 
 * common stub
 * stack status now: [int_no][error_code][hardware-handled regs] <- stack bottom
 *                   ^ sp
 */
isr_common_stub:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* rsp now points to the interrupt_frame structure */
    movq %rsp, %rdi
    call x86_64_exception_handler

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /* pop int_no and error_code */
    addq $16, %rsp

    /* resume */
    iretq
