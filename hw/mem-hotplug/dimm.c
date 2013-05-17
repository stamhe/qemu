/*
 * Dimm device for Memory Hotplug
 *
 * Copyright ProfitBricks GmbH 2012
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
#include "qemu/notify.h"
#include "sysemu/sysemu.h"

/* Memory hot-plug notifiers */
static NotifierList mem_added_notifiers =
    NOTIFIER_LIST_INITIALIZER(mem_added_notifiers);

void qemu_register_mem_added_notifier(Notifier *notifier)
{
    notifier_list_add(&mem_added_notifiers, notifier);
}

static void dimm_bus_initfn(Object *obj)
{
    BusState *b = BUS(obj);

    b->allow_hotplug = true;
}

static const TypeInfo dimm_bus_info = {
    .name = TYPE_DIMM_BUS,
    .parent = TYPE_BUS,
    .instance_init = dimm_bus_initfn,
};

static Property dimm_properties[] = {
    DEFINE_PROP_UINT64("start", DimmDevice, start, 0),
    DEFINE_PROP_SIZE("size", DimmDevice, size, DEFAULT_DIMMSIZE),
    DEFINE_PROP_UINT32("node", DimmDevice, node, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void dimm_realize(DeviceState *dev, Error **errp)
{
    MemoryRegion *new;
    DimmDevice *dimm = DIMM(dev);

    new = g_malloc(sizeof(MemoryRegion));
    memory_region_init_ram(new, dev->id, dimm->size);
    vmstate_register_ram_global(new);
    memory_region_add_subregion(get_system_memory(), dimm->start, new);
    dimm->mr = new;

    notifier_list_notify(&mem_added_notifiers, dev);
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
