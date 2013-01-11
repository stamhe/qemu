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

#include "qemu/option.h"
#include "qemu/config-file.h"
#include "qapi/qmp/qerror.h"

#include "qapi/visitor.h"
#include "sysemu/arch_init.h"

#include "hyperv.h"

#include "hw/hw.h"
#include "hw/qdev-properties.h"
#if defined(CONFIG_KVM)
#include <linux/kvm_para.h>
#endif

#include "sysemu/sysemu.h"
#ifndef CONFIG_USER_ONLY
#include "hw/xen.h"
#include "hw/sysbus.h"
#include "hw/apic_internal.h"
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

/* feature flags taken from "Intel Processor Identification and the CPUID
 * Instruction" and AMD's "CPUID Specification".  In cases of disagreement
 * between feature naming conventions, aliases may be added.
 */
static const char *feature_name[] = {
    "fpu", "vme", "de", "pse",
    "tsc", "msr", "pae", "mce",
    "cx8", "apic", NULL, "sep",
    "mtrr", "pge", "mca", "cmov",
    "pat", "pse36", "pn" /* Intel psn */, "clflush" /* Intel clfsh */,
    NULL, "ds" /* Intel dts */, "acpi", "mmx",
    "fxsr", "sse", "sse2", "ss",
    "ht" /* Intel htt */, "tm", "ia64", "pbe",
};
static const char *ext_feature_name[] = {
    "pni|sse3" /* Intel,AMD sse3 */, "pclmulqdq|pclmuldq", "dtes64", "monitor",
    "ds_cpl", "vmx", "smx", "est",
    "tm2", "ssse3", "cid", NULL,
    "fma", "cx16", "xtpr", "pdcm",
    NULL, "pcid", "dca", "sse4.1|sse4_1",
    "sse4.2|sse4_2", "x2apic", "movbe", "popcnt",
    "tsc-deadline", "aes", "xsave", "osxsave",
    "avx", "f16c", "rdrand", "hypervisor",
};
/* Feature names that are already defined on feature_name[] but are set on
 * CPUID[8000_0001].EDX on AMD CPUs don't have their names on
 * ext2_feature_name[]. They are copied automatically to cpuid_ext2_features
 * if and only if CPU vendor is AMD.
 */
