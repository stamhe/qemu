/*
 *  i386 CPUID helper functions
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "sysemu/kvm.h"
#include "sysemu/cpus.h"
#include "topology.h"

#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qapi/qmp/qerror.h"

#include "qapi-types.h"
#include "qapi-visit.h"
#include "qapi/visitor.h"
#include "sysemu/arch_init.h"

#include "hw/hw.h"
#if defined(CONFIG_KVM)
#include <linux/kvm_para.h>
#endif

#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "hw/cpu/icc_bus.h"
#ifndef CONFIG_USER_ONLY
#include "hw/xen/xen.h"
#include "hw/i386/apic_internal.h"
#endif

static void x86_cpu_vendor_words2str(char *dst, uint32_t vendor1,
                                     uint32_t vendor2, uint32_t vendor3)
{
    int i;
    for (i = 0; i < 4; i++) {
        dst[i] = vendor1 >> (8 * i);
        dst[i + 4] = vendor2 >> (8 * i);
        dst[i + 8] = vendor3 >> (8 * i);
    }
    dst[CPUID_VENDOR_SZ] = '\0';
}

typedef struct FeatureWordInfo {
    uint32_t cpuid_eax;   /* Input EAX for CPUID */
    bool cpuid_needs_ecx; /* CPUID instruction uses ECX as input */
    uint32_t cpuid_ecx;   /* Input ECX value for CPUID */
    int cpuid_reg;        /* output register (R_* constant) */
} FeatureWordInfo;

static FeatureWordInfo feature_word_info[FEATURE_WORDS] = {
    [FEAT_1_EDX] = {
        .cpuid_eax = 1, .cpuid_reg = R_EDX,
    },
    [FEAT_1_ECX] = {
        .cpuid_eax = 1, .cpuid_reg = R_ECX,
    },
    [FEAT_8000_0001_EDX] = {
        .cpuid_eax = 0x80000001, .cpuid_reg = R_EDX,
    },
    [FEAT_8000_0001_ECX] = {
        .cpuid_eax = 0x80000001, .cpuid_reg = R_ECX,
    },
    [FEAT_C000_0001_EDX] = {
        .cpuid_eax = 0xC0000001, .cpuid_reg = R_EDX,
    },
    [FEAT_KVM] = {
        .cpuid_eax = KVM_CPUID_FEATURES, .cpuid_reg = R_EAX,
    },
    [FEAT_SVM] = {
        .cpuid_eax = 0x8000000A, .cpuid_reg = R_EDX,
    },
    [FEAT_7_0_EBX] = {
        .cpuid_eax = 7,
        .cpuid_needs_ecx = true, .cpuid_ecx = 0,
        .cpuid_reg = R_EBX,
    },
};

typedef struct X86RegisterInfo32 {
    /* Name of register */
    const char *name;
    /* QAPI enum value register */
    X86CPURegister32 qapi_enum;
} X86RegisterInfo32;

#define REGISTER(reg) \
    [R_##reg] = { .name = #reg, .qapi_enum = X86_C_P_U_REGISTER32_##reg }
X86RegisterInfo32 x86_reg_info_32[CPU_NB_REGS32] = {
    REGISTER(EAX),
    REGISTER(ECX),
    REGISTER(EDX),
    REGISTER(EBX),
    REGISTER(ESP),
    REGISTER(EBP),
    REGISTER(ESI),
    REGISTER(EDI),
};
#undef REGISTER


const char *get_register_name_32(unsigned int reg)
{
    if (reg >= CPU_NB_REGS32) {
        return NULL;
    }
    return x86_reg_info_32[reg].name;
}

/* collects per-function cpuid data
 */
typedef struct model_features_t {
    uint32_t *guest_feat;
    uint32_t *host_feat;
    FeatureWord feat_word;
} model_features_t;

static uint32_t kvm_default_features = (1 << KVM_FEATURE_CLOCKSOURCE) |
        (1 << KVM_FEATURE_NOP_IO_DELAY) |
        (1 << KVM_FEATURE_CLOCKSOURCE2) |
        (1 << KVM_FEATURE_ASYNC_PF) |
        (1 << KVM_FEATURE_STEAL_TIME) |
        (1 << KVM_FEATURE_PV_EOI) |
        (1 << KVM_FEATURE_CLOCKSOURCE_STABLE_BIT);

void disable_kvm_pv_eoi(void)
{
    kvm_default_features &= ~(1UL << KVM_FEATURE_PV_EOI);
}

void host_cpuid(uint32_t function, uint32_t count,
                uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
#if defined(CONFIG_KVM)
    uint32_t vec[4];

#ifdef __x86_64__
    asm volatile("cpuid"
                 : "=a"(vec[0]), "=b"(vec[1]),
                   "=c"(vec[2]), "=d"(vec[3])
                 : "0"(function), "c"(count) : "cc");
#else
    asm volatile("pusha \n\t"
                 "cpuid \n\t"
                 "mov %%eax, 0(%2) \n\t"
                 "mov %%ebx, 4(%2) \n\t"
                 "mov %%ecx, 8(%2) \n\t"
                 "mov %%edx, 12(%2) \n\t"
                 "popa"
                 : : "a"(function), "c"(count), "S"(vec)
                 : "memory", "cc");
#endif

    if (eax)
        *eax = vec[0];
    if (ebx)
        *ebx = vec[1];
    if (ecx)
        *ecx = vec[2];
    if (edx)
        *edx = vec[3];
#endif
}

typedef struct x86_def_t {
    const char *name;
    uint32_t level;
    uint32_t xlevel;
    uint32_t xlevel2;
    /* vendor is zero-terminated, 12 character ASCII string */
    char vendor[CPUID_VENDOR_SZ + 1];
    int family;
    int model;
    int stepping;
    FeatureWordArray features;
    char model_id[48];
} x86_def_t;

#define I486_FEATURES (CPUID_FP87 | CPUID_VME | CPUID_PSE)
#define PENTIUM_FEATURES (I486_FEATURES | CPUID_DE | CPUID_TSC | \
          CPUID_MSR | CPUID_MCE | CPUID_CX8 | CPUID_MMX | CPUID_APIC)
#define PENTIUM2_FEATURES (PENTIUM_FEATURES | CPUID_PAE | CPUID_SEP | \
          CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT | \
          CPUID_PSE36 | CPUID_FXSR)
#define PENTIUM3_FEATURES (PENTIUM2_FEATURES | CPUID_SSE)
#define PPRO_FEATURES (CPUID_FP87 | CPUID_DE | CPUID_PSE | CPUID_TSC | \
          CPUID_MSR | CPUID_MCE | CPUID_CX8 | CPUID_PGE | CPUID_CMOV | \
          CPUID_PAT | CPUID_FXSR | CPUID_MMX | CPUID_SSE | CPUID_SSE2 | \
          CPUID_PAE | CPUID_SEP | CPUID_APIC)

#define TCG_FEATURES (CPUID_FP87 | CPUID_PSE | CPUID_TSC | CPUID_MSR | \
          CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC | CPUID_SEP | \
          CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT | \
          CPUID_PSE36 | CPUID_CLFLUSH | CPUID_ACPI | CPUID_MMX | \
          CPUID_FXSR | CPUID_SSE | CPUID_SSE2 | CPUID_SS)
          /* partly implemented:
          CPUID_MTRR, CPUID_MCA, CPUID_CLFLUSH (needed for Win64)
          CPUID_PSE36 (needed for Solaris) */
          /* missing:
          CPUID_VME, CPUID_DTS, CPUID_SS, CPUID_HT, CPUID_TM, CPUID_PBE */
#define TCG_EXT_FEATURES (CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | \
          CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 | CPUID_EXT_CX16 | \
          CPUID_EXT_SSE41 | CPUID_EXT_SSE42 | CPUID_EXT_POPCNT | \
          CPUID_EXT_MOVBE | CPUID_EXT_AES | CPUID_EXT_HYPERVISOR)
          /* missing:
          CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_VMX, CPUID_EXT_SMX,
          CPUID_EXT_EST, CPUID_EXT_TM2, CPUID_EXT_CID, CPUID_EXT_FMA,
          CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_PCID, CPUID_EXT_DCA,
          CPUID_EXT_X2APIC, CPUID_EXT_TSC_DEADLINE_TIMER, CPUID_EXT_XSAVE,
          CPUID_EXT_OSXSAVE, CPUID_EXT_AVX, CPUID_EXT_F16C,
          CPUID_EXT_RDRAND */
#define TCG_EXT2_FEATURES ((TCG_FEATURES & CPUID_EXT2_AMD_ALIASES) | \
          CPUID_EXT2_NX | CPUID_EXT2_MMXEXT | CPUID_EXT2_RDTSCP | \
          CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT)
          /* missing:
          CPUID_EXT2_PDPE1GB */
#define TCG_EXT3_FEATURES (CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM | \
          CPUID_EXT3_CR8LEG | CPUID_EXT3_ABM | CPUID_EXT3_SSE4A)
#define TCG_SVM_FEATURES 0
#define TCG_7_0_EBX_FEATURES (CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_SMAP \
          CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ADX)
          /* missing:
          CPUID_7_0_EBX_FSGSBASE, CPUID_7_0_EBX_HLE, CPUID_7_0_EBX_AVX2,
          CPUID_7_0_EBX_ERMS, CPUID_7_0_EBX_INVPCID, CPUID_7_0_EBX_RTM,
          CPUID_7_0_EBX_RDSEED */

/* built-in CPU model definitions
 */
