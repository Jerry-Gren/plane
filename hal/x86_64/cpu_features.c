#include <stddef.h>

#include <hal/cpu.h>
#include <hal/x86_64/cpu_features.h>
#include <klib/string.h>

/*
 * CPUID decoding follows the common Intel SDM Vol.1/Vol.2 and AMD APM Vol.3
 * CPUID leaves. vendor-specific leaves must be gated by vendor before use.
 */

#define CPUID_LEAF_FEATURES      0x00000001u
#define CPUID_LEAF_STRUCTURED    0x00000007u
#define CPUID_LEAF_XSAVE         0x0000000du
#define CPUID_LEAF_EXT_MAX       0x80000000u
#define CPUID_LEAF_EXT_FEATURES  0x80000001u
#define CPUID_LEAF_BRAND_FIRST   0x80000002u
#define CPUID_LEAF_BRAND_LAST    0x80000004u

#define CPUID_1_EDX_FPU          (1u << 0)
#define CPUID_1_EDX_TSC          (1u << 4)
#define CPUID_1_EDX_MSR          (1u << 5)
#define CPUID_1_EDX_PAE          (1u << 6)
#define CPUID_1_EDX_CX8          (1u << 8)
#define CPUID_1_EDX_APIC         (1u << 9)
#define CPUID_1_EDX_MTRR         (1u << 12)
#define CPUID_1_EDX_PGE          (1u << 13)
#define CPUID_1_EDX_CMOV         (1u << 15)
#define CPUID_1_EDX_PAT          (1u << 16)
#define CPUID_1_EDX_CLFLUSH      (1u << 19)
#define CPUID_1_EDX_FXSR         (1u << 24)
#define CPUID_1_EDX_SSE          (1u << 25)
#define CPUID_1_EDX_SSE2         (1u << 26)
#define CPUID_1_EDX_HTT          (1u << 28)

#define CPUID_1_ECX_SSE3         (1u << 0)
#define CPUID_1_ECX_PCLMULQDQ    (1u << 1)
#define CPUID_1_ECX_SSSE3        (1u << 9)
#define CPUID_1_ECX_FMA          (1u << 12)
#define CPUID_1_ECX_CX16         (1u << 13)
#define CPUID_1_ECX_PCID         (1u << 17)
#define CPUID_1_ECX_SSE4_1       (1u << 19)
#define CPUID_1_ECX_SSE4_2       (1u << 20)
#define CPUID_1_ECX_X2APIC       (1u << 21)
#define CPUID_1_ECX_MOVBE        (1u << 22)
#define CPUID_1_ECX_POPCNT       (1u << 23)
#define CPUID_1_ECX_TSC_DEADLINE (1u << 24)
#define CPUID_1_ECX_AES          (1u << 25)
#define CPUID_1_ECX_XSAVE        (1u << 26)
#define CPUID_1_ECX_OSXSAVE      (1u << 27)
#define CPUID_1_ECX_AVX          (1u << 28)
#define CPUID_1_ECX_F16C         (1u << 29)
#define CPUID_1_ECX_RDRAND       (1u << 30)
#define CPUID_1_ECX_HYPERVISOR   (1u << 31)

#define CPUID_7_0_EBX_FSGSBASE   (1u << 0)
#define CPUID_7_0_EBX_TSC_ADJUST (1u << 1)
#define CPUID_7_0_EBX_BMI1       (1u << 3)
#define CPUID_7_0_EBX_AVX2       (1u << 5)
#define CPUID_7_0_EBX_SMEP       (1u << 7)
#define CPUID_7_0_EBX_BMI2       (1u << 8)
#define CPUID_7_0_EBX_ERMS       (1u << 9)
#define CPUID_7_0_EBX_INVPCID    (1u << 10)
#define CPUID_7_0_EBX_RDSEED     (1u << 18)
#define CPUID_7_0_EBX_ADX        (1u << 19)
#define CPUID_7_0_EBX_SMAP       (1u << 20)
#define CPUID_7_0_EBX_CLFLUSHOPT (1u << 23)
#define CPUID_7_0_EBX_CLWB       (1u << 24)
#define CPUID_7_0_EBX_SHA        (1u << 29)

#define CPUID_7_0_ECX_UMIP       (1u << 2)
#define CPUID_7_0_ECX_LA57       (1u << 16)
#define CPUID_7_0_ECX_RDPID      (1u << 22)

#define CPUID_7_0_EDX_INTEL_SERIALIZE (1u << 14)

