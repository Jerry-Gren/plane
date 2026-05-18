#include <hal/x86_64/arch_mmu.h>

/* Tell the linker that we want an x86_64 ELF64 output file */
OUTPUT_FORMAT(elf64-x86-64)

/* We want the symbol _start to be our entry point */
ENTRY(_start)

/* Define the program headers we want so the bootloader gives us the right */
/* MMU permissions; this also allows us to exert more control over the linking */
/* process. */
PHDRS
{
    boot_text PT_LOAD FLAGS(5);
    boot_data PT_LOAD FLAGS(6);
    text      PT_LOAD FLAGS(5);
    rodata    PT_LOAD FLAGS(4);
    data      PT_LOAD FLAGS(6);
}

SECTIONS {
    /* Physical address */
    . = 1M;

    .boot.text : {
        KEEP(*(.multiboot_header))
        *(.boot.text)
    } :boot_text

    . = ALIGN(4K);
    .boot.data : {
        *(.boot.rodata)
        *(.boot.data)
    } :boot_data

    . = ALIGN(4K);
    .boot.bss : {
        __boot_bss_start = .;
        *(.boot.bss)
        __boot_bss_end = .;
    } :boot_data

    /* Virtual address */
    . += KERNEL_VMA_BASE;

    . = ALIGN(4K);
    .text : AT(ADDR(.text) - KERNEL_VMA_BASE) {
        *(.text .text.*)
    } :text

    . = ALIGN(4K);
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA_BASE) {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(4K);
    .data : AT(ADDR(.data) - KERNEL_VMA_BASE) {
        *(.data .data.*)
    } :data

    . = ALIGN(4K);
    .bss : AT(ADDR(.bss) - KERNEL_VMA_BASE) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    /* Discard .note.* and .eh_frame* since they may cause issues on some hosts. */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
    }
}