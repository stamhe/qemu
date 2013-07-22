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

static void dimm_bus_initfn(Object *obj)
{
    BusState *b = BUS(obj);

    b->allow_hotplug = true;
}
static void dimm_bus_class_init(ObjectClass *oc, void *data)
{
    BusClass *bc = BUS_CLASS(oc);
    QemuOpts *opts = qemu_opts_find(qemu_find_opts("memory-opts"), NULL);

    bc->max_dev = qemu_opt_get_number(opts, "slots", 0);
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
    DEFINE_PROP_UINT32("node", DimmDevice, node, 0),
    DEFINE_PROP_INT32("slot", DimmDevice, slot, 0),
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
