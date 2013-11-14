/*
 * ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "sysemu/sysemu.h"
#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/acpi/acpi.h"
#include "monitor/monitor.h"
#include "qemu/config-file.h"
#include "qapi/opts-visitor.h"
#include "qapi/dealloc-visitor.h"
#include "qapi-visit.h"

struct acpi_table_header {
    uint16_t _length;         /* our length, not actual part of the hdr */
                              /* allows easier parsing for fw_cfg clients */
    char sig[4];              /* ACPI signature (4 ASCII characters) */
    uint32_t length;          /* Length of table, in bytes, including header */
    uint8_t revision;         /* ACPI Specification minor version # */
    uint8_t checksum;         /* To make sum of entire table == 0 */
    char oem_id[6];           /* OEM identification */
    char oem_table_id[8];     /* OEM table identification */
    uint32_t oem_revision;    /* OEM revision number */
    char asl_compiler_id[4];  /* ASL compiler vendor ID */
    uint32_t asl_compiler_revision; /* ASL compiler revision number */
} QEMU_PACKED;

#define ACPI_TABLE_HDR_SIZE sizeof(struct acpi_table_header)
#define ACPI_TABLE_PFX_SIZE sizeof(uint16_t)  /* size of the extra prefix */

static const char unsigned dfl_hdr[ACPI_TABLE_HDR_SIZE - ACPI_TABLE_PFX_SIZE] =
    "QEMU\0\0\0\0\1\0"       /* sig (4), len(4), revno (1), csum (1) */
    "QEMUQEQEMUQEMU\1\0\0\0" /* OEM id (6), table (8), revno (4) */
    "QEMU\1\0\0\0"           /* ASL compiler ID (4), version (4) */
    ;

char unsigned *acpi_tables;
size_t acpi_tables_len;

static QemuOptsList qemu_acpi_opts = {
    .name = "acpi",
    .implied_opt_name = "data",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_acpi_opts.head),
    .desc = { { 0 } } /* validated with OptsVisitor */
};

static void acpi_register_config(void)
{
    qemu_add_opts(&qemu_acpi_opts);
}

machine_init(acpi_register_config);

static int acpi_checksum(const uint8_t *data, int len)
{
    int sum, i;
    sum = 0;
    for (i = 0; i < len; i++) {
        sum += data[i];
    }
    return (-sum) & 0xff;
}


/* Install a copy of the ACPI table specified in @blob.
 *
 * If @has_header is set, @blob starts with the System Description Table Header
 * structure. Otherwise, "dfl_hdr" is prepended. In any case, each header field
 * is optionally overwritten from @hdrs.
 *
 * It is valid to call this function with
 * (@blob == NULL && bloblen == 0 && !has_header).
 *
 * @hdrs->file and @hdrs->data are ignored.
 *
 * SIZE_MAX is considered "infinity" in this function.
 *
 * The number of tables that can be installed is not limited, but the 16-bit
 * counter at the beginning of "acpi_tables" wraps around after UINT16_MAX.
 */
