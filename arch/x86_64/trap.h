#ifndef X86_64_TRAP_H
#define X86_64_TRAP_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <x86_64/interrupt_defs.h>

bool x86_64_trap_try_handle_page_fault(uint64_t int_no,
				       plane_vaddr_t fault_addr,
				       uint64_t error_code);
void x86_64_trap_handler(struct x86_64_intr_frame *frame);

#endif /* X86_64_TRAP_H */
