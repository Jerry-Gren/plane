#include <stddef.h>

#include <hal/x86_64/cpuid_defs.h>
#include <hal/x86_64/cpu_features.h>
#include <klib/string.h>
#include <plane/bits.h>
#include <plane/printk.h>

/*
 * CPUID decoding follows the common Intel SDM Vol.1/Vol.2 and AMD APM Vol.3
 * CPUID leaves. vendor-specific leaves must be gated by vendor before use.
 */

#define CPUID_SIGNATURE_STEPPING        GENMASK(3, 0)
#define CPUID_SIGNATURE_BASE_MODEL      GENMASK(7, 4)
#define CPUID_SIGNATURE_BASE_FAMILY     GENMASK(11, 8)
#define CPUID_SIGNATURE_PROCESSOR_TYPE  GENMASK(13, 12)
#define CPUID_SIGNATURE_EXT_MODEL       GENMASK(19, 16)
#define CPUID_SIGNATURE_EXT_FAMILY      GENMASK(27, 20)
#define CPUID_DISPLAY_EXT_MODEL         GENMASK(7, 4)

#define CPUID_1_EBX_CLFLUSH_LINE_SIZE   GENMASK(15, 8)
#define CPUID_1_EBX_LOGICAL_PROCESSORS  GENMASK(23, 16)
#define CPUID_1_EBX_INITIAL_APIC_ID     GENMASK(31, 24)

#define CPUID_XSAVE_XCR0_LOW            GENMASK_ULL(31, 0)
#define CPUID_XSAVE_XCR0_HIGH           GENMASK_ULL(63, 32)

static struct x86_64_cpu_features boot_cpu_features;

static struct x86_64_cpuid_leaf cpuid_count(uint32_t leaf, uint32_t subleaf)
{
	struct x86_64_cpuid_leaf out;

	__asm__ volatile (
		"cpuid"
		: "=a" (out.eax), "=b" (out.ebx), "=c" (out.ecx), "=d" (out.edx)
		: "a" (leaf), "c" (subleaf)
	);

	return out;
}

