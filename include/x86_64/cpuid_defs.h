#ifndef X86_64_CPUID_DEFS_H
#define X86_64_CPUID_DEFS_H

#include <plane/bits.h>

/*
 * CPUID leaf, subleaf, and register field definitions used by Plane today.
 *
 * Intel SDM Vol.2 CPUID and AMD APM Vol.3 CPUID define the architectural
 * leaves and bit assignments below. Keep this header limited to fields that
 * current BSP CPU decoding consumes; cache descriptors, topology leaves,
 * SGX, AVX-512 details, and xstate enable policy are later milestones.
 */
#define X86_64_CPUID_LEAF_BASIC_MAX     0x00000000
#define X86_64_CPUID_LEAF_FEATURES      0x00000001
#define X86_64_CPUID_LEAF_STRUCTURED    0x00000007
#define X86_64_CPUID_LEAF_XSAVE         0x0000000d
#define X86_64_CPUID_LEAF_EXT_MAX       0x80000000
#define X86_64_CPUID_LEAF_EXT_FEATURES  0x80000001
#define X86_64_CPUID_LEAF_BRAND_FIRST   0x80000002
#define X86_64_CPUID_LEAF_BRAND_LAST    0x80000004

#define X86_64_CPUID_1_EAX_STEPPING       GENMASK(3, 0)
#define X86_64_CPUID_1_EAX_BASE_MODEL     GENMASK(7, 4)
#define X86_64_CPUID_1_EAX_BASE_FAMILY    GENMASK(11, 8)
#define X86_64_CPUID_1_EAX_PROCESSOR_TYPE GENMASK(13, 12)
#define X86_64_CPUID_1_EAX_EXT_MODEL      GENMASK(19, 16)
#define X86_64_CPUID_1_EAX_EXT_FAMILY     GENMASK(27, 20)

#define X86_64_CPUID_DISPLAY_EXT_MODEL GENMASK(7, 4)

#define X86_64_CPUID_1_EBX_CLFLUSH_LINE_SIZE  GENMASK(15, 8)
#define X86_64_CPUID_1_EBX_LOGICAL_PROCESSORS GENMASK(23, 16)
#define X86_64_CPUID_1_EBX_INITIAL_APIC_ID    GENMASK(31, 24)

#define X86_64_CPUID_1_EDX_FPU          BIT(0)
#define X86_64_CPUID_1_EDX_TSC          BIT(4)
#define X86_64_CPUID_1_EDX_MSR          BIT(5)
#define X86_64_CPUID_1_EDX_PAE          BIT(6)
#define X86_64_CPUID_1_EDX_CX8          BIT(8)
#define X86_64_CPUID_1_EDX_APIC         BIT(9)
#define X86_64_CPUID_1_EDX_MTRR         BIT(12)
#define X86_64_CPUID_1_EDX_PGE          BIT(13)
#define X86_64_CPUID_1_EDX_CMOV         BIT(15)
#define X86_64_CPUID_1_EDX_PAT          BIT(16)
#define X86_64_CPUID_1_EDX_CLFLUSH      BIT(19)
#define X86_64_CPUID_1_EDX_FXSR         BIT(24)
#define X86_64_CPUID_1_EDX_SSE          BIT(25)
#define X86_64_CPUID_1_EDX_SSE2         BIT(26)
#define X86_64_CPUID_1_EDX_HTT          BIT(28)

#define X86_64_CPUID_1_ECX_SSE3         BIT(0)
#define X86_64_CPUID_1_ECX_PCLMULQDQ    BIT(1)
#define X86_64_CPUID_1_ECX_SSSE3        BIT(9)
#define X86_64_CPUID_1_ECX_FMA          BIT(12)
#define X86_64_CPUID_1_ECX_CX16         BIT(13)
#define X86_64_CPUID_1_ECX_INTEL_PCID   BIT(17)
#define X86_64_CPUID_1_ECX_SSE4_1       BIT(19)
#define X86_64_CPUID_1_ECX_SSE4_2       BIT(20)
#define X86_64_CPUID_1_ECX_X2APIC       BIT(21)
#define X86_64_CPUID_1_ECX_MOVBE        BIT(22)
#define X86_64_CPUID_1_ECX_POPCNT       BIT(23)
#define X86_64_CPUID_1_ECX_INTEL_TSC_DEADLINE BIT(24)
#define X86_64_CPUID_1_ECX_AES          BIT(25)
#define X86_64_CPUID_1_ECX_XSAVE        BIT(26)
#define X86_64_CPUID_1_ECX_OSXSAVE      BIT(27)
#define X86_64_CPUID_1_ECX_AVX          BIT(28)
#define X86_64_CPUID_1_ECX_F16C         BIT(29)
#define X86_64_CPUID_1_ECX_RDRAND       BIT(30)
#define X86_64_CPUID_1_ECX_HYPERVISOR   BIT(31)

#define X86_64_CPUID_7_0_EBX_FSGSBASE   BIT(0)
#define X86_64_CPUID_7_0_EBX_TSC_ADJUST BIT(1)
#define X86_64_CPUID_7_0_EBX_BMI1       BIT(3)
#define X86_64_CPUID_7_0_EBX_AVX2       BIT(5)
#define X86_64_CPUID_7_0_EBX_SMEP       BIT(7)
#define X86_64_CPUID_7_0_EBX_BMI2       BIT(8)
#define X86_64_CPUID_7_0_EBX_ERMS       BIT(9)
#define X86_64_CPUID_7_0_EBX_INVPCID    BIT(10)
#define X86_64_CPUID_7_0_EBX_RDSEED     BIT(18)
#define X86_64_CPUID_7_0_EBX_ADX        BIT(19)
#define X86_64_CPUID_7_0_EBX_SMAP       BIT(20)
#define X86_64_CPUID_7_0_EBX_CLFLUSHOPT BIT(23)
#define X86_64_CPUID_7_0_EBX_CLWB       BIT(24)
#define X86_64_CPUID_7_0_EBX_SHA        BIT(29)

#define X86_64_CPUID_7_0_ECX_UMIP       BIT(2)
#define X86_64_CPUID_7_0_ECX_LA57       BIT(16)
#define X86_64_CPUID_7_0_ECX_RDPID      BIT(22)

#define X86_64_CPUID_7_0_EDX_INTEL_SERIALIZE BIT(14)

#define X86_64_CPUID_EXT_1_EDX_SYSCALL  BIT(11)
#define X86_64_CPUID_EXT_1_EDX_NX       BIT(20)
#define X86_64_CPUID_EXT_1_EDX_PAGE1GB  BIT(26)
#define X86_64_CPUID_EXT_1_EDX_RDTSCP   BIT(27)
#define X86_64_CPUID_EXT_1_EDX_LM       BIT(29)

#define X86_64_CPUID_EXT_1_ECX_LAHF_LM  BIT(0)
#define X86_64_CPUID_EXT_1_ECX_LZCNT    BIT(5)
#define X86_64_CPUID_EXT_1_ECX_PREFETCHW BIT(8)

#ifndef __ASSEMBLER__

#define X86_64_CPUID_D_0_XCR0_LOW  GENMASK_ULL(31, 0)
#define X86_64_CPUID_D_0_XCR0_HIGH GENMASK_ULL(63, 32)

#endif /* !__ASSEMBLER__ */

#endif /* X86_64_CPUID_DEFS_H */