#define CPUID_EXT_1_EDX_SYSCALL  (1u << 11)
#define CPUID_EXT_1_EDX_NX       (1u << 20)
#define CPUID_EXT_1_EDX_PAGE1GB  (1u << 26)
#define CPUID_EXT_1_EDX_RDTSCP   (1u << 27)
#define CPUID_EXT_1_EDX_LM       (1u << 29)

#define CPUID_EXT_1_ECX_LAHF_LM  (1u << 0)
#define CPUID_EXT_1_ECX_LZCNT    (1u << 5)
#define CPUID_EXT_1_ECX_PREFETCHW (1u << 8)

static struct x86_64_cpu_features boot_cpu_features;

static struct x86_64_cpuid_leaf cpuid_count(uint32_t leaf, uint32_t subleaf) {
	struct x86_64_cpuid_leaf out;

	__asm__ volatile (
		"cpuid"
		: "=a" (out.eax), "=b" (out.ebx), "=c" (out.ecx), "=d" (out.edx)
		: "a" (leaf), "c" (subleaf)
	);

	return out;
}

static void write_u32_string(char *dst, uint32_t value) {
	for (size_t byte = 0; byte < 4; byte++) {
		dst[byte] = (char)(value >> (byte * 8));
	}
}

static void set_vendor_string(char vendor[X86_64_CPU_VENDOR_LENGTH + 1],
			      const struct x86_64_cpuid_leaf *leaf0) {
	write_u32_string(&vendor[0], leaf0->ebx);
	write_u32_string(&vendor[4], leaf0->edx);
	write_u32_string(&vendor[8], leaf0->ecx);
	vendor[X86_64_CPU_VENDOR_LENGTH] = '\0';
}

static enum x86_64_cpu_vendor vendor_id_from_string(const char *vendor) {
	if (memcmp(vendor, "GenuineIntel", X86_64_CPU_VENDOR_LENGTH) == 0) {
		return X86_64_CPU_VENDOR_INTEL;
	}
	if (memcmp(vendor, "AuthenticAMD", X86_64_CPU_VENDOR_LENGTH) == 0) {
		return X86_64_CPU_VENDOR_AMD;
	}

	return X86_64_CPU_VENDOR_UNKNOWN;
}

static void set_brand_string(char brand[X86_64_CPU_BRAND_LENGTH + 1],
			     const struct x86_64_cpuid_leaf brand_leaf[3]) {
	char *dst = brand;

	for (size_t i = 0; i < 3; i++) {
		write_u32_string(&dst[0], brand_leaf[i].eax);
		write_u32_string(&dst[4], brand_leaf[i].ebx);
		write_u32_string(&dst[8], brand_leaf[i].ecx);
		write_u32_string(&dst[12], brand_leaf[i].edx);
		dst += 16;
	}
	brand[X86_64_CPU_BRAND_LENGTH] = '\0';
}

static void decode_signature(struct x86_64_cpu_features *features,
			     const struct x86_64_cpuid_leaf *leaf1) {
	uint32_t signature = leaf1->eax;

	features->stepping = signature & 0xf;
	features->base_model = (signature >> 4) & 0xf;
	features->base_family = (signature >> 8) & 0xf;
	features->processor_type = (signature >> 12) & 0x3;
	features->extended_model = (signature >> 16) & 0xf;
	features->extended_family = (signature >> 20) & 0xff;

	features->display_family = features->base_family;
	if (features->base_family == 0xf) {
		features->display_family += features->extended_family;
	}

	features->display_model = features->base_model;
	if (features->base_family == 0x6 || features->base_family == 0xf) {
		features->display_model |= (uint32_t)features->extended_model << 4;
	}
}

