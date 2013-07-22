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

#include "hw/mem/dimm.h"
#include "qemu/config-file.h"
#include "qapi/visitor.h"
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

    if (!bus->base) {
        error_setg(errp, "adding memory to '%s' is disabled", BUS(bus)->name);
        return 0;
    }

    qbus_walk_children(BUS(bus), dimm_bus_built_dimm_list, NULL, &list);

    if (hint) {
        new_start = *hint;
    } else {
        new_start = bus->base;
    }

    /* find address range that will fit new DIMM */
    for (item = list; item; item = g_slist_next(item)) {
        DimmDevice *dimm = item->data;
        if (ranges_overlap(dimm->start, memory_region_size(dimm->mr),
                           new_start, size)) {
            if (hint) {
                DeviceState *d = DEVICE(dimm);
                error_setg(errp, "address range conflicts with '%s'", d->id);
                break;
            }
            new_start = dimm->start + memory_region_size(dimm->mr);
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
    memory_region_add_subregion(&bus->as, dimm->start - bus->base, dimm->mr);
    vmstate_register_ram(dimm->mr, DEVICE(dimm));
}

static void dimm_bus_class_init(ObjectClass *oc, void *data)
{
    BusClass *bc = BUS_CLASS(oc);
    DimmBusClass *dbc = DIMM_BUS_CLASS(oc);
    QemuOpts *opts = qemu_opts_find(qemu_find_opts("memory-opts"), NULL);

    bc->max_dev = qemu_opt_get_number(opts, "slots", 0);
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
    DEFINE_PROP_UINT32("node", DimmDevice, node, 0),
    DEFINE_PROP_INT32("slot", DimmDevice, slot, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void dimm_set_memdev(Object *obj, Visitor *v, void *opaque,
                            const char *name, Error **errp)
{
    HostMemoryBackendClass *backend_cls;
    DimmDevice *dimm = DIMM(obj);
    MemoryRegion *mr;
    Object *memdev;
    char *str;

    visit_type_str(v, &str, name, errp);
    if (error_is_set(errp)) {
        return;
    }

    memdev = object_resolve_path_type(str, TYPE_MEMORY_BACKEND, NULL);
    if (!memdev) {
        error_setg(errp, "couldn't find memdev object with ID='%s'", str);
        return;
    }

    backend_cls = MEMORY_BACKEND_GET_CLASS(memdev);
    mr = backend_cls->get_memory(MEMORY_BACKEND(memdev), errp);
    if (error_is_set(errp)) {
        return;
    }
    memory_region_unref(dimm->mr);
    memory_region_ref(mr);
    dimm->mr = mr;
}

static void dimm_get_memdev(Object *obj, Visitor *v, void *opaque,
                            const char *name, Error **errp)
{
    DimmDevice *dimm = DIMM(obj);
    Object *memdev;
    char *str;

    if (!dimm->mr) {
        error_setg(errp, "property %s hasn't been set", name);
        return;
    }

    memdev = memory_region_owner(dimm->mr);
    str = object_property_get_str(memdev, "id", errp);
    visit_type_str(v, &str, name, errp);
    g_free(str);
}

static void dimm_get_size(Object *obj, Visitor *v, void *opaque,
                          const char *name, Error **errp)
{
    DimmDevice *dimm = DIMM(obj);
    int64_t value = memory_region_size(dimm->mr);

    visit_type_int(v, &value, name, errp);
}

static void dimm_initfn(Object *obj)
{
    object_property_add(obj, "memdev", "string", dimm_get_memdev,
                        dimm_set_memdev, NULL, NULL, NULL);
    object_property_add(obj, "size", "int", dimm_get_size,
                        NULL, NULL, NULL, NULL);
}

static void dimm_realize(DeviceState *dev, Error **errp)
{
    DimmDevice *dimm = DIMM(dev);
    DimmBus *bus = DIMM_BUS(qdev_get_parent_bus(dev));
    BusClass *bc = BUS_GET_CLASS(bus);
    DimmBusClass *dbc = DIMM_BUS_GET_CLASS(bus);
    int *slot_hint;
    hwaddr *start_hint;

    if (!dimm->mr) {
        error_setg(errp, "'memdev' property is not set");
        return;
    }

    if (!dev->id) {
        error_setg(errp, "'id' property is not set");
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
    dimm->start = dbc->get_free_addr(bus, start_hint,
                                     memory_region_size(dimm->mr), errp);
    if (error_is_set(errp)) {
        return;
    }

    g_assert(dbc->register_memory);
    dbc->register_memory(bus, dimm, errp);
}

static void dimm_finalize(Object *obj)
{
    DimmDevice *dimm = DIMM(obj);

    if (dimm->mr) {
        memory_region_unref(dimm->mr);
        dimm->mr = NULL;
    }
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
    .instance_init = dimm_initfn,
    .instance_finalize = dimm_finalize,
    .class_init    = dimm_class_init,
};

static void dimm_register_types(void)
{
    type_register_static(&dimm_bus_info);
    type_register_static(&dimm_info);
}

type_init(dimm_register_types)
