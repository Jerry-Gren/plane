.section .text
.global ml_cpu_enter_on_stack
.type ml_cpu_enter_on_stack, @function
ml_cpu_enter_on_stack:
    movq %rdi, %rsp
    andq $~0xf, %rsp
    xorq %rbp, %rbp
    movq %rdx, %rdi
    call *%rsi
.Lml_cpu_enter_on_stack_hang:
    cli
    hlt
    jmp .Lml_cpu_enter_on_stack_hang