static void decode_leaf1(struct x86_64_cpu_features *features,
			 const struct x86_64_cpuid_leaf *leaf1) {
	decode_signature(features, leaf1);

	features->initial_apic_id = (leaf1->ebx >> 24) & 0xff;
	features->logical_processor_count = (leaf1->ebx >> 16) & 0xff;
	features->clflush_line_size = (uint16_t)(((leaf1->ebx >> 8) & 0xff) * 8);

	features->has[X86_64_CPU_FEATURE_FPU] =
		(leaf1->edx & CPUID_1_EDX_FPU) != 0;
	features->has[X86_64_CPU_FEATURE_TSC] =
		(leaf1->edx & CPUID_1_EDX_TSC) != 0;
	features->has[X86_64_CPU_FEATURE_MSR] =
		(leaf1->edx & CPUID_1_EDX_MSR) != 0;
	features->has[X86_64_CPU_FEATURE_PAE] =
		(leaf1->edx & CPUID_1_EDX_PAE) != 0;
	features->has[X86_64_CPU_FEATURE_CX8] =
		(leaf1->edx & CPUID_1_EDX_CX8) != 0;
	features->has[X86_64_CPU_FEATURE_APIC] =
		(leaf1->edx & CPUID_1_EDX_APIC) != 0;
	features->has[X86_64_CPU_FEATURE_MTRR] =
		(leaf1->edx & CPUID_1_EDX_MTRR) != 0;
	features->has[X86_64_CPU_FEATURE_PGE] =
		(leaf1->edx & CPUID_1_EDX_PGE) != 0;
	features->has[X86_64_CPU_FEATURE_CMOV] =
		(leaf1->edx & CPUID_1_EDX_CMOV) != 0;
	features->has[X86_64_CPU_FEATURE_PAT] =
		(leaf1->edx & CPUID_1_EDX_PAT) != 0;
	features->has[X86_64_CPU_FEATURE_CLFLUSH] =
		(leaf1->edx & CPUID_1_EDX_CLFLUSH) != 0;
	features->has[X86_64_CPU_FEATURE_FXSR] =
		(leaf1->edx & CPUID_1_EDX_FXSR) != 0;
	features->has[X86_64_CPU_FEATURE_SSE] =
		(leaf1->edx & CPUID_1_EDX_SSE) != 0;
	features->has[X86_64_CPU_FEATURE_SSE2] =
		(leaf1->edx & CPUID_1_EDX_SSE2) != 0;
	features->has[X86_64_CPU_FEATURE_HTT] =
		(leaf1->edx & CPUID_1_EDX_HTT) != 0;

	features->has[X86_64_CPU_FEATURE_SSE3] =
		(leaf1->ecx & CPUID_1_ECX_SSE3) != 0;
	features->has[X86_64_CPU_FEATURE_PCLMULQDQ] =
		(leaf1->ecx & CPUID_1_ECX_PCLMULQDQ) != 0;
	features->has[X86_64_CPU_FEATURE_SSSE3] =
		(leaf1->ecx & CPUID_1_ECX_SSSE3) != 0;
	features->has[X86_64_CPU_FEATURE_FMA] =
		(leaf1->ecx & CPUID_1_ECX_FMA) != 0;
	features->has[X86_64_CPU_FEATURE_CX16] =
		(leaf1->ecx & CPUID_1_ECX_CX16) != 0;
	features->has[X86_64_CPU_FEATURE_PCID] =
		(leaf1->ecx & CPUID_1_ECX_PCID) != 0;
	features->has[X86_64_CPU_FEATURE_SSE4_1] =
		(leaf1->ecx & CPUID_1_ECX_SSE4_1) != 0;
	features->has[X86_64_CPU_FEATURE_SSE4_2] =
		(leaf1->ecx & CPUID_1_ECX_SSE4_2) != 0;
	features->has[X86_64_CPU_FEATURE_X2APIC] =
		(leaf1->ecx & CPUID_1_ECX_X2APIC) != 0;
	features->has[X86_64_CPU_FEATURE_MOVBE] =
		(leaf1->ecx & CPUID_1_ECX_MOVBE) != 0;
	features->has[X86_64_CPU_FEATURE_POPCNT] =
		(leaf1->ecx & CPUID_1_ECX_POPCNT) != 0;
	features->has[X86_64_CPU_FEATURE_TSC_DEADLINE] =
		(leaf1->ecx & CPUID_1_ECX_TSC_DEADLINE) != 0;
	features->has[X86_64_CPU_FEATURE_AES] =
		(leaf1->ecx & CPUID_1_ECX_AES) != 0;
	features->has[X86_64_CPU_FEATURE_XSAVE] =
		(leaf1->ecx & CPUID_1_ECX_XSAVE) != 0;
	features->has[X86_64_CPU_FEATURE_OSXSAVE] =
		(leaf1->ecx & CPUID_1_ECX_OSXSAVE) != 0;
	features->has[X86_64_CPU_FEATURE_AVX] =
		(leaf1->ecx & CPUID_1_ECX_AVX) != 0;
	features->has[X86_64_CPU_FEATURE_F16C] =
		(leaf1->ecx & CPUID_1_ECX_F16C) != 0;
	features->has[X86_64_CPU_FEATURE_RDRAND] =
		(leaf1->ecx & CPUID_1_ECX_RDRAND) != 0;
	features->has[X86_64_CPU_FEATURE_HYPERVISOR] =
		(leaf1->ecx & CPUID_1_ECX_HYPERVISOR) != 0;
}

