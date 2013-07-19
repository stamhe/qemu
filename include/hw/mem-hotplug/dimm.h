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
 */
typedef struct DimmBus {
    BusState qbus;
    hwaddr base;
} DimmBus;

/**
 * DimmBusClass:
 * @get_free_slot: returns a not occupied slot number. If @hint is provided,
 * it tries to return slot specified by @hint if it's not busy or returns
 * error in @errp. 
 * @get_free_addr: returns address where @DimmDevice of specified size
 * might be mapped. If @hint is specified it returns hinted address is
 * region is available or error.
 */
typedef struct DimmBusClass {
    BusClass parent_class;

    int (*get_free_slot)(DimmBus *bus, const int *hint, Error **errp);
    hwaddr (*get_free_addr)(DimmBus *bus, const hwaddr *hint,
                            uint64_t size, Error **errp);
} DimmBusClass;


#endif
