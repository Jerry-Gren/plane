cpu_features_test_DEPS := arch/x86_64/cpu_features.c arch/x86_64/cpu.c arch/x86_64/msr.c klib/string.c tests/support/printk_stubs.c
cpu_features_test_PREREQS := arch/x86_64/msr.h include/x86_64/cpuid_defs.h
