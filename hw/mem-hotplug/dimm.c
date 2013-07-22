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
#include "qemu/bitmap.h"

static void dimm_bus_initfn(Object *obj)
{
    BusState *b = BUS(obj);

    b->allow_hotplug = true;
}

static int dimm_bus_slot2bitmap(DeviceState *dev, void *opaque)
{
    unsigned long *bitmap = opaque;
    BusClass *bc = BUS_GET_CLASS(qdev_get_parent_bus(dev));
    DimmDevice *d = DIMM(dev);

    if (dev->realized) { /* count only realized DIMMs */
        g_assert(d->slot < bc->max_dev);
        set_bit(d->slot, bitmap);
    }
    return 0;
}

static int dimm_bus_get_free_slot(DimmBus *bus, const int *hint, Error **errp)
{
    BusClass *bc = BUS_GET_CLASS(bus);
    unsigned long *bitmap = bitmap_new(bc->max_dev);
    int slot = 0;

    qbus_walk_children(BUS(bus), dimm_bus_slot2bitmap, NULL, bitmap);

    /* check if requested slot is not occupied */
    if (hint) {
        if (!test_bit(*hint, bitmap)) {
            slot = *hint;
        } else {
            error_setg(errp, "slot %d is busy", *hint);
        }
        goto out;
    }

    /* search for free slot */
    slot = find_first_zero_bit(bitmap, bc->max_dev);
out:
    g_free(bitmap);
    return slot;
}

static void dimm_bus_register_memory(DimmBus *bus, DimmDevice *dimm,
                                     Error **errp)
{
    memory_region_add_subregion(&bus->as, dimm->start - bus->base, &dimm->mr);
    vmstate_register_ram_global(&dimm->mr);
}

static void dimm_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *bc = BUS_CLASS(klass);
    DimmBusClass *dc = DIMM_BUS_CLASS(klass);
    QemuOpts *opts = qemu_opts_find(qemu_find_opts("memory-opts"), NULL);

    if (opts) {
        bc->max_dev = qemu_opt_get_number(opts, "slots", 0);
    }
    dc->register_memory = dimm_bus_register_memory;
    dc->get_free_slot = dimm_bus_get_free_slot;
}

static const TypeInfo dimm_bus_info = {
    .name = TYPE_DIMM_BUS,
    .parent = TYPE_BUS,
    .instance_init = dimm_bus_initfn,
    .instance_size = sizeof(DimmBus),
    .class_init = dimm_bus_class_init,
    .class_size = sizeof(DimmBusClass),
};

static Property dimm_properties[] = {
    DEFINE_PROP_UINT64("start", DimmDevice, start, 0),
    DEFINE_PROP_SIZE("size", DimmDevice, size, DEFAULT_DIMMSIZE),
    DEFINE_PROP_UINT32("node", DimmDevice, node, 0),
    DEFINE_PROP_INT32("slot", DimmDevice, slot, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void dimm_realize(DeviceState *dev, Error **errp)
{
    DimmDevice *dimm = DIMM(dev);
    DimmBus *bus = DIMM_BUS(qdev_get_parent_bus(dev));
    BusClass *bc = BUS_GET_CLASS(bus);
    DimmBusClass *dc = DIMM_BUS_GET_CLASS(bus);
    int *slot_hint;

    if (!dev->id) {
        error_setg(errp, "missing 'id' property");
        return;
    }

    if (dimm->slot >= bc->max_dev) {
        error_setg(errp, "maximum allowed slot is: %d", bc->max_dev - 1);
        return;
    }
    g_assert(dc->get_free_slot);
    slot_hint = dimm->slot < 0 ? NULL : &dimm->slot;
    dimm->slot = dc->get_free_slot(bus, slot_hint, errp);
    if (error_is_set(errp)) {
        return;
    }


    memory_region_init_ram(&dimm->mr, dev->id, dimm->size);

    g_assert(dc->register_memory);
    dc->register_memory(bus, dimm, errp);
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