static void write_u32_string(char *dst, uint32_t value)
{
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

static enum x86_64_cpu_vendor vendor_id_from_string(const char *vendor)
{
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

static bool should_extend_display_model(enum x86_64_cpu_vendor vendor_id,
					uint8_t base_family) {
	if (vendor_id == X86_64_CPU_VENDOR_INTEL) {
		return base_family == 0x6 || base_family == 0xf;
	}

	return base_family == 0xf;
}

static void decode_signature(struct x86_64_cpu_features *features,
			     const struct x86_64_cpuid_leaf *leaf1) {
	uint32_t signature = leaf1->eax;

	features->stepping = FIELD_GET(CPUID_SIGNATURE_STEPPING, signature);
	features->base_model = FIELD_GET(CPUID_SIGNATURE_BASE_MODEL, signature);
	features->base_family = FIELD_GET(CPUID_SIGNATURE_BASE_FAMILY, signature);
	features->processor_type =
		FIELD_GET(CPUID_SIGNATURE_PROCESSOR_TYPE, signature);
	features->extended_model =
		FIELD_GET(CPUID_SIGNATURE_EXT_MODEL, signature);
	features->extended_family =
		FIELD_GET(CPUID_SIGNATURE_EXT_FAMILY, signature);

	features->display_family = features->base_family;
	if (features->base_family == 0xf) {
		features->display_family += features->extended_family;
	}

	features->display_model = features->base_model;
	if (should_extend_display_model(features->vendor_id,
					features->base_family)) {
		features->display_model |=
			FIELD_PREP(CPUID_DISPLAY_EXT_MODEL,
				   features->extended_model);
	}
}

static void decode_leaf1(struct x86_64_cpu_features *features,
			 const struct x86_64_cpuid_leaf *leaf1) {
	/*
	 * Bit        Intel field                         AMD field
	 *
	 * EDX[0]     FPU                                 FPU
	 * EDX[1]     VME                                 VME
	 * EDX[2]     DE                                  DE
	 * EDX[3]     PSE                                 PSE
	 * EDX[4]     TSC                                 TSC
	 * EDX[5]     MSR                                 MSR
	 * EDX[6]     PAE                                 PAE
	 * EDX[7]     MCE                                 MCE
	 * EDX[8]     CMPXCHG8B                           CMPXCHG8B
	 * EDX[9]     APIC                                APIC
	 * EDX[10]    Reserved                            Reserved
	 * EDX[11]    SEP                                 SysEnterSysExit
	 * EDX[12]    MTRR                                MTRR
	 * EDX[13]    PGE                                 PGE
	 * EDX[14]    MCA                                 MCA
	 * EDX[15]    CMOV                                CMOV
	 * EDX[16]    PAT                                 PAT
	 * EDX[17]    PSE_36                              PSE36
	 * EDX[18]    PSN                                 Reserved
	 * EDX[19]    CLFLUSH                             CLFSH
	 * EDX[20]    Reserved                            Reserved
	 * EDX[21]    DS                                  Reserved
	 * EDX[22]    ACPI                                Reserved
	 * EDX[23]    MMX                                 MMX
	 * EDX[24]    FXSR                                FXSR
	 * EDX[25]    SSE                                 SSE
	 * EDX[26]    SSE2                                SSE2
	 * EDX[27]    SELF_SNOOP                          Reserved
	 * EDX[28]    HTT                                 HTT
	 * EDX[29]    TM                                  Reserved
	 * EDX[30]    Reserved                            Reserved
	 * EDX[31]    PBE                                 Reserved
	 *
	 * ECX[0]     SSE3                                SSE3
	 * ECX[1]     PCLMULQDQ                           PCLMULQDQ
	 * ECX[2]     DTES64                              Reserved
	 * ECX[3]     MONITOR                             MONITOR
	 * ECX[4]     DS_CPL                              Reserved
	 * ECX[5]     VMX                                 Reserved
	 * ECX[6]     SMX                                 Reserved
	 * ECX[7]     EIST                                Reserved
	 * ECX[8]     TM2                                 Reserved
	 * ECX[9]     SSSE3                               SSSE3
	 * ECX[10]    L1_CONTEXT_ID                       Reserved
	 * ECX[11]    DEBUG_INTERFACE                     Reserved
	 * ECX[12]    FMA                                 FMA
	 * ECX[13]    CMPXCHG16B                          CMPXCHG16B
	 * ECX[14]    XTPR_UPDATE_CONTROL                 Reserved
	 * ECX[15]    PERF_CAPABILITIES                   Reserved
	 * ECX[16]    Reserved                            Reserved
	 * ECX[17]    PCID                                Reserved
	 * ECX[18]    DCA                                 Reserved
	 * ECX[19]    SSE4_1                              SSE41
	 * ECX[20]    SSE4_2                              SSE42
	 * ECX[21]    X2APIC                              x2APIC
	 * ECX[22]    MOVBE                               MOVBE
	 * ECX[23]    POPCNT                              POPCNT
	 * ECX[24]    TSC_DEADLINE                        Reserved
	 * ECX[25]    AESNI                               AES
	 * ECX[26]    XSAVE                               XSAVE
	 * ECX[27]    OSXSAVE runtime state               OSXSAVE runtime state
	 * ECX[28]    AVX                                 AVX
	 * ECX[29]    F16C                                F16C
	 * ECX[30]    RDRAND                              RDRAND
	 * ECX[31]    Not Used, software emulation        Hypervisor guest status
	 */
	decode_signature(features, leaf1);

	features->initial_apic_id =
		FIELD_GET(CPUID_1_EBX_INITIAL_APIC_ID, leaf1->ebx);
	features->logical_processor_count =
		FIELD_GET(CPUID_1_EBX_LOGICAL_PROCESSORS, leaf1->ebx);
	features->clflush_line_size =
		(uint16_t)(FIELD_GET(CPUID_1_EBX_CLFLUSH_LINE_SIZE,
				     leaf1->ebx) * 8);

	features->has[X86_64_CPU_FEATURE_FPU] =
		(leaf1->edx & X86_64_CPUID_1_EDX_FPU) != 0;
	features->has[X86_64_CPU_FEATURE_TSC] =
		(leaf1->edx & X86_64_CPUID_1_EDX_TSC) != 0;
	features->has[X86_64_CPU_FEATURE_MSR] =
		(leaf1->edx & X86_64_CPUID_1_EDX_MSR) != 0;
	features->has[X86_64_CPU_FEATURE_PAE] =
		(leaf1->edx & X86_64_CPUID_1_EDX_PAE) != 0;
	features->has[X86_64_CPU_FEATURE_CX8] =
		(leaf1->edx & X86_64_CPUID_1_EDX_CX8) != 0;
	features->has[X86_64_CPU_FEATURE_APIC] =
		(leaf1->edx & X86_64_CPUID_1_EDX_APIC) != 0;
	features->has[X86_64_CPU_FEATURE_MTRR] =
		(leaf1->edx & X86_64_CPUID_1_EDX_MTRR) != 0;
	features->has[X86_64_CPU_FEATURE_PGE] =
		(leaf1->edx & X86_64_CPUID_1_EDX_PGE) != 0;
	features->has[X86_64_CPU_FEATURE_CMOV] =
		(leaf1->edx & X86_64_CPUID_1_EDX_CMOV) != 0;
	features->has[X86_64_CPU_FEATURE_PAT] =
		(leaf1->edx & X86_64_CPUID_1_EDX_PAT) != 0;
	features->has[X86_64_CPU_FEATURE_CLFLUSH] =
		(leaf1->edx & X86_64_CPUID_1_EDX_CLFLUSH) != 0;
	features->has[X86_64_CPU_FEATURE_FXSR] =
		(leaf1->edx & X86_64_CPUID_1_EDX_FXSR) != 0;
	features->has[X86_64_CPU_FEATURE_SSE] =
		(leaf1->edx & X86_64_CPUID_1_EDX_SSE) != 0;
	features->has[X86_64_CPU_FEATURE_SSE2] =
		(leaf1->edx & X86_64_CPUID_1_EDX_SSE2) != 0;
	features->has[X86_64_CPU_FEATURE_HTT] =
		(leaf1->edx & X86_64_CPUID_1_EDX_HTT) != 0;

	features->has[X86_64_CPU_FEATURE_SSE3] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_SSE3) != 0;
	features->has[X86_64_CPU_FEATURE_PCLMULQDQ] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_PCLMULQDQ) != 0;
	features->has[X86_64_CPU_FEATURE_SSSE3] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_SSSE3) != 0;
	features->has[X86_64_CPU_FEATURE_FMA] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_FMA) != 0;
	features->has[X86_64_CPU_FEATURE_CX16] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_CX16) != 0;
	features->has[X86_64_CPU_FEATURE_SSE4_1] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_SSE4_1) != 0;
	features->has[X86_64_CPU_FEATURE_SSE4_2] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_SSE4_2) != 0;
	features->has[X86_64_CPU_FEATURE_X2APIC] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_X2APIC) != 0;
	features->has[X86_64_CPU_FEATURE_MOVBE] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_MOVBE) != 0;
	features->has[X86_64_CPU_FEATURE_POPCNT] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_POPCNT) != 0;
	features->has[X86_64_CPU_FEATURE_AES] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_AES) != 0;
	features->has[X86_64_CPU_FEATURE_XSAVE] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_XSAVE) != 0;
	features->has[X86_64_CPU_FEATURE_OSXSAVE] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_OSXSAVE) != 0;
	features->has[X86_64_CPU_FEATURE_AVX] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_AVX) != 0;
	features->has[X86_64_CPU_FEATURE_F16C] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_F16C) != 0;
	features->has[X86_64_CPU_FEATURE_RDRAND] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_RDRAND) != 0;

	/*
	 * Intel reserves ECX[31] for software emulation; AMD reserves it for
	 * hypervisor guest status. Treat it as an environment hint.
	 */
	features->has[X86_64_CPU_FEATURE_HYPERVISOR] =
		(leaf1->ecx & X86_64_CPUID_1_ECX_HYPERVISOR) != 0;

	/* Intel defines ECX[17] as PCID; AMD reserves ECX[18:14]. */
	if (features->vendor_id == X86_64_CPU_VENDOR_INTEL) {
		features->has[X86_64_CPU_FEATURE_INTEL_PCID] =
			(leaf1->ecx & X86_64_CPUID_1_ECX_INTEL_PCID) != 0;
	}

	/* Intel defines ECX[24] as TSC_DEADLINE; AMD reserves this bit. */
	if (features->vendor_id == X86_64_CPU_VENDOR_INTEL) {
		features->has[X86_64_CPU_FEATURE_INTEL_TSC_DEADLINE] =
			(leaf1->ecx & X86_64_CPUID_1_ECX_INTEL_TSC_DEADLINE) != 0;
	}
}

