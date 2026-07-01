#ifndef HAL_X86_64_LINKAGE_H
#define HAL_X86_64_LINKAGE_H

#ifndef __ASSEMBLER__

extern char __kernel_text_start[];
extern char __kernel_text_end[];
extern char __kernel_phys_start[];
extern char __kernel_phys_end[];

#endif /* __ASSEMBLER__ */

#endif /* HAL_X86_64_LINKAGE_H */
