/*
 * QEMU ACPI hotplug utilities
 *
 * Copyright (C) 2013 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef ACPI_HOTPLUG_H
#define ACPI_HOTPLUG_H

#include "hw/acpi/acpi.h"

#define ACPI_CPU_HOTPLUG_IO_BASE_PROP "cpu-hotplug-io-base"
#define ACPI_CPU_HOTPLUG_STATUS 4

#define ACPI_GPE_PROC_LEN 32

typedef struct AcpiCpuHotplug {
    MemoryRegion io;
    uint16_t io_base;
    uint8_t sts[ACPI_GPE_PROC_LEN];
} AcpiCpuHotplug;

void AcpiCpuHotplug_add(ACPIGPE *gpe, AcpiCpuHotplug *g, CPUState *cpu);

void AcpiCpuHotplug_init(MemoryRegion *parent, Object *owner,
                         AcpiCpuHotplug *gpe_cpu, uint16_t base);
#endif
