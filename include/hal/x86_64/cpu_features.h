#ifndef HAL_ARCH_CPU_FEATURES_H
#define HAL_ARCH_CPU_FEATURES_H

#ifndef __ASSEMBLER__

#include <stdbool.h>
#include <stdint.h>

#define X86_64_CPU_VENDOR_LENGTH 12
#define X86_64_CPU_BRAND_LENGTH  48

enum x86_64_cpu_vendor {
	X86_64_CPU_VENDOR_UNKNOWN,
	X86_64_CPU_VENDOR_INTEL,
	X86_64_CPU_VENDOR_AMD
};

enum x86_64_cpu_feature {
	/* CPUID.01H:EDX common feature flags */
	X86_64_CPU_FEATURE_FPU,
	X86_64_CPU_FEATURE_TSC,
	X86_64_CPU_FEATURE_MSR,
	X86_64_CPU_FEATURE_PAE,
	X86_64_CPU_FEATURE_CX8,
	X86_64_CPU_FEATURE_APIC,
	X86_64_CPU_FEATURE_MTRR,
	X86_64_CPU_FEATURE_PGE,
	X86_64_CPU_FEATURE_CMOV,
	X86_64_CPU_FEATURE_PAT,
	X86_64_CPU_FEATURE_CLFLUSH,
	X86_64_CPU_FEATURE_FXSR,
	X86_64_CPU_FEATURE_SSE,
	X86_64_CPU_FEATURE_SSE2,
	X86_64_CPU_FEATURE_HTT,

	/* CPUID.01H:ECX common feature flags */
	X86_64_CPU_FEATURE_SSE3,
	X86_64_CPU_FEATURE_PCLMULQDQ,
	X86_64_CPU_FEATURE_SSSE3,
	X86_64_CPU_FEATURE_FMA,
	X86_64_CPU_FEATURE_CX16,
	X86_64_CPU_FEATURE_SSE4_1,
	X86_64_CPU_FEATURE_SSE4_2,
	X86_64_CPU_FEATURE_X2APIC,
	X86_64_CPU_FEATURE_MOVBE,
	X86_64_CPU_FEATURE_POPCNT,
	X86_64_CPU_FEATURE_AES,
	X86_64_CPU_FEATURE_XSAVE,
	X86_64_CPU_FEATURE_OSXSAVE,
	X86_64_CPU_FEATURE_AVX,
	X86_64_CPU_FEATURE_F16C,
	X86_64_CPU_FEATURE_RDRAND,

	/* Hypervisor-present environment hint, not a hardware capability. */
	X86_64_CPU_FEATURE_HYPERVISOR,

	/* CPUID.01H:ECX Intel-defined feature flags */
	X86_64_CPU_FEATURE_INTEL_PCID,
	X86_64_CPU_FEATURE_INTEL_TSC_DEADLINE,

	/* CPUID.07H.00H structured extended common feature flags */
	X86_64_CPU_FEATURE_FSGSBASE,
	X86_64_CPU_FEATURE_TSC_ADJUST,
	X86_64_CPU_FEATURE_BMI1,
	X86_64_CPU_FEATURE_AVX2,
	X86_64_CPU_FEATURE_SMEP,
	X86_64_CPU_FEATURE_BMI2,
	X86_64_CPU_FEATURE_ERMS,
	X86_64_CPU_FEATURE_INVPCID,
	X86_64_CPU_FEATURE_RDSEED,
	X86_64_CPU_FEATURE_ADX,
	X86_64_CPU_FEATURE_SMAP,
	X86_64_CPU_FEATURE_CLFLUSHOPT,
	X86_64_CPU_FEATURE_CLWB,
	X86_64_CPU_FEATURE_SHA,
	X86_64_CPU_FEATURE_UMIP,
	X86_64_CPU_FEATURE_LA57,
	X86_64_CPU_FEATURE_RDPID,

	/* CPUID.07H.00H Intel-defined feature flags */
	X86_64_CPU_FEATURE_INTEL_SERIALIZE,

	/* CPUID.80000001H extended-common feature flags */
	X86_64_CPU_FEATURE_SYSCALL,
	X86_64_CPU_FEATURE_NX,
	X86_64_CPU_FEATURE_PAGE1GB,
	X86_64_CPU_FEATURE_RDTSCP,
	X86_64_CPU_FEATURE_LONG_MODE,
	X86_64_CPU_FEATURE_LAHF_LM,
	X86_64_CPU_FEATURE_LZCNT,
	X86_64_CPU_FEATURE_PREFETCHW,

	X86_64_CPU_FEATURE_NR
};

struct x86_64_cpuid_leaf {
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
};

struct x86_64_cpuid_raw {
	struct x86_64_cpuid_leaf leaf0;
	struct x86_64_cpuid_leaf leaf1;
	struct x86_64_cpuid_leaf leaf7_0;
	struct x86_64_cpuid_leaf leafd_0;
	struct x86_64_cpuid_leaf leafd_1;
	struct x86_64_cpuid_leaf leaf_ext0;
	struct x86_64_cpuid_leaf leaf_ext1;
	struct x86_64_cpuid_leaf brand[3];
};

struct x86_64_cpu_features {
	struct x86_64_cpuid_raw raw;

	uint32_t max_basic_leaf;
	uint32_t max_extended_leaf;
	char vendor[X86_64_CPU_VENDOR_LENGTH + 1];
	char brand[X86_64_CPU_BRAND_LENGTH + 1];
	enum x86_64_cpu_vendor vendor_id;

	uint8_t stepping;
	uint8_t base_model;
	uint8_t base_family;
	uint8_t processor_type;
	uint8_t extended_model;
	uint8_t extended_family;
	uint32_t display_model;
	uint32_t display_family;

	uint8_t initial_apic_id;
	uint8_t logical_processor_count;
	uint16_t clflush_line_size;

	uint32_t leaf7_max_subleaf;

	uint64_t xcr0_supported_mask;
	uint32_t xsave_area_size_enabled;
	uint32_t xsave_area_size_supported;
	struct x86_64_cpuid_leaf xsave_leaf1;

	bool has[X86_64_CPU_FEATURE_NR];
};

void x86_64_cpu_features_init(void);
const struct x86_64_cpu_features *x86_64_cpu_features_get(void);
bool x86_64_cpu_has_feature(enum x86_64_cpu_feature feature);

void x86_64_cpu_features_decode(struct x86_64_cpu_features *features,
				const struct x86_64_cpuid_raw *raw);

#endif /* __ASSEMBLER__ */

#endif /* HAL_ARCH_CPU_FEATURES_H */