static void acpi_table_install(const char unsigned *blob, size_t bloblen,
                               bool has_header,
                               const struct AcpiTableOptions *hdrs,
                               Error **errp)
{
    size_t body_start;
    const char unsigned *hdr_src;
    size_t body_size, acpi_payload_size;
    struct acpi_table_header *ext_hdr;
    unsigned changed_fields;

    /* Calculate where the ACPI table body starts within the blob, plus where
     * to copy the ACPI table header from.
     */
    if (has_header) {
        /*   _length             | ACPI header in blob | blob body
         *   ^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^
         *   ACPI_TABLE_PFX_SIZE     sizeof dfl_hdr      body_size
         *                           == body_start
         *
         *                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
         *                           acpi_payload_size == bloblen
         */
        body_start = sizeof dfl_hdr;

        if (bloblen < body_start) {
            error_setg(errp, "ACPI table claiming to have header is too "
                       "short, available: %zu, expected: %zu", bloblen,
                       body_start);
            return;
        }
        hdr_src = blob;
    } else {
        /*   _length             | ACPI header in template | blob body
         *   ^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^^^^^^^   ^^^^^^^^^^
         *   ACPI_TABLE_PFX_SIZE       sizeof dfl_hdr        body_size
         *                                                   == bloblen
         *
         *                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
         *                                  acpi_payload_size
         */
        body_start = 0;
        hdr_src = dfl_hdr;
    }
    body_size = bloblen - body_start;
    acpi_payload_size = sizeof dfl_hdr + body_size;

    if (acpi_payload_size > UINT16_MAX) {
        error_setg(errp, "ACPI table too big, requested: %zu, max: %u",
                   acpi_payload_size, (unsigned)UINT16_MAX);
        return;
    }

    /* We won't fail from here on. Initialize / extend the globals. */
    if (acpi_tables == NULL) {
        acpi_tables_len = sizeof(uint16_t);
        acpi_tables = g_malloc0(acpi_tables_len);
    }

    acpi_tables = g_realloc(acpi_tables, acpi_tables_len +
                                         ACPI_TABLE_PFX_SIZE +
                                         sizeof dfl_hdr + body_size);

    ext_hdr = (struct acpi_table_header *)(acpi_tables + acpi_tables_len);
    acpi_tables_len += ACPI_TABLE_PFX_SIZE;

    memcpy(acpi_tables + acpi_tables_len, hdr_src, sizeof dfl_hdr);
    acpi_tables_len += sizeof dfl_hdr;

    if (blob != NULL) {
        memcpy(acpi_tables + acpi_tables_len, blob + body_start, body_size);
        acpi_tables_len += body_size;
    }

    /* increase number of tables */
    stw_le_p(acpi_tables, lduw_le_p(acpi_tables) + 1u);

    /* Update the header fields. The strings need not be NUL-terminated. */
    changed_fields = 0;
    ext_hdr->_length = cpu_to_le16(acpi_payload_size);

    if (hdrs->has_sig) {
        strncpy(ext_hdr->sig, hdrs->sig, sizeof ext_hdr->sig);
        ++changed_fields;
    }

    if (has_header && le32_to_cpu(ext_hdr->length) != acpi_payload_size) {
        fprintf(stderr,
                "warning: ACPI table has wrong length, header says "
                "%" PRIu32 ", actual size %zu bytes\n",
                le32_to_cpu(ext_hdr->length), acpi_payload_size);
    }
    ext_hdr->length = cpu_to_le32(acpi_payload_size);

    if (hdrs->has_rev) {
        ext_hdr->revision = hdrs->rev;
        ++changed_fields;
    }

    ext_hdr->checksum = 0;

    if (hdrs->has_oem_id) {
        strncpy(ext_hdr->oem_id, hdrs->oem_id, sizeof ext_hdr->oem_id);
        ++changed_fields;
    }
    if (hdrs->has_oem_table_id) {
        strncpy(ext_hdr->oem_table_id, hdrs->oem_table_id,
                sizeof ext_hdr->oem_table_id);
        ++changed_fields;
    }
    if (hdrs->has_oem_rev) {
        ext_hdr->oem_revision = cpu_to_le32(hdrs->oem_rev);
        ++changed_fields;
    }
    if (hdrs->has_asl_compiler_id) {
        strncpy(ext_hdr->asl_compiler_id, hdrs->asl_compiler_id,
                sizeof ext_hdr->asl_compiler_id);
        ++changed_fields;
    }
    if (hdrs->has_asl_compiler_rev) {
        ext_hdr->asl_compiler_revision = cpu_to_le32(hdrs->asl_compiler_rev);
        ++changed_fields;
    }

    if (!has_header && changed_fields == 0) {
        fprintf(stderr, "warning: ACPI table: no headers are specified\n");
    }

    /* recalculate checksum */
    ext_hdr->checksum = acpi_checksum((const char unsigned *)ext_hdr +
                                      ACPI_TABLE_PFX_SIZE, acpi_payload_size);
}