static const char *ext2_feature_name[] = {
    NULL /* fpu */, NULL /* vme */, NULL /* de */, NULL /* pse */,
    NULL /* tsc */, NULL /* msr */, NULL /* pae */, NULL /* mce */,
    NULL /* cx8 */ /* AMD CMPXCHG8B */, NULL /* apic */, NULL, "syscall",
    NULL /* mtrr */, NULL /* pge */, NULL /* mca */, NULL /* cmov */,
    NULL /* pat */, NULL /* pse36 */, NULL, NULL /* Linux mp */,
    "nx|xd", NULL, "mmxext", NULL /* mmx */,
    NULL /* fxsr */, "fxsr_opt|ffxsr", "pdpe1gb" /* AMD Page1GB */, "rdtscp",
    NULL, "lm|i64", "3dnowext", "3dnow",
};
static const char *ext3_feature_name[] = {
    "lahf_lm" /* AMD LahfSahf */, "cmp_legacy", "svm", "extapic" /* AMD ExtApicSpace */,
    "cr8legacy" /* AMD AltMovCr8 */, "abm", "sse4a", "misalignsse",
    "3dnowprefetch", "osvw", "ibs", "xop",
    "skinit", "wdt", NULL, "lwp",
    "fma4", "tce", NULL, "nodeid_msr",
    NULL, "tbm", "topoext", "perfctr_core",
    "perfctr_nb", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *ext4_feature_name[] = {
    NULL, NULL, "xstore", "xstore-en",
    NULL, NULL, "xcrypt", "xcrypt-en",
    "ace2", "ace2-en", "phe", "phe-en",
    "pmm", "pmm-en", NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *kvm_feature_name[] = {
    "kvmclock", "kvm_nopiodelay", "kvm_mmu", "kvmclock",
    "kvm_asyncpf", "kvm_steal_time", "kvm_pv_eoi", NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *svm_feature_name[] = {
    "npt", "lbrv", "svm_lock", "nrip_save",
    "tsc_scale", "vmcb_clean",  "flushbyasid", "decodeassists",
    NULL, NULL, "pause_filter", NULL,
    "pfthreshold", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
};

static const char *cpuid_7_0_ebx_feature_name[] = {
    "fsgsbase", NULL, NULL, "bmi1", "hle", "avx2", NULL, "smep",
    "bmi2", "erms", "invpcid", "rtm", NULL, NULL, NULL, NULL,
    NULL, NULL, "rdseed", "adx", "smap", NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

typedef struct FeatureWordInfo {
    const char **feat_names;
    uint32_t cpuid_eax; /* Input EAX for CPUID */
    int cpuid_reg;      /* R_* register constant */
} FeatureWordInfo;

static FeatureWordInfo feature_word_info[FEATURE_WORDS] = {
    [FEAT_1_EDX] = {
        .feat_names = feature_name,
        .cpuid_eax = 1, .cpuid_reg = R_EDX,
    },
    [FEAT_1_ECX] = {
        .feat_names = ext_feature_name,
        .cpuid_eax = 1, .cpuid_reg = R_ECX,
    },
    [FEAT_8000_0001_EDX] = {
        .feat_names = ext2_feature_name,
        .cpuid_eax = 0x80000001, .cpuid_reg = R_EDX,
    },
    [FEAT_8000_0001_ECX] = {
        .feat_names = ext3_feature_name,
        .cpuid_eax = 0x80000001, .cpuid_reg = R_ECX,
    },
    [FEAT_C000_0001_EDX] = {
        .feat_names = ext4_feature_name,
        .cpuid_eax = 0xC0000001, .cpuid_reg = R_EDX,
    },
    [FEAT_KVM] = {
        .feat_names = kvm_feature_name,
        .cpuid_eax = KVM_CPUID_FEATURES, .cpuid_reg = R_EAX,
    },
    [FEAT_SVM] = {
        .feat_names = svm_feature_name,
        .cpuid_eax = 0x8000000A, .cpuid_reg = R_EDX,
    },
    [FEAT_7_0_EBX] = {
        .feat_names = cpuid_7_0_ebx_feature_name,
        .cpuid_eax = 7, .cpuid_reg = R_EBX,
    },
};

const char *get_register_name_32(unsigned int reg)
{
    static const char *reg_names[CPU_NB_REGS32] = {
        [R_EAX] = "EAX",
        [R_ECX] = "ECX",
        [R_EDX] = "EDX",
        [R_EBX] = "EBX",
        [R_ESP] = "ESP",
        [R_EBP] = "EBP",
        [R_ESI] = "ESI",
        [R_EDI] = "EDI",
    };

    if (reg > CPU_NB_REGS32) {
        return NULL;
    }
    return reg_names[reg];
}

#if defined(CONFIG_KVM)
static void x86_cpu_get_kvmclock(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    bool value = cpu->env.cpuid_kvm_features;
    value = (value & KVM_FEATURE_CLOCKSOURCE) &&
            (value & KVM_FEATURE_CLOCKSOURCE2);
    visit_type_bool(v, &value, name, errp);
}

static void x86_cpu_set_kvmclock(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    bool value;
    visit_type_bool(v, &value, name, errp);
    if (value == true) {
        cpu->env.cpuid_kvm_features |= KVM_FEATURE_CLOCKSOURCE |
                                      KVM_FEATURE_CLOCKSOURCE2;
    } else {
        cpu->env.cpuid_kvm_features &= ~(KVM_FEATURE_CLOCKSOURCE |
                                       KVM_FEATURE_CLOCKSOURCE2);
    }
}

PropertyInfo qdev_prop_kvmclock = {
    .name  = "boolean",
    .get   = x86_cpu_get_kvmclock,
    .set   = x86_cpu_set_kvmclock,
};
#define DEFINE_PROP_KVMCLOCK(_n, _f)                                           \
    DEFINE_PROP(_n, X86CPU, _f, qdev_prop_kvmclock, uint32_t)

#endif

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
        error_setg(errp, "Property '%s.%s' doesn't take value '%s'",
                   object_get_typename(obj), name, value);
        g_free(value);
        return;
    }

    env->cpuid_vendor1 = 0;
    env->cpuid_vendor2 = 0;
    env->cpuid_vendor3 = 0;
    for (i = 0; i < 4; i++) {
        env->cpuid_vendor1 |= ((uint8_t)value[i]) << (8 * i);
        env->cpuid_vendor2 |= ((uint8_t)value[i + 4]) << (8 * i);
        env->cpuid_vendor3 |= ((uint8_t)value[i + 8]) << (8 * i);
    }
    g_free(value);
}

PropertyInfo qdev_prop_vendor = {
    .name  = "string",
    .get   = x86_cpuid_get_vendor,
    .set   = x86_cpuid_set_vendor,
};
#define DEFINE_PROP_VENDOR(_n, _s, _f)                                         \
    DEFINE_PROP(_n, _s, _f, qdev_prop_vendor, uint32_t)

static void x86_get_hv_spinlocks(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    int64_t value = hyperv_get_spinlock_retries();

    visit_type_int(v, &value, name, errp);
}

static void x86_set_hv_spinlocks(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    int64_t value;

    visit_type_int(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    if (!value) {
        error_setg(errp, "Property '%s.%s' doesn't take value '%s'",
                   object_get_typename(obj), name, "0");
        return;
    }
    hyperv_set_spinlock_retries(value);
}

PropertyInfo qdev_prop_spinlocks = {
    .name  = "int",
    .get   = x86_get_hv_spinlocks,
    .set   = x86_set_hv_spinlocks,
};
#define DEFINE_PROP_HV_SPINLOCKS(_n) {                                         \
    .name  = _n,                                                               \
    .info  = &qdev_prop_spinlocks,                                             \
}

static void x86_get_hv_relaxed(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    bool value = hyperv_relaxed_timing_enabled();

    visit_type_bool(v, &value, name, errp);
}

static void x86_set_hv_relaxed(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    bool value;

    visit_type_bool(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    hyperv_enable_relaxed_timing(value);
}

PropertyInfo qdev_prop_hv_relaxed = {
    .name  = "boolean",
    .get   = x86_get_hv_relaxed,
    .set   = x86_set_hv_relaxed,
};
#define DEFINE_PROP_HV_RELAXED(_n) {                                           \
    .name  = _n,                                                               \
    .info  = &qdev_prop_hv_relaxed,                                            \
}

static void x86_get_hv_vapic(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    bool value = hyperv_vapic_recommended();

    visit_type_bool(v, &value, name, errp);
}

static void x86_set_hv_vapic(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    bool value;

    visit_type_bool(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    hyperv_enable_vapic_recommended(value);
}

PropertyInfo qdev_prop_hv_vapic = {
    .name  = "boolean",
    .get   = x86_get_hv_vapic,
    .set   = x86_set_hv_vapic,
};
#define DEFINE_PROP_HV_VAPIC(_n) {                                             \
    .name  = _n,                                                               \
    .info  = &qdev_prop_hv_vapic,                                              \
}

static bool check_cpuid;

static void x86_cpuid_get_check(Object *obj, Visitor *v, void *opaque,
                                         const char *name, Error **errp)
{
    visit_type_bool(v, &check_cpuid, name, errp);
}

static void x86_cpuid_set_check(Object *obj, Visitor *v, void *opaque,
                                         const char *name, Error **errp)
{
    bool value;

    visit_type_bool(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    check_cpuid = value;
}

PropertyInfo qdev_prop_check = {
    .name  = "bool",
    .get   = x86_cpuid_get_check,
    .set   = x86_cpuid_set_check,
};
#define DEFINE_PROP_CHECK(_n) {                                                \
    .name  = _n,                                                               \
    .info  = &qdev_prop_check,                                                 \
}

static bool enforce_cpuid;

static void x86_cpuid_get_enforce(Object *obj, Visitor *v, void *opaque,
                                         const char *name, Error **errp)
{
    visit_type_bool(v, &enforce_cpuid, name, errp);
}

static void x86_cpuid_set_enforce(Object *obj, Visitor *v, void *opaque,
                                         const char *name, Error **errp)
{
    bool value;

    visit_type_bool(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    enforce_cpuid = value;
}

PropertyInfo qdev_prop_enforce = {
    .name  = "boolean",
    .get   = x86_cpuid_get_enforce,
    .set   = x86_cpuid_set_enforce,
};
#define DEFINE_PROP_ENFORCE(_n) {                                              \
    .name  = _n,                                                               \
    .info  = &qdev_prop_enforce,                                               \
}

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
        error_setg(errp, "Property %s.%s doesn't take value %" PRId64 " (min"
                  "imum: %" PRId64 ", maximum: %" PRId64,
                  object_get_typename(obj), name, value, min, max);
        return;
    }

    cpu->env.tsc_khz = value / 1000;
}

PropertyInfo qdev_prop_tsc_freq = {
    .name  = "int32",
    .get   = x86_cpuid_get_tsc_freq,
    .set   = x86_cpuid_set_tsc_freq,
};
#define DEFINE_PROP_TSC_FREQ(_n, _s, _f)                                       \
    DEFINE_PROP(_n, _s, _f, qdev_prop_tsc_freq, int32_t)

#define FEAT(_name, _field, _bit, _val) \
    DEFINE_PROP_BIT(_name, X86CPU, _field, _bit, _val)