static x86_def_t builtin_x86_defs[] = {
    {
        .name = "qemu64",
        .level = 4,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16 | CPUID_EXT_POPCNT,
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        .xlevel = 0x8000000A,
    },
    {
        .name = "phenom",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 16,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_HT,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_CX16 |
            CPUID_EXT_POPCNT,
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX |
            CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_MMXEXT |
            CPUID_EXT2_FFXSR | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP,
        /* Missing: CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS */
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_LBRV,
        .xlevel = 0x8000001A,
        .model_id = "AMD Phenom(tm) 9550 Quad-Core Processor"
    },
    {
        .name = "core2duo",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 15,
        .stepping = 11,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_DTS | CPUID_ACPI | CPUID_SS |
            CPUID_HT | CPUID_TM | CPUID_PBE,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_DTES64 | CPUID_EXT_DSCPL | CPUID_EXT_VMX | CPUID_EXT_EST |
            CPUID_EXT_TM2 | CPUID_EXT_CX16 | CPUID_EXT_XTPR | CPUID_EXT_PDCM,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "Intel(R) Core(TM)2 Duo CPU     T7700  @ 2.40GHz",
    },
    {
        .name = "kvm64",
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        /* Missing: CPUID_VME, CPUID_HT */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        /* Missing: CPUID_EXT_POPCNT, CPUID_EXT_MONITOR */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        /* Missing: CPUID_EXT2_PDPE1GB, CPUID_EXT2_RDTSCP */
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        /* Missing: CPUID_EXT3_LAHF_LM, CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG, CPUID_EXT3_ABM, CPUID_EXT3_SSE4A,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS, CPUID_EXT3_SVM */
        .features[FEAT_8000_0001_ECX] =
            0,
        .xlevel = 0x80000008,
        .model_id = "Common KVM processor"
    },
    {
        .name = "qemu32",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 3,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_POPCNT,
        .xlevel = 0x80000004,
    },
    {
        .name = "kvm32",
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES,
        .features[FEAT_8000_0001_ECX] =
            0,
        .xlevel = 0x80000008,
        .model_id = "Common 32-bit KVM processor"
    },
    {
        .name = "coreduo",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 14,
        .stepping = 8,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_DTS | CPUID_ACPI |
            CPUID_SS | CPUID_HT | CPUID_TM | CPUID_PBE,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_VMX |
            CPUID_EXT_EST | CPUID_EXT_TM2 | CPUID_EXT_XTPR | CPUID_EXT_PDCM,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_NX,
        .xlevel = 0x80000008,
        .model_id = "Genuine Intel(R) CPU           T2600  @ 2.16GHz",
    },
    {
        .name = "486",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 4,
        .model = 8,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            I486_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 5,
        .model = 4,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PENTIUM_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium2",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 5,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            PENTIUM2_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium3",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 7,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PENTIUM3_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "athlon",
        .level = 2,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_PSE36 | CPUID_VME | CPUID_MTRR |
            CPUID_MCA,
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_MMXEXT | CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT,
        .xlevel = 0x80000008,
    },
    {
        .name = "n270",
        /* original is on level 10 */
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 28,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_VME | CPUID_DTS |
            CPUID_ACPI | CPUID_SS | CPUID_HT | CPUID_TM | CPUID_PBE,
            /* Some CPUs got no CPUID_SEP */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_DSCPL | CPUID_EXT_EST | CPUID_EXT_TM2 | CPUID_EXT_XTPR |
            CPUID_EXT_MOVBE,
        .features[FEAT_8000_0001_EDX] =
            (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel(R) Atom(TM) CPU N270   @ 1.60GHz",
    },
    {
        .name = "Conroe",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 15,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Celeron_4x0 (Conroe/Merom Class Core 2)",
    },
    {
        .name = "Penryn",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 23,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
             CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Core 2 Duo P9xxx (Penryn Class Core 2)",
    },
    {
        .name = "Nehalem",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 26,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
             CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Core i7 9xx (Nehalem Class Core i7)",
    },
    {
        .name = "Westmere",
        .level = 11,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 44,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
             CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
             CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Westmere E56xx/L56xx/X56xx (Nehalem-C)",
    },
    {
        .name = "SandyBridge",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 42,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
             CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_POPCNT |
             CPUID_EXT_X2APIC | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
             CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
             CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
             CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Xeon E312xx (Sandy Bridge)",
    },
    {
        .name = "Haswell",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 60,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
             CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
             CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
             CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
             CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
             CPUID_EXT_PCID,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
             CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Core Processor (Haswell)",
    },
    {
        .name = "Opteron_G1",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_FXSR | CPUID_EXT2_MMX |
             CPUID_EXT2_NX | CPUID_EXT2_PSE36 | CPUID_EXT2_PAT |
             CPUID_EXT2_CMOV | CPUID_EXT2_MCA | CPUID_EXT2_PGE |
             CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL | CPUID_EXT2_APIC |
             CPUID_EXT2_CX8 | CPUID_EXT2_MCE | CPUID_EXT2_PAE | CPUID_EXT2_MSR |
             CPUID_EXT2_TSC | CPUID_EXT2_PSE | CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 240 (Gen 1 Class Opteron)",
    },
    {
        .name = "Opteron_G2",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_CX16 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_FXSR |
             CPUID_EXT2_MMX | CPUID_EXT2_NX | CPUID_EXT2_PSE36 |
             CPUID_EXT2_PAT | CPUID_EXT2_CMOV | CPUID_EXT2_MCA |
             CPUID_EXT2_PGE | CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL |
             CPUID_EXT2_APIC | CPUID_EXT2_CX8 | CPUID_EXT2_MCE |
             CPUID_EXT2_PAE | CPUID_EXT2_MSR | CPUID_EXT2_TSC | CPUID_EXT2_PSE |
             CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 22xx (Gen 2 Class Opteron)",
    },
    {
        .name = "Opteron_G3",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_POPCNT | CPUID_EXT_CX16 | CPUID_EXT_MONITOR |
             CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_FXSR |
             CPUID_EXT2_MMX | CPUID_EXT2_NX | CPUID_EXT2_PSE36 |
             CPUID_EXT2_PAT | CPUID_EXT2_CMOV | CPUID_EXT2_MCA |
             CPUID_EXT2_PGE | CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL |
             CPUID_EXT2_APIC | CPUID_EXT2_CX8 | CPUID_EXT2_MCE |
             CPUID_EXT2_PAE | CPUID_EXT2_MSR | CPUID_EXT2_TSC | CPUID_EXT2_PSE |
             CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A |
             CPUID_EXT3_ABM | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 23xx (Gen 3 Class Opteron)",
    },
    {
        .name = "Opteron_G4",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 21,
        .model = 1,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
             CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
             CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
             CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP |
             CPUID_EXT2_PDPE1GB | CPUID_EXT2_FXSR | CPUID_EXT2_MMX |
             CPUID_EXT2_NX | CPUID_EXT2_PSE36 | CPUID_EXT2_PAT |
             CPUID_EXT2_CMOV | CPUID_EXT2_MCA | CPUID_EXT2_PGE |
             CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL | CPUID_EXT2_APIC |
             CPUID_EXT2_CX8 | CPUID_EXT2_MCE | CPUID_EXT2_PAE | CPUID_EXT2_MSR |
             CPUID_EXT2_TSC | CPUID_EXT2_PSE | CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
             CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
             CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
             CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 62xx class CPU",
    },
    {
        .name = "Opteron_G5",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 21,
        .model = 2,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_F16C | CPUID_EXT_AVX | CPUID_EXT_XSAVE |
             CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
             CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_FMA |
             CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP |
             CPUID_EXT2_PDPE1GB | CPUID_EXT2_FXSR | CPUID_EXT2_MMX |
             CPUID_EXT2_NX | CPUID_EXT2_PSE36 | CPUID_EXT2_PAT |
             CPUID_EXT2_CMOV | CPUID_EXT2_MCA | CPUID_EXT2_PGE |
             CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL | CPUID_EXT2_APIC |
             CPUID_EXT2_CX8 | CPUID_EXT2_MCE | CPUID_EXT2_PAE | CPUID_EXT2_MSR |
             CPUID_EXT2_TSC | CPUID_EXT2_PSE | CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_TBM | CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
             CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
             CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
             CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 63xx class CPU",
    },
};

/**
 * x86_cpu_compat_set_features:
 * @cpu_model: CPU model name to be changed. If NULL, all CPU models are changed
 * @w: Identifies the feature word to be changed.
 * @feat_add: Feature bits to be added to feature word
 * @feat_remove: Feature bits to be removed from feature word
 *
 * Change CPU model feature bits for compatibility.
 *
 * This function may be used by machine-type compatibility functions
 * to enable or disable feature bits on specific CPU models.
 */
void x86_cpu_compat_set_features(const char *cpu_model, FeatureWord w,
                                 uint32_t feat_add, uint32_t feat_remove)
{
    x86_def_t *def;
    int i;
    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        def = &builtin_x86_defs[i];
        if (!cpu_model || !strcmp(cpu_model, def->name)) {
            def->features[w] |= feat_add;
            def->features[w] &= ~feat_remove;
        }
    }
}

#ifdef CONFIG_KVM
static int cpu_x86_fill_model_id(char *str)
{
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    int i;

    for (i = 0; i < 3; i++) {
        host_cpuid(0x80000002 + i, 0, &eax, &ebx, &ecx, &edx);
        memcpy(str + i * 16 +  0, &eax, 4);
        memcpy(str + i * 16 +  4, &ebx, 4);
        memcpy(str + i * 16 +  8, &ecx, 4);
        memcpy(str + i * 16 + 12, &edx, 4);
    }
    return 0;
}
#endif

