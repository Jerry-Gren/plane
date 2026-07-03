#ifndef TESTS_SUPPORT_MM_TEST_HOOKS_H
#define TESTS_SUPPORT_MM_TEST_HOOKS_H

#include <stdint.h>

#ifndef PLANE_HOST_TEST
#error "mm_test_hooks.h is only for host unit tests"
#endif

void plane_kernel_map_test_reset(void);
void plane_kmem_test_reset(void);
uint64_t x86_64_pmap_test_current_root_phys(void);

#endif /* TESTS_SUPPORT_MM_TEST_HOOKS_H */
