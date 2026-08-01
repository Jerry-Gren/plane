.section .text
.global hal_cpu_enter_on_stack
.type hal_cpu_enter_on_stack, @function
hal_cpu_enter_on_stack:
    movq %rdi, %rsp
    andq $~0xf, %rsp
    xorq %rbp, %rbp
    movq %rdx, %rdi
    call *%rsi
.Lhal_cpu_enter_on_stack_hang:
    cli
    hlt
    jmp .Lhal_cpu_enter_on_stack_hang
