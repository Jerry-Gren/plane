#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <hal/x86_64/cpu_features.h>

void printk(const char *fmt, ...)
{
	va_list args;

	(void)fmt;
	va_start(args, fmt);
	va_end(args);
}

void panic(const char *fmt, ...)
{
	va_list args;

	(void)fmt;
	va_start(args, fmt);
	va_end(args);

	__builtin_trap();
	__builtin_unreachable();
}

static struct x86_64_cpuid_leaf leaf(uint32_t eax,
				     uint32_t ebx,
				     uint32_t ecx,
				     uint32_t edx) {
	struct x86_64_cpuid_leaf out = {
		.eax = eax,
		.ebx = ebx,
		.ecx = ecx,
		.edx = edx
	};
	return out;
}

static uint32_t chars4(const char text[4]) {
	return ((uint32_t)text[0]) |
	       ((uint32_t)text[1] << 8) |
	       ((uint32_t)text[2] << 16) |
	       ((uint32_t)text[3] << 24);
}

static struct x86_64_cpuid_leaf vendor_leaf(uint32_t max_leaf,
					    const char vendor[12]) {
	return leaf(max_leaf,
		    chars4(&vendor[0]),
		    chars4(&vendor[8]),
		    chars4(&vendor[4]));
}

static void set_brand_leaf(struct x86_64_cpuid_leaf *out,
			   const char text[16]) {
	*out = leaf(chars4(&text[0]),
		    chars4(&text[4]),
		    chars4(&text[8]),
		    chars4(&text[12]));
}

static int expect_bool(const char *name, bool actual, bool expected) {
	if (actual == expected) {
		return 1;
	}

	printf("FAIL: %s expected=%d actual=%d\n", name, expected, actual);
	return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected) {
	if (actual == expected) {
		return 1;
	}

	printf("FAIL: %s expected=0x%08x actual=0x%08x\n",
	       name, expected, actual);
	return 0;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected) {
	if (actual == expected) {
		return 1;
	}

	printf("FAIL: %s expected=0x%016llx actual=0x%016llx\n",
	       name, (unsigned long long)expected, (unsigned long long)actual);
	return 0;
}

static int expect_str(const char *name, const char *actual, const char *expected) {
	if (strcmp(actual, expected) == 0) {
		return 1;
	}

	printf("FAIL: %s expected=%s actual=%s\n", name, expected, actual);
	return 0;
}

