#define ASM_FILE           1
#include <multiboot2.h>

#include <hal/x86_64/boot/multiboot2/mb2_early_mmu.h>
#include <hal/x86_64/cpuid_defs.h>
#include <hal/x86_64/descriptor_defs.h>
#include <hal/x86_64/msr_defs.h>
#include <hal/x86_64/processor_defs.h>
#include <plane/bits.h>

/* Macros */
#define MB2_FB_TAG_SIZE    20
#define MB2_END_TAG_SIZE   8
#define MB2_FB_DEPTH       32

#define X86_64_MB2_BOOT_STACK_SIZE 16384
#define X86_64_MB2_GDT_ALIGN       8

/* Multiboot2 header */
.code32
.section .multiboot_header
.align MULTIBOOT_HEADER_ALIGN
header_start:
    .long MULTIBOOT2_HEADER_MAGIC       /* magic number (multiboot 2) */
    .long MULTIBOOT_ARCHITECTURE_I386   /* protected mode i386 */
    .long header_end - header_start
    /* checksum */
    .long - (MULTIBOOT2_HEADER_MAGIC + MULTIBOOT_ARCHITECTURE_I386 + (header_end - header_start))

    /* insert optional multiboot tags here */
    /* framebuffer tag */
    .align MULTIBOOT_TAG_ALIGN
    .word MULTIBOOT_HEADER_TAG_FRAMEBUFFER /* type */
    .word 0                                /* flags */
    .long MB2_FB_TAG_SIZE                  /* size */
    .long 0                                /* width (0 = Let GRUB decide) */
    .long 0                                /* height (0 = Let GRUB decide) */
    .long MB2_FB_DEPTH                     /* depth */

    /* required end tag */
    .align MULTIBOOT_TAG_ALIGN
    .word MULTIBOOT_HEADER_TAG_END /* type */
    .word 0                        /* flags */
    .long MB2_END_TAG_SIZE         /* size */
header_end:

/* Section data */
.section .boot.data, "aw"
.align 4
mb2_magic: .long 0
mb2_info:  .long 0

/* Section bss */
.section .boot.bss, "aw", @nobits
.global x86_64_mb2_early_pml4
.global x86_64_mb2_early_pd_kernel
.global x86_64_mb2_early_pd_fb
.align ARCH_PAGE_SIZE
x86_64_mb2_early_pml4:      .skip ARCH_PAGE_SIZE
early_pdpt_ident:           .skip ARCH_PAGE_SIZE
early_pdpt_high:            .skip ARCH_PAGE_SIZE
early_pdpt_direct:          .skip ARCH_PAGE_SIZE
early_pd_ident:             .skip ARCH_PAGE_SIZE
x86_64_mb2_early_pd_kernel: .skip ARCH_PAGE_SIZE
x86_64_mb2_early_pd_fb:     .skip ARCH_PAGE_SIZE
early_pd_direct:            .skip (X86_64_DIRECT_MAP_MAX_SIZE / ARCH_HUGE_PAGE_SIZE) * ARCH_PAGE_SIZE

