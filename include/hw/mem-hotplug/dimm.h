#ifndef QEMU_DIMM_H
#define QEMU_DIMM_H

#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/qdev.h"

#define MAX_DIMMS 255
#define DEFAULT_DIMMSIZE (1024*1024*1024)

#define TYPE_DIMM "dimm"
#define DIMM(obj) \
    OBJECT_CHECK(DimmDevice, (obj), TYPE_DIMM)
#define DIMM_CLASS(klass) \
    OBJECT_CLASS_CHECK(DimmDeviceClass, (klass), TYPE_DIMM)
#define DIMM_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DimmDeviceClass, (obj), TYPE_DIMM)

typedef struct DimmDevice {
    DeviceState qdev;
    int32_t slot_nr; /* slot number where dimm is plugged, if negative auto allocate */
    ram_addr_t start; /* starting physical address */
    ram_addr_t size;
    uint32_t node; /* numa node proximity */
    MemoryRegion *mr; /* MemoryRegion for this slot. !NULL only if populated */
} DimmDevice;

typedef struct DimmDeviceClass {
    DeviceClass parent_class;

    int (*init)(DimmDevice *dev);
} DimmDeviceClass;

#define TYPE_DIMM_BUS "dimmbus"
#define DIMM_BUS(obj) OBJECT_CHECK(DimmBus, (obj), TYPE_DIMM_BUS)

typedef struct DimmBus {
    BusState qbus;
} DimmBus;

#endif