static void decode_leaf7_0(struct x86_64_cpu_features *features,
			   const struct x86_64_cpuid_leaf *leaf7) {
	/*
	 * Bit         Intel field                        AMD field
	 *
	 * EBX[0]      FSGSBASE                           FSGSBASE
	 * EBX[1]      TSC_ADJUST                         TSCADJUST
	 * EBX[2]      SGX                                Reserved
	 * EBX[3]      BMI1                               BMI1
	 * EBX[4]      HLE                                Reserved
	 * EBX[5]      AVX2                               AVX2
	 * EBX[6]      FDP_EXCPTN_ONLY                    Reserved
	 * EBX[7]      SMEP                               SMEP
	 * EBX[8]      BMI2                               BMI2
	 * EBX[9]      ENH_REP_MOVSB_STOSB                ERMS
	 * EBX[10]     INVPCID                            INVPCID
	 * EBX[11]     RTM                                Reserved
	 * EBX[12]     RDT_M                              PQM
	 * EBX[13]     FCS_FDS_DEPRECATION                Reserved
	 * EBX[14]     MPX                                Reserved
	 * EBX[15]     RDT_A                              PQE
	 * EBX[16]     AVX512F                            AVX512F
	 * EBX[17]     AVX512DQ                           AVX512DQ
	 * EBX[18]     RDSEED                             RDSEED
	 * EBX[19]     ADX                                ADX
	 * EBX[20]     SMAP                               SMAP
	 * EBX[21]     AVX512_IFMA                        AVX512_IFMA
	 * EBX[22]     Reserved                           Reserved
	 * EBX[23]     CLFLUSHOPT                         CLFLUSHOPT
	 * EBX[24]     CLWB                               CLWB
	 * EBX[25]     INTEL_PROC_TRACE                   Reserved
	 * EBX[26]     AVX512PF                           Reserved
	 * EBX[27]     AVX512ER                           Reserved
	 * EBX[28]     AVX512CD                           AVX512CD
	 * EBX[29]     SHA                                SHA
	 * EBX[30]     AVX512BW                           AVX512BW
	 * EBX[31]     AVX512VL                           AVX512VL
	 *
	 * ECX[0]      PREFETCHWT1                        Reserved
	 * ECX[1]      AVX512_VBMI                        AVX512_VBMI
	 * ECX[2]      UMIP                               UMIP
	 * ECX[3]      PKU                                PKU
	 * ECX[4]      OSPKE runtime state                OSPKE runtime state
	 * ECX[5]      WAITPKG                            Reserved
	 * ECX[6]      AVX512_VBMI2                       AVX512_VBMI2
	 * ECX[7]      CET_SS                             CET_SS
	 * ECX[8]      GFNI                               GFNI
	 * ECX[9]      VAES                               VAES
	 * ECX[10]     VPCLMULQDQ                         VPCMULQDQ
	 * ECX[11]     AVX512_VNNI                        AVX512_VNNI
	 * ECX[12]     AVX512_BITALG                      AVX512_BITALG
	 * ECX[13]     TME_EN                             Reserved
	 * ECX[14]     AVX512_VPOPCNTDQ                   AVX512_VPOPCNTDQ
	 * ECX[15]     Reserved                           Reserved
	 * ECX[16]     LA57                               LA57
	 * ECX[21:17]  MPX_MAWAU                          Reserved
	 * ECX[22]     RDPID                              RDPID
	 * ECX[23]     KEY_LOCKER                         Reserved
	 * ECX[24]     BUS_LOCK_DETECT                    BUSLOCKTRAP
	 * ECX[25]     CLDEMOTE                           Reserved
	 * ECX[26]     Reserved                           Reserved
	 * ECX[27]     MOVDIRI                            MOVDIRI
	 * ECX[28]     MOVDIR64B                          MOVDIR64B
	 * ECX[29]     ENQCMD                             Reserved
	 * ECX[30]     SGX_LC                             Reserved
	 * ECX[31]     PKS                                Reserved
	 *
	 * EDX[0]      Reserved                           Reserved
	 * EDX[1]      SGX_KEYS                           Reserved
	 * EDX[2]      AVX512_4VNNIW                      Reserved
	 * EDX[3]      AVX512_4FMAPS                      Reserved
	 * EDX[4]      FAST_SHORT_REP_MOVSB               Reserved
	 * EDX[5]      UINTR                              Reserved
	 * EDX[7:6]    Reserved                           Reserved
	 * EDX[8]      AVX512_VP2INTERSECT                Reserved
	 * EDX[9]      MCU_OPT_CTRL                       Reserved
	 * EDX[10]     MD_CLEAR                           Reserved
	 * EDX[11]     RTM_ALWAYS_ABORT                   Reserved
	 * EDX[12]     Reserved                           Reserved
	 * EDX[13]     RTM_FORCE_ABORT                    Reserved
	 * EDX[14]     SERIALIZE                          Reserved
	 * EDX[15]     HYBRID                             Reserved
	 * EDX[16]     TSXLDTRK                           Reserved
	 * EDX[17]     Reserved                           Reserved
	 * EDX[18]     PCONFIG                            Reserved
	 * EDX[19]     ARCH_LBRS                          Reserved
	 * EDX[20]     CET_IBT                            Reserved
	 * EDX[21]     Reserved                           Reserved
	 * EDX[22]     AMX_BF16                           Reserved
	 * EDX[23]     AVX512_FP16                        Reserved
	 * EDX[24]     AMX_TILE                           Reserved
	 * EDX[25]     AMX_INT8                           Reserved
	 * EDX[26]     IBRS_IBPB                          Reserved
	 * EDX[27]     SPEC_CTRL_ST_PREDICTORS            Reserved
	 * EDX[28]     L1D_FLUSH_INTERFACE                Reserved
	 * EDX[29]     ARCH_CAPABILITIES                  Reserved
	 * EDX[30]     CORE_CAPABILITIES                  Reserved
	 * EDX[31]     SPEC_CTRL_SSBD                     Reserved
	 */
	features->leaf7_max_subleaf = leaf7->eax;

	features->has[X86_64_CPU_FEATURE_FSGSBASE] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_FSGSBASE) != 0;
	features->has[X86_64_CPU_FEATURE_TSC_ADJUST] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_TSC_ADJUST) != 0;
	features->has[X86_64_CPU_FEATURE_BMI1] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_BMI1) != 0;
	features->has[X86_64_CPU_FEATURE_AVX2] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_AVX2) != 0;
	features->has[X86_64_CPU_FEATURE_SMEP] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_SMEP) != 0;
	features->has[X86_64_CPU_FEATURE_BMI2] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_BMI2) != 0;
	features->has[X86_64_CPU_FEATURE_ERMS] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_ERMS) != 0;
	features->has[X86_64_CPU_FEATURE_INVPCID] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_INVPCID) != 0;
	features->has[X86_64_CPU_FEATURE_RDSEED] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_RDSEED) != 0;
	features->has[X86_64_CPU_FEATURE_ADX] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_ADX) != 0;
	features->has[X86_64_CPU_FEATURE_SMAP] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_SMAP) != 0;
	features->has[X86_64_CPU_FEATURE_CLFLUSHOPT] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_CLFLUSHOPT) != 0;
	features->has[X86_64_CPU_FEATURE_CLWB] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_CLWB) != 0;
	features->has[X86_64_CPU_FEATURE_SHA] =
		(leaf7->ebx & X86_64_CPUID_7_0_EBX_SHA) != 0;

	features->has[X86_64_CPU_FEATURE_UMIP] =
		(leaf7->ecx & X86_64_CPUID_7_0_ECX_UMIP) != 0;
	features->has[X86_64_CPU_FEATURE_LA57] =
		(leaf7->ecx & X86_64_CPUID_7_0_ECX_LA57) != 0;
	features->has[X86_64_CPU_FEATURE_RDPID] =
		(leaf7->ecx & X86_64_CPUID_7_0_ECX_RDPID) != 0;

	/* Intel defines CPUID.07H.00H:EDX[14] as SERIALIZE. */
	if (features->vendor_id == X86_64_CPU_VENDOR_INTEL) {
		features->has[X86_64_CPU_FEATURE_INTEL_SERIALIZE] =
			(leaf7->edx & X86_64_CPUID_7_0_EDX_INTEL_SERIALIZE) != 0;
	}
}