/* Fill a x86_def_t struct with information about the host CPU, and
 * the CPU features supported by the host hardware + host kernel
 *
 * This function may be called only if KVM is enabled.
 */
static void kvm_cpu_fill_host(x86_def_t *x86_cpu_def)
{
#ifdef CONFIG_KVM
    KVMState *s = kvm_state;
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;

    assert(kvm_enabled());

    x86_cpu_def->name = "host";
    host_cpuid(0x0, 0, &eax, &ebx, &ecx, &edx);
    x86_cpu_vendor_words2str(x86_cpu_def->vendor, ebx, edx, ecx);

    host_cpuid(0x1, 0, &eax, &ebx, &ecx, &edx);
    x86_cpu_def->family = ((eax >> 8) & 0x0F) + ((eax >> 20) & 0xFF);
    x86_cpu_def->model = ((eax >> 4) & 0x0F) | ((eax & 0xF0000) >> 12);
    x86_cpu_def->stepping = eax & 0x0F;

    x86_cpu_def->level = kvm_arch_get_supported_cpuid(s, 0x0, 0, R_EAX);
    x86_cpu_def->features[FEAT_1_EDX] =
        kvm_arch_get_supported_cpuid(s, 0x1, 0, R_EDX);
    x86_cpu_def->features[FEAT_1_ECX] =
        kvm_arch_get_supported_cpuid(s, 0x1, 0, R_ECX);

    if (x86_cpu_def->level >= 7) {
        x86_cpu_def->features[FEAT_7_0_EBX] =
                    kvm_arch_get_supported_cpuid(s, 0x7, 0, R_EBX);
    } else {
        x86_cpu_def->features[FEAT_7_0_EBX] = 0;
    }

    x86_cpu_def->xlevel = kvm_arch_get_supported_cpuid(s, 0x80000000, 0, R_EAX);
    x86_cpu_def->features[FEAT_8000_0001_EDX] =
                kvm_arch_get_supported_cpuid(s, 0x80000001, 0, R_EDX);
    x86_cpu_def->features[FEAT_8000_0001_ECX] =
                kvm_arch_get_supported_cpuid(s, 0x80000001, 0, R_ECX);

    cpu_x86_fill_model_id(x86_cpu_def->model_id);

    /* Call Centaur's CPUID instruction. */
    if (!strcmp(x86_cpu_def->vendor, CPUID_VENDOR_VIA)) {
        host_cpuid(0xC0000000, 0, &eax, &ebx, &ecx, &edx);
        eax = kvm_arch_get_supported_cpuid(s, 0xC0000000, 0, R_EAX);
        if (eax >= 0xC0000001) {
            /* Support VIA max extended level */
            x86_cpu_def->xlevel2 = eax;
            host_cpuid(0xC0000001, 0, &eax, &ebx, &ecx, &edx);
            x86_cpu_def->features[FEAT_C000_0001_EDX] =
                    kvm_arch_get_supported_cpuid(s, 0xC0000001, 0, R_EDX);
        }
    }

    /* Other KVM-specific feature fields: */
    x86_cpu_def->features[FEAT_SVM] =
        kvm_arch_get_supported_cpuid(s, 0x8000000A, 0, R_EDX);
    x86_cpu_def->features[FEAT_KVM] =
        kvm_arch_get_supported_cpuid(s, KVM_CPUID_FEATURES, 0, R_EAX);

#endif /* CONFIG_KVM */
}

/* Check if all requested cpu flags are making their way to the guest
 *
 * Returns 0 if all flags are supported by the host, non-zero otherwise.
 *
 * This function may be called only if KVM is enabled.
 */
static int kvm_check_features_against_host(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    x86_def_t host_def;
    uint32_t mask;
    int rv, i;
    struct model_features_t ft[] = {
        {&env->features[FEAT_1_EDX],
            &host_def.features[FEAT_1_EDX],
            FEAT_1_EDX },
        {&env->features[FEAT_1_ECX],
            &host_def.features[FEAT_1_ECX],
            FEAT_1_ECX },
        {&env->features[FEAT_8000_0001_EDX],
            &host_def.features[FEAT_8000_0001_EDX],
            FEAT_8000_0001_EDX },
        {&env->features[FEAT_8000_0001_ECX],
            &host_def.features[FEAT_8000_0001_ECX],
            FEAT_8000_0001_ECX },
        {&env->features[FEAT_C000_0001_EDX],
            &host_def.features[FEAT_C000_0001_EDX],
            FEAT_C000_0001_EDX },
        {&env->features[FEAT_7_0_EBX],
            &host_def.features[FEAT_7_0_EBX],
            FEAT_7_0_EBX },
        {&env->features[FEAT_SVM],
            &host_def.features[FEAT_SVM],
            FEAT_SVM },
        {&env->features[FEAT_KVM],
            &host_def.features[FEAT_KVM],
            FEAT_KVM },
    };
    const DeviceClass *dc = DEVICE_CLASS(object_get_class(OBJECT(cpu)));

    assert(kvm_enabled());

    kvm_cpu_fill_host(&host_def);
    for (rv = 0, i = 0; i < ARRAY_SIZE(ft); ++i) {
        FeatureWord w = ft[i].feat_word;
        FeatureWordInfo *wi = &feature_word_info[w];
        int offset = (char *)&((X86CPU *)0)->env.features[w] - (char *)0;
        for (mask = 1; mask; mask <<= 1) {
            if (*ft[i].guest_feat & mask &&
                !(*ft[i].host_feat & mask)) {
                int bitnr = ffsl(mask) - 1;
                const Property *prop = qdev_prop_find_bit(dc, offset, bitnr);
                const char *name = prop ? prop->name : NULL;
                const char *reg = get_register_name_32(wi->cpuid_reg);

                assert(reg);
                fprintf(stderr, "warning: host doesn't support requested"
                        "feature: CPUID.%02XH:%s%s%s [bit %d]\n",
                         wi->cpuid_eax,
                         reg, name ? "." : "",
                         name ? name : "",
                         bitnr);
                rv = 1;
            }
        }
    }
    return rv;
}