static Property cpu_x86_properties[] = {
    FEAT("f-fpu", env.cpuid_features,  0, false),
    FEAT("f-vme", env.cpuid_features,  1, false),
    FEAT("f-de", env.cpuid_features,  2, false),
    FEAT("f-pse", env.cpuid_features,  3, false),
    FEAT("f-tsc", env.cpuid_features,  4, false),
    FEAT("f-msr", env.cpuid_features,  5, false),
    FEAT("f-pae", env.cpuid_features,  6, false),
    FEAT("f-mce", env.cpuid_features,  7, false),
    FEAT("f-cx8", env.cpuid_features,  8, false),
    FEAT("f-apic", env.cpuid_features,  9, false),
    FEAT("f-sep", env.cpuid_features, 11, false),
    FEAT("f-mtrr", env.cpuid_features, 12, false),
    FEAT("f-pge", env.cpuid_features, 13, false),
    FEAT("f-mca", env.cpuid_features, 14, false),
    FEAT("f-cmov", env.cpuid_features, 15, false),
    FEAT("f-pat", env.cpuid_features, 16, false),
    FEAT("f-pse36", env.cpuid_features, 17, false),
    /* Intel psn */
    FEAT("f-pn", env.cpuid_features, 18, false),
    /* Intel clfsh */
    FEAT("f-clflush", env.cpuid_features, 19, false),
    /* Intel dts */
    FEAT("f-ds", env.cpuid_features, 21, false),
    FEAT("f-acpi", env.cpuid_features, 22, false),
    FEAT("f-mmx", env.cpuid_features, 23, false),
    FEAT("f-fxsr", env.cpuid_features, 24, false),
    FEAT("f-sse", env.cpuid_features, 25, false),
    FEAT("f-sse2", env.cpuid_features, 26, false),
    FEAT("f-ss", env.cpuid_features, 27, false),
    /* Intel htt */
    FEAT("f-ht", env.cpuid_features, 28, false),
    FEAT("f-tm", env.cpuid_features, 29, false),
    FEAT("f-ia64", env.cpuid_features, 30, false),
    FEAT("f-pbe", env.cpuid_features, 31, false),
    /* Intel,AMD sse3 */
    FEAT("f-pni", env.cpuid_ext_features,  0, false),
    /* Intel,AMD sse3 */
    FEAT("f-sse3", env.cpuid_ext_features,  0, false),
    FEAT("f-pclmulqdq", env.cpuid_ext_features,  1, false),
    FEAT("f-pclmuldq", env.cpuid_ext_features,  1, false),
    FEAT("f-dtes64", env.cpuid_ext_features,  2, false),
    FEAT("f-monitor", env.cpuid_ext_features,  3, false),
    FEAT("f-ds_cpl", env.cpuid_ext_features,  4, false),
    FEAT("f-vmx", env.cpuid_ext_features,  5, false),
    FEAT("f-smx", env.cpuid_ext_features,  6, false),
    FEAT("f-est", env.cpuid_ext_features,  7, false),
    FEAT("f-tm2", env.cpuid_ext_features,  8, false),
    FEAT("f-ssse3", env.cpuid_ext_features,  9, false),
    FEAT("f-cid", env.cpuid_ext_features, 10, false),
    FEAT("f-fma", env.cpuid_ext_features, 12, false),
    FEAT("f-cx16", env.cpuid_ext_features, 13, false),
    FEAT("f-xtpr", env.cpuid_ext_features, 14, false),
    FEAT("f-pdcm", env.cpuid_ext_features, 15, false),
    FEAT("f-pcid", env.cpuid_ext_features, 17, false),
    FEAT("f-dca", env.cpuid_ext_features, 18, false),
    FEAT("f-sse4.1", env.cpuid_ext_features, 19, false),
    FEAT("f-sse4.2", env.cpuid_ext_features, 20, false),
    FEAT("f-sse4_1", env.cpuid_ext_features, 19, false),
    FEAT("f-sse4_2", env.cpuid_ext_features, 20, false),
    FEAT("f-x2apic", env.cpuid_ext_features, 21, false),
    FEAT("f-movbe", env.cpuid_ext_features, 22, false),
    FEAT("f-popcnt", env.cpuid_ext_features, 23, false),
    FEAT("f-tsc-deadline", env.cpuid_ext_features, 24, false),
    FEAT("f-aes", env.cpuid_ext_features, 25, false),
    FEAT("f-xsave", env.cpuid_ext_features, 26, false),
    FEAT("f-osxsave", env.cpuid_ext_features, 27, false),
    FEAT("f-avx", env.cpuid_ext_features, 28, false),
    FEAT("f-f16c", env.cpuid_ext_features, 29, false),
    FEAT("f-rdrand", env.cpuid_ext_features, 30, false),
    FEAT("f-hypervisor", env.cpuid_ext_features, 31, true),
    FEAT("f-syscall", env.cpuid_ext2_features, 11, false),
    FEAT("f-nx", env.cpuid_ext2_features, 20, false),
    FEAT("f-xd", env.cpuid_ext2_features, 20, false),
    FEAT("f-mmxext", env.cpuid_ext2_features, 22, false),
    FEAT("f-fxsr_opt", env.cpuid_ext2_features, 25, false),
    FEAT("f-ffxsr", env.cpuid_ext2_features, 25, false),
    /* AMD Page1GB */
    FEAT("f-pdpe1gb", env.cpuid_ext2_features, 26, false),
    FEAT("f-rdtscp", env.cpuid_ext2_features, 27, false),
    FEAT("f-lm", env.cpuid_ext2_features, 29, false),
    FEAT("f-i64", env.cpuid_ext2_features, 29, false),
    FEAT("f-3dnowext", env.cpuid_ext2_features, 30, false),
    FEAT("f-3dnow", env.cpuid_ext2_features, 31, false),
    /* AMD LahfSahf */
    FEAT("f-lahf_lm", env.cpuid_ext3_features,  0, false),
    FEAT("f-cmp_legacy", env.cpuid_ext3_features,  1, false),
    FEAT("f-svm", env.cpuid_ext3_features,  2, false),
    /* AMD ExtApicSpace */
    FEAT("f-extapic", env.cpuid_ext3_features,  3, false),
    /* AMD AltMovCr8 */
    FEAT("f-cr8legacy", env.cpuid_ext3_features,  4, false),
    FEAT("f-abm", env.cpuid_ext3_features,  5, false),
    FEAT("f-sse4a", env.cpuid_ext3_features,  6, false),
    FEAT("f-misalignsse", env.cpuid_ext3_features,  7, false),
    FEAT("f-3dnowprefetch", env.cpuid_ext3_features,  8, false),
    FEAT("f-osvw", env.cpuid_ext3_features,  9, false),
    FEAT("f-ibs", env.cpuid_ext3_features, 10, false),
    FEAT("f-xop", env.cpuid_ext3_features, 11, false),
    FEAT("f-skinit", env.cpuid_ext3_features, 12, false),
    FEAT("f-wdt", env.cpuid_ext3_features, 13, false),
    FEAT("f-lwp", env.cpuid_ext3_features, 15, false),
    FEAT("f-fma4", env.cpuid_ext3_features, 16, false),
    FEAT("f-tce", env.cpuid_ext3_features, 17, false),
    FEAT("f-cvt16", env.cpuid_ext3_features, 18, false),
    FEAT("f-nodeid_msr", env.cpuid_ext3_features, 19, false),
    FEAT("f-tbm", env.cpuid_ext3_features, 21, false),
    FEAT("f-topoext", env.cpuid_ext3_features, 22, false),
    FEAT("f-perfctr_core", env.cpuid_ext3_features, 23, false),
    FEAT("f-perfctr_nb", env.cpuid_ext3_features, 24, false),
    FEAT("f-xstore", env.cpuid_ext4_features, 2, false),
    FEAT("f-xstore-en", env.cpuid_ext4_features, 3, false),
    FEAT("f-xcrypt", env.cpuid_ext4_features, 6, false),
    FEAT("f-xcrypt-en", env.cpuid_ext4_features, 7, false),
    FEAT("f-ace2", env.cpuid_ext4_features, 8, false),
    FEAT("f-ace2-en", env.cpuid_ext4_features, 9, false),
    FEAT("f-phe", env.cpuid_ext4_features, 10, false),
    FEAT("f-phe-en", env.cpuid_ext4_features, 11, false),
    FEAT("f-pmm", env.cpuid_ext4_features, 12, false),
    FEAT("f-pmm-en", env.cpuid_ext4_features, 13, false),
#if defined(CONFIG_KVM)
    DEFINE_PROP_KVMCLOCK("f-kvmclock", env.cpuid_kvm_features),
    FEAT("f-kvmclock1", env.cpuid_kvm_features,  0, true),
    FEAT("f-kvm_nopiodelay", env.cpuid_kvm_features,  1, true),
    FEAT("f-kvm_mmu", env.cpuid_kvm_features,  2, false),
    FEAT("f-kvmclock2", env.cpuid_kvm_features,  3, true),
    FEAT("f-kvm_asyncpf", env.cpuid_kvm_features,  4, true),
    FEAT("f-kvm_steal_tm", env.cpuid_kvm_features,  5, true),
    FEAT("f-kvm_pv_eoi", env.cpuid_kvm_features,  6, true),
    FEAT("f-kvmclock_stable", env.cpuid_kvm_features,  24, true),
#endif
    FEAT("f-npt", env.cpuid_svm_features,  0, false),
    FEAT("f-lbrv", env.cpuid_svm_features,  1, false),
    FEAT("f-svm_lock", env.cpuid_svm_features,  2, false),
    FEAT("f-nrip_save", env.cpuid_svm_features,  3, false),
    FEAT("f-tsc_scale", env.cpuid_svm_features,  4, false),
    FEAT("f-vmcb_clean", env.cpuid_svm_features,  5, false),
    FEAT("f-flushbyasid", env.cpuid_svm_features,  6, false),
    FEAT("f-decodeassists", env.cpuid_svm_features,  7, false),
    FEAT("f-pause_filter", env.cpuid_svm_features, 10, false),
    FEAT("f-pfthreshold", env.cpuid_svm_features, 12, false),
    FEAT("f-fsgsbase", env.cpuid_7_0_ebx_features, 0, false),
    FEAT("f-bmi1", env.cpuid_7_0_ebx_features, 3, false),
    FEAT("f-hle", env.cpuid_7_0_ebx_features, 4, false),
    FEAT("f-avx2", env.cpuid_7_0_ebx_features, 5, false),
    FEAT("f-smep", env.cpuid_7_0_ebx_features, 7, false),
    FEAT("f-bmi2", env.cpuid_7_0_ebx_features, 8, false),
    FEAT("f-erms", env.cpuid_7_0_ebx_features, 9, false),
    FEAT("f-invpcid", env.cpuid_7_0_ebx_features, 10, false),
    FEAT("f-rtm", env.cpuid_7_0_ebx_features, 11, false),
    FEAT("f-rdseed", env.cpuid_7_0_ebx_features, 18, false),
    FEAT("f-adx", env.cpuid_7_0_ebx_features, 19, false),
    FEAT("f-smap", env.cpuid_7_0_ebx_features, 20, false),
    DEFINE_PROP_VENDOR("vendor", X86CPU, env.cpuid_vendor1),
    DEFINE_PROP_UINT32("xlevel", X86CPU, env.cpuid_xlevel, 0),
    DEFINE_PROP_UINT32("level", X86CPU, env.cpuid_level, 0),
    DEFINE_PROP_HV_SPINLOCKS("hv_spinlocks"),
    DEFINE_PROP_HV_RELAXED("hv_relaxed"),
    DEFINE_PROP_HV_VAPIC("hv_vapic"),
    DEFINE_PROP_CHECK("check"),
    DEFINE_PROP_ENFORCE("enforce"),
    DEFINE_PROP_TSC_FREQ("tsc-frequency", X86CPU, env.tsc_khz),
    DEFINE_PROP_END_OF_LIST(),
 };