static void decode_xsave(struct x86_64_cpu_features *features,
			 const struct x86_64_cpuid_raw *raw) {
	features->xcr0_supported_mask =
		FIELD_PREP(CPUID_XSAVE_XCR0_HIGH, raw->leafd_0.edx) |
		FIELD_PREP(CPUID_XSAVE_XCR0_LOW, raw->leafd_0.eax);
	features->xsave_area_size_enabled = raw->leafd_0.ebx;
	features->xsave_area_size_supported = raw->leafd_0.ecx;
	features->xsave_leaf1 = raw->leafd_1;
}

static void decode_ext_leaf1(struct x86_64_cpu_features *features,
			     const struct x86_64_cpuid_leaf *leaf_ext1) {
	/*
	 * Bit        Intel field                         AMD field
	 *
	 * ECX[0]     LAHF_SAHF_64                        LahfSahf
	 * ECX[1]     Reserved                            CmpLegacy
	 * ECX[2]     Reserved                            SVM
	 * ECX[3]     Reserved                            ExtApicSpace
	 * ECX[4]     Reserved                            AltMovCr8
	 * ECX[5]     LZCNT                               ABM
	 * ECX[6]     Reserved                            SSE4A
	 * ECX[7]     Reserved                            MisAlignSse
	 * ECX[8]     PREFETCHW                           3DNowPrefetch
	 * ECX[9]     Reserved                            OSVW
	 * ECX[10]    Reserved                            IBS
	 * ECX[11]    Reserved                            XOP
	 * ECX[12]    Reserved                            SKINIT
	 * ECX[13]    Reserved                            WDT
	 * ECX[14]    Reserved                            Reserved
	 * ECX[15]    Reserved                            LWP
	 * ECX[16]    Reserved                            FMA4
	 * ECX[17]    Reserved                            TCE
	 * ECX[18]    Reserved                            Reserved
	 * ECX[19]    Reserved                            Reserved
	 * ECX[20]    Reserved                            Reserved
	 * ECX[21]    Reserved                            TBM
	 * ECX[22]    Reserved                            TopologyExtensions
	 * ECX[23]    Reserved                            PerfCtrExtCore
	 * ECX[24]    Reserved                            PerfCtrExtNB
	 * ECX[25]    Reserved                            Reserved
	 * ECX[26]    Reserved                            DataBkptExt
	 * ECX[27]    Reserved                            PerfTsc
	 * ECX[28]    Reserved                            PerfCtrExtLLC
	 * ECX[29]    Reserved                            MONITORX
	 * ECX[30]    Reserved                            AddrMaskExt
	 * ECX[31]    Reserved                            Reserved
	 *
	 * EDX[0]     Reserved                            FPU
	 * EDX[1]     Reserved                            VME
	 * EDX[2]     Reserved                            DE
	 * EDX[3]     Reserved                            PSE
	 * EDX[4]     Reserved                            TSC
	 * EDX[5]     Reserved                            MSR
	 * EDX[6]     Reserved                            PAE
	 * EDX[7]     Reserved                            MCE
	 * EDX[8]     Reserved                            CMPXCHG8B
	 * EDX[9]     Reserved                            APIC
	 * EDX[10]    Reserved                            Reserved
	 * EDX[11]    SYSCALL_SYSRET_64                   SysCallSysRet
	 * EDX[12]    Reserved                            MTRR
	 * EDX[13]    Reserved                            PGE
	 * EDX[14]    Reserved                            MCA
	 * EDX[15]    Reserved                            CMOV
	 * EDX[16]    Reserved                            PAT
	 * EDX[17]    Reserved                            PSE36
	 * EDX[18]    Reserved                            Reserved
	 * EDX[19]    Reserved                            Reserved
	 * EDX[20]    EXECUTE_DIS                         NX
	 * EDX[21]    Reserved                            Reserved
	 * EDX[22]    Reserved                            MmxExt
	 * EDX[23]    Reserved                            MMX
	 * EDX[24]    Reserved                            FXSR
	 * EDX[25]    Reserved                            FFXSR
	 * EDX[26]    PAGE_1GB                            Page1GB
	 * EDX[27]    RDTSCP                              RDTSCP
	 * EDX[28]    Reserved                            Reserved
	 * EDX[29]    INTEL64                             LM
	 * EDX[30]    Reserved                            3DNowExt
	 * EDX[31]    Reserved                            3DNow
	 */
	features->has[X86_64_CPU_FEATURE_SYSCALL] =
		(leaf_ext1->edx & X86_64_CPUID_EXT_1_EDX_SYSCALL) != 0;
	features->has[X86_64_CPU_FEATURE_NX] =
		(leaf_ext1->edx & X86_64_CPUID_EXT_1_EDX_NX) != 0;
	features->has[X86_64_CPU_FEATURE_PAGE1GB] =
		(leaf_ext1->edx & X86_64_CPUID_EXT_1_EDX_PAGE1GB) != 0;
	features->has[X86_64_CPU_FEATURE_RDTSCP] =
		(leaf_ext1->edx & X86_64_CPUID_EXT_1_EDX_RDTSCP) != 0;
	features->has[X86_64_CPU_FEATURE_LONG_MODE] =
		(leaf_ext1->edx & X86_64_CPUID_EXT_1_EDX_LM) != 0;

	features->has[X86_64_CPU_FEATURE_LAHF_LM] =
		(leaf_ext1->ecx & X86_64_CPUID_EXT_1_ECX_LAHF_LM) != 0;
	features->has[X86_64_CPU_FEATURE_LZCNT] =
		(leaf_ext1->ecx & X86_64_CPUID_EXT_1_ECX_LZCNT) != 0;
	features->has[X86_64_CPU_FEATURE_PREFETCHW] =
		(leaf_ext1->ecx & X86_64_CPUID_EXT_1_ECX_PREFETCHW) != 0;
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

	if (features->max_basic_leaf >= X86_64_CPUID_LEAF_FEATURES) {
		decode_leaf1(features, &raw->leaf1);
	}

	if (features->max_basic_leaf >= X86_64_CPUID_LEAF_STRUCTURED) {
		decode_leaf7_0(features, &raw->leaf7_0);
	}

	if (features->has[X86_64_CPU_FEATURE_XSAVE] &&
	    features->max_basic_leaf >= X86_64_CPUID_LEAF_XSAVE) {
		decode_xsave(features, raw);
	}

	if (features->max_extended_leaf >= X86_64_CPUID_LEAF_EXT_FEATURES) {
		decode_ext_leaf1(features, &raw->leaf_ext1);
	}

	if (features->max_extended_leaf >= X86_64_CPUID_LEAF_BRAND_LAST) {
		set_brand_string(features->brand, raw->brand);
	}
}