static int test_decode_intel_like_common_features(void) {
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int passed = 0;

	raw.leaf0 = vendor_leaf(0x0d, "GenuineIntel");
	raw.leaf1 = leaf(0x000506e3,
			 (0x24u << 24) | (8u << 16) | (8u << 8),
			 (1u << 0) | (1u << 1) | (1u << 9) |
			 (1u << 12) | (1u << 13) | (1u << 17) |
			 (1u << 19) | (1u << 20) | (1u << 21) |
			 (1u << 22) | (1u << 23) | (1u << 24) |
			 (1u << 25) | (1u << 26) | (1u << 27) |
			 (1u << 28) | (1u << 29) | (1u << 30) |
			 (1u << 31),
			 (1u << 0) | (1u << 4) | (1u << 5) |
			 (1u << 6) | (1u << 8) | (1u << 9) |
			 (1u << 12) | (1u << 13) | (1u << 15) |
			 (1u << 16) | (1u << 19) | (1u << 24) |
			 (1u << 25) | (1u << 26) | (1u << 28));
	raw.leaf7_0 = leaf(2,
			   (1u << 0) | (1u << 1) | (1u << 3) |
			   (1u << 5) | (1u << 7) | (1u << 8) |
			   (1u << 9) | (1u << 10) | (1u << 18) |
			   (1u << 19) | (1u << 20) | (1u << 23) |
			   (1u << 24) | (1u << 29),
			   (1u << 2) | (1u << 16) | (1u << 22),
			   (1u << 14));
	raw.leafd_0 = leaf(0x00000007, 0x00000340, 0x00000840, 0x00000002);
	raw.leafd_1 = leaf(0x0000000f, 0x11111111, 0x22222222, 0x33333333);
	raw.leaf_ext0 = vendor_leaf(0x80000001u, "GenuineIntel");
	raw.leaf_ext1 = leaf(0, 0, (1u << 0) | (1u << 5) | (1u << 8),
			     (1u << 11) | (1u << 20) | (1u << 26) |
			     (1u << 27) | (1u << 29));

	x86_64_cpu_features_decode(&features, &raw);

	passed += expect_str("intel vendor", features.vendor, "GenuineIntel");
	passed += expect_u32("intel vendor id", features.vendor_id,
			      X86_64_CPU_VENDOR_INTEL);
	passed += expect_u32("max basic leaf", features.max_basic_leaf, 0x0d);
	passed += expect_u32("max extended leaf",
			      features.max_extended_leaf, 0x80000001u);
	passed += expect_u32("raw leaf 7 ebx",
			      features.raw.leaf7_0.ebx, raw.leaf7_0.ebx);
	passed += expect_u32("leaf 7 max subleaf",
			      features.leaf7_max_subleaf, 2);
	passed += expect_u32("stepping", features.stepping, 3);
	passed += expect_u32("base model", features.base_model, 0xe);
	passed += expect_u32("base family", features.base_family, 6);
	passed += expect_u32("extended model", features.extended_model, 5);
	passed += expect_u32("display model", features.display_model, 0x5e);
	passed += expect_u32("display family", features.display_family, 6);
	passed += expect_u32("initial apic id", features.initial_apic_id, 0x24);
	passed += expect_u32("logical processor count",
			      features.logical_processor_count, 8);
	passed += expect_u32("clflush line size",
			      features.clflush_line_size, 64);

	passed += expect_bool("fpu", features.has[X86_64_CPU_FEATURE_FPU], true);
	passed += expect_bool("tsc", features.has[X86_64_CPU_FEATURE_TSC], true);
	passed += expect_bool("msr", features.has[X86_64_CPU_FEATURE_MSR], true);
	passed += expect_bool("pae", features.has[X86_64_CPU_FEATURE_PAE], true);
	passed += expect_bool("cx8", features.has[X86_64_CPU_FEATURE_CX8], true);
	passed += expect_bool("apic", features.has[X86_64_CPU_FEATURE_APIC], true);
	passed += expect_bool("mtrr", features.has[X86_64_CPU_FEATURE_MTRR], true);
	passed += expect_bool("pge", features.has[X86_64_CPU_FEATURE_PGE], true);
	passed += expect_bool("cmov", features.has[X86_64_CPU_FEATURE_CMOV], true);
	passed += expect_bool("pat", features.has[X86_64_CPU_FEATURE_PAT], true);
	passed += expect_bool("clflush",
			      features.has[X86_64_CPU_FEATURE_CLFLUSH], true);
	passed += expect_bool("fxsr", features.has[X86_64_CPU_FEATURE_FXSR], true);
	passed += expect_bool("sse", features.has[X86_64_CPU_FEATURE_SSE], true);
	passed += expect_bool("sse2", features.has[X86_64_CPU_FEATURE_SSE2], true);
	passed += expect_bool("htt", features.has[X86_64_CPU_FEATURE_HTT], true);

	passed += expect_bool("sse3", features.has[X86_64_CPU_FEATURE_SSE3], true);
	passed += expect_bool("pclmulqdq",
			      features.has[X86_64_CPU_FEATURE_PCLMULQDQ], true);
	passed += expect_bool("ssse3", features.has[X86_64_CPU_FEATURE_SSSE3], true);
	passed += expect_bool("fma", features.has[X86_64_CPU_FEATURE_FMA], true);
	passed += expect_bool("cx16", features.has[X86_64_CPU_FEATURE_CX16], true);
	passed += expect_bool("intel pcid",
			      features.has[X86_64_CPU_FEATURE_INTEL_PCID], true);
	passed += expect_bool("sse4.1",
			      features.has[X86_64_CPU_FEATURE_SSE4_1], true);
	passed += expect_bool("sse4.2",
			      features.has[X86_64_CPU_FEATURE_SSE4_2], true);
	passed += expect_bool("x2apic",
			      features.has[X86_64_CPU_FEATURE_X2APIC], true);
	passed += expect_bool("movbe", features.has[X86_64_CPU_FEATURE_MOVBE], true);
	passed += expect_bool("popcnt",
			      features.has[X86_64_CPU_FEATURE_POPCNT], true);
	passed += expect_bool("intel tsc deadline",
			      features.has[X86_64_CPU_FEATURE_INTEL_TSC_DEADLINE],
			      true);
	passed += expect_bool("aes", features.has[X86_64_CPU_FEATURE_AES], true);
	passed += expect_bool("xsave", features.has[X86_64_CPU_FEATURE_XSAVE], true);
	passed += expect_bool("osxsave",
			      features.has[X86_64_CPU_FEATURE_OSXSAVE], true);
	passed += expect_bool("avx", features.has[X86_64_CPU_FEATURE_AVX], true);
	passed += expect_bool("f16c", features.has[X86_64_CPU_FEATURE_F16C], true);
	passed += expect_bool("rdrand",
			      features.has[X86_64_CPU_FEATURE_RDRAND], true);
	passed += expect_bool("hypervisor",
			      features.has[X86_64_CPU_FEATURE_HYPERVISOR], true);

	passed += expect_bool("fsgsbase",
			      features.has[X86_64_CPU_FEATURE_FSGSBASE], true);
	passed += expect_bool("tsc adjust",
			      features.has[X86_64_CPU_FEATURE_TSC_ADJUST], true);
	passed += expect_bool("bmi1", features.has[X86_64_CPU_FEATURE_BMI1], true);
	passed += expect_bool("avx2", features.has[X86_64_CPU_FEATURE_AVX2], true);
	passed += expect_bool("smep", features.has[X86_64_CPU_FEATURE_SMEP], true);
	passed += expect_bool("bmi2", features.has[X86_64_CPU_FEATURE_BMI2], true);
	passed += expect_bool("erms", features.has[X86_64_CPU_FEATURE_ERMS], true);
	passed += expect_bool("invpcid",
			      features.has[X86_64_CPU_FEATURE_INVPCID], true);
	passed += expect_bool("rdseed",
			      features.has[X86_64_CPU_FEATURE_RDSEED], true);
	passed += expect_bool("adx", features.has[X86_64_CPU_FEATURE_ADX], true);
	passed += expect_bool("smap", features.has[X86_64_CPU_FEATURE_SMAP], true);
	passed += expect_bool("clflushopt",
			      features.has[X86_64_CPU_FEATURE_CLFLUSHOPT], true);
	passed += expect_bool("clwb", features.has[X86_64_CPU_FEATURE_CLWB], true);
	passed += expect_bool("sha", features.has[X86_64_CPU_FEATURE_SHA], true);
	passed += expect_bool("umip", features.has[X86_64_CPU_FEATURE_UMIP], true);
	passed += expect_bool("la57", features.has[X86_64_CPU_FEATURE_LA57], true);
	passed += expect_bool("rdpid", features.has[X86_64_CPU_FEATURE_RDPID], true);
	passed += expect_bool("intel serialize",
			      features.has[X86_64_CPU_FEATURE_INTEL_SERIALIZE],
			      true);

	passed += expect_u64("xcr0 supported mask",
			      features.xcr0_supported_mask, 0x0000000200000007ull);
	passed += expect_u32("xsave enabled size",
			      features.xsave_area_size_enabled, 0x340);
	passed += expect_u32("xsave supported size",
			      features.xsave_area_size_supported, 0x840);
	passed += expect_u32("xsave leaf 1 eax",
			      features.xsave_leaf1.eax, 0x0000000f);
	passed += expect_u32("xsave leaf 1 ebx",
			      features.xsave_leaf1.ebx, 0x11111111);
	passed += expect_u32("xsave leaf 1 ecx",
			      features.xsave_leaf1.ecx, 0x22222222);
	passed += expect_u32("xsave leaf 1 edx",
			      features.xsave_leaf1.edx, 0x33333333);

	passed += expect_bool("syscall",
			      features.has[X86_64_CPU_FEATURE_SYSCALL], true);
	passed += expect_bool("nx", features.has[X86_64_CPU_FEATURE_NX], true);
	passed += expect_bool("page1gb",
			      features.has[X86_64_CPU_FEATURE_PAGE1GB], true);
	passed += expect_bool("rdtscp",
			      features.has[X86_64_CPU_FEATURE_RDTSCP], true);
	passed += expect_bool("long mode",
			      features.has[X86_64_CPU_FEATURE_LONG_MODE], true);
	passed += expect_bool("lahf lm",
			      features.has[X86_64_CPU_FEATURE_LAHF_LM], true);
	passed += expect_bool("lzcnt", features.has[X86_64_CPU_FEATURE_LZCNT], true);
	passed += expect_bool("prefetchw",
			      features.has[X86_64_CPU_FEATURE_PREFETCHW], true);

	return passed;
}

