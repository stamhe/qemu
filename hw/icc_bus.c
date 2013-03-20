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
#include "qemu/module.h"

static const TypeInfo icc_bus_info = {
    .name = TYPE_ICC_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(ICCBus),
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

static BusState *icc_bus;

BusState *get_icc_bus(void)
{
    if (icc_bus == NULL) {
        icc_bus = g_malloc0(icc_bus_info.instance_size);
        qbus_create_inplace(icc_bus, TYPE_ICC_BUS, NULL, "icc-bus");
        icc_bus->allow_hotplug = 1;
        OBJECT(icc_bus)->free = g_free;
        object_property_add_child(container_get(qdev_get_machine(),
                                                "/unattached"),
                                  "icc-bus", OBJECT(icc_bus), NULL);
    }
    return BUS(icc_bus);
}

static void icc_bus_register_types(void)
{
    type_register_static(&icc_bus_info);
    type_register_static(&icc_device_info);
}

type_init(icc_bus_register_types)
