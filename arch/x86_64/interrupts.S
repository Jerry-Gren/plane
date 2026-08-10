.section .text

.extern x86_64_trap_handler

/* hardware does not push an error code, manually push 0 */
.macro ISR_NOERRCODE vector
.global x86_64_isr\vector
x86_64_isr\vector:
    pushq $0            /* a dummy error code */
    pushq $\vector
    jmp isr_common_stub
.endm

/* hardware handles error code */
.macro ISR_ERRCODE vector
.global x86_64_isr\vector
x86_64_isr\vector:
    pushq $\vector
    jmp isr_common_stub
.endm

.macro ISR_EXTERNAL vector
.global x86_64_isr\vector
x86_64_isr\vector:
    pushq $0            /* a dummy error code */
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
 * External vectors 32-255 do not push an error code. Plane gives each vector
 * its own small stub so the C dispatcher sees the real external vector.
 */
.irp vector,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
ISR_EXTERNAL \vector
.endr

.section .rodata
.global x86_64_isr_external_stub_table
x86_64_isr_external_stub_table:
.irp vector,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
    .quad x86_64_isr\vector
.endr

.section .text

/*
 * common stub
 * stack status now: [int_no][error_code][hardware-handled regs] <- stack bottom
 *                   ^ sp
 * The pushes below complete struct x86_64_intr_frame.
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

    /* rsp now points to the x86_64_intr_frame structure */
    movq %rsp, %rdi
    call x86_64_trap_handler

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
