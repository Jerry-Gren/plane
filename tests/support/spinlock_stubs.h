#ifndef TEST_SUPPORT_SPINLOCK_STUBS_H
#define TEST_SUPPORT_SPINLOCK_STUBS_H

#include <stdint.h>

void test_spinlock_stub_reset_counts(void);
uint64_t test_spinlock_stub_irqsave_count(void);
uint64_t test_spinlock_stub_irqrestore_count(void);
uint64_t test_spinlock_stub_irqsave_depth(void);
uint64_t test_spinlock_stub_irqsave_max_depth(void);

#endif /* TEST_SUPPORT_SPINLOCK_STUBS_H */