static void x86_cpuid_version_get_family(Object *obj, Visitor *v, void *opaque,
                                         const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = (env->cpuid_version >> 8) & 0xf;
    if (value == 0xf) {
        value += (env->cpuid_version >> 20) & 0xff;
    }
    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_version_set_family(Object *obj, Visitor *v, void *opaque,
                                         const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff + 0xf;
    int64_t value;

    visit_type_int(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    if (value < min || value > max) {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                  name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xff00f00;
    if (value > 0x0f) {
        env->cpuid_version |= 0xf00 | ((value - 0x0f) << 20);
    } else {
        env->cpuid_version |= value << 8;
    }
}

static PropertyInfo qdev_prop_family = {
    .name  = "uint32",
    .get   = x86_cpuid_version_get_family,
    .set   = x86_cpuid_version_set_family,
};

static void x86_cpuid_version_get_model(Object *obj, Visitor *v, void *opaque,
                                        const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = (env->cpuid_version >> 4) & 0xf;
    value |= ((env->cpuid_version >> 16) & 0xf) << 4;
    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_version_set_model(Object *obj, Visitor *v, void *opaque,
                                        const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff;
    int64_t value;

    visit_type_int(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    if (value < min || value > max) {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                  name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf00f0;
    env->cpuid_version |= ((value & 0xf) << 4) | ((value >> 4) << 16);
}

static PropertyInfo qdev_prop_model = {
    .name  = "uint32",
    .get   = x86_cpuid_version_get_model,
    .set   = x86_cpuid_version_set_model,
};

static void x86_cpuid_version_get_stepping(Object *obj, Visitor *v,
                                           void *opaque, const char *name,
                                           Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = env->cpuid_version & 0xf;
    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_version_set_stepping(Object *obj, Visitor *v,
                                           void *opaque, const char *name,
                                           Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xf;
    int64_t value;

    visit_type_int(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    if (value < min || value > max) {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                  name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf;
    env->cpuid_version |= value & 0xf;
}

static PropertyInfo qdev_prop_stepping = {
    .name  = "uint32",
    .get   = x86_cpuid_version_get_stepping,
    .set   = x86_cpuid_version_set_stepping,
};

static void x86_cpuid_get_vendor(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    char *value;

    value = (char *)g_malloc(CPUID_VENDOR_SZ + 1);
    x86_cpu_vendor_words2str(value, env->cpuid_vendor1, env->cpuid_vendor2,
                             env->cpuid_vendor3);
    visit_type_str(v, &value, name, errp);
    g_free(value);
}

static void x86_cpuid_set_vendor(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    char *value;
    int i;

    visit_type_str(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }

    if (strlen(value) != CPUID_VENDOR_SZ) {
        error_set(errp, QERR_PROPERTY_VALUE_BAD, "",
                  "vendor", value);
        return;
    }

    env->cpuid_vendor1 = 0;
    env->cpuid_vendor2 = 0;
    env->cpuid_vendor3 = 0;
    for (i = 0; i < 4; i++) {
        env->cpuid_vendor1 |= ((uint8_t)value[i    ]) << (8 * i);
        env->cpuid_vendor2 |= ((uint8_t)value[i + 4]) << (8 * i);
        env->cpuid_vendor3 |= ((uint8_t)value[i + 8]) << (8 * i);
    }
    g_free(value);
}

static PropertyInfo qdev_prop_vendor = {
    .name  = "string",
    .get   = x86_cpuid_get_vendor,
    .set   = x86_cpuid_set_vendor,
};

static void x86_cpuid_get_model_id(Object *obj, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    char *value;
    int i;

    value = g_malloc(48 + 1);
    for (i = 0; i < 48; i++) {
        value[i] = env->cpuid_model[i >> 2] >> (8 * (i & 3));
    }
    value[48] = '\0';
    visit_type_str(v, &value, name, errp);
    g_free(value);
}

static void x86_cpuid_set_model_id(Object *obj, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int c, len, i;
    char *model_id;

    visit_type_str(v, &model_id, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    len = strlen(model_id);
    memset(env->cpuid_model, 0, 48);
    for (i = 0; i < 48; i++) {
        if (i >= len) {
            c = '\0';
        } else {
            c = (uint8_t)model_id[i];
        }
        env->cpuid_model[i >> 2] |= c << (8 * (i & 3));
    }
    g_free(model_id);
}

static PropertyInfo qdev_prop_model_id = {
    .name  = "string",
    .get   = x86_cpuid_get_model_id,
    .set   = x86_cpuid_set_model_id,
};

static void x86_cpuid_get_tsc_freq(Object *obj, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    int64_t value;

    value = cpu->env.tsc_khz * 1000;
    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_set_tsc_freq(Object *obj, Visitor *v, void *opaque,
                                   const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    const int64_t min = 0;
    const int64_t max = INT64_MAX;
    int64_t value;

    visit_type_int(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    if (value < min || value > max) {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                  name ? name : "null", value, min, max);
        return;
    }

    cpu->env.tsc_khz = value / 1000;
}

static PropertyInfo qdev_prop_tsc_freq = {
    .name  = "int64",
    .get   = x86_cpuid_get_tsc_freq,
    .set   = x86_cpuid_set_tsc_freq,
};

static void x86_cpuid_get_apic_id(Object *obj, Visitor *v, void *opaque,
                                  const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    int64_t value = cpu->env.cpuid_apic_id;

    visit_type_int(v, &value, name, errp);
}

static void x86_cpuid_set_apic_id(Object *obj, Visitor *v, void *opaque,
                                  const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    DeviceState *dev = DEVICE(obj);
    const int64_t min = 0;
    const int64_t max = UINT32_MAX;
    Error *error = NULL;
    int64_t value;

    if (dev->realized) {
        error_setg(errp, "Attempt to set property '%s' on '%s' after "
                   "it was realized", name, object_get_typename(obj));
        return;
    }

    visit_type_int(v, &value, name, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, "Property %s.%s doesn't take value %" PRId64
                   " (minimum: %" PRId64 ", maximum: %" PRId64 ")" ,
                   object_get_typename(obj), name, value, min, max);
        return;
    }

    if ((value != cpu->env.cpuid_apic_id) && cpu_exists(value)) {
        error_setg(errp, "CPU with APIC ID %" PRIi64 " exists", value);
        return;
    }
    cpu->env.cpuid_apic_id = value;
}

/* Generic getter for "feature-words" and "filtered-features" properties */
static void x86_cpu_get_feature_words(Object *obj, Visitor *v, void *opaque,
                                      const char *name, Error **errp)
{
    uint32_t *array = (uint32_t *)opaque;
    FeatureWord w;
    Error *err = NULL;
    X86CPUFeatureWordInfo word_infos[FEATURE_WORDS] = { };
    X86CPUFeatureWordInfoList list_entries[FEATURE_WORDS] = { };
    X86CPUFeatureWordInfoList *list = NULL;

    for (w = 0; w < FEATURE_WORDS; w++) {
        FeatureWordInfo *wi = &feature_word_info[w];
        X86CPUFeatureWordInfo *qwi = &word_infos[w];
        qwi->cpuid_input_eax = wi->cpuid_eax;
        qwi->has_cpuid_input_ecx = wi->cpuid_needs_ecx;
        qwi->cpuid_input_ecx = wi->cpuid_ecx;
        qwi->cpuid_register = x86_reg_info_32[wi->cpuid_reg].qapi_enum;
        qwi->features = array[w];

        /* List will be in reverse order, but order shouldn't matter */
        list_entries[w].next = list;
        list_entries[w].value = &word_infos[w];
        list = &list_entries[w];
    }

    visit_type_X86CPUFeatureWordInfoList(v, &list, "feature-words", &err);
    error_propagate(errp, err);
}

static void x86_get_hv_spinlocks(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    int64_t value = cpu->hyperv_spinlock_attempts;

    visit_type_int(v, &value, name, errp);
}

static void x86_set_hv_spinlocks(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    const int64_t min = 0xFFF;
    const int64_t max = UINT_MAX;
    X86CPU *cpu = X86_CPU(obj);
    int64_t value;

    visit_type_int(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }

    if (value < min || value > max) {
        error_setg(errp, "Property %s.%s doesn't take value %" PRId64
                  " (minimum: %" PRId64 ", maximum: %" PRId64 ")",
                  object_get_typename(obj), name ? name : "null",
                  value, min, max);
        return;
    }
    cpu->hyperv_spinlock_attempts = value;
}

static PropertyInfo qdev_prop_spinlocks = {
    .name  = "int",
    .get   = x86_get_hv_spinlocks,
    .set   = x86_set_hv_spinlocks,
};

#define X86CPU_FEAT(_name, _bit, _leaf) \
    DEFINE_PROP_BIT(_name, X86CPU, env.features[_leaf], _bit, 0)

static Property cpu_x86_properties[] = {
    { .name  = "hv-spinlocks", .info  = &qdev_prop_spinlocks },
    DEFINE_PROP_BOOL("hv-relaxed", X86CPU, hyperv_relaxed_timing, false),
    DEFINE_PROP_BOOL("hv-vapic", X86CPU, hyperv_vapic, false),
    DEFINE_PROP_BOOL("check", X86CPU, check_cpuid, false),
    DEFINE_PROP_BOOL("enforce", X86CPU, enforce_cpuid, false),
    DEFINE_PROP_UINT32("level", X86CPU, env.cpuid_level, 0),
    DEFINE_PROP_UINT32("xlevel", X86CPU, env.cpuid_xlevel, 0),
    { .name = "family", .info = &qdev_prop_family },
    { .name = "model", .info = &qdev_prop_model },
    { .name = "stepping", .info = &qdev_prop_stepping },
    { .name = "vendor", .info  = &qdev_prop_vendor },
    { .name  = "model-id", .info  = &qdev_prop_model_id },
    { .name  = "tsc-frequency", .info  = &qdev_prop_tsc_freq },
    X86CPU_FEAT("feat-fpu",     0, FEAT_1_EDX),
    X86CPU_FEAT("feat-vme",     1, FEAT_1_EDX),
    X86CPU_FEAT("feat-de",      2, FEAT_1_EDX),
    X86CPU_FEAT("feat-pse",     3, FEAT_1_EDX),
    X86CPU_FEAT("feat-tsc",     4, FEAT_1_EDX),
    X86CPU_FEAT("feat-msr",     5, FEAT_1_EDX),
    X86CPU_FEAT("feat-pae",     6, FEAT_1_EDX),
    X86CPU_FEAT("feat-mce",     7, FEAT_1_EDX),
    X86CPU_FEAT("feat-cx8",     8, FEAT_1_EDX),
    X86CPU_FEAT("feat-apic",    9, FEAT_1_EDX),
    X86CPU_FEAT("feat-sep",     11, FEAT_1_EDX),
    X86CPU_FEAT("feat-mtrr",    12, FEAT_1_EDX),
    X86CPU_FEAT("feat-pge",     13, FEAT_1_EDX),
    X86CPU_FEAT("feat-mca",     14, FEAT_1_EDX),
    X86CPU_FEAT("feat-cmov",    15, FEAT_1_EDX),
    X86CPU_FEAT("feat-pat",     16, FEAT_1_EDX),
    X86CPU_FEAT("feat-pse36",   17, FEAT_1_EDX),
    /* Intel psn */
    X86CPU_FEAT("feat-pn",      18, FEAT_1_EDX),
    /* Intel clfsh */
    X86CPU_FEAT("feat-clflush", 19, FEAT_1_EDX),
    /* Intel dts */
    X86CPU_FEAT("feat-ds",      21, FEAT_1_EDX),
    X86CPU_FEAT("feat-acpi",    22, FEAT_1_EDX),
    X86CPU_FEAT("feat-mmx",     23, FEAT_1_EDX),
    X86CPU_FEAT("feat-fxsr",    24, FEAT_1_EDX),
    X86CPU_FEAT("feat-sse",     25, FEAT_1_EDX),
    X86CPU_FEAT("feat-sse2",    26, FEAT_1_EDX),
    X86CPU_FEAT("feat-ss",      27, FEAT_1_EDX),
    /* Intel htt */
    X86CPU_FEAT("feat-ht",      28, FEAT_1_EDX),
    X86CPU_FEAT("feat-tm",      29, FEAT_1_EDX),
    X86CPU_FEAT("feat-ia64",    30, FEAT_1_EDX),
    X86CPU_FEAT("feat-pbe",     31, FEAT_1_EDX),
    /* Intel */
    X86CPU_FEAT("feat-pni",          0, FEAT_1_ECX),
    /* AMD sse3 */
    X86CPU_FEAT("feat-sse3",         0, FEAT_1_ECX),
    X86CPU_FEAT("feat-pclmulqdq",    1, FEAT_1_ECX),
    X86CPU_FEAT("feat-pclmuldq",     1, FEAT_1_ECX),
    X86CPU_FEAT("feat-dtes64",       2, FEAT_1_ECX),
    X86CPU_FEAT("feat-monitor",      3, FEAT_1_ECX),
    X86CPU_FEAT("feat-ds-cpl",       4, FEAT_1_ECX),
    X86CPU_FEAT("feat-vmx",          5, FEAT_1_ECX),
    X86CPU_FEAT("feat-smx",          6, FEAT_1_ECX),
    X86CPU_FEAT("feat-est",          7, FEAT_1_ECX),
    X86CPU_FEAT("feat-tm2",          8, FEAT_1_ECX),
    X86CPU_FEAT("feat-ssse3",        9, FEAT_1_ECX),
    X86CPU_FEAT("feat-cid",          10, FEAT_1_ECX),
    X86CPU_FEAT("feat-fma",          12, FEAT_1_ECX),
    X86CPU_FEAT("feat-cx16",         13, FEAT_1_ECX),
    X86CPU_FEAT("feat-xtpr",         14, FEAT_1_ECX),
    X86CPU_FEAT("feat-pdcm",         15, FEAT_1_ECX),
    X86CPU_FEAT("feat-pcid",         17, FEAT_1_ECX),
    X86CPU_FEAT("feat-dca",          18, FEAT_1_ECX),
    X86CPU_FEAT("feat-sse4-1",       19, FEAT_1_ECX),
    X86CPU_FEAT("feat-sse4.1",       19, FEAT_1_ECX),
    X86CPU_FEAT("feat-sse4-2",       20, FEAT_1_ECX),
    X86CPU_FEAT("feat-sse4.2",       20, FEAT_1_ECX),
    X86CPU_FEAT("feat-x2apic",       21, FEAT_1_ECX),
    X86CPU_FEAT("feat-movbe",        22, FEAT_1_ECX),
    X86CPU_FEAT("feat-popcnt",       23, FEAT_1_ECX),
    X86CPU_FEAT("feat-tsc-deadline", 24, FEAT_1_ECX),
    X86CPU_FEAT("feat-aes",          25, FEAT_1_ECX),
    X86CPU_FEAT("feat-xsave",        26, FEAT_1_ECX),
    X86CPU_FEAT("feat-osxsave",      27, FEAT_1_ECX),
    X86CPU_FEAT("feat-avx",          28, FEAT_1_ECX),
    X86CPU_FEAT("feat-f16c",         29, FEAT_1_ECX),
    X86CPU_FEAT("feat-rdrand",       30, FEAT_1_ECX),
    X86CPU_FEAT("feat-hypervisor",   31, FEAT_1_ECX),
    X86CPU_FEAT("feat-syscall",  11, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-nx",       20, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-xd",       20, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-mmxext",   22, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-fxsr-opt", 25, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-ffxsr",    25, FEAT_8000_0001_EDX),
    /* AMD Page1GB */
    X86CPU_FEAT("feat-pdpe1gb",  26, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-rdtscp",   27, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-lm",       29, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-i64",      29, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-3dnowext", 30, FEAT_8000_0001_EDX),
    X86CPU_FEAT("feat-3dnow",    31, FEAT_8000_0001_EDX),
    /* AMD LahfSahf */
    X86CPU_FEAT("feat-lahf-lm",       0, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-cmp-legacy",    1, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-svm",           2, FEAT_8000_0001_ECX),
    /* AMD ExtApicSpace */
    X86CPU_FEAT("feat-extapic",       3, FEAT_8000_0001_ECX),
    /* AMD AltMovCr8 */
    X86CPU_FEAT("feat-cr8legacy",     4, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-abm",           5, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-sse4a",         6, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-misalignsse",   7, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-3dnowprefetch", 8, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-osvw",          9, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-ibs",           10, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-xop",           11, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-skinit",        12, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-wdt",           13, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-lwp",           15, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-fma4",          16, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-tce",           17, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-nodeid-msr",    19, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-tbm",           21, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-topoext",       22, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-perfctr-core",  23, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-perfctr-nb",    24, FEAT_8000_0001_ECX),
    X86CPU_FEAT("feat-xstore",    2, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-xstore-en", 3, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-xcrypt",    6, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-xcrypt-en", 7, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-ace2",      8, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-ace2-en",   9, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-phe",      10, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-phe-en",   11, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-pmm",      12, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-pmm-en",   13, FEAT_C000_0001_EDX),
    X86CPU_FEAT("feat-kvmclock",        0, FEAT_KVM),
    X86CPU_FEAT("feat-kvm-nopiodelay",  1, FEAT_KVM),
    X86CPU_FEAT("feat-kvm-mmu",         2, FEAT_KVM),
    X86CPU_FEAT("feat-kvmclock2",       3, FEAT_KVM),
    X86CPU_FEAT("feat-kvm-asyncpf",     4, FEAT_KVM),
    X86CPU_FEAT("feat-kvm-steal-tm",    5, FEAT_KVM),
    X86CPU_FEAT("feat-kvm-pv-eoi",      6, FEAT_KVM),
    X86CPU_FEAT("feat-npt",           0, FEAT_SVM),
    X86CPU_FEAT("feat-lbrv",          1, FEAT_SVM),
    X86CPU_FEAT("feat-svm-lock",      2, FEAT_SVM),
    X86CPU_FEAT("feat-nrip-save",     3, FEAT_SVM),
    X86CPU_FEAT("feat-tsc-scale",     4, FEAT_SVM),
    X86CPU_FEAT("feat-vmcb-clean",    5, FEAT_SVM),
    X86CPU_FEAT("feat-flushbyasid",   6, FEAT_SVM),
    X86CPU_FEAT("feat-decodeassists", 7, FEAT_SVM),
    X86CPU_FEAT("feat-pause-filter",  10, FEAT_SVM),
    X86CPU_FEAT("feat-pfthreshold",   12, FEAT_SVM),
    X86CPU_FEAT("feat-fsgsbase",  0, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-bmi1",    3, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-hle",     4, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-avx2",    5, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-smep",    7, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-bmi2",    8, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-erms",    9, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-invpcid", 10, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-rtm",     11, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-rdseed",  18, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-adx",     19, FEAT_7_0_EBX),
    X86CPU_FEAT("feat-smap",    20, FEAT_7_0_EBX),
    DEFINE_PROP_END_OF_LIST(),
};

static int cpu_x86_find_by_name(x86_def_t *x86_cpu_def, const char *name)
{
    x86_def_t *def;
    int i;

    if (name == NULL) {
        return -1;
    }
    if (kvm_enabled() && strcmp(name, "host") == 0) {
        kvm_cpu_fill_host(x86_cpu_def);
        return 0;
    }

    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        def = &builtin_x86_defs[i];
        if (strcmp(name, def->name) == 0) {
            memcpy(x86_cpu_def, def, sizeof(*def));
            /* sysenter isn't supported in compatibility mode on AMD,
             * syscall isn't supported in compatibility mode on Intel.
             * Normally we advertise the actual CPU vendor, but you can
             * override this using the 'vendor' property if you want to use
             * KVM's sysenter/syscall emulation in compatibility mode and
             * when doing cross vendor migration
             */
            if (kvm_enabled()) {
                uint32_t  ebx = 0, ecx = 0, edx = 0;
                host_cpuid(0, 0, NULL, &ebx, &ecx, &edx);
                x86_cpu_vendor_words2str(x86_cpu_def->vendor, ebx, edx, ecx);
            }
            return 0;
        }
    }

    return -1;
}

/* Convert all '_' in a feature string option name to '-', to make feature
 * name conform to QOM property naming rule, which uses '-' instead of '_'.
 */
static inline void feat2prop(char *s)
{
    while ((s = strchr(s, '_'))) {
        *s = '-';
    }
}

/* Parse "+feature,-feature,feature=foo" CPU feature string
 */
static void cpu_x86_parse_featurestr(X86CPU *cpu, char *features, Error **errp)
{
    char *featurestr; /* Single 'key=value" string being parsed */
    uint32_t numvalue;
    QDict *props = qdict_new();
    const QDictEntry *ent;

    featurestr = features ? strtok(features, ",") : NULL;

    while (featurestr) {
        char *val;
        feat2prop(featurestr);
        if (featurestr[0] == '+' || featurestr[0] == '-') {
            const gchar *feat = featurestr + 1;
            gchar *cpuid_fname = NULL;
            bool set_kvmclock2 = false;

            if (strncmp(feat, "feat-", 5)) {
                cpuid_fname = g_strconcat("feat-", feat, NULL);
                feat = cpuid_fname;
            }

            if (!strcmp(feat, "feat-kvmclock")) {
                set_kvmclock2 = true;
            }

        rep_feat_set:
            if (featurestr[0] == '+') {
                /* preseve legacy behaviour, if feature was disabled once
                 * do not allow to enable it again */
                if (!qdict_haskey(props, feat)) {
                    qdict_put(props, feat, qstring_from_str("on"));
                }
            } else {
                qdict_put(props, feat, qstring_from_str("off"));
            }

            if (set_kvmclock2) {
                feat = "feat-kvmclock2";
                set_kvmclock2 = false;
                goto rep_feat_set;
            }
            g_free(cpuid_fname);
        } else if ((val = strchr(featurestr, '='))) {
            *val = 0; val++;
            if (!strcmp(featurestr, "xlevel")) {
                char *err;
                char num[32];

                numvalue = strtoul(val, &err, 0);
                if (!*val || *err) {
                    error_setg(errp, "bad numerical value %s", val);
                    goto out;
                }
                if (numvalue < 0x80000000) {
                    fprintf(stderr, "xlevel value shall always be >= 0x80000000"
                            ", fixup will be removed in future versions\n");
                    numvalue += 0x80000000;
                }
                snprintf(num, sizeof(num), "%" PRIu32, numvalue);
                object_property_parse(OBJECT(cpu), num, featurestr, errp);
            } else if (!strcmp(featurestr, "tsc-freq")) {
                int64_t tsc_freq;
                char *err;
                char num[32];

                tsc_freq = strtosz_suffix_unit(val, &err,
                                               STRTOSZ_DEFSUFFIX_B, 1000);
                if (tsc_freq < 0 || *err) {
                    error_setg(errp, "bad numerical value %s", val);
                    goto out;
                }
                snprintf(num, sizeof(num), "%" PRId64, tsc_freq);
                object_property_parse(OBJECT(cpu), num, "tsc-frequency", errp);
            } else if (!strcmp(featurestr, "hv-spinlocks")) {
                char *err;
                const int min = 0xFFF;
                char num[32];
                numvalue = strtoul(val, &err, 0);
                if (!*val || *err) {
                    error_setg(errp, "bad numerical value %s", val);
                    goto out;
                }
                if (numvalue < min) {
                    fprintf(stderr, "hv-spinlocks value shall always be >= 0x%x"
                            ", fixup will be removed in future versions\n",
                            min);
                    numvalue = min;
                }
                snprintf(num, sizeof(num), "%" PRId32, numvalue);
                object_property_parse(OBJECT(cpu), num, featurestr, errp);
            } else {
                object_property_parse(OBJECT(cpu), val, featurestr, errp);
            }
        } else {
            object_property_parse(OBJECT(cpu), "on", featurestr, errp);
        }
        if (error_is_set(errp)) {
            goto out;
        }
        featurestr = strtok(NULL, ",");
    }

    for (ent = qdict_first(props); ent; ent = qdict_next(props, ent)) {
        const QString *qval = qobject_to_qstring(qdict_entry_value(ent));
        /* TODO: switch to using global properties after subclasses are done */
        object_property_parse(OBJECT(cpu), qstring_get_str(qval),
                              qdict_entry_key(ent), errp);
        if (error_is_set(errp)) {
            QDECREF(props);
            return;
        }
    }

out:
    QDECREF(props);
    return;
}

/* generate CPU information. */
void x86_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    x86_def_t *def;
    char buf[256];
    int i;
    const Property *prop;

    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        def = &builtin_x86_defs[i];
        snprintf(buf, sizeof(buf), "%s", def->name);
        (*cpu_fprintf)(f, "x86 %16s  %-48s\n", buf, def->model_id);
    }
#ifdef CONFIG_KVM
    (*cpu_fprintf)(f, "x86 %16s  %-48s\n", "host",
                   "KVM processor with all supported host features "
                   "(only available in KVM mode)");
#endif

    (*cpu_fprintf)(f, "\nRecognized CPUID flags:\n");

    (*cpu_fprintf)(f, " ");
    QDEV_PROP_FOREACH(prop, object_class_by_name(TYPE_X86_CPU)) {
        const char *name = prop ? prop->name : "";

        if (!g_str_has_prefix(name, "feat-")) {
            continue;
        }
        (*cpu_fprintf)(f, " %s", name);
    }
    (*cpu_fprintf)(f, "\n");
}

CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    x86_def_t *def;
    int i;

    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        CpuDefinitionInfoList *entry;
        CpuDefinitionInfo *info;

        def = &builtin_x86_defs[i];
        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(def->name);

        entry = g_malloc0(sizeof(*entry));
        entry->value = info;
        entry->next = cpu_list;
        cpu_list = entry;
    }

    return cpu_list;
}

#ifdef CONFIG_KVM
static void filter_features_for_kvm(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    KVMState *s = kvm_state;
    FeatureWord w;

    for (w = 0; w < FEATURE_WORDS; w++) {
        FeatureWordInfo *wi = &feature_word_info[w];
        uint32_t host_feat = kvm_arch_get_supported_cpuid(s, wi->cpuid_eax,
                                                             wi->cpuid_ecx,
                                                             wi->cpuid_reg);
        uint32_t requested_features = env->features[w];
        env->features[w] &= host_feat;
        cpu->filtered_features[w] = requested_features & ~env->features[w];
    }
}
#endif

static void cpu_x86_register(X86CPU *cpu, const char *name, Error **errp)
{
    CPUX86State *env = &cpu->env;
    x86_def_t def1, *def = &def1;

    memset(def, 0, sizeof(*def));

    if (cpu_x86_find_by_name(def, name) < 0) {
        error_setg(errp, "Unable to find CPU definition: %s", name);
        return;
    }

    if (kvm_enabled()) {
        def->features[FEAT_KVM] |= kvm_default_features;
    }
    def->features[FEAT_1_ECX] |= CPUID_EXT_HYPERVISOR;

    object_property_set_str(OBJECT(cpu), def->vendor, "vendor", errp);
    object_property_set_int(OBJECT(cpu), def->level, "level", errp);
    object_property_set_int(OBJECT(cpu), def->family, "family", errp);
    object_property_set_int(OBJECT(cpu), def->model, "model", errp);
    object_property_set_int(OBJECT(cpu), def->stepping, "stepping", errp);
    env->features[FEAT_1_EDX] = def->features[FEAT_1_EDX];
    env->features[FEAT_1_ECX] = def->features[FEAT_1_ECX];
    env->features[FEAT_8000_0001_EDX] = def->features[FEAT_8000_0001_EDX];
    env->features[FEAT_8000_0001_ECX] = def->features[FEAT_8000_0001_ECX];
    object_property_set_int(OBJECT(cpu), def->xlevel, "xlevel", errp);
    env->features[FEAT_KVM] = def->features[FEAT_KVM];
    env->features[FEAT_SVM] = def->features[FEAT_SVM];
    env->features[FEAT_C000_0001_EDX] = def->features[FEAT_C000_0001_EDX];
    env->features[FEAT_7_0_EBX] = def->features[FEAT_7_0_EBX];
    env->cpuid_xlevel2 = def->xlevel2;

    object_property_set_str(OBJECT(cpu), def->model_id, "model-id", errp);
}

X86CPU *cpu_x86_create(const char *cpu_model, DeviceState *icc_bridge,
                       Error **errp)
{
    X86CPU *cpu = NULL;
    CPUX86State *env;
    gchar **model_pieces;
    char *name, *features;
    char *typename;
    Error *error = NULL;

    model_pieces = g_strsplit(cpu_model, ",", 2);
    if (!model_pieces[0]) {
        error_setg(&error, "Invalid/empty CPU model name");
        goto out;
    }
    name = model_pieces[0];
    features = model_pieces[1];

    cpu = X86_CPU(object_new(TYPE_X86_CPU));
#ifndef CONFIG_USER_ONLY
    if (icc_bridge == NULL) {
        error_setg(&error, "Invalid icc-bridge value");
        goto out;
    }
    qdev_set_parent_bus(DEVICE(cpu), qdev_get_child_bus(icc_bridge, "icc"));
    object_unref(OBJECT(cpu));
#endif
    env = &cpu->env;
    env->cpu_model_str = cpu_model;

    cpu_x86_register(cpu, name, &error);
    if (error) {
        goto out;
    }

    /* Emulate per-model subclasses for global properties */
    typename = g_strdup_printf("%s-" TYPE_X86_CPU, name);
    qdev_prop_set_globals_for_type(DEVICE(cpu), typename, &error);
    g_free(typename);
    if (error) {
        goto out;
    }

    cpu_x86_parse_featurestr(cpu, features, &error);
    if (error) {
        goto out;
    }

out:
    error_propagate(errp, error);
    g_strfreev(model_pieces);
    return cpu;
}

X86CPU *cpu_x86_init(const char *cpu_model)
{
    Error *error = NULL;
    X86CPU *cpu;

    cpu = cpu_x86_create(cpu_model, NULL, &error);
    if (error) {
        goto out;
    }

    object_property_set_bool(OBJECT(cpu), true, "realized", &error);

out:
    if (error) {
        fprintf(stderr, "%s\n", error_get_pretty(error));
        error_free(error);
        if (cpu != NULL) {
            object_unref(OBJECT(cpu));
            cpu = NULL;
        }
    }
    return cpu;
}

#if !defined(CONFIG_USER_ONLY)

void cpu_clear_apic_feature(CPUX86State *env)
{
    env->features[FEAT_1_EDX] &= ~CPUID_APIC;
}

#endif /* !CONFIG_USER_ONLY */

/* Initialize list of CPU models, filling some non-static fields if necessary
 */
void x86_cpudef_setup(void)
{
    int i, j;
    static const char *model_with_versions[] = { "qemu32", "qemu64", "athlon" };

    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); ++i) {
        x86_def_t *def = &builtin_x86_defs[i];

        /* Look for specific "cpudef" models that */
        /* have the QEMU version in .model_id */
        for (j = 0; j < ARRAY_SIZE(model_with_versions); j++) {
            if (strcmp(model_with_versions[j], def->name) == 0) {
                pstrcpy(def->model_id, sizeof(def->model_id),
                        "QEMU Virtual CPU version ");
                pstrcat(def->model_id, sizeof(def->model_id),
                        qemu_get_version());
                break;
            }
        }
    }
}

static void get_cpuid_vendor(CPUX86State *env, uint32_t *ebx,
                             uint32_t *ecx, uint32_t *edx)
{
    *ebx = env->cpuid_vendor1;
    *edx = env->cpuid_vendor2;
    *ecx = env->cpuid_vendor3;
}

void cpu_x86_cpuid(CPUX86State *env, uint32_t index, uint32_t count,
                   uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    /* test if maximum index reached */
    if (index & 0x80000000) {
        if (index > env->cpuid_xlevel) {
            if (env->cpuid_xlevel2 > 0) {
                /* Handle the Centaur's CPUID instruction. */
                if (index > env->cpuid_xlevel2) {
                    index = env->cpuid_xlevel2;
                } else if (index < 0xC0000000) {
                    index = env->cpuid_xlevel;
                }
            } else {
                /* Intel documentation states that invalid EAX input will
                 * return the same information as EAX=cpuid_level
                 * (Intel SDM Vol. 2A - Instruction Set Reference - CPUID)
                 */
                index =  env->cpuid_level;
            }
        }
    } else {
        if (index > env->cpuid_level)
            index = env->cpuid_level;
    }

    switch(index) {
    case 0:
        *eax = env->cpuid_level;
        get_cpuid_vendor(env, ebx, ecx, edx);
        break;
    case 1:
        *eax = env->cpuid_version;
        *ebx = (env->cpuid_apic_id << 24) | 8 << 8; /* CLFLUSH size in quad words, Linux wants it. */
        *ecx = env->features[FEAT_1_ECX];
        *edx = env->features[FEAT_1_EDX];
        if (cs->nr_cores * cs->nr_threads > 1) {
            *ebx |= (cs->nr_cores * cs->nr_threads) << 16;
            *edx |= 1 << 28;    /* HTT bit */
        }
        break;
    case 2:
        /* cache info: needed for Pentium Pro compatibility */
        *eax = 1;
        *ebx = 0;
        *ecx = 0;
        *edx = 0x2c307d;
        break;
    case 4:
        /* cache info: needed for Core compatibility */
        if (cs->nr_cores > 1) {
            *eax = (cs->nr_cores - 1) << 26;
        } else {
            *eax = 0;
        }
        switch (count) {
            case 0: /* L1 dcache info */
                *eax |= 0x0000121;
                *ebx = 0x1c0003f;
                *ecx = 0x000003f;
                *edx = 0x0000001;
                break;
            case 1: /* L1 icache info */
                *eax |= 0x0000122;
                *ebx = 0x1c0003f;
                *ecx = 0x000003f;
                *edx = 0x0000001;
                break;
            case 2: /* L2 cache info */
                *eax |= 0x0000143;
                if (cs->nr_threads > 1) {
                    *eax |= (cs->nr_threads - 1) << 14;
                }
                *ebx = 0x3c0003f;
                *ecx = 0x0000fff;
                *edx = 0x0000001;
                break;
            default: /* end of info */
                *eax = 0;
                *ebx = 0;
                *ecx = 0;
                *edx = 0;
                break;
        }
        break;
    case 5:
        /* mwait info: needed for Core compatibility */
        *eax = 0; /* Smallest monitor-line size in bytes */
        *ebx = 0; /* Largest monitor-line size in bytes */
        *ecx = CPUID_MWAIT_EMX | CPUID_MWAIT_IBE;
        *edx = 0;
        break;
    case 6:
        /* Thermal and Power Leaf */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 7:
        /* Structured Extended Feature Flags Enumeration Leaf */
        if (count == 0) {
            *eax = 0; /* Maximum ECX value for sub-leaves */
            *ebx = env->features[FEAT_7_0_EBX]; /* Feature flags */
            *ecx = 0; /* Reserved */
            *edx = 0; /* Reserved */
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 9:
        /* Direct Cache Access Information Leaf */
        *eax = 0; /* Bits 0-31 in DCA_CAP MSR */
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xA:
        /* Architectural Performance Monitoring Leaf */
        if (kvm_enabled()) {
            KVMState *s = cs->kvm_state;

            *eax = kvm_arch_get_supported_cpuid(s, 0xA, count, R_EAX);
            *ebx = kvm_arch_get_supported_cpuid(s, 0xA, count, R_EBX);
            *ecx = kvm_arch_get_supported_cpuid(s, 0xA, count, R_ECX);
            *edx = kvm_arch_get_supported_cpuid(s, 0xA, count, R_EDX);
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0xD:
        /* Processor Extended State */
        if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE)) {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
            break;
        }
        if (kvm_enabled()) {
            KVMState *s = cs->kvm_state;

            *eax = kvm_arch_get_supported_cpuid(s, 0xd, count, R_EAX);
            *ebx = kvm_arch_get_supported_cpuid(s, 0xd, count, R_EBX);
            *ecx = kvm_arch_get_supported_cpuid(s, 0xd, count, R_ECX);
            *edx = kvm_arch_get_supported_cpuid(s, 0xd, count, R_EDX);
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0x80000000:
        *eax = env->cpuid_xlevel;
        *ebx = env->cpuid_vendor1;
        *edx = env->cpuid_vendor2;
        *ecx = env->cpuid_vendor3;
        break;
    case 0x80000001:
        *eax = env->cpuid_version;
        *ebx = 0;
        *ecx = env->features[FEAT_8000_0001_ECX];
        *edx = env->features[FEAT_8000_0001_EDX];

        /* The Linux kernel checks for the CMPLegacy bit and
         * discards multiple thread information if it is set.
         * So dont set it here for Intel to make Linux guests happy.
         */
        if (cs->nr_cores * cs->nr_threads > 1) {
            uint32_t tebx, tecx, tedx;
            get_cpuid_vendor(env, &tebx, &tecx, &tedx);
            if (tebx != CPUID_VENDOR_INTEL_1 ||
                tedx != CPUID_VENDOR_INTEL_2 ||
                tecx != CPUID_VENDOR_INTEL_3) {
                *ecx |= 1 << 1;    /* CmpLegacy bit */
            }
        }
        break;
    case 0x80000002:
    case 0x80000003:
    case 0x80000004:
        *eax = env->cpuid_model[(index - 0x80000002) * 4 + 0];
        *ebx = env->cpuid_model[(index - 0x80000002) * 4 + 1];
        *ecx = env->cpuid_model[(index - 0x80000002) * 4 + 2];
        *edx = env->cpuid_model[(index - 0x80000002) * 4 + 3];
        break;
    case 0x80000005:
        /* cache info (L1 cache) */
        *eax = 0x01ff01ff;
        *ebx = 0x01ff01ff;
        *ecx = 0x40020140;
        *edx = 0x40020140;
        break;
    case 0x80000006:
        /* cache info (L2 cache) */
        *eax = 0;
        *ebx = 0x42004200;
        *ecx = 0x02008140;
        *edx = 0;
        break;
    case 0x80000008:
        /* virtual & phys address size in low 2 bytes. */
/* XXX: This value must match the one used in the MMU code. */
        if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
            /* 64 bit processor */
/* XXX: The physical address space is limited to 42 bits in exec.c. */
            *eax = 0x00003028; /* 48 bits virtual, 40 bits physical */
        } else {
            if (env->features[FEAT_1_EDX] & CPUID_PSE36) {
                *eax = 0x00000024; /* 36 bits physical */
            } else {
                *eax = 0x00000020; /* 32 bits physical */
            }
        }
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (cs->nr_cores * cs->nr_threads > 1) {
            *ecx |= (cs->nr_cores * cs->nr_threads) - 1;
        }
        break;
    case 0x8000000A:
        if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
            *eax = 0x00000001; /* SVM Revision */
            *ebx = 0x00000010; /* nr of ASIDs */
            *ecx = 0;
            *edx = env->features[FEAT_SVM]; /* optional features */
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0xC0000000:
        *eax = env->cpuid_xlevel2;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xC0000001:
        /* Support for VIA CPU's CPUID instruction */
        *eax = env->cpuid_version;
        *ebx = 0;
        *ecx = 0;
        *edx = env->features[FEAT_C000_0001_EDX];
        break;
    case 0xC0000002:
    case 0xC0000003:
    case 0xC0000004:
        /* Reserved for the future, and now filled with zero */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    default:
        /* reserved values: zero */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    }
}

/* CPUClass::reset() */
static void x86_cpu_reset(CPUState *s)
{
    X86CPU *cpu = X86_CPU(s);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);
    CPUX86State *env = &cpu->env;
    int i;

    xcc->parent_reset(s);


    memset(env, 0, offsetof(CPUX86State, breakpoints));

    tlb_flush(env, 1);

    env->old_exception = -1;

    /* init to reset state */

#ifdef CONFIG_SOFTMMU
    env->hflags |= HF_SOFTMMU_MASK;
#endif
    env->hflags2 |= HF2_GIF_MASK;

    cpu_x86_update_cr0(env, 0x60000010);
    env->a20_mask = ~0x0;
    env->smbase = 0x30000;

    env->idt.limit = 0xffff;
    env->gdt.limit = 0xffff;
    env->ldt.limit = 0xffff;
    env->ldt.flags = DESC_P_MASK | (2 << DESC_TYPE_SHIFT);
    env->tr.limit = 0xffff;
    env->tr.flags = DESC_P_MASK | (11 << DESC_TYPE_SHIFT);

    cpu_x86_load_seg_cache(env, R_CS, 0xf000, 0xffff0000, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                           DESC_R_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_SS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);

    env->eip = 0xfff0;
    env->regs[R_EDX] = env->cpuid_version;

    env->eflags = 0x2;

    /* FPU init */
    for (i = 0; i < 8; i++) {
        env->fptags[i] = 1;
    }
    env->fpuc = 0x37f;

    env->mxcsr = 0x1f80;

    env->pat = 0x0007040600070406ULL;
    env->msr_ia32_misc_enable = MSR_IA32_MISC_ENABLE_DEFAULT;

    memset(env->dr, 0, sizeof(env->dr));
    env->dr[6] = DR6_FIXED_1;
    env->dr[7] = DR7_FIXED_1;
    cpu_breakpoint_remove_all(env, BP_CPU);
    cpu_watchpoint_remove_all(env, BP_CPU);

#if !defined(CONFIG_USER_ONLY)
    /* We hard-wire the BSP to the first CPU. */
    if (s->cpu_index == 0) {
        apic_designate_bsp(env->apic_state);
    }

    s->halted = !cpu_is_bsp(cpu);
#endif
}

#ifndef CONFIG_USER_ONLY
bool cpu_is_bsp(X86CPU *cpu)
{
    return cpu_get_apic_base(cpu->env.apic_state) & MSR_IA32_APICBASE_BSP;
}

/* TODO: remove me, when reset over QOM tree is implemented */
static void x86_cpu_machine_reset_cb(void *opaque)
{
    X86CPU *cpu = opaque;
    cpu_reset(CPU(cpu));
}
#endif

static void mce_init(X86CPU *cpu)
{
    CPUX86State *cenv = &cpu->env;
    unsigned int bank;

    if (((cenv->cpuid_version >> 8) & 0xf) >= 6
        && (cenv->features[FEAT_1_EDX] & (CPUID_MCE | CPUID_MCA)) ==
            (CPUID_MCE | CPUID_MCA)) {
        cenv->mcg_cap = MCE_CAP_DEF | MCE_BANKS_DEF;
        cenv->mcg_ctl = ~(uint64_t)0;
        for (bank = 0; bank < MCE_BANKS_DEF; bank++) {
            cenv->mce_banks[bank * 4] = ~(uint64_t)0;
        }
    }
}

#ifndef CONFIG_USER_ONLY
static void x86_cpu_apic_create(X86CPU *cpu, Error **errp)
{
    CPUX86State *env = &cpu->env;
    DeviceState *dev = DEVICE(cpu);
    APICCommonState *apic;
    const char *apic_type = "apic";

    if (kvm_irqchip_in_kernel()) {
        apic_type = "kvm-apic";
    } else if (xen_enabled()) {
        apic_type = "xen-apic";
    }

    env->apic_state = qdev_try_create(qdev_get_parent_bus(dev), apic_type);
    if (env->apic_state == NULL) {
        error_setg(errp, "APIC device '%s' could not be created", apic_type);
        return;
    }

    object_property_add_child(OBJECT(cpu), "apic",
                              OBJECT(env->apic_state), NULL);
    qdev_prop_set_uint8(env->apic_state, "id", env->cpuid_apic_id);
    /* TODO: convert to link<> */
    apic = APIC_COMMON(env->apic_state);
    apic->cpu = cpu;
}

static void x86_cpu_apic_realize(X86CPU *cpu, Error **errp)
{
    CPUX86State *env = &cpu->env;

    if (env->apic_state == NULL) {
        return;
    }

    if (qdev_init(env->apic_state)) {
        error_setg(errp, "APIC device '%s' could not be initialized",
                   object_get_typename(OBJECT(env->apic_state)));
        return;
    }
}
#else
static void x86_cpu_apic_realize(X86CPU *cpu, Error **errp)
{
}
#endif

static void x86_cpu_realizefn(DeviceState *dev, Error **errp)
{
    X86CPU *cpu = X86_CPU(dev);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(dev);
    CPUX86State *env = &cpu->env;
    Error *local_err = NULL;

    if (env->features[FEAT_7_0_EBX] && env->cpuid_level < 7) {
        env->cpuid_level = 7;
    }

    /* On AMD CPUs, some CPUID[8000_0001].EDX bits must match the bits on
     * CPUID[1].EDX.
     */
    if (env->cpuid_vendor1 == CPUID_VENDOR_AMD_1 &&
        env->cpuid_vendor2 == CPUID_VENDOR_AMD_2 &&
        env->cpuid_vendor3 == CPUID_VENDOR_AMD_3) {
        env->features[FEAT_8000_0001_EDX] &= ~CPUID_EXT2_AMD_ALIASES;
        env->features[FEAT_8000_0001_EDX] |= (env->features[FEAT_1_EDX]
           & CPUID_EXT2_AMD_ALIASES);
    }

    if (!kvm_enabled()) {
        env->features[FEAT_1_EDX] &= TCG_FEATURES;
        env->features[FEAT_1_ECX] &= TCG_EXT_FEATURES;
        env->features[FEAT_8000_0001_EDX] &= (TCG_EXT2_FEATURES
#ifdef TARGET_X86_64
            | CPUID_EXT2_SYSCALL | CPUID_EXT2_LM
#endif
            );
        env->features[FEAT_8000_0001_ECX] &= TCG_EXT3_FEATURES;
        env->features[FEAT_SVM] &= TCG_SVM_FEATURES;
    } else {
        if ((cpu->check_cpuid || cpu->enforce_cpuid)
            && kvm_check_features_against_host(cpu) && cpu->enforce_cpuid) {
            error_setg(&local_err,
                       "Host's CPU doesn't support requested features");
            goto out;
        }
#ifdef CONFIG_KVM
        filter_features_for_kvm(cpu);
#endif
    }

#ifndef CONFIG_USER_ONLY
    qemu_register_reset(x86_cpu_machine_reset_cb, cpu);

    if (cpu->env.features[FEAT_1_EDX] & CPUID_APIC || smp_cpus > 1) {
        x86_cpu_apic_create(cpu, &local_err);
        if (local_err != NULL) {
            goto out;
        }
    }
#endif

    mce_init(cpu);

    x86_cpu_apic_realize(cpu, &local_err);
    if (local_err != NULL) {
        goto out;
    }
    cpu_reset(CPU(cpu));

    xcc->parent_realize(dev, &local_err);
out:
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
}

/* Enables contiguous-apic-ID mode, for compatibility */
static bool compat_apic_id_mode;

void enable_compat_apic_id_mode(void)
{
    compat_apic_id_mode = true;
}

/* Calculates initial APIC ID for a specific CPU index
 *
 * Currently we need to be able to calculate the APIC ID from the CPU index
 * alone (without requiring a CPU object), as the QEMU<->Seabios interfaces have
 * no concept of "CPU index", and the NUMA tables on fw_cfg need the APIC ID of
 * all CPUs up to max_cpus.
 */
uint32_t x86_cpu_apic_id_from_index(unsigned int cpu_index)
{
    uint32_t correct_id;
    static bool warned;

    correct_id = x86_apicid_from_cpu_idx(smp_cores, smp_threads, cpu_index);
    if (compat_apic_id_mode) {
        if (cpu_index != correct_id && !warned) {
            error_report("APIC IDs set in compatibility mode, "
                         "CPU topology won't match the configuration");
            warned = true;
        }
        return cpu_index;
    } else {
        return correct_id;
    }
}

static void x86_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    static int inited;

    cs->env_ptr = env;
    cpu_exec_init(env);

    object_property_add(obj, "apic-id", "int",
                        x86_cpuid_get_apic_id,
                        x86_cpuid_set_apic_id, NULL, NULL, NULL);
    object_property_add(obj, "feature-words", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)env->features, NULL);
    object_property_add(obj, "filtered-features", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)cpu->filtered_features, NULL);

    cpu->hyperv_spinlock_attempts = HYPERV_SPINLOCK_NEVER_RETRY;
    env->cpuid_apic_id = x86_cpu_apic_id_from_index(cs->cpu_index);

    /* init various static tables used in TCG mode */
    if (tcg_enabled() && !inited) {
        inited = 1;
        optimize_flags_init();
#ifndef CONFIG_USER_ONLY
        cpu_set_debug_excp_handler(breakpoint_handler);
#endif
    }
}

static int64_t x86_cpu_get_arch_id(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    return env->cpuid_apic_id;
}

static bool x86_cpu_get_paging_enabled(const CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    return cpu->env.cr[0] & CR0_PG_MASK;
}

static void x86_cpu_common_class_init(ObjectClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    xcc->parent_realize = dc->realize;
    dc->realize = x86_cpu_realizefn;
    dc->bus_type = TYPE_ICC_BUS;
    dc->props = cpu_x86_properties;

    xcc->parent_reset = cc->reset;
    cc->reset = x86_cpu_reset;
    cc->reset_dump_flags = CPU_DUMP_FPU | CPU_DUMP_CCOP;

    cc->do_interrupt = x86_cpu_do_interrupt;
    cc->dump_state = x86_cpu_dump_state;
    cc->get_arch_id = x86_cpu_get_arch_id;
    cc->get_paging_enabled = x86_cpu_get_paging_enabled;
#ifndef CONFIG_USER_ONLY
    cc->get_memory_mapping = x86_cpu_get_memory_mapping;
    cc->write_elf64_note = x86_cpu_write_elf64_note;
    cc->write_elf64_qemunote = x86_cpu_write_elf64_qemunote;
    cc->write_elf32_note = x86_cpu_write_elf32_note;
    cc->write_elf32_qemunote = x86_cpu_write_elf32_qemunote;
#endif
    cpu_class_set_vmsd(cc, &vmstate_x86_cpu);
}

static const TypeInfo x86_cpu_type_info = {
    .name = TYPE_X86_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(X86CPU),
    .instance_init = x86_cpu_initfn,
    .abstract = false,
    .class_size = sizeof(X86CPUClass),
    .class_init = x86_cpu_common_class_init,
};

static void x86_cpu_register_types(void)
{
    type_register_static(&x86_cpu_type_info);
}

type_init(x86_cpu_register_types)