void acpi_table_add(const QemuOpts *opts, Error **errp)
{
    AcpiTableOptions *hdrs = NULL;
    Error *err = NULL;
    char **pathnames = NULL;
    char **cur;
    size_t bloblen = 0;
    char unsigned *blob = NULL;

    {
        OptsVisitor *ov;

        ov = opts_visitor_new(opts);
        visit_type_AcpiTableOptions(opts_get_visitor(ov), &hdrs, NULL, &err);
        opts_visitor_cleanup(ov);
    }

    if (err) {
        goto out;
    }
    if (hdrs->has_file == hdrs->has_data) {
        error_setg(&err, "'-acpitable' requires one of 'data' or 'file'");
        goto out;
    }

    pathnames = g_strsplit(hdrs->has_file ? hdrs->file : hdrs->data, ":", 0);
    if (pathnames == NULL || pathnames[0] == NULL) {
        error_setg(&err, "'-acpitable' requires at least one pathname");
        goto out;
    }

    /* now read in the data files, reallocating buffer as needed */
    for (cur = pathnames; *cur; ++cur) {
        int fd = open(*cur, O_RDONLY | O_BINARY);

        if (fd < 0) {
            error_setg(&err, "can't open file %s: %s", *cur, strerror(errno));
            goto out;
        }

        for (;;) {
            char unsigned data[8192];
            ssize_t r;

            r = read(fd, data, sizeof data);
            if (r == 0) {
                break;
            } else if (r > 0) {
                blob = g_realloc(blob, bloblen + r);
                memcpy(blob + bloblen, data, r);
                bloblen += r;
            } else if (errno != EINTR) {
                error_setg(&err, "can't read file %s: %s",
                           *cur, strerror(errno));
                close(fd);
                goto out;
            }
        }

        close(fd);
    }

    acpi_table_install(blob, bloblen, hdrs->has_file, hdrs, &err);

out:
    g_free(blob);
    g_strfreev(pathnames);

    if (hdrs != NULL) {
        QapiDeallocVisitor *dv;

        dv = qapi_dealloc_visitor_new();
        visit_type_AcpiTableOptions(qapi_dealloc_get_visitor(dv), &hdrs, NULL,
                                    NULL);
        qapi_dealloc_visitor_cleanup(dv);
    }

    error_propagate(errp, err);
}

static bool acpi_table_builtin = false;

void acpi_table_add_builtin(const QemuOpts *opts, Error **errp)
{
    acpi_table_builtin = true;
    acpi_table_add(opts, errp);
}

unsigned acpi_table_len(void *current)
{
    struct acpi_table_header *hdr = current - sizeof(hdr->_length);
    return hdr->_length;
}

static
void *acpi_table_hdr(void *h)
{
    struct acpi_table_header *hdr = h;
    return &hdr->sig;
}

uint8_t *acpi_table_first(void)
{
    if (acpi_table_builtin || !acpi_tables) {
        return NULL;
    }
    return acpi_table_hdr(acpi_tables + ACPI_TABLE_PFX_SIZE);
}

uint8_t *acpi_table_next(uint8_t *current)
{
    uint8_t *next = current + acpi_table_len(current);

    if (next - acpi_tables >= acpi_tables_len) {
        return NULL;
    } else {
        return acpi_table_hdr(next);
    }
}

static void acpi_notify_wakeup(Notifier *notifier, void *data)
{
    ACPIREGS *ar = container_of(notifier, ACPIREGS, wakeup);
    WakeupReason *reason = data;

    switch (*reason) {
    case QEMU_WAKEUP_REASON_RTC:
        ar->pm1.evt.sts |=
            (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_RT_CLOCK_STATUS);
        break;
    case QEMU_WAKEUP_REASON_PMTIMER:
        ar->pm1.evt.sts |=
            (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_TIMER_STATUS);
        break;
    case QEMU_WAKEUP_REASON_OTHER:
        /* ACPI_BITMASK_WAKE_STATUS should be set on resume.
           Pretend that resume was caused by power button */
        ar->pm1.evt.sts |=
            (ACPI_BITMASK_WAKE_STATUS | ACPI_BITMASK_POWER_BUTTON_STATUS);
        break;
    default:
        break;
    }
}

/* ACPI PM1a EVT */
uint16_t acpi_pm1_evt_get_sts(ACPIREGS *ar)
{
    int64_t d = acpi_pm_tmr_get_clock();
    if (d >= ar->tmr.overflow_time) {
        ar->pm1.evt.sts |= ACPI_BITMASK_TIMER_STATUS;
    }
    return ar->pm1.evt.sts;
}

static void acpi_pm1_evt_write_sts(ACPIREGS *ar, uint16_t val)
{
    uint16_t pm1_sts = acpi_pm1_evt_get_sts(ar);
    if (pm1_sts & val & ACPI_BITMASK_TIMER_STATUS) {
        /* if TMRSTS is reset, then compute the new overflow time */
        acpi_pm_tmr_calc_overflow_time(ar);
    }
    ar->pm1.evt.sts &= ~val;
}

static void acpi_pm1_evt_write_en(ACPIREGS *ar, uint16_t val)
{
    ar->pm1.evt.en = val;
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_RTC,
                              val & ACPI_BITMASK_RT_CLOCK_ENABLE);
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_PMTIMER,
                              val & ACPI_BITMASK_TIMER_ENABLE);
}

