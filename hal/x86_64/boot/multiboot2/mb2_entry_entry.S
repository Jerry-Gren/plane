#define ASM_FILE           1
#include <multiboot2.h>

#include <plane/mm.h>

/* Macros */
#define MB2_MAGIC          MULTIBOOT2_HEADER_MAGIC
#define MB2_ARCH_I386      MULTIBOOT_ARCHITECTURE_I386
#define MB2_TAG_END_TYPE   MULTIBOOT_HEADER_TAG_END
#define MB2_TAG_FB_TYPE    MULTIBOOT_HEADER_TAG_FRAMEBUFFER

#define CR4_PAE_ENABLE     (1 << 5)
#define EFER_MSR           0xc0000080
#define EFER_LM_ENABLE     (1 << 8)
#define CR0_PAGING_ENABLE  (1 << 31)
#define CR0_PROTECTED_MODE (1 << 0)

# /* DO NOT MODIFY THESE TWO LINES */
# #define X86_PD_PAGE_SIZE  0x200000
# #define X86_PD_PAGE_SHIFT 21

#define GDT_PRESENT        (1 << 7)
#define GDT_NOT_SYS        (1 << 4)
#define GDT_EXEC           (1 << 3)
#define GDT_RW             (1 << 1)
#define GDT_GRAN_4K        (1 << 7)
#define GDT_LONG_MODE      (1 << 5)
#define GDT_SZ_32          (1 << 6)

#define CPUID_EXTENSIONS      0x80000000
#define CPUID_EXT_FEATURES    0x80000001
#define CPUID_EDX_EXT_FEAT_LM (1 << 29)

#define MSR_IA32_CR_PAT       0x277
#define PAT_WC                0x01
#define PAT1_SHIFT            8
#define PAT1_MASK_CLEAR       0xffff00ff

/* Multiboot2 header */
.code32
.section .multiboot_header
.align 8
header_start:
    .long MB2_MAGIC              /* magic number (multiboot 2) */
    .long MB2_ARCH_I386          /* architecture 0 (protected mode i386) */
    .long header_end - header_start
    /* checksum */
    .long - (MB2_MAGIC + MB2_ARCH_I386 + (header_end - header_start))

    /* insert optional multiboot tags here */
    /* framebuffer tag */
    .align 8
    .word MB2_TAG_FB_TYPE /* type */
    .word 0               /* flags */
    .long 20              /* size */
    .long 0               /* width (0 = Let GRUB decide) */
    .long 0               /* height (0 = Let GRUB decide) */
    .long 32              /* depth */

    /* required end tag */
    .align 8
    .word MB2_TAG_END_TYPE /* type */
    .word 0                /* flags */
    .long 8                /* size */
header_end:

/* Section data */
.section .boot.data, "aw"
.align 4
mb2_magic: .long 0
mb2_info:  .long 0

/* Section bss */
.section .boot.bss, "aw", @nobits
.global early_pml4
.global early_pd_kernel
.global early_pd_fb
.align 4096
early_pml4:       .skip 4096
early_pdpt_ident: .skip 4096
early_pdpt_high:  .skip 4096
early_pd_ident:   .skip 4096
early_pd_kernel:  .skip 4096
early_pd_fb:      .skip 4096

.align 16
stack_bottom:
    .skip 16384
stack_top:

/* Section text */
.section .boot.text, "ax"
.global _start
_start:
    cli
    cld

    /* save params received from GRUB */
    mov %eax, mb2_magic /* MB2 Magic */
    mov %ebx, mb2_info /* MB2 Info Structure Address */

    /* clear .boot.bss */
    mov $__boot_bss_start, %edi
    mov $__boot_bss_end, %ecx
    sub %edi, %ecx
    shr $2, %ecx
    xor %eax, %eax
    rep stosl /* length can be divide without remainder */

    /* initialize the stack pointer */
    mov $stack_top, %esp

    /*
     * checks if CPUID is supported by attempting to flip the ID bit (bit 21) in
     * the EFLAGS register. If we can flip it, CPUID is available.
     * returns eax = 1 if there is cpuid support; 0 otherwise
     */
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $(1 << 21), %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    pushl %ecx
    popfl
    cmpl %ecx, %eax
    je .Lno_long_mode

    /* check for long mode support */
    mov $CPUID_EXTENSIONS, %eax
    cpuid
    cmp $CPUID_EXT_FEATURES, %eax
    jb .Lno_long_mode
    mov $CPUID_EXT_FEATURES, %eax
    cpuid
    test $CPUID_EDX_EXT_FEAT_LM, %edx
    jz .Lno_long_mode

    /* setup page table */
    /* PML4 */
    mov $early_pml4, %edi

    mov $early_pdpt_ident, %eax
    or $(PAGE_PRESENT | PAGE_RW), %eax
    mov %eax, 0(%edi) /* PML4[0] -> early_pdpt_ident */

    mov $early_pdpt_high, %eax
    or $(PAGE_PRESENT | PAGE_RW), %eax
    mov %eax, (PML4_INDEX(KERNEL_VMA_BASE) * 8)(%edi) /* dynamic PML4 index */

    /* PDPT */
    mov $early_pdpt_ident, %edi
    mov $early_pd_ident, %eax
    or $(PAGE_PRESENT | PAGE_RW), %eax
    mov %eax, 0(%edi) /* early_pdpt_ident[0] -> early_pd_ident */

    mov $early_pdpt_high, %edi
    mov $early_pd_kernel, %eax
    or $(PAGE_PRESENT | PAGE_RW), %eax
    mov %eax, (PDPT_INDEX(KERNEL_VMA_BASE) * 8)(%edi)