/* collects per-function cpuid data
 */
typedef struct model_features_t {
    uint32_t *guest_feat;
    uint32_t *host_feat;
    FeatureWord feat_word;
} model_features_t;

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
    struct x86_def_t *next;
    const char *name;
    uint32_t level;
    char vendor[CPUID_VENDOR_SZ + 1];
    int family;
    int model;
    int stepping;
    uint32_t features, ext_features, ext2_features, ext3_features;
    uint32_t kvm_features, svm_features;
    uint32_t xlevel;
    char model_id[48];
    /* Store the results of Centaur's CPUID instructions */
    uint32_t ext4_features;
    uint32_t xlevel2;
    /* The feature bits on CPUID[EAX=7,ECX=0].EBX */
    uint32_t cpuid_7_0_ebx_features;
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
#define TCG_EXT_FEATURES (CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | \
          CPUID_EXT_SSSE3 | CPUID_EXT_CX16 | CPUID_EXT_POPCNT | \
          CPUID_EXT_HYPERVISOR)
          /* missing:
          CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_VMX, CPUID_EXT_EST,
          CPUID_EXT_TM2, CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_XSAVE */
#define TCG_EXT2_FEATURES ((TCG_FEATURES & CPUID_EXT2_AMD_ALIASES) | \
          CPUID_EXT2_NX | CPUID_EXT2_MMXEXT | CPUID_EXT2_RDTSCP | \
          CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT)
          /* missing:
          CPUID_EXT2_PDPE1GB */
#define TCG_EXT3_FEATURES (CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM | \
          CPUID_EXT3_CR8LEG | CPUID_EXT3_ABM | CPUID_EXT3_SSE4A)
#define TCG_SVM_FEATURES 0
#define TCG_7_0_EBX_FEATURES (CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_SMAP)

/* maintains list of cpu model definitions
 */
static x86_def_t *x86_defs = {NULL};

/* built-in cpu model definitions (deprecated)
 */
