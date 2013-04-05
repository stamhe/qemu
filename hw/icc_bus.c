/* icc_bus.c
 * emulate x86 ICC(INTERRUPT CONTROLLER COMMUNICATIONS) bus
 *
 * Copyright (c) 2013 Red Hat, Inc
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */
#include "icc_bus.h"
#include "sysbus.h"

static void icc_bus_initfn(Object *obj)
{
    BusState *b = BUS(obj);
    b->allow_hotplug = true;
}

static const TypeInfo icc_bus_info = {
    .name = TYPE_ICC_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(ICCBus),
    .instance_init = icc_bus_initfn,
};

static int icc_device_init(DeviceState *dev)
{
    ICCDevice *id = ICC_DEVICE(dev);
    ICCDeviceClass *k = ICC_DEVICE_GET_CLASS(id);

    return k->init(id);
}

static void icc_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->init = icc_device_init;
    k->bus_type = TYPE_ICC_BUS;
}

static const TypeInfo icc_device_info = {
    .name = TYPE_ICC_DEVICE,
    .parent = TYPE_DEVICE,
    .abstract = true,
    .instance_size = sizeof(ICCDevice),
    .class_size = sizeof(ICCDeviceClass),
    .class_init = icc_device_class_init,
};

typedef struct ICCBridgeState {
    SysBusDevice busdev;
    MemoryRegion apic_container;
} ICCBridgeState;
#define ICC_BRIGDE(obj) OBJECT_CHECK(ICCBridgeState, (obj), TYPE_ICC_BRIDGE)


static void icc_bridge_initfn(Object *obj)
{
    ICCBridgeState *s = ICC_BRIGDE(obj);
    SysBusDevice *sb = SYS_BUS_DEVICE(obj);
    ICCBus *ibus;

    ibus = ICC_BUS(qbus_create(TYPE_ICC_BUS, DEVICE(obj), "icc-bus"));

    /* Do not change order of registering regions,
     * APIC must be first registered region, board maps it by 0 index
     */
    memory_region_init(&s->apic_container, "icc-apic-container",
                       APIC_SPACE_SIZE);
    sysbus_init_mmio(sb, &s->apic_container);
    ibus->apic_address_space = &s->apic_container;
}

static const TypeInfo icc_bridge_info = {
    .name  = "icc-bridge",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init  = icc_bridge_initfn,
    .instance_size  = sizeof(ICCBridgeState),
};

static void icc_bus_register_types(void)
{
    type_register_static(&icc_bus_info);
    type_register_static(&icc_device_info);
    type_register_static(&icc_bridge_info);
}

type_init(icc_bus_register_types)