.align 16
stack_bottom:
    .skip X86_64_MB2_BOOT_STACK_SIZE
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
    mov $__kernel_boot_bss_start, %edi
    mov $__kernel_boot_bss_end, %ecx
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
    xorl $X86_64_RFLAGS_ID, %eax
    pushl %eax
    popfl
    pushfl
    popl %eax
    pushl %ecx
    popfl
    cmpl %ecx, %eax
    je .Lno_long_mode

    /* check for MSR and PAT support before writing EFER and IA32_PAT */
    mov $X86_64_CPUID_LEAF_BASIC_MAX, %eax
    cpuid
    cmp $X86_64_CPUID_LEAF_FEATURES, %eax
    jb .Lno_long_mode
    mov $X86_64_CPUID_LEAF_FEATURES, %eax
    cpuid
    test $X86_64_CPUID_1_EDX_MSR, %edx
    jz .Lno_long_mode
    test $X86_64_CPUID_1_EDX_PAT, %edx
    jz .Lno_long_mode

    /* check for long mode support */
    mov $X86_64_CPUID_LEAF_EXT_MAX, %eax
    cpuid
    cmp $X86_64_CPUID_LEAF_EXT_FEATURES, %eax
    jb .Lno_long_mode
    mov $X86_64_CPUID_LEAF_EXT_FEATURES, %eax
    cpuid
    test $X86_64_CPUID_EXT_1_EDX_LM, %edx
    jz .Lno_long_mode

    /* setup page table */
    /* PML4 */
    mov $x86_64_mb2_early_pml4, %edi

    mov $early_pdpt_ident, %eax
    or $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE), %eax
    mov %eax, 0(%edi) /* PML4[0] -> early_pdpt_ident */

    mov $early_pdpt_high, %eax
    or $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE), %eax
    mov %eax, (X86_64_PAGING_PML4_INDEX(KERNEL_VMA_BASE) * 8)(%edi) /* dynamic PML4 index */

    mov $early_pdpt_direct, %eax
    or $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE), %eax
    mov %eax, (X86_64_PAGING_PML4_INDEX(X86_64_DIRECT_MAP_BASE) * 8)(%edi)

    /* PDPT */
    mov $early_pdpt_ident, %edi
    mov $early_pd_ident, %eax
    or $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE), %eax
    mov %eax, 0(%edi) /* early_pdpt_ident[0] -> early_pd_ident */

    mov $early_pdpt_high, %edi
    mov $x86_64_mb2_early_pd_kernel, %eax
    or $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE), %eax
    mov %eax, (X86_64_PAGING_PDPT_INDEX(KERNEL_VMA_BASE) * 8)(%edi)

#if X86_64_PAGING_PDPT_INDEX(KERNEL_VMA_BASE) != X86_64_PAGING_PDPT_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE)
    mov $x86_64_mb2_early_pd_fb, %eax
    or $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE), %eax
    mov %eax, (X86_64_PAGING_PDPT_INDEX(X86_64_MB2_FRAMEBUFFER_VMA_BASE) * 8)(%edi)
#endif

    mov $early_pdpt_direct, %edi
    mov $early_pd_direct, %eax
    or $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE), %eax
    mov $(X86_64_DIRECT_MAP_MAX_SIZE / ARCH_HUGE_PAGE_SIZE), %ecx
.Lfill_direct_pdpt:
    mov %eax, (%edi)
    movl $0, 4(%edi)
    add $ARCH_PAGE_SIZE, %eax
    add $8, %edi
    loop .Lfill_direct_pdpt

    /* PDT */
    mov mb2_info, %ebx /* cpuinfo will overwrite ebx, load manually */
    mov %ebx, %eax
    add (%ebx), %eax
    mov $(__kernel_bss_end - KERNEL_VMA_BASE), %edx
    cmp %edx, %eax
    jae .Lmax_found
    mov %edx, %eax
.Lmax_found:
    add $(ARCH_LARGE_PAGE_SIZE - 1), %eax
    shr $ARCH_LARGE_PAGE_SHIFT, %eax
    mov %eax, %ecx

    cmp $X86_64_PAGING_TABLE_ENTRIES, %ecx
    jbe .Lpages_ok
    mov $X86_64_PAGING_TABLE_ENTRIES, %ecx
.Lpages_ok:

    push %ecx
    mov $early_pd_ident, %edi
    mov $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE | X86_64_PAGING_ENTRY_PS), %eax
.Lfill_ident_pd:
    mov %eax, (%edi)
    add $ARCH_LARGE_PAGE_SIZE, %eax
    add $8, %edi
    loop .Lfill_ident_pd
    pop %ecx

    mov $x86_64_mb2_early_pd_kernel, %edi
    add $(X86_64_PAGING_PD_INDEX(KERNEL_VMA_BASE) * 8), %edi
    mov $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE | X86_64_PAGING_ENTRY_PS), %eax