#if PDPT_INDEX(KERNEL_VMA_BASE) != PDPT_INDEX(FRAMEBUFFER_VMA_BASE)
    mov $early_pd_fb, %eax
    or $(PAGE_PRESENT | PAGE_RW), %eax
    mov %eax, (PDPT_INDEX(FRAMEBUFFER_VMA_BASE) * 8)(%edi)
#endif

    /* PDT */
    # mov $boot_pd, %edi
    # mov $(PAGE_PRESENT | PAGE_READWRITE | PAGE_PS), %eax
    # mov $512, %ecx
    mov mb2_info, %ebx /* cpuinfo will overwrite ebx, load manually */
    mov %ebx, %eax
    add (%ebx), %eax
    mov $(__bss_end - KERNEL_VMA_BASE), %edx
    cmp %edx, %eax
    jae .Lmax_found
    mov %edx, %eax
.Lmax_found:
    add $(ARCH_LARGE_PAGE_SIZE - 1), %eax
    shr $ARCH_LARGE_PAGE_SHIFT, %eax
    mov %eax, %ecx

    cmp $512, %ecx
    jbe .Lpages_ok
    mov $512, %ecx
.Lpages_ok:

    push %ecx
    mov $early_pd_ident, %edi
    mov $(PAGE_PRESENT | PAGE_RW | PAGE_PS), %eax
.Lfill_ident_pd:
    mov %eax, (%edi)
    add $ARCH_LARGE_PAGE_SIZE, %eax
    add $8, %edi
    loop .Lfill_ident_pd
    pop %ecx

    mov $early_pd_kernel, %edi
    add $(PD_INDEX(KERNEL_VMA_BASE) * 8), %edi
    mov $(PAGE_PRESENT | PAGE_RW | PAGE_PS), %eax
.Lfill_kernel_pd:
    mov %eax, (%edi)
    add $ARCH_LARGE_PAGE_SIZE, %eax
    add $8, %edi
    loop .Lfill_kernel_pd

    /* CR3 = PML4 */
    mov $early_pml4, %eax
    mov %eax, %cr3
    /* enable PAE */
    mov %cr4, %eax
    or $CR4_PAE_ENABLE, %eax
    mov %eax, %cr4
    /* enable long mode */
    mov $EFER_MSR, %ecx
    rdmsr
    or $EFER_LM_ENABLE, %eax
    wrmsr
    /* enable write-combining */
    mov $MSR_IA32_CR_PAT, %ecx
    rdmsr
    and $PAT1_MASK_CLEAR, %eax
    or  $(PAT_WC << PAT1_SHIFT), %eax
    wrmsr
    /* enable paging and protected mode */
    mov %cr0, %eax
    or $(CR0_PAGING_ENABLE | CR0_PROTECTED_MODE), %eax
    mov %eax, %cr0

    /* load 64-bit gdt */
    lgdt gdt64_pointer
    ljmp $0x08, $.Ltrampoline64

.Lno_long_mode:
    cli
.Lhlt32:
    hlt
    jmp .Lhlt32

.code64
.Ltrampoline64:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    movabs $realm64, %rax
    jmp *%rax

.section .text
realm64:
    /* initialize sp */
    movabs $KERNEL_VMA_BASE, %rax
    add %rax, %rsp

    and $-16, %rsp

    /* clear .bss */
    movabs $__bss_start, %rdi
    movabs $__bss_end, %rcx
    sub %rdi, %rcx
    xor %rax, %rax
    rep stosb             /* ensure every byte is cleared */

    /* prepare for params */
    movabs $mb2_magic, %rax
    mov (%rax), %edi      /* arg 1: rdi = Magic */
    
    movabs $mb2_info, %rax
    mov (%rax), %esi      /* arg 2: rsi = Info Structure Address */

    movabs $mb2_entry, %rax
    call *%rax

    cli
.Lhlt64:
    hlt
    jmp .Lhlt64

/* GDT */
.section .boot.rodata, "a"
.align 8
gdt64:
    /* null 0x00 */
    .Lnull_desc:
        .quad 0

    /* code 0x08 */
    .Lcode_desc:
        .word 0x0000      /* limit, ignored in 64 bit mode */
        .word 0x0000      /* base, ignored in 64 bit mode */
        .byte 0x00        /* base */
        .byte (GDT_PRESENT | GDT_NOT_SYS | GDT_EXEC | GDT_RW) /* access byte */
        .byte (GDT_GRAN_4K | GDT_LONG_MODE)                   /* flags & limit */
        .byte 0x00        /* base */

    /* data 0x10 */
    .Ldata_desc:
        .word 0x0000      /* limit, ignored in 64 bit mode */
        .word 0x0000      /* base, ignored in 64 bit mode */
        .byte 0x00        /* base */
        .byte (GDT_PRESENT | GDT_NOT_SYS | GDT_RW)            /* access byte */
        .byte (GDT_GRAN_4K | GDT_SZ_32)                       /* flags & limit */
        .byte 0x00        /* base */

gdt64_pointer:
    .word gdt64_pointer - gdt64 - 1
    .quad gdt64