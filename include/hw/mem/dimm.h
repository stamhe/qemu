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
#include "sysemu/hostmem.h"
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
 *         Default value: 0, means that address is auto-allocated.
 * @node: numa node to which @DimmDevice is attached.
 * @slot: slot number into which @DimmDevice is plugged in.
 *        Default value: -1, means that slot is auto-allocated.
 * @mr: memory region provided by host memory backend
 */
typedef struct DimmDevice {
    /* private */
    DeviceState parent_obj;
    ram_addr_t start;
    uint32_t node;
    int32_t slot;
    MemoryRegion *mr;
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
 * @base: address from which to start mapping @DimmDevice
 * @as: hot-plugabble memory area where @DimmDevice-s are attached
 */
typedef struct DimmBus {
    BusState parent_obj;
    hwaddr base;
    MemoryRegion as;
} DimmBus;

/**
 * DimmBusClass:
 * @parent_class: opaque parent class container
 * @register_memory: map @DimmDevice into hot-plugable address space
 */
typedef struct DimmBusClass {
    BusClass parent_class;

    void (*register_memory)(DimmBus *bus, DimmDevice *dimm, Error **errp);
} DimmBusClass;

#endif