static int test_decode_amd_like_extended_features_and_brand(void) {
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int passed = 0;

	raw.leaf0 = vendor_leaf(7, "AuthenticAMD");
	raw.leaf1 = leaf(0x00800f82, (0x02u << 24) | (1u << 16) | (8u << 8),
			 (1u << 17) | (1u << 24), (1u << 5) | (1u << 16));
	raw.leaf7_0 = leaf(0, 0, 0, (1u << 14));
	raw.leaf_ext0 = vendor_leaf(0x80000004u, "AuthenticAMD");
	raw.leaf_ext1 = leaf(0, 0, (1u << 0) | (1u << 5) | (1u << 8),
			     (1u << 11) | (1u << 20) | (1u << 26) |
			     (1u << 27) | (1u << 29));
	set_brand_leaf(&raw.brand[0], "Plane CPU       ");
	set_brand_leaf(&raw.brand[1], "Test Brand      ");
	set_brand_leaf(&raw.brand[2], "Model 0001      ");

	x86_64_cpu_features_decode(&features, &raw);

	passed += expect_str("amd vendor", features.vendor, "AuthenticAMD");
	passed += expect_u32("amd vendor id", features.vendor_id,
			      X86_64_CPU_VENDOR_AMD);
	passed += expect_str("amd brand", features.brand,
			      "Plane CPU       Test Brand      Model 0001      ");
	passed += expect_u32("amd display family", features.display_family, 0x17);
	passed += expect_u32("amd display model", features.display_model, 0x08);
	passed += expect_bool("amd syscall",
			      features.has[X86_64_CPU_FEATURE_SYSCALL], true);
	passed += expect_bool("amd nx", features.has[X86_64_CPU_FEATURE_NX], true);
	passed += expect_bool("amd page1gb",
			      features.has[X86_64_CPU_FEATURE_PAGE1GB], true);
	passed += expect_bool("amd rdtscp",
			      features.has[X86_64_CPU_FEATURE_RDTSCP], true);
	passed += expect_bool("amd long mode",
			      features.has[X86_64_CPU_FEATURE_LONG_MODE], true);
	passed += expect_bool("amd lahf lm",
			      features.has[X86_64_CPU_FEATURE_LAHF_LM], true);
	passed += expect_bool("amd lzcnt",
			      features.has[X86_64_CPU_FEATURE_LZCNT], true);
	passed += expect_bool("amd prefetchw",
			      features.has[X86_64_CPU_FEATURE_PREFETCHW], true);
	passed += expect_bool("amd serialize reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_SERIALIZE],
			      false);
	passed += expect_bool("amd pcid reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_PCID],
			      false);
	passed += expect_bool("amd tsc deadline reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_TSC_DEADLINE],
			      false);

	return passed;
}