static void decode_leaf7_0(struct x86_64_cpu_features *features,
			   const struct x86_64_cpuid_leaf *leaf7) {
	features->leaf7_max_subleaf = leaf7->eax;

	features->has[X86_64_CPU_FEATURE_FSGSBASE] =
		(leaf7->ebx & CPUID_7_0_EBX_FSGSBASE) != 0;
	features->has[X86_64_CPU_FEATURE_TSC_ADJUST] =
		(leaf7->ebx & CPUID_7_0_EBX_TSC_ADJUST) != 0;
	features->has[X86_64_CPU_FEATURE_BMI1] =
		(leaf7->ebx & CPUID_7_0_EBX_BMI1) != 0;
	features->has[X86_64_CPU_FEATURE_AVX2] =
		(leaf7->ebx & CPUID_7_0_EBX_AVX2) != 0;
	features->has[X86_64_CPU_FEATURE_SMEP] =
		(leaf7->ebx & CPUID_7_0_EBX_SMEP) != 0;
	features->has[X86_64_CPU_FEATURE_BMI2] =
		(leaf7->ebx & CPUID_7_0_EBX_BMI2) != 0;
	features->has[X86_64_CPU_FEATURE_ERMS] =
		(leaf7->ebx & CPUID_7_0_EBX_ERMS) != 0;
	features->has[X86_64_CPU_FEATURE_INVPCID] =
		(leaf7->ebx & CPUID_7_0_EBX_INVPCID) != 0;
	features->has[X86_64_CPU_FEATURE_RDSEED] =
		(leaf7->ebx & CPUID_7_0_EBX_RDSEED) != 0;
	features->has[X86_64_CPU_FEATURE_ADX] =
		(leaf7->ebx & CPUID_7_0_EBX_ADX) != 0;
	features->has[X86_64_CPU_FEATURE_SMAP] =
		(leaf7->ebx & CPUID_7_0_EBX_SMAP) != 0;
	features->has[X86_64_CPU_FEATURE_CLFLUSHOPT] =
		(leaf7->ebx & CPUID_7_0_EBX_CLFLUSHOPT) != 0;
	features->has[X86_64_CPU_FEATURE_CLWB] =
		(leaf7->ebx & CPUID_7_0_EBX_CLWB) != 0;
	features->has[X86_64_CPU_FEATURE_SHA] =
		(leaf7->ebx & CPUID_7_0_EBX_SHA) != 0;

	features->has[X86_64_CPU_FEATURE_UMIP] =
		(leaf7->ecx & CPUID_7_0_ECX_UMIP) != 0;
	features->has[X86_64_CPU_FEATURE_LA57] =
		(leaf7->ecx & CPUID_7_0_ECX_LA57) != 0;
	features->has[X86_64_CPU_FEATURE_RDPID] =
		(leaf7->ecx & CPUID_7_0_ECX_RDPID) != 0;

	/* Intel defines CPUID.07H.00H:EDX[14] as SERIALIZE. */
	if (features->vendor_id == X86_64_CPU_VENDOR_INTEL) {
		features->has[X86_64_CPU_FEATURE_INTEL_SERIALIZE] =
			(leaf7->edx & CPUID_7_0_EDX_INTEL_SERIALIZE) != 0;
	}
}

static void decode_xsave(struct x86_64_cpu_features *features,
			 const struct x86_64_cpuid_raw *raw) {
	features->xcr0_supported_mask =
		((uint64_t)raw->leafd_0.edx << 32) | raw->leafd_0.eax;
	features->xsave_area_size_enabled = raw->leafd_0.ebx;
	features->xsave_area_size_supported = raw->leafd_0.ecx;
	features->xsave_leaf1 = raw->leafd_1;
}

static void decode_ext_leaf1(struct x86_64_cpu_features *features,
			     const struct x86_64_cpuid_leaf *leaf_ext1) {
	features->has[X86_64_CPU_FEATURE_SYSCALL] =
		(leaf_ext1->edx & CPUID_EXT_1_EDX_SYSCALL) != 0;
	features->has[X86_64_CPU_FEATURE_NX] =
		(leaf_ext1->edx & CPUID_EXT_1_EDX_NX) != 0;
	features->has[X86_64_CPU_FEATURE_PAGE1GB] =
		(leaf_ext1->edx & CPUID_EXT_1_EDX_PAGE1GB) != 0;
	features->has[X86_64_CPU_FEATURE_RDTSCP] =
		(leaf_ext1->edx & CPUID_EXT_1_EDX_RDTSCP) != 0;
	features->has[X86_64_CPU_FEATURE_LONG_MODE] =
		(leaf_ext1->edx & CPUID_EXT_1_EDX_LM) != 0;

	features->has[X86_64_CPU_FEATURE_LAHF_LM] =
		(leaf_ext1->ecx & CPUID_EXT_1_ECX_LAHF_LM) != 0;
	features->has[X86_64_CPU_FEATURE_LZCNT] =
		(leaf_ext1->ecx & CPUID_EXT_1_ECX_LZCNT) != 0;
	features->has[X86_64_CPU_FEATURE_PREFETCHW] =
		(leaf_ext1->ecx & CPUID_EXT_1_ECX_PREFETCHW) != 0;
}

