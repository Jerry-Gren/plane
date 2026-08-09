cpu_features_test_DEPS := arch/x86_64/cpu_features.c arch/x86_64/cpu.c arch/x86_64/proc_reg.c klib/string.c tests/support/printk_stubs.c
cpu_features_test_PREREQS := arch/x86_64/proc_reg.h include/x86_64/cpuid_defs.h