void acpi_pm1_evt_power_down(ACPIREGS *ar)
{
    if (ar->pm1.evt.en & ACPI_BITMASK_POWER_BUTTON_ENABLE) {
        ar->pm1.evt.sts |= ACPI_BITMASK_POWER_BUTTON_STATUS;
        ar->tmr.update_sci(ar);
    }
}

void acpi_pm1_evt_reset(ACPIREGS *ar)
{
    ar->pm1.evt.sts = 0;
    ar->pm1.evt.en = 0;
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_RTC, 0);
    qemu_system_wakeup_enable(QEMU_WAKEUP_REASON_PMTIMER, 0);
}

static uint64_t acpi_pm_evt_read(void *opaque, hwaddr addr, unsigned width)
{
    ACPIREGS *ar = opaque;
    switch (addr) {
    case 0:
        return acpi_pm1_evt_get_sts(ar);
    case 2:
        return ar->pm1.evt.en;
    default:
        return 0;
    }
}

static void acpi_pm_evt_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    ACPIREGS *ar = opaque;
    switch (addr) {
    case 0:
        acpi_pm1_evt_write_sts(ar, val);
        ar->pm1.evt.update_sci(ar);
        break;
    case 2:
        acpi_pm1_evt_write_en(ar, val);
        ar->pm1.evt.update_sci(ar);
        break;
    }
}

static const MemoryRegionOps acpi_pm_evt_ops = {
    .read = acpi_pm_evt_read,
    .write = acpi_pm_evt_write,
    .valid.min_access_size = 2,
    .valid.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void acpi_pm1_evt_init(ACPIREGS *ar, acpi_update_sci_fn update_sci,
                       MemoryRegion *parent)
{
    ar->pm1.evt.update_sci = update_sci;
    memory_region_init_io(&ar->pm1.evt.io, memory_region_owner(parent),
                          &acpi_pm_evt_ops, ar, "acpi-evt", 4);
    memory_region_add_subregion(parent, 0, &ar->pm1.evt.io);
}

/* ACPI PM_TMR */
void acpi_pm_tmr_update(ACPIREGS *ar, bool enable)
{
    int64_t expire_time;

    /* schedule a timer interruption if needed */
    if (enable) {
        expire_time = muldiv64(ar->tmr.overflow_time, get_ticks_per_sec(),
                               PM_TIMER_FREQUENCY);
        timer_mod(ar->tmr.timer, expire_time);
    } else {
        timer_del(ar->tmr.timer);
    }
}

void acpi_pm_tmr_calc_overflow_time(ACPIREGS *ar)
{
    int64_t d = acpi_pm_tmr_get_clock();
    ar->tmr.overflow_time = (d + 0x800000LL) & ~0x7fffffLL;
}

static uint32_t acpi_pm_tmr_get(ACPIREGS *ar)
{
    uint32_t d = acpi_pm_tmr_get_clock();
    return d & 0xffffff;
}

static void acpi_pm_tmr_timer(void *opaque)
{
    ACPIREGS *ar = opaque;
    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_PMTIMER);
    ar->tmr.update_sci(ar);
}

static uint64_t acpi_pm_tmr_read(void *opaque, hwaddr addr, unsigned width)
{
    return acpi_pm_tmr_get(opaque);
}

static void acpi_pm_tmr_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    /* nothing */
}

static const MemoryRegionOps acpi_pm_tmr_ops = {
    .read = acpi_pm_tmr_read,
    .write = acpi_pm_tmr_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void acpi_pm_tmr_init(ACPIREGS *ar, acpi_update_sci_fn update_sci,
                      MemoryRegion *parent)
{
    ar->tmr.update_sci = update_sci;
    ar->tmr.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, acpi_pm_tmr_timer, ar);
    memory_region_init_io(&ar->tmr.io, memory_region_owner(parent),
                          &acpi_pm_tmr_ops, ar, "acpi-tmr", 4);
    memory_region_add_subregion(parent, 8, &ar->tmr.io);
}

void acpi_pm_tmr_reset(ACPIREGS *ar)
{
    ar->tmr.overflow_time = 0;
    timer_del(ar->tmr.timer);
}

/* ACPI PM1aCNT */
static void acpi_pm1_cnt_write(ACPIREGS *ar, uint16_t val)
{
    ar->pm1.cnt.cnt = val & ~(ACPI_BITMASK_SLEEP_ENABLE);

    if (val & ACPI_BITMASK_SLEEP_ENABLE) {
        /* change suspend type */
        uint16_t sus_typ = (val >> 10) & 7;
        switch(sus_typ) {
        case 0: /* soft power off */
            qemu_system_shutdown_request();
            break;
        case 1:
            qemu_system_suspend_request();
            break;
        default:
            if (sus_typ == ar->pm1.cnt.s4_val) { /* S4 request */
                monitor_protocol_event(QEVENT_SUSPEND_DISK, NULL);
                qemu_system_shutdown_request();
            }
            break;
        }
    }
}