void x86_64_cpu_features_decode(struct x86_64_cpu_features *features,
				const struct x86_64_cpuid_raw *raw) {
	memset(features, 0, sizeof(*features));

	if (raw == NULL) {
		return;
	}

	features->raw = *raw;
	features->max_basic_leaf = raw->leaf0.eax;
	set_vendor_string(features->vendor, &raw->leaf0);
	features->vendor_id = vendor_id_from_string(features->vendor);
	features->max_extended_leaf = raw->leaf_ext0.eax;

	if (features->max_basic_leaf >= CPUID_LEAF_FEATURES) {
		decode_leaf1(features, &raw->leaf1);
	}

	if (features->max_basic_leaf >= CPUID_LEAF_STRUCTURED) {
		decode_leaf7_0(features, &raw->leaf7_0);
	}

	if (features->has[X86_64_CPU_FEATURE_XSAVE] &&
	    features->max_basic_leaf >= CPUID_LEAF_XSAVE) {
		decode_xsave(features, raw);
	}

	if (features->max_extended_leaf >= CPUID_LEAF_EXT_FEATURES) {
		decode_ext_leaf1(features, &raw->leaf_ext1);
	}

	if (features->max_extended_leaf >= CPUID_LEAF_BRAND_LAST) {
		set_brand_string(features->brand, raw->brand);
	}
}

static bool cpu_features_have_required(void) {
	return boot_cpu_features.has[X86_64_CPU_FEATURE_MSR] &&
	       boot_cpu_features.has[X86_64_CPU_FEATURE_PAT] &&
	       boot_cpu_features.has[X86_64_CPU_FEATURE_LONG_MODE];
}

static void collect_cpuid_raw(struct x86_64_cpuid_raw *raw) {
	memset(raw, 0, sizeof(*raw));

	raw->leaf0 = cpuid_count(0, 0);
	raw->leaf_ext0 = cpuid_count(CPUID_LEAF_EXT_MAX, 0);

	if (raw->leaf0.eax >= CPUID_LEAF_FEATURES) {
		raw->leaf1 = cpuid_count(CPUID_LEAF_FEATURES, 0);
	}
	if (raw->leaf0.eax >= CPUID_LEAF_STRUCTURED) {
		raw->leaf7_0 = cpuid_count(CPUID_LEAF_STRUCTURED, 0);
	}
	/* CPUID.0DH is valid when CPUID.01H:ECX.XSAVE is present. */
	if (raw->leaf0.eax >= CPUID_LEAF_XSAVE &&
	    (raw->leaf1.ecx & CPUID_1_ECX_XSAVE) != 0) {
		raw->leafd_0 = cpuid_count(CPUID_LEAF_XSAVE, 0);
		raw->leafd_1 = cpuid_count(CPUID_LEAF_XSAVE, 1);
	}
	if (raw->leaf_ext0.eax >= CPUID_LEAF_EXT_FEATURES) {
		raw->leaf_ext1 = cpuid_count(CPUID_LEAF_EXT_FEATURES, 0);
	}
	if (raw->leaf_ext0.eax >= CPUID_LEAF_BRAND_LAST) {
		for (uint32_t i = 0; i < 3; i++) {
			raw->brand[i] = cpuid_count(CPUID_LEAF_BRAND_FIRST + i, 0);
		}
	}
}

void x86_64_cpu_features_init(void) {
	struct x86_64_cpuid_raw raw;

	collect_cpuid_raw(&raw);
	x86_64_cpu_features_decode(&boot_cpu_features, &raw);

	if (!cpu_features_have_required()) {
		hal_cpu_hang();
	}
}

const struct x86_64_cpu_features *x86_64_cpu_features_get(void) {
	return &boot_cpu_features;
}

bool x86_64_cpu_has_feature(enum x86_64_cpu_feature feature) {
	if (feature < 0 || feature >= X86_64_CPU_FEATURE_NR) {
		return false;
	}

	return boot_cpu_features.has[feature];
}