static int test_vendor_signature_display_model_rules(void) {
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int passed = 0;

	raw.leaf0 = vendor_leaf(1, "GenuineIntel");
	raw.leaf1 = leaf(0x000a0631, 0, 0, 0);
	x86_64_cpu_features_decode(&features, &raw);
	passed += expect_u32("intel family 6 display model",
			      features.display_model, 0xa3);

	raw.leaf0 = vendor_leaf(1, "AuthenticAMD");
	raw.leaf1 = leaf(0x000a0631, 0, 0, 0);
	x86_64_cpu_features_decode(&features, &raw);
	passed += expect_u32("amd family 6 display model",
			      features.display_model, 0x03);

	raw.leaf0 = vendor_leaf(1, "AuthenticAMD");
	raw.leaf1 = leaf(0x00800f82, 0, 0, 0);
	x86_64_cpu_features_decode(&features, &raw);
	passed += expect_u32("amd family f display model",
			      features.display_model, 0x08);

	raw.leaf0 = vendor_leaf(1, "TCGTCGTCGTCG");
	raw.leaf1 = leaf(0x000a0631, 0, 0, 0);
	x86_64_cpu_features_decode(&features, &raw);
	passed += expect_u32("unknown family 6 display model",
			      features.display_model, 0x03);

	return passed;
}