static bool cpu_features_have_required(void)
{
	return boot_cpu_features.has[X86_64_CPU_FEATURE_MSR] &&
	       boot_cpu_features.has[X86_64_CPU_FEATURE_PAT] &&
	       boot_cpu_features.has[X86_64_CPU_FEATURE_LONG_MODE];
}

static void collect_cpuid_raw(struct x86_64_cpuid_raw *raw)
{
	memset(raw, 0, sizeof(*raw));

	raw->leaf0 = cpuid_count(0, 0);
	raw->leaf_ext0 = cpuid_count(X86_64_CPUID_LEAF_EXT_MAX, 0);

	if (raw->leaf0.eax >= X86_64_CPUID_LEAF_FEATURES) {
		raw->leaf1 = cpuid_count(X86_64_CPUID_LEAF_FEATURES, 0);
	}
	if (raw->leaf0.eax >= X86_64_CPUID_LEAF_STRUCTURED) {
		raw->leaf7_0 = cpuid_count(X86_64_CPUID_LEAF_STRUCTURED, 0);
	}
	/* CPUID.0DH is valid when CPUID.01H:ECX.XSAVE is present. */
	if (raw->leaf0.eax >= X86_64_CPUID_LEAF_XSAVE &&
	    (raw->leaf1.ecx & X86_64_CPUID_1_ECX_XSAVE) != 0) {
		raw->leafd_0 = cpuid_count(X86_64_CPUID_LEAF_XSAVE, 0);
		raw->leafd_1 = cpuid_count(X86_64_CPUID_LEAF_XSAVE, 1);
	}
	if (raw->leaf_ext0.eax >= X86_64_CPUID_LEAF_EXT_FEATURES) {
		raw->leaf_ext1 = cpuid_count(X86_64_CPUID_LEAF_EXT_FEATURES, 0);
	}
	if (raw->leaf_ext0.eax >= X86_64_CPUID_LEAF_BRAND_LAST) {
		for (uint32_t i = 0; i < 3; i++) {
			raw->brand[i] = cpuid_count(X86_64_CPUID_LEAF_BRAND_FIRST + i, 0);
		}
	}
}

void x86_64_cpu_features_init(void)
{
	struct x86_64_cpuid_raw raw;

	collect_cpuid_raw(&raw);
	x86_64_cpu_features_decode(&boot_cpu_features, &raw);

	BUG_ON_MSG(!cpu_features_have_required(),
		   "required CPU features missing: msr=%d pat=%d long_mode=%d",
		   boot_cpu_features.has[X86_64_CPU_FEATURE_MSR],
		   boot_cpu_features.has[X86_64_CPU_FEATURE_PAT],
		   boot_cpu_features.has[X86_64_CPU_FEATURE_LONG_MODE]);
}

const struct x86_64_cpu_features *x86_64_cpu_features_get(void)
{
	return &boot_cpu_features;
}

bool x86_64_cpu_has_feature(enum x86_64_cpu_feature feature)
{
	if (feature < 0 || feature >= X86_64_CPU_FEATURE_NR) {
		return false;
	}

	return boot_cpu_features.has[feature];
}