void acpi_pm1_cnt_update(ACPIREGS *ar,
                         bool sci_enable, bool sci_disable)
{
    /* ACPI specs 3.0, 4.7.2.5 */
    if (sci_enable) {
        ar->pm1.cnt.cnt |= ACPI_BITMASK_SCI_ENABLE;
    } else if (sci_disable) {
        ar->pm1.cnt.cnt &= ~ACPI_BITMASK_SCI_ENABLE;
    }
}

static uint64_t acpi_pm_cnt_read(void *opaque, hwaddr addr, unsigned width)
{
    ACPIREGS *ar = opaque;
    return ar->pm1.cnt.cnt;
}

static void acpi_pm_cnt_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned width)
{
    acpi_pm1_cnt_write(opaque, val);
}

static const MemoryRegionOps acpi_pm_cnt_ops = {
    .read = acpi_pm_cnt_read,
    .write = acpi_pm_cnt_write,
    .valid.min_access_size = 2,
    .valid.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void acpi_pm1_cnt_init(ACPIREGS *ar, MemoryRegion *parent, uint8_t s4_val)
{
    ar->pm1.cnt.s4_val = s4_val;
    ar->wakeup.notify = acpi_notify_wakeup;
    qemu_register_wakeup_notifier(&ar->wakeup);
    memory_region_init_io(&ar->pm1.cnt.io, memory_region_owner(parent),
                          &acpi_pm_cnt_ops, ar, "acpi-cnt", 2);
    memory_region_add_subregion(parent, 4, &ar->pm1.cnt.io);
}

void acpi_pm1_cnt_reset(ACPIREGS *ar)
{
    ar->pm1.cnt.cnt = 0;
}

/* ACPI GPE */
void acpi_gpe_init(ACPIREGS *ar, uint8_t len)
{
    ar->gpe.len = len;
    ar->gpe.sts = g_malloc0(len / 2);
    ar->gpe.en = g_malloc0(len / 2);
}

void acpi_gpe_reset(ACPIREGS *ar)
{
    memset(ar->gpe.sts, 0, ar->gpe.len / 2);
    memset(ar->gpe.en, 0, ar->gpe.len / 2);
}

static uint8_t *acpi_gpe_ioport_get_ptr(ACPIREGS *ar, uint32_t addr)
{
    uint8_t *cur = NULL;

    if (addr < ar->gpe.len / 2) {
        cur = ar->gpe.sts + addr;
    } else if (addr < ar->gpe.len) {
        cur = ar->gpe.en + addr - ar->gpe.len / 2;
    } else {
        abort();
    }

    return cur;
}

void acpi_gpe_ioport_writeb(ACPIREGS *ar, uint32_t addr, uint32_t val)
{
    uint8_t *cur;

    cur = acpi_gpe_ioport_get_ptr(ar, addr);
    if (addr < ar->gpe.len / 2) {
        /* GPE_STS */
        *cur = (*cur) & ~val;
    } else if (addr < ar->gpe.len) {
        /* GPE_EN */
        *cur = val;
    } else {
        abort();
    }
}

uint32_t acpi_gpe_ioport_readb(ACPIREGS *ar, uint32_t addr)
{
    uint8_t *cur;
    uint32_t val;

    cur = acpi_gpe_ioport_get_ptr(ar, addr);
    val = 0;
    if (cur != NULL) {
        val = *cur;
    }

    return val;
}

void acpi_update_sci(ACPIREGS *regs, qemu_irq irq, uint32_t gpe0_sts_mask)
{
    int sci_level, pm1a_sts;

    pm1a_sts = acpi_pm1_evt_get_sts(regs);

    sci_level = ((pm1a_sts &
                  regs->pm1.evt.en & ACPI_BITMASK_PM1_COMMON_ENABLED) != 0) ||
                ((regs->gpe.sts[0] & regs->gpe.en[0] & gpe0_sts_mask) != 0);

    qemu_set_irq(irq, sci_level);

    /* schedule a timer interruption if needed */
    acpi_pm_tmr_update(regs,
                       (regs->pm1.evt.en & ACPI_BITMASK_TIMER_ENABLE) &&
                       !(pm1a_sts & ACPI_BITMASK_TIMER_STATUS));
}
