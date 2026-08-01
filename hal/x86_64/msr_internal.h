#ifndef HAL_X86_64_MSR_INTERNAL_H
#define HAL_X86_64_MSR_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

uint64_t x86_64_msr_read(uint32_t msr);
bool x86_64_msr_write(uint32_t msr, uint64_t value);

#endif