static int test_unknown_vendor_keeps_common_decode(void) {
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int passed = 0;

	raw.leaf0 = vendor_leaf(7, "TCGTCGTCGTCG");
	raw.leaf1 = leaf(0, 0, (1u << 17) | (1u << 24),
			 (1u << 5) | (1u << 16) | (1u << 26));
	raw.leaf7_0 = leaf(0, 0, 0, (1u << 14));
	raw.leaf_ext0 = vendor_leaf(0x80000001u, "TCGTCGTCGTCG");
	raw.leaf_ext1 = leaf(0, 0, 0, (1u << 29));

	x86_64_cpu_features_decode(&features, &raw);

	passed += expect_str("unknown vendor", features.vendor, "TCGTCGTCGTCG");
	passed += expect_u32("unknown vendor id", features.vendor_id,
			      X86_64_CPU_VENDOR_UNKNOWN);
	passed += expect_bool("unknown common msr",
			      features.has[X86_64_CPU_FEATURE_MSR], true);
	passed += expect_bool("unknown common pat",
			      features.has[X86_64_CPU_FEATURE_PAT], true);
	passed += expect_bool("unknown common sse2",
			      features.has[X86_64_CPU_FEATURE_SSE2], true);
	passed += expect_bool("unknown ext long mode",
			      features.has[X86_64_CPU_FEATURE_LONG_MODE], true);
	passed += expect_bool("unknown serialize reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_SERIALIZE],
			      false);
	passed += expect_bool("unknown pcid reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_PCID],
			      false);
	passed += expect_bool("unknown tsc deadline reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_TSC_DEADLINE],
			      false);

	return passed;
}

static int test_missing_leaves_default_to_zero(void) {
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int passed = 0;

	raw.leaf0 = vendor_leaf(1, "GenuineIntel");
	raw.leaf1 = leaf(0, 0, (1u << 26), (1u << 5) | (1u << 16));
	raw.leaf7_0 = leaf(0, (1u << 5), (1u << 16), (1u << 14));
	raw.leafd_0 = leaf(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu);
	raw.leafd_1 = leaf(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu);
	raw.leaf_ext0 = vendor_leaf(0x80000000u, "GenuineIntel");
	raw.leaf_ext1 = leaf(0, 0, (1u << 0), (1u << 29));

	x86_64_cpu_features_decode(&features, &raw);

	passed += expect_bool("leaf 7 unavailable",
			      features.has[X86_64_CPU_FEATURE_AVX2], false);
	passed += expect_bool("ext leaf unavailable",
			      features.has[X86_64_CPU_FEATURE_LONG_MODE], false);
	passed += expect_str("brand unavailable", features.brand, "");

	/* XSAVE leaf is unavailable because max basic leaf is 1. */
	passed += expect_bool("xsave leaf1 available",
			      features.has[X86_64_CPU_FEATURE_XSAVE], true);
	passed += expect_u64("xsave mask unavailable",
			      features.xcr0_supported_mask, 0);
	passed += expect_u32("xsave size unavailable",
			      features.xsave_area_size_supported, 0);

	raw.leaf1.ecx = 0;
	raw.leaf0.eax = 0x0d;
	x86_64_cpu_features_decode(&features, &raw);
	passed += expect_bool("xsave feature unavailable",
			      features.has[X86_64_CPU_FEATURE_XSAVE], false);
	passed += expect_u64("xsave gated by feature",
			      features.xcr0_supported_mask, 0);

	return passed;
}

static int test_feature_query_rejects_invalid_id(void) {
	int passed = 0;

	passed += expect_bool("invalid negative feature",
			      x86_64_cpu_has_feature((enum x86_64_cpu_feature)-1),
			      false);
	passed += expect_bool("invalid high feature",
			      x86_64_cpu_has_feature(X86_64_CPU_FEATURE_NR),
			      false);

	return passed;
}

int main(void) {
	int passed = 0;
	int total = 0;

	passed += test_decode_intel_like_common_features();
	total += 82;

	passed += test_decode_amd_like_extended_features_and_brand();
	total += 16;

	passed += test_vendor_signature_display_model_rules();
	total += 4;

	passed += test_unknown_vendor_keeps_common_decode();
	total += 9;

	passed += test_missing_leaves_default_to_zero();
	total += 8;

	passed += test_feature_query_rejects_invalid_id();
	total += 2;

	if (passed != total) {
		printf("cpu_features_test: %d/%d passed\n", passed, total);
		return 1;
	}

	printf("cpu_features_test: %d cases passed\n", total);
	return 0;
}
