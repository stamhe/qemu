/*
 * DIMM device
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2013 Red Hat Inc
 *
 * Authors:
 *  Vasilis Liaskovitis <vasilis.liaskovitis@profitbricks.com>
 *  Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_DIMM_H
#define QEMU_DIMM_H

#include "exec/memory.h"
#include "hw/qdev.h"

#define DEFAULT_DIMMSIZE (1024*1024*1024)

#define TYPE_DIMM "dimm"
#define DIMM(obj) \
    OBJECT_CHECK(DimmDevice, (obj), TYPE_DIMM)
#define DIMM_CLASS(oc) \
    OBJECT_CLASS_CHECK(DimmDeviceClass, (oc), TYPE_DIMM)
#define DIMM_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DimmDeviceClass, (obj), TYPE_DIMM)

/**
 * DimmDevice:
 * @parent_obj: opaque parent object container
 * @start: starting guest physical address, where @DimmDevice is mapped.
 * @size: amount of memory mapped at @start.
 * @node: numa node to which @DimmDevice is attached.
 * @slot: slot number into which @DimmDevice is plugged in.
 */
typedef struct DimmDevice {
    DeviceState parent_obj;
    ram_addr_t start;
    ram_addr_t size;
    uint32_t node;
    int32_t slot;
    MemoryRegion mr;
} DimmDevice;

typedef struct DimmDeviceClass {
    DeviceClass parent_class;
} DimmDeviceClass;

#define TYPE_DIMM_BUS "dimm-bus"
#define DIMM_BUS(obj) OBJECT_CHECK(DimmBus, (obj), TYPE_DIMM_BUS)
#define DIMM_BUS_CLASS(oc) \
    OBJECT_CLASS_CHECK(DimmBusClass, (oc), TYPE_DIMM_BUS)
#define DIMM_BUS_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DimmBusClass, (obj), TYPE_DIMM_BUS)

/**
 * DimmBus:
 * @parent_obj: opaque parent object container
 */
typedef struct DimmBus {
    BusState parent_obj;
} DimmBus;

#endif
