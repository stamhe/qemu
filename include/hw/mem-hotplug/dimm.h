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
#define DIMM_CLASS(klass) \
    OBJECT_CLASS_CHECK(DimmDeviceClass, (klass), TYPE_DIMM)
#define DIMM_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DimmDeviceClass, (obj), TYPE_DIMM)

/**
 * DimmBus:
 * @start: starting physical address, where @DimmDevice is mapped.
 * @size: amount of memory mapped at @start.
 * @node: numa node to which @DimmDevice is attached.
 * @slot: slot number into which @DimmDevice is plugged in.
 * Default value: -1, means that slot is auto-allocated.
 */
typedef struct DimmDevice {
    DeviceState qdev;
    ram_addr_t start;
    ram_addr_t size;
    uint32_t node;
    int32_t slot;
    MemoryRegion mr;
} DimmDevice;

typedef struct DimmDeviceClass {
    DeviceClass parent_class;
} DimmDeviceClass;

#define TYPE_DIMM_BUS "dimmbus"
#define DIMM_BUS(obj) OBJECT_CHECK(DimmBus, (obj), TYPE_DIMM_BUS)
#define DIMM_BUS_CLASS(klass) \
    OBJECT_CLASS_CHECK(DimmBusClass, (klass), TYPE_DIMM_BUS)
#define DIMM_BUS_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DimmBusClass, (obj), TYPE_DIMM_BUS)

/**
 * DimmBus:
 * @base: address from which to start mapping @DimmDevice
 * @as: hot-plugabble memory area where @DimmDevice-s are attached
 */
typedef struct DimmBus {
    BusState qbus;
    hwaddr base;
    MemoryRegion as;
} DimmBus;

/**
 * DimmBusClass:
 * @get_free_slot: returns a not occupied slot number. If @hint is provided,
 * it tries to return slot specified by @hint if it's not busy or returns
 * error in @errp.
 * @register_memory: map @DimmDevice into hot-plugable address space
 */
typedef struct DimmBusClass {
    BusClass parent_class;

    int (*get_free_slot)(DimmBus *bus, const int *hint, Error **errp);
    void (*register_memory)(DimmBus *bus, DimmDevice *dimm, Error **errp);
} DimmBusClass;

#endif
