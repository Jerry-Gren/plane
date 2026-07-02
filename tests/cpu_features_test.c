#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <hal/x86_64/cpu_features.h>

#include "support/printk_stubs.h"
#include "support/test.h"

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

static uint32_t chars4(const char text[4])
{
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

static int test_decode_intel_like_common_features(void)
{
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int failures = 0;

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

	failures += test_expect_str("intel vendor", features.vendor, "GenuineIntel");
	failures += test_expect_u32("intel vendor id", features.vendor_id,
			      X86_64_CPU_VENDOR_INTEL);
	failures += test_expect_u32("max basic leaf", features.max_basic_leaf, 0x0d);
	failures += test_expect_u32("max extended leaf",
			      features.max_extended_leaf, 0x80000001u);
	failures += test_expect_u32("raw leaf 7 ebx",
			      features.raw.leaf7_0.ebx, raw.leaf7_0.ebx);
	failures += test_expect_u32("leaf 7 max subleaf",
			      features.leaf7_max_subleaf, 2);
	failures += test_expect_u32("stepping", features.stepping, 3);
	failures += test_expect_u32("base model", features.base_model, 0xe);
	failures += test_expect_u32("base family", features.base_family, 6);
	failures += test_expect_u32("extended model", features.extended_model, 5);
	failures += test_expect_u32("display model", features.display_model, 0x5e);
	failures += test_expect_u32("display family", features.display_family, 6);
	failures += test_expect_u32("initial apic id", features.initial_apic_id, 0x24);
	failures += test_expect_u32("logical processor count",
			      features.logical_processor_count, 8);
	failures += test_expect_u32("clflush line size",
			      features.clflush_line_size, 64);

	failures += test_expect_bool("fpu", features.has[X86_64_CPU_FEATURE_FPU], true);
	failures += test_expect_bool("tsc", features.has[X86_64_CPU_FEATURE_TSC], true);
	failures += test_expect_bool("msr", features.has[X86_64_CPU_FEATURE_MSR], true);
	failures += test_expect_bool("pae", features.has[X86_64_CPU_FEATURE_PAE], true);
	failures += test_expect_bool("cx8", features.has[X86_64_CPU_FEATURE_CX8], true);
	failures += test_expect_bool("apic", features.has[X86_64_CPU_FEATURE_APIC], true);
	failures += test_expect_bool("mtrr", features.has[X86_64_CPU_FEATURE_MTRR], true);
	failures += test_expect_bool("pge", features.has[X86_64_CPU_FEATURE_PGE], true);
	failures += test_expect_bool("cmov", features.has[X86_64_CPU_FEATURE_CMOV], true);
	failures += test_expect_bool("pat", features.has[X86_64_CPU_FEATURE_PAT], true);
	failures += test_expect_bool("clflush",
			      features.has[X86_64_CPU_FEATURE_CLFLUSH], true);
	failures += test_expect_bool("fxsr", features.has[X86_64_CPU_FEATURE_FXSR], true);
	failures += test_expect_bool("sse", features.has[X86_64_CPU_FEATURE_SSE], true);
	failures += test_expect_bool("sse2", features.has[X86_64_CPU_FEATURE_SSE2], true);
	failures += test_expect_bool("htt", features.has[X86_64_CPU_FEATURE_HTT], true);

	failures += test_expect_bool("sse3", features.has[X86_64_CPU_FEATURE_SSE3], true);
	failures += test_expect_bool("pclmulqdq",
			      features.has[X86_64_CPU_FEATURE_PCLMULQDQ], true);
	failures += test_expect_bool("ssse3", features.has[X86_64_CPU_FEATURE_SSSE3], true);
	failures += test_expect_bool("fma", features.has[X86_64_CPU_FEATURE_FMA], true);
	failures += test_expect_bool("cx16", features.has[X86_64_CPU_FEATURE_CX16], true);
	failures += test_expect_bool("intel pcid",
			      features.has[X86_64_CPU_FEATURE_INTEL_PCID], true);
	failures += test_expect_bool("sse4.1",
			      features.has[X86_64_CPU_FEATURE_SSE4_1], true);
	failures += test_expect_bool("sse4.2",
			      features.has[X86_64_CPU_FEATURE_SSE4_2], true);
	failures += test_expect_bool("x2apic",
			      features.has[X86_64_CPU_FEATURE_X2APIC], true);
	failures += test_expect_bool("movbe", features.has[X86_64_CPU_FEATURE_MOVBE], true);
	failures += test_expect_bool("popcnt",
			      features.has[X86_64_CPU_FEATURE_POPCNT], true);
	failures += test_expect_bool("intel tsc deadline",
			      features.has[X86_64_CPU_FEATURE_INTEL_TSC_DEADLINE],
			      true);
	failures += test_expect_bool("aes", features.has[X86_64_CPU_FEATURE_AES], true);
	failures += test_expect_bool("xsave", features.has[X86_64_CPU_FEATURE_XSAVE], true);
	failures += test_expect_bool("osxsave",
			      features.has[X86_64_CPU_FEATURE_OSXSAVE], true);
	failures += test_expect_bool("avx", features.has[X86_64_CPU_FEATURE_AVX], true);
	failures += test_expect_bool("f16c", features.has[X86_64_CPU_FEATURE_F16C], true);
	failures += test_expect_bool("rdrand",
			      features.has[X86_64_CPU_FEATURE_RDRAND], true);
	failures += test_expect_bool("hypervisor",
			      features.has[X86_64_CPU_FEATURE_HYPERVISOR], true);

	failures += test_expect_bool("fsgsbase",
			      features.has[X86_64_CPU_FEATURE_FSGSBASE], true);
	failures += test_expect_bool("tsc adjust",
			      features.has[X86_64_CPU_FEATURE_TSC_ADJUST], true);
	failures += test_expect_bool("bmi1", features.has[X86_64_CPU_FEATURE_BMI1], true);
	failures += test_expect_bool("avx2", features.has[X86_64_CPU_FEATURE_AVX2], true);
	failures += test_expect_bool("smep", features.has[X86_64_CPU_FEATURE_SMEP], true);
	failures += test_expect_bool("bmi2", features.has[X86_64_CPU_FEATURE_BMI2], true);
	failures += test_expect_bool("erms", features.has[X86_64_CPU_FEATURE_ERMS], true);
	failures += test_expect_bool("invpcid",
			      features.has[X86_64_CPU_FEATURE_INVPCID], true);
	failures += test_expect_bool("rdseed",
			      features.has[X86_64_CPU_FEATURE_RDSEED], true);
	failures += test_expect_bool("adx", features.has[X86_64_CPU_FEATURE_ADX], true);
	failures += test_expect_bool("smap", features.has[X86_64_CPU_FEATURE_SMAP], true);
	failures += test_expect_bool("clflushopt",
			      features.has[X86_64_CPU_FEATURE_CLFLUSHOPT], true);
	failures += test_expect_bool("clwb", features.has[X86_64_CPU_FEATURE_CLWB], true);
	failures += test_expect_bool("sha", features.has[X86_64_CPU_FEATURE_SHA], true);
	failures += test_expect_bool("umip", features.has[X86_64_CPU_FEATURE_UMIP], true);
	failures += test_expect_bool("la57", features.has[X86_64_CPU_FEATURE_LA57], true);
	failures += test_expect_bool("rdpid", features.has[X86_64_CPU_FEATURE_RDPID], true);
	failures += test_expect_bool("intel serialize",
			      features.has[X86_64_CPU_FEATURE_INTEL_SERIALIZE],
			      true);

	failures += test_expect_u64("xcr0 supported mask",
			      features.xcr0_supported_mask, 0x0000000200000007ull);
	failures += test_expect_u32("xsave enabled size",
			      features.xsave_area_size_enabled, 0x340);
	failures += test_expect_u32("xsave supported size",
			      features.xsave_area_size_supported, 0x840);
	failures += test_expect_u32("xsave leaf 1 eax",
			      features.xsave_leaf1.eax, 0x0000000f);
	failures += test_expect_u32("xsave leaf 1 ebx",
			      features.xsave_leaf1.ebx, 0x11111111);
	failures += test_expect_u32("xsave leaf 1 ecx",
			      features.xsave_leaf1.ecx, 0x22222222);
	failures += test_expect_u32("xsave leaf 1 edx",
			      features.xsave_leaf1.edx, 0x33333333);

	failures += test_expect_bool("syscall",
			      features.has[X86_64_CPU_FEATURE_SYSCALL], true);
	failures += test_expect_bool("nx", features.has[X86_64_CPU_FEATURE_NX], true);
	failures += test_expect_bool("page1gb",
			      features.has[X86_64_CPU_FEATURE_PAGE1GB], true);
	failures += test_expect_bool("rdtscp",
			      features.has[X86_64_CPU_FEATURE_RDTSCP], true);
	failures += test_expect_bool("long mode",
			      features.has[X86_64_CPU_FEATURE_LONG_MODE], true);
	failures += test_expect_bool("lahf lm",
			      features.has[X86_64_CPU_FEATURE_LAHF_LM], true);
	failures += test_expect_bool("lzcnt", features.has[X86_64_CPU_FEATURE_LZCNT], true);
	failures += test_expect_bool("prefetchw",
			      features.has[X86_64_CPU_FEATURE_PREFETCHW], true);

	return failures;
}

static int test_decode_amd_like_extended_features_and_brand(void)
{
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int failures = 0;

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

	failures += test_expect_str("amd vendor", features.vendor, "AuthenticAMD");
	failures += test_expect_u32("amd vendor id", features.vendor_id,
			      X86_64_CPU_VENDOR_AMD);
	failures += test_expect_str("amd brand", features.brand,
			      "Plane CPU       Test Brand      Model 0001      ");
	failures += test_expect_u32("amd display family", features.display_family, 0x17);
	failures += test_expect_u32("amd display model", features.display_model, 0x08);
	failures += test_expect_bool("amd syscall",
			      features.has[X86_64_CPU_FEATURE_SYSCALL], true);
	failures += test_expect_bool("amd nx", features.has[X86_64_CPU_FEATURE_NX], true);
	failures += test_expect_bool("amd page1gb",
			      features.has[X86_64_CPU_FEATURE_PAGE1GB], true);
	failures += test_expect_bool("amd rdtscp",
			      features.has[X86_64_CPU_FEATURE_RDTSCP], true);
	failures += test_expect_bool("amd long mode",
			      features.has[X86_64_CPU_FEATURE_LONG_MODE], true);
	failures += test_expect_bool("amd lahf lm",
			      features.has[X86_64_CPU_FEATURE_LAHF_LM], true);
	failures += test_expect_bool("amd lzcnt",
			      features.has[X86_64_CPU_FEATURE_LZCNT], true);
	failures += test_expect_bool("amd prefetchw",
			      features.has[X86_64_CPU_FEATURE_PREFETCHW], true);
	failures += test_expect_bool("amd serialize reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_SERIALIZE],
			      false);
	failures += test_expect_bool("amd pcid reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_PCID],
			      false);
	failures += test_expect_bool("amd tsc deadline reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_TSC_DEADLINE],
			      false);

	return failures;
}

static int test_vendor_signature_display_model_rules(void)
{
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int failures = 0;

	raw.leaf0 = vendor_leaf(1, "GenuineIntel");
	raw.leaf1 = leaf(0x000a0631, 0, 0, 0);
	x86_64_cpu_features_decode(&features, &raw);
	failures += test_expect_u32("intel family 6 display model",
			      features.display_model, 0xa3);

	raw.leaf0 = vendor_leaf(1, "AuthenticAMD");
	raw.leaf1 = leaf(0x000a0631, 0, 0, 0);
	x86_64_cpu_features_decode(&features, &raw);
	failures += test_expect_u32("amd family 6 display model",
			      features.display_model, 0x03);

	raw.leaf0 = vendor_leaf(1, "AuthenticAMD");
	raw.leaf1 = leaf(0x00800f82, 0, 0, 0);
	x86_64_cpu_features_decode(&features, &raw);
	failures += test_expect_u32("amd family f display model",
			      features.display_model, 0x08);

	raw.leaf0 = vendor_leaf(1, "TCGTCGTCGTCG");
	raw.leaf1 = leaf(0x000a0631, 0, 0, 0);
	x86_64_cpu_features_decode(&features, &raw);
	failures += test_expect_u32("unknown family 6 display model",
			      features.display_model, 0x03);

	return failures;
}

static int test_unknown_vendor_keeps_common_decode(void)
{
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int failures = 0;

	raw.leaf0 = vendor_leaf(7, "TCGTCGTCGTCG");
	raw.leaf1 = leaf(0, 0, (1u << 17) | (1u << 24),
			 (1u << 5) | (1u << 16) | (1u << 26));
	raw.leaf7_0 = leaf(0, 0, 0, (1u << 14));
	raw.leaf_ext0 = vendor_leaf(0x80000001u, "TCGTCGTCGTCG");
	raw.leaf_ext1 = leaf(0, 0, 0, (1u << 29));

	x86_64_cpu_features_decode(&features, &raw);

	failures += test_expect_str("unknown vendor", features.vendor, "TCGTCGTCGTCG");
	failures += test_expect_u32("unknown vendor id", features.vendor_id,
			      X86_64_CPU_VENDOR_UNKNOWN);
	failures += test_expect_bool("unknown common msr",
			      features.has[X86_64_CPU_FEATURE_MSR], true);
	failures += test_expect_bool("unknown common pat",
			      features.has[X86_64_CPU_FEATURE_PAT], true);
	failures += test_expect_bool("unknown common sse2",
			      features.has[X86_64_CPU_FEATURE_SSE2], true);
	failures += test_expect_bool("unknown ext long mode",
			      features.has[X86_64_CPU_FEATURE_LONG_MODE], true);
	failures += test_expect_bool("unknown serialize reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_SERIALIZE],
			      false);
	failures += test_expect_bool("unknown pcid reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_PCID],
			      false);
	failures += test_expect_bool("unknown tsc deadline reserved",
			      features.has[X86_64_CPU_FEATURE_INTEL_TSC_DEADLINE],
			      false);

	return failures;
}

static int test_missing_leaves_default_to_zero(void)
{
	struct x86_64_cpuid_raw raw = {0};
	struct x86_64_cpu_features features;
	int failures = 0;

	raw.leaf0 = vendor_leaf(1, "GenuineIntel");
	raw.leaf1 = leaf(0, 0, (1u << 26), (1u << 5) | (1u << 16));
	raw.leaf7_0 = leaf(0, (1u << 5), (1u << 16), (1u << 14));
	raw.leafd_0 = leaf(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu);
	raw.leafd_1 = leaf(0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu);
	raw.leaf_ext0 = vendor_leaf(0x80000000u, "GenuineIntel");
	raw.leaf_ext1 = leaf(0, 0, (1u << 0), (1u << 29));

	x86_64_cpu_features_decode(&features, &raw);

	failures += test_expect_bool("leaf 7 unavailable",
			      features.has[X86_64_CPU_FEATURE_AVX2], false);
	failures += test_expect_bool("ext leaf unavailable",
			      features.has[X86_64_CPU_FEATURE_LONG_MODE], false);
	failures += test_expect_str("brand unavailable", features.brand, "");

	/* XSAVE leaf is unavailable because max basic leaf is 1. */
	failures += test_expect_bool("xsave leaf1 available",
			      features.has[X86_64_CPU_FEATURE_XSAVE], true);
	failures += test_expect_u64("xsave mask unavailable",
			      features.xcr0_supported_mask, 0);
	failures += test_expect_u32("xsave size unavailable",
			      features.xsave_area_size_supported, 0);

	raw.leaf1.ecx = 0;
	raw.leaf0.eax = 0x0d;
	x86_64_cpu_features_decode(&features, &raw);
	failures += test_expect_bool("xsave feature unavailable",
			      features.has[X86_64_CPU_FEATURE_XSAVE], false);
	failures += test_expect_u64("xsave gated by feature",
			      features.xcr0_supported_mask, 0);

	return failures;
}

static int test_feature_query_rejects_invalid_id(void)
{
	int failures = 0;

	failures += test_expect_bool("invalid negative feature",
			      x86_64_cpu_has_feature((enum x86_64_cpu_feature)-1),
			      false);
	failures += test_expect_bool("invalid high feature",
			      x86_64_cpu_has_feature(X86_64_CPU_FEATURE_NR),
			      false);

	return failures;
}

int main(void)
{
	static const struct test_case cases[] = {
		TEST_CASE(test_decode_intel_like_common_features),
		TEST_CASE(test_decode_amd_like_extended_features_and_brand),
		TEST_CASE(test_vendor_signature_display_model_rules),
		TEST_CASE(test_unknown_vendor_keeps_common_decode),
		TEST_CASE(test_missing_leaves_default_to_zero),
		TEST_CASE(test_feature_query_rejects_invalid_id),
	};

	return test_run_cases("cpu_features_test", cases, TEST_ARRAY_SIZE(cases));
}
