#ifndef HAL_ARCH_EXCEPTION_H
#define HAL_ARCH_EXCEPTION_H

#include <stdbool.h>
#include <stdint.h>

#include <plane/address.h>
#include <hal/x86_64/interrupt_defs.h>

bool x86_64_try_handle_page_fault(uint64_t int_no,
				  plane_vaddr_t fault_addr,
				  uint64_t error_code);
void x86_64_exception_handler(struct x86_64_intr_frame *frame);

#endif /* HAL_ARCH_EXCEPTION_H */
