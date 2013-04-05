/* icc_bus.c
 * emulate x86 ICC(Interrupt Controller Communications) bus
 *
 * Copyright (c) 2013 Red Hat, Inc
 *
 * Authors:
 *     Igor Mammedov <imammedo@redhat.com>
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
#include "hw/cpu/icc_bus.h"
#include "hw/sysbus.h"

/* icc-bridge implementation */
static void icc_bus_init(Object *obj)
{
    BusState *b = BUS(obj);

    b->allow_hotplug = true;
}

static const TypeInfo icc_bus_info = {
    .name = TYPE_ICC_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(ICCBus),
    .instance_init = icc_bus_init,
};

/* icc-device implementation */
static void icc_device_realizefn(DeviceState *dev, Error **err)
{
    ICCDevice *id = ICC_DEVICE(dev);
    ICCDeviceClass *k = ICC_DEVICE_GET_CLASS(id);
    Error *local_err = NULL;

    if (k->init) {
        if (k->init(id) < 0) {
            error_setg(&local_err, "%s initialization failed.",
                       object_get_typename(OBJECT(dev)));
            error_propagate(err, local_err);
        }
    }
}

static void icc_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->realize = icc_device_realizefn;
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

/*  icc-bridge implementation */
typedef struct ICCBridgeState {
    /*< private >*/
    SysBusDevice parent_obj;
    ICCBus icc_bus;
} ICCBridgeState;

#define ICC_BRIGDE(obj) OBJECT_CHECK(ICCBridgeState, (obj), TYPE_ICC_BRIDGE)

static void icc_bridge_init(Object *obj)
{
    ICCBridgeState *s = ICC_BRIGDE(obj);

    qbus_create_inplace(&s->icc_bus, TYPE_ICC_BUS, DEVICE(s), "icc-bus");
}

static const TypeInfo icc_bridge_info = {
    .name  = TYPE_ICC_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init  = icc_bridge_init,
    .instance_size  = sizeof(ICCBridgeState),
};


static void icc_bus_register_types(void)
{
    type_register_static(&icc_bus_info);
    type_register_static(&icc_device_info);
    type_register_static(&icc_bridge_info);
}

type_init(icc_bus_register_types)