static x86_def_t builtin_x86_defs[] = {
    {
        .name = "qemu64",
        .level = 4,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_CX16 | CPUID_EXT_POPCNT,
        .ext2_features = (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .ext3_features = CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
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
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_HT,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_CX16 |
            CPUID_EXT_POPCNT,
        .ext2_features = (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX |
            CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_MMXEXT |
            CPUID_EXT2_FFXSR | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP,
        /* Missing: CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS */
        .ext3_features = CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        .svm_features = CPUID_SVM_NPT | CPUID_SVM_LBRV,
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
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_DTS | CPUID_ACPI | CPUID_SS |
            CPUID_HT | CPUID_TM | CPUID_PBE,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_DTES64 | CPUID_EXT_DSCPL | CPUID_EXT_VMX | CPUID_EXT_EST |
            CPUID_EXT_TM2 | CPUID_EXT_CX16 | CPUID_EXT_XTPR | CPUID_EXT_PDCM,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .ext3_features = CPUID_EXT3_LAHF_LM,
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
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        /* Missing: CPUID_EXT_POPCNT, CPUID_EXT_MONITOR */
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        /* Missing: CPUID_EXT2_PDPE1GB, CPUID_EXT2_RDTSCP */
        .ext2_features = (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        /* Missing: CPUID_EXT3_LAHF_LM, CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG, CPUID_EXT3_ABM, CPUID_EXT3_SSE4A,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS, CPUID_EXT3_SVM */
        .ext3_features = 0,
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
        .features = PPRO_FEATURES,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_POPCNT,
        .xlevel = 0x80000004,
    },
    {
        .name = "kvm32",
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_PSE36,
        .ext_features = CPUID_EXT_SSE3,
        .ext2_features = PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES,
        .ext3_features = 0,
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
        .features = PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_DTS | CPUID_ACPI |
            CPUID_SS | CPUID_HT | CPUID_TM | CPUID_PBE,
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_VMX |
            CPUID_EXT_EST | CPUID_EXT_TM2 | CPUID_EXT_XTPR | CPUID_EXT_PDCM,
        .ext2_features = CPUID_EXT2_NX,
        .xlevel = 0x80000008,
        .model_id = "Genuine Intel(R) CPU           T2600  @ 2.16GHz",
    },
    {
        .name = "486",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 4,
        .model = 0,
        .stepping = 0,
        .features = I486_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 5,
        .model = 4,
        .stepping = 3,
        .features = PENTIUM_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium2",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 5,
        .stepping = 2,
        .features = PENTIUM2_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "pentium3",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 7,
        .stepping = 3,
        .features = PENTIUM3_FEATURES,
        .xlevel = 0,
    },
    {
        .name = "athlon",
        .level = 2,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features = PPRO_FEATURES | CPUID_PSE36 | CPUID_VME | CPUID_MTRR |
            CPUID_MCA,
        .ext2_features = (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
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
        .features = PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_VME | CPUID_DTS |
            CPUID_ACPI | CPUID_SS | CPUID_HT | CPUID_TM | CPUID_PBE,
            /* Some CPUs got no CPUID_SEP */
        .ext_features = CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_DSCPL | CPUID_EXT_EST | CPUID_EXT_TM2 | CPUID_EXT_XTPR,
        .ext2_features = (PPRO_FEATURES & CPUID_EXT2_AMD_ALIASES) |
            CPUID_EXT2_NX,
        .ext3_features = CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel(R) Atom(TM) CPU N270   @ 1.60GHz",
    },
    {
        .name = "Conroe",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .ext3_features = CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Celeron_4x0 (Conroe/Merom Class Core 2)",
    },
    {
        .name = "Penryn",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
             CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .ext3_features = CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000000A,
        .model_id = "Intel Core 2 Duo P9xxx (Penryn Class Core 2)",
    },
    {
        .name = "Nehalem",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
             CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .ext3_features = CPUID_EXT3_LAHF_LM,
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
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
             CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
             CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .ext3_features = CPUID_EXT3_LAHF_LM,
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
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
             CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_POPCNT |
             CPUID_EXT_X2APIC | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
             CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
             CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
             CPUID_EXT2_SYSCALL,
        .ext3_features = CPUID_EXT3_LAHF_LM,
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
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
             CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
             CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
             CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
             CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
             CPUID_EXT_PCID,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
             CPUID_EXT2_SYSCALL,
        .ext3_features = CPUID_EXT3_LAHF_LM,
        .cpuid_7_0_ebx_features = CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
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
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_FXSR | CPUID_EXT2_MMX |
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
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_CX16 | CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_FXSR |
             CPUID_EXT2_MMX | CPUID_EXT2_NX | CPUID_EXT2_PSE36 |
             CPUID_EXT2_PAT | CPUID_EXT2_CMOV | CPUID_EXT2_MCA |
             CPUID_EXT2_PGE | CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL |
             CPUID_EXT2_APIC | CPUID_EXT2_CX8 | CPUID_EXT2_MCE |
             CPUID_EXT2_PAE | CPUID_EXT2_MSR | CPUID_EXT2_TSC | CPUID_EXT2_PSE |
             CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .ext3_features = CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
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
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_POPCNT | CPUID_EXT_CX16 | CPUID_EXT_MONITOR |
             CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_FXSR |
             CPUID_EXT2_MMX | CPUID_EXT2_NX | CPUID_EXT2_PSE36 |
             CPUID_EXT2_PAT | CPUID_EXT2_CMOV | CPUID_EXT2_MCA |
             CPUID_EXT2_PGE | CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL |
             CPUID_EXT2_APIC | CPUID_EXT2_CX8 | CPUID_EXT2_MCE |
             CPUID_EXT2_PAE | CPUID_EXT2_MSR | CPUID_EXT2_TSC | CPUID_EXT2_PSE |
             CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .ext3_features = CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A |
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
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
             CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
             CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
             CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_RDTSCP |
             CPUID_EXT2_PDPE1GB | CPUID_EXT2_FXSR | CPUID_EXT2_MMX |
             CPUID_EXT2_NX | CPUID_EXT2_PSE36 | CPUID_EXT2_PAT |
             CPUID_EXT2_CMOV | CPUID_EXT2_MCA | CPUID_EXT2_PGE |
             CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL | CPUID_EXT2_APIC |
             CPUID_EXT2_CX8 | CPUID_EXT2_MCE | CPUID_EXT2_PAE | CPUID_EXT2_MSR |
             CPUID_EXT2_TSC | CPUID_EXT2_PSE | CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .ext3_features = CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
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
        .features = CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
             CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
             CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
             CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
             CPUID_DE | CPUID_FP87,
        .ext_features = CPUID_EXT_F16C | CPUID_EXT_AVX | CPUID_EXT_XSAVE |
             CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
             CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_FMA |
             CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .ext2_features = CPUID_EXT2_LM | CPUID_EXT2_RDTSCP |
             CPUID_EXT2_PDPE1GB | CPUID_EXT2_FXSR | CPUID_EXT2_MMX |
             CPUID_EXT2_NX | CPUID_EXT2_PSE36 | CPUID_EXT2_PAT |
             CPUID_EXT2_CMOV | CPUID_EXT2_MCA | CPUID_EXT2_PGE |
             CPUID_EXT2_MTRR | CPUID_EXT2_SYSCALL | CPUID_EXT2_APIC |
             CPUID_EXT2_CX8 | CPUID_EXT2_MCE | CPUID_EXT2_PAE | CPUID_EXT2_MSR |
             CPUID_EXT2_TSC | CPUID_EXT2_PSE | CPUID_EXT2_DE | CPUID_EXT2_FPU,
        .ext3_features = CPUID_EXT3_TBM | CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
             CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
             CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
             CPUID_EXT3_LAHF_LM,
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 63xx class CPU",
    },
};

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
    x86_cpu_def->features = kvm_arch_get_supported_cpuid(s, 0x1, 0, R_EDX);
    x86_cpu_def->ext_features = kvm_arch_get_supported_cpuid(s, 0x1, 0, R_ECX);

    if (x86_cpu_def->level >= 7) {
        x86_cpu_def->cpuid_7_0_ebx_features =
                    kvm_arch_get_supported_cpuid(s, 0x7, 0, R_EBX);
    } else {
        x86_cpu_def->cpuid_7_0_ebx_features = 0;
    }

    x86_cpu_def->xlevel = kvm_arch_get_supported_cpuid(s, 0x80000000, 0, R_EAX);
    x86_cpu_def->ext2_features =
                kvm_arch_get_supported_cpuid(s, 0x80000001, 0, R_EDX);
    x86_cpu_def->ext3_features =
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
            x86_cpu_def->ext4_features =
                    kvm_arch_get_supported_cpuid(s, 0xC0000001, 0, R_EDX);
        }
    }

    /* Other KVM-specific feature fields: */
    x86_cpu_def->svm_features =
        kvm_arch_get_supported_cpuid(s, 0x8000000A, 0, R_EDX);
    x86_cpu_def->kvm_features =
        kvm_arch_get_supported_cpuid(s, KVM_CPUID_FEATURES, 0, R_EAX);

#endif /* CONFIG_KVM */
}

static int unavailable_host_feature(FeatureWordInfo *f, uint32_t mask)
{
    int i;

    for (i = 0; i < 32; ++i)
        if (1 << i & mask) {
            const char *reg = get_register_name_32(f->cpuid_reg);
            assert(reg);
            fprintf(stderr, "warning: host doesn't support requested feature: "
                "CPUID.%02XH:%s%s%s [bit %d]\n",
                f->cpuid_eax, reg,
                f->feat_names[i] ? "." : "",
                f->feat_names[i] ? f->feat_names[i] : "", i);
            break;
        }
    return 0;
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
        {&env->cpuid_features, &host_def.features,
            FEAT_1_EDX },
        {&env->cpuid_ext_features, &host_def.ext_features,
            FEAT_1_ECX },
        {&env->cpuid_ext2_features, &host_def.ext2_features,
            FEAT_8000_0001_EDX },
        {&env->cpuid_ext3_features, &host_def.ext3_features,
            FEAT_8000_0001_ECX },
        {&env->cpuid_ext4_features, &host_def.ext4_features,
            FEAT_C000_0001_EDX },
        {&env->cpuid_7_0_ebx_features, &host_def.cpuid_7_0_ebx_features,
            FEAT_7_0_EBX },
        {&env->cpuid_svm_features, &host_def.svm_features,
            FEAT_SVM },
        {&env->cpuid_kvm_features, &host_def.kvm_features,
            FEAT_KVM },
    };

    assert(kvm_enabled());

    kvm_cpu_fill_host(&host_def);
    for (rv = 0, i = 0; i < ARRAY_SIZE(ft); ++i) {
        FeatureWord w = ft[i].feat_word;
        FeatureWordInfo *wi = &feature_word_info[w];
        for (mask = 1; mask; mask <<= 1) {
            if (*ft[i].guest_feat & mask &&
                !(*ft[i].host_feat & mask)) {
                unavailable_host_feature(wi, mask);
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

static char *x86_cpuid_get_model_id(Object *obj, Error **errp)
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
    return value;
}

static void x86_cpuid_set_model_id(Object *obj, const char *model_id,
                                   Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int c, len, i;

    if (model_id == NULL) {
        model_id = "";
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
}

static int cpu_x86_find_by_name(x86_def_t *x86_cpu_def, const char *name)
{
    x86_def_t *def;

    for (def = x86_defs; def; def = def->next) {
        if (name && !strcmp(name, def->name)) {
            break;
        }
    }
    if (kvm_enabled() && name && strcmp(name, "host") == 0) {
        kvm_cpu_fill_host(x86_cpu_def);
    } else if (!def) {
        return -1;
    } else {
        memcpy(x86_cpu_def, def, sizeof(*def));
        /* sysenter isn't supported on compatibility mode on AMD, syscall
         * isn't supported in compatibility mode on Intel.
         * Normally we advertise the actual cpu vendor, but you can override
         * this using the 'vendor' property if you want to use KVM's
         * sysenter/syscall emulation in compatibility mode and when doing
         * cross vendor migration
         */
        if (kvm_enabled()) {
            uint32_t  ebx = 0, ecx = 0, edx = 0;
            host_cpuid(0, 0, NULL, &ebx, &ecx, &edx);
            x86_cpu_vendor_words2str(x86_cpu_def->vendor, ebx, edx, ecx);
        }
    }

    return 0;
}

/* Set features on X86CPU object based on a provided key,value list */
static void x86_cpu_set_props(X86CPU *cpu, QDict *features, Error **errp)
{
    const QDictEntry *ent;

    for (ent = qdict_first(features); ent; ent = qdict_next(features, ent)) {
        const QString *qval = qobject_to_qstring(qdict_entry_value(ent));
        /* TODO: switch to using global properties after subclasses are done */
        object_property_parse(OBJECT(cpu), qstring_get_str(qval),
                              qdict_entry_key(ent), errp);
        if (error_is_set(errp)) {
            return;
        }
    }
}

/* Parse "+feature,-feature,feature=foo" CPU feature string
 */
static int cpu_x86_parse_featurestr(x86_def_t *x86_cpu_def, char *features,
                                    QDict **props)
{
    char *featurestr; /* Single 'key=value" string being parsed */
    uint32_t numvalue;
    gchar **feat_array = g_strsplit(features ? features : "", ",", 0);
    *props = qdict_new();
    int j = 0;

    while ((featurestr = feat_array[j++])) {
        char *val;
        if (featurestr[0] == '+' || featurestr[0] == '-') {
            const gchar *feat = featurestr + 1;
            gchar *cpuid_fname = g_strconcat("f-", feat, NULL);

            if (qdev_prop_find(DEVICE_CLASS(object_class_by_name(TYPE_X86_CPU)),
                               cpuid_fname)) {
                feat = cpuid_fname;
            }

            if (featurestr[0] == '+') {
                /* preseve legacy behaviour, if feature was disabled once
                 * do not allow to enable it again */
                if (!qdict_haskey(*props, feat)) {
                    qdict_put(*props, feat, qstring_from_str("on"));
                }
            } else {
                qdict_put(*props, feat, qstring_from_str("off"));
            }
            g_free(cpuid_fname);
        } else if ((val = strchr(featurestr, '='))) {
            *val = 0; val++;
            if (!strcmp(featurestr, "family")) {
                qdict_put(*props, featurestr, qstring_from_str(val));
            } else if (!strcmp(featurestr, "model")) {
                qdict_put(*props, featurestr, qstring_from_str(val));
            } else if (!strcmp(featurestr, "stepping")) {
                qdict_put(*props, featurestr, qstring_from_str(val));
            } else if (!strcmp(featurestr, "level")) {
                qdict_put(*props, featurestr, qstring_from_str(val));
            } else if (!strcmp(featurestr, "xlevel")) {
                char *err;
                QString *s;
                numvalue = strtoul(val, &err, 0);
                if (!*val || *err) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                if (numvalue < 0x80000000) {
                    fprintf(stderr, "xlevel value shall always be >= 0x80000000"
                            ", fixup will be deprecated in future versions\n");
                    numvalue += 0x80000000;
                }
                s = qstring_new();
                qstring_append_int(s, numvalue);
                qdict_put(*props, featurestr, s);
            } else if (!strcmp(featurestr, "vendor")) {
                qdict_put(*props, featurestr, qstring_from_str(val));
            } else if (!strcmp(featurestr, "model_id")) {
                qdict_put(*props, "model-id", qstring_from_str(val));
            } else if (!strcmp(featurestr, "tsc_freq")) {
                int64_t tsc_freq;
                char *err;
                QString *s;

                tsc_freq = strtosz_suffix_unit(val, &err,
                                               STRTOSZ_DEFSUFFIX_B, 1000);
                if (tsc_freq < 0 || *err) {
                    fprintf(stderr, "bad numerical value %s\n", val);
                    goto error;
                }
                s = qstring_new();
                qstring_append_int(s, tsc_freq);
                qdict_put(*props, "tsc-frequency", s);
            } else if (!strcmp(featurestr, "hv_spinlocks")) {
                qdict_put(*props, featurestr, qstring_from_str(val));
            } else {
                fprintf(stderr, "unrecognized feature %s\n", featurestr);
                goto error;
            }
        } else if (!strcmp(featurestr, "check")) {
            qdict_put(*props, featurestr, qstring_from_str("on"));
        } else if (!strcmp(featurestr, "enforce")) {
            qdict_put(*props, featurestr, qstring_from_str("on"));
            qdict_put(*props, "check", qstring_from_str("on"));
        } else if (!strcmp(featurestr, "hv_relaxed")) {
            qdict_put(*props, featurestr, qstring_from_str("on"));
        } else if (!strcmp(featurestr, "hv_vapic")) {
            qdict_put(*props, featurestr, qstring_from_str("on"));
        } else {
            fprintf(stderr, "feature string `%s' not in format (+feature|-feature|feature=xyz)\n", featurestr);
            goto error;
        }
    }
    g_strfreev(feat_array);
    return 0;

error:
    g_strfreev(feat_array);
    return -1;
}

/* generate a composite string into buf of all cpuid names in featureset
 * selected by fbits.  indicate truncation at bufsize in the event of overflow.
 * if flags, suppress names undefined in featureset.
 */
static void listflags(char *buf, int bufsize, uint32_t fbits,
    const char **featureset, uint32_t flags)
{
    const char **p = &featureset[31];
    char *q, *b, bit;
    int nc;

    b = 4 <= bufsize ? buf + (bufsize -= 3) - 1 : NULL;
    *buf = '\0';
    for (q = buf, bit = 31; fbits && bufsize; --p, fbits &= ~(1 << bit), --bit)
        if (fbits & 1 << bit && (*p || !flags)) {
            if (*p)
                nc = snprintf(q, bufsize, "%s%s", q == buf ? "" : " ", *p);
            else
                nc = snprintf(q, bufsize, "%s[%d]", q == buf ? "" : " ", bit);
            if (bufsize <= nc) {
                if (b) {
                    memcpy(b, "...", sizeof("..."));
                }
                return;
            }
            q += nc;
            bufsize -= nc;
        }
}

/* generate CPU information. */
void x86_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    x86_def_t *def;
    char buf[256];

    for (def = x86_defs; def; def = def->next) {
        snprintf(buf, sizeof(buf), "%s", def->name);
        (*cpu_fprintf)(f, "x86 %16s  %-48s\n", buf, def->model_id);
    }
    if (kvm_enabled()) {
        (*cpu_fprintf)(f, "x86 %16s\n", "[host]");
    }
    (*cpu_fprintf)(f, "\nRecognized CPUID flags:\n");
    listflags(buf, sizeof(buf), (uint32_t)~0, feature_name, 1);
    (*cpu_fprintf)(f, "  %s\n", buf);
    listflags(buf, sizeof(buf), (uint32_t)~0, ext_feature_name, 1);
    (*cpu_fprintf)(f, "  %s\n", buf);
    listflags(buf, sizeof(buf), (uint32_t)~0, ext2_feature_name, 1);
    (*cpu_fprintf)(f, "  %s\n", buf);
    listflags(buf, sizeof(buf), (uint32_t)~0, ext3_feature_name, 1);
    (*cpu_fprintf)(f, "  %s\n", buf);
}

CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    x86_def_t *def;

    for (def = x86_defs; def; def = def->next) {
        CpuDefinitionInfoList *entry;
        CpuDefinitionInfo *info;

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

    env->cpuid_features &=
        kvm_arch_get_supported_cpuid(s, 1, 0, R_EDX);
    env->cpuid_ext_features &=
        kvm_arch_get_supported_cpuid(s, 1, 0, R_ECX);
    env->cpuid_ext2_features &=
        kvm_arch_get_supported_cpuid(s, 0x80000001, 0, R_EDX);
    env->cpuid_ext3_features &=
        kvm_arch_get_supported_cpuid(s, 0x80000001, 0, R_ECX);
    env->cpuid_svm_features  &=
        kvm_arch_get_supported_cpuid(s, 0x8000000A, 0, R_EDX);
    env->cpuid_7_0_ebx_features &=
        kvm_arch_get_supported_cpuid(s, 7, 0, R_EBX);
    env->cpuid_kvm_features &=
        kvm_arch_get_supported_cpuid(s, KVM_CPUID_FEATURES, 0, R_EAX);
    env->cpuid_ext4_features &=
        kvm_arch_get_supported_cpuid(s, 0xC0000001, 0, R_EDX);

}
#endif

int cpu_x86_register(X86CPU *cpu, const char *cpu_model)
{
    CPUX86State *env = &cpu->env;
    x86_def_t def1, *def = &def1;
    QDict *props = NULL;
    Error *error = NULL;
    char *name, *features;
    gchar **model_pieces;

    memset(def, 0, sizeof(*def));

    model_pieces = g_strsplit(cpu_model, ",", 2);
    if (!model_pieces[0]) {
        error_setg(&error, "Invalid/empty CPU model name");
        goto out;
    }
    name = model_pieces[0];
    features = model_pieces[1];

    if (cpu_x86_find_by_name(def, name) < 0) {
        error_setg(&error, "Unable to find CPU definition: %s", name);
        goto out;
    }

    def->kvm_features |= cpu->env.cpuid_kvm_features;
    def->ext_features |= cpu->env.cpuid_ext_features & CPUID_EXT_HYPERVISOR;

    if (cpu_x86_parse_featurestr(def, features, &props) < 0) {
        error_setg(&error, "Invalid cpu_model string format: %s", cpu_model);
        goto out;
    }
    assert(def->vendor[0]);
    object_property_set_str(OBJECT(cpu), def->vendor, "vendor", &error);
    object_property_set_int(OBJECT(cpu), def->level, "level", &error);
    object_property_set_int(OBJECT(cpu), def->family, "family", &error);
    object_property_set_int(OBJECT(cpu), def->model, "model", &error);
    object_property_set_int(OBJECT(cpu), def->stepping, "stepping", &error);
    env->cpuid_features = def->features;
    env->cpuid_ext_features = def->ext_features;
    env->cpuid_ext2_features = def->ext2_features;
    env->cpuid_ext3_features = def->ext3_features;
    object_property_set_int(OBJECT(cpu), def->xlevel, "xlevel", &error);
    env->cpuid_kvm_features = def->kvm_features;
    env->cpuid_svm_features = def->svm_features;
    env->cpuid_ext4_features = def->ext4_features;
    env->cpuid_7_0_ebx_features = def->cpuid_7_0_ebx_features;
    env->cpuid_xlevel2 = def->xlevel2;

    object_property_set_str(OBJECT(cpu), def->model_id, "model-id", &error);
    x86_cpu_set_props(cpu, props, &error);

out:
    QDECREF(props);
    g_strfreev(model_pieces);
    if (error) {
        fprintf(stderr, "%s\n", error_get_pretty(error));
        error_free(error);
        return -1;
    }
    return 0;
}

#if !defined(CONFIG_USER_ONLY)

void cpu_clear_apic_feature(CPUX86State *env)
{
    env->cpuid_features &= ~CPUID_APIC;
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
        def->next = x86_defs;

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

        x86_defs = def;
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
        *ecx = env->cpuid_ext_features;
        *edx = env->cpuid_features;
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
            *ebx = env->cpuid_7_0_ebx_features; /* Feature flags */
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
        if (!(env->cpuid_ext_features & CPUID_EXT_XSAVE)) {
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
        *ecx = env->cpuid_ext3_features;
        *edx = env->cpuid_ext2_features;

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
        if (env->cpuid_ext2_features & CPUID_EXT2_LM) {
            /* 64 bit processor */
/* XXX: The physical address space is limited to 42 bits in exec.c. */
            *eax = 0x00003028;	/* 48 bits virtual, 40 bits physical */
        } else {
            if (env->cpuid_features & CPUID_PSE36)
                *eax = 0x00000024; /* 36 bits physical */
            else
                *eax = 0x00000020; /* 32 bits physical */
        }
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (cs->nr_cores * cs->nr_threads > 1) {
            *ecx |= (cs->nr_cores * cs->nr_threads) - 1;
        }
        break;
    case 0x8000000A:
        if (env->cpuid_ext3_features & CPUID_EXT3_SVM) {
            *eax = 0x00000001; /* SVM Revision */
            *ebx = 0x00000010; /* nr of ASIDs */
            *ecx = 0;
            *edx = env->cpuid_svm_features; /* optional features */
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
        *edx = env->cpuid_ext4_features;
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

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", s->cpu_index);
        log_cpu_state(env, CPU_DUMP_FPU | CPU_DUMP_CCOP);
    }

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

    env->halted = !cpu_is_bsp(cpu);
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
        && (cenv->cpuid_features & (CPUID_MCE | CPUID_MCA)) ==
            (CPUID_MCE | CPUID_MCA)) {
        cenv->mcg_cap = MCE_CAP_DEF | MCE_BANKS_DEF;
        cenv->mcg_ctl = ~(uint64_t)0;
        for (bank = 0; bank < MCE_BANKS_DEF; bank++) {
            cenv->mce_banks[bank * 4] = ~(uint64_t)0;
        }
    }
}

#define MSI_ADDR_BASE 0xfee00000

#ifndef CONFIG_USER_ONLY
static void x86_cpu_apic_init(X86CPU *cpu, Error **errp)
{
    static int apic_mapped;
    CPUX86State *env = &cpu->env;
    APICCommonState *apic;
    const char *apic_type = "apic";

    if (kvm_irqchip_in_kernel()) {
        apic_type = "kvm-apic";
    } else if (xen_enabled()) {
        apic_type = "xen-apic";
    }

    env->apic_state = qdev_try_create(NULL, apic_type);
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

    if (qdev_init(env->apic_state)) {
        error_setg(errp, "APIC device '%s' could not be initialized",
                   object_get_typename(OBJECT(env->apic_state)));
        return;
    }

    /* XXX: mapping more APICs at the same memory location */
    if (apic_mapped == 0) {
        /* NOTE: the APIC is directly connected to the CPU - it is not
           on the global memory bus. */
        /* XXX: what if the base changes? */
        sysbus_mmio_map(sysbus_from_qdev(env->apic_state), 0, MSI_ADDR_BASE);
        apic_mapped = 1;
    }
}
#endif

void x86_cpu_realize(Object *obj, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;

    if (env->cpuid_7_0_ebx_features && env->cpuid_level < 7) {
        env->cpuid_level = 7;
    }

    /* On AMD CPUs, some CPUID[8000_0001].EDX bits must match the bits on
     * CPUID[1].EDX.
     */
    if (env->cpuid_vendor1 == CPUID_VENDOR_AMD_1 &&
        env->cpuid_vendor2 == CPUID_VENDOR_AMD_2 &&
        env->cpuid_vendor3 == CPUID_VENDOR_AMD_3) {
        env->cpuid_ext2_features &= ~CPUID_EXT2_AMD_ALIASES;
        env->cpuid_ext2_features |= (env->cpuid_features
           & CPUID_EXT2_AMD_ALIASES);
    }

    if (!kvm_enabled()) {
        env->cpuid_features &= TCG_FEATURES;
        env->cpuid_ext_features &= TCG_EXT_FEATURES;
        env->cpuid_ext2_features &= (TCG_EXT2_FEATURES
#ifdef TARGET_X86_64
            | CPUID_EXT2_SYSCALL | CPUID_EXT2_LM
#endif
            );
        env->cpuid_ext3_features &= TCG_EXT3_FEATURES;
        env->cpuid_svm_features &= TCG_SVM_FEATURES;
    } else {
#ifdef CONFIG_KVM
        filter_features_for_kvm(cpu);
#endif
        if (check_cpuid && kvm_check_features_against_host(cpu)
            && enforce_cpuid) {
            error_setg(errp, "Host's CPU doesn't support requested features");
            return;
        }
    }

#ifndef CONFIG_USER_ONLY
    qemu_register_reset(x86_cpu_machine_reset_cb, cpu);

    if (cpu->env.cpuid_features & CPUID_APIC || smp_cpus > 1) {
        x86_cpu_apic_init(cpu, errp);
        if (error_is_set(errp)) {
            return;
        }
    }
#endif

    mce_init(cpu);
    qemu_init_vcpu(&cpu->env);
    cpu_reset(CPU(cpu));
}

static void x86_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    static int inited;

    cpu_exec_init(env);

    object_property_add(obj, "family", "int",
                        x86_cpuid_version_get_family,
                        x86_cpuid_version_set_family, NULL, NULL, NULL);
    object_property_add(obj, "model", "int",
                        x86_cpuid_version_get_model,
                        x86_cpuid_version_set_model, NULL, NULL, NULL);
    object_property_add(obj, "stepping", "int",
                        x86_cpuid_version_get_stepping,
                        x86_cpuid_version_set_stepping, NULL, NULL, NULL);
    object_property_add_str(obj, "model-id",
                            x86_cpuid_get_model_id,
                            x86_cpuid_set_model_id, NULL);

    env->cpuid_apic_id = cs->cpu_index;

    /* init various static tables used in TCG mode */
    if (tcg_enabled() && !inited) {
        inited = 1;
        optimize_flags_init();
#ifndef CONFIG_USER_ONLY
        cpu_set_debug_excp_handler(breakpoint_handler);
#endif
    }
}

static void x86_cpu_common_class_init(ObjectClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    xcc->parent_reset = cc->reset;
    cc->reset = x86_cpu_reset;
    dc->props = cpu_x86_properties;
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
