/*
 * Dimm device for Memory Hotplug
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2013 Red Hat Inc
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

#include "hw/mem-hotplug/dimm.h"
#include "qemu/config-file.h"

static void dimm_bus_initfn(Object *obj)
{
    BusState *b = BUS(obj);

    b->allow_hotplug = true;
}
static void dimm_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *bc = BUS_CLASS(klass);
    QemuOpts *opts = qemu_opts_find(qemu_find_opts("memory-opts"), NULL);

    if (opts) {
        bc->max_dev = qemu_opt_get_number(opts, "slots", 0);
    }
}

static const TypeInfo dimm_bus_info = {
    .name = TYPE_DIMM_BUS,
    .parent = TYPE_BUS,
    .instance_init = dimm_bus_initfn,
    .instance_size = sizeof(DimmBus),
    .class_init = dimm_bus_class_init,
};

static Property dimm_properties[] = {
    DEFINE_PROP_UINT64("start", DimmDevice, start, 0),
    DEFINE_PROP_SIZE("size", DimmDevice, size, DEFAULT_DIMMSIZE),
    DEFINE_PROP_UINT32("node", DimmDevice, node, 0),
    DEFINE_PROP_INT32("slot", DimmDevice, slot, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void dimm_realize(DeviceState *dev, Error **errp)
{
    DimmDevice *dimm = DIMM(dev);
    DimmBus *bus = DIMM_BUS(qdev_get_parent_bus(dev));
    BusClass *bc = BUS_GET_CLASS(bus);

    if (!dev->id) {
        error_setg(errp, "missing 'id' property");
        return;
    }

    if (dimm->slot >= bc->max_dev) {
        error_setg(errp, "maximum allowed slot is: %d", bc->max_dev - 1);
        return;
    }

    memory_region_init_ram(&dimm->mr, dev->id, dimm->size);
}

static void dimm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = dimm_realize;
    dc->props = dimm_properties;
    dc->bus_type = TYPE_DIMM_BUS;
}

static TypeInfo dimm_info = {
    .name          = TYPE_DIMM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(DimmDevice),
    .class_init    = dimm_class_init,
};

static void dimm_register_types(void)
{
    type_register_static(&dimm_bus_info);
    type_register_static(&dimm_info);
}

type_init(dimm_register_types)
