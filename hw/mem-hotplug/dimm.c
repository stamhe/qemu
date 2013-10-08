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
#include "qemu/range.h"

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

static gint dimm_bus_addr_sort(gconstpointer a, gconstpointer b)
{
    DimmDevice *x = DIMM(a);
    DimmDevice *y = DIMM(b);

    return x->start - y->start;
}

static int dimm_bus_built_dimm_list(DeviceState *dev, void *opaque)
{
    GSList **list = opaque;

    if (dev->realized) { /* only realized DIMMs matter */
        *list = g_slist_insert_sorted(*list, dev, dimm_bus_addr_sort);
    }
    return 0;
}

static hwaddr dimm_bus_get_free_addr(DimmBus *bus, const hwaddr *hint,
                                     uint64_t size, Error **errp)
{
    GSList *list = NULL, *item;
    hwaddr new_start, ret;
    uint64_t as_size;

    qbus_walk_children(BUS(bus), dimm_bus_built_dimm_list, NULL, &list);

    if (hint) {
        new_start = *hint;
    } else {
        new_start = bus->base;
    }

    /* find address range that will fit new DIMM */
    for (item = list; item; item = g_slist_next(item)) {
        DimmDevice *dimm = item->data;
        if (ranges_overlap(dimm->start, dimm->size, new_start, size)) {
            if (hint) {
                DeviceState *d = DEVICE(dimm);
                error_setg(errp, "address range conflicts with '%s'", d->id);
                break;
            }
            new_start = dimm->start + dimm->size;
        }
    }
    ret = new_start;

    g_slist_free(list);

    as_size = memory_region_size(&bus->as);
    if ((new_start + size) > (bus->base + as_size)) {
        error_setg(errp, "can't add memory beyond 0x%" PRIx64,
                   bus->base + as_size);
    }

    return ret;
}

static void dimm_bus_register_memory(DimmBus *bus, DimmDevice *dimm,
                                     Error **errp)
{
    memory_region_add_subregion(&bus->as, dimm->start - bus->base, &dimm->mr);
    vmstate_register_ram_global(&dimm->mr);
}

static void dimm_bus_class_init(ObjectClass *oc, void *data)
{
    BusClass *bc = BUS_CLASS(oc);
    DimmBusClass *dbc = DIMM_BUS_CLASS(oc);
    QemuOpts *opts = qemu_opts_find(qemu_find_opts("memory-opts"), NULL);

    if (opts) {
        bc->max_dev = qemu_opt_get_number(opts, "slots", 0);
    }
    dbc->register_memory = dimm_bus_register_memory;
    dbc->get_free_slot = dimm_bus_get_free_slot;
    dbc->get_free_addr = dimm_bus_get_free_addr;
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
    DimmBusClass *dbc = DIMM_BUS_GET_CLASS(bus);
    int *slot_hint;
    hwaddr *start_hint;

    if (!dev->id) {
        error_setg(errp, "missing 'id' property");
        return;
    }

    if (dimm->slot >= bc->max_dev) {
        error_setg(errp, "maximum allowed slot is: %d", bc->max_dev - 1);
        return;
    }
    g_assert(dbc->get_free_slot);
    slot_hint = dimm->slot < 0 ? NULL : &dimm->slot;
    dimm->slot = dbc->get_free_slot(bus, slot_hint, errp);
    if (error_is_set(errp)) {
        return;
    }

    start_hint = !dimm->start ? NULL : &dimm->start;
    if (start_hint && (dimm->start < bus->base)) {
        error_setg(errp, "can't map DIMM below: 0x%" PRIx64, bus->base);
        return;
    }
    g_assert(dbc->get_free_addr);
    dimm->start = dbc->get_free_addr(bus, start_hint, dimm->size, errp);
    if (error_is_set(errp)) {
        return;
    }

    memory_region_init_ram(&dimm->mr, OBJECT(dev), dev->id, dimm->size);

    g_assert(dbc->register_memory);
    dbc->register_memory(bus, dimm, errp);
}

static void dimm_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

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