.Lfill_kernel_pd:
    mov %eax, (%edi)
    add $ARCH_LARGE_PAGE_SIZE, %eax
    add $8, %edi
    loop .Lfill_kernel_pd

    mov $early_pd_direct, %edi
    mov $(X86_64_PAGING_ENTRY_PRESENT | X86_64_PAGING_ENTRY_WRITE | X86_64_PAGING_ENTRY_PS), %eax
    mov $(X86_64_DIRECT_MAP_MAX_SIZE / ARCH_LARGE_PAGE_SIZE), %ecx
.Lfill_direct_pd:
    mov %eax, (%edi)
    movl $0, 4(%edi)
    add $ARCH_LARGE_PAGE_SIZE, %eax
    add $8, %edi
    loop .Lfill_direct_pd

    /* CR3 = PML4 */
    mov $x86_64_mb2_early_pml4, %eax
    mov %eax, %cr3
    /* enable PAE */
    mov %cr4, %eax
    or $X86_64_CR4_PAE, %eax
    mov %eax, %cr4
    /* enable long mode */
    mov $X86_64_MSR_IA32_EFER, %ecx
    rdmsr
    or $X86_64_MSR_IA32_EFER_LME, %eax
    wrmsr
    /*
     * Early framebuffer write-combining setup.
     *
     * Plane currently gives framebuffer PDEs PWT=1, selecting PAT entry 1.
     * Program that entry as WC here as a boot-time shortcut. This should move
     * behind a real cache-attribute / kernel IO-map policy when pmap grows
     * PAT-aware mappings.
     */
    mov $X86_64_MSR_IA32_CR_PAT, %ecx
    rdmsr
    and $~X86_64_MSR_IA32_CR_PAT_ENTRY_MASK(1), %eax
    mov $X86_64_MSR_IA32_CR_PAT_MEMORY_WC, %esi
    shl $X86_64_MSR_IA32_CR_PAT_ENTRY_SHIFT(1), %esi
    or  %esi, %eax
    wrmsr
    /* enable paging and protected mode */
    mov %cr0, %eax
    or $(X86_64_CR0_PG | X86_64_CR0_PE), %eax
    mov %eax, %cr0

    /* load 64-bit gdt */
    lgdt gdt64_pointer
    ljmp $X86_64_DESC_SELECTOR_KERNEL_CS, $.Ltrampoline64

.Lno_long_mode:
    cli
.Lhlt32:
    hlt
    jmp .Lhlt32

.code64
.Ltrampoline64:
    mov $X86_64_DESC_SELECTOR_KERNEL_DS, %ax
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
    movabs $__kernel_bss_start, %rdi
    movabs $__kernel_bss_end, %rcx
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
.align X86_64_MB2_GDT_ALIGN
gdt64:
    /* null 0x00 */
    .Lnull_desc:
        .quad 0

    /* code 0x08 */
    .Lcode_desc:
        .word 0x0000      /* limit, ignored in 64 bit mode */
        .word 0x0000      /* base, ignored in 64 bit mode */
        .byte 0x00        /* base */
        .byte (X86_64_DESC_ACCESS_PRESENT | X86_64_DESC_ACCESS_CODE_DATA | X86_64_DESC_TYPE_CODE_XR)
        .byte (X86_64_DESC_FLAGS_GRAN_4K | X86_64_DESC_FLAGS_LONG_MODE)
        .byte 0x00        /* base */

    /* data 0x10 */
    .Ldata_desc:
        .word 0x0000      /* limit, ignored in 64 bit mode */
        .word 0x0000      /* base, ignored in 64 bit mode */
        .byte 0x00        /* base */
        .byte (X86_64_DESC_ACCESS_PRESENT | X86_64_DESC_ACCESS_CODE_DATA | X86_64_DESC_TYPE_DATA_RW)
        .byte (X86_64_DESC_FLAGS_GRAN_4K | X86_64_DESC_FLAGS_DEFAULT_BIG)
        .byte 0x00        /* base */

gdt64_pointer:
    .word gdt64_pointer - gdt64 - 1
    .quad gdt64
