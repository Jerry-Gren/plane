cpu_features_test_DEPS := hal/x86_64/cpu_features.c hal/x86_64/cpu.c hal/x86_64/msr.c klib/string.c tests/support/printk_stubs.c
cpu_features_test_PREREQS := include/hal/x86_64/cpuid_defs.h
