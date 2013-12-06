/*
 * Hotplug device interface.
 *
 * Copyright (c) 2013 Red Hat Inc.
 *
 * Authors:
 *  Igor Mammedov <imammedo@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "hw/hotplug.h"

static const TypeInfo hotplug_device_info = {
    .name          = TYPE_HOTPLUG_DEVICE,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(HotplugDeviceClass),
};

static void hotplug_device_register_types(void)
{
    type_register_static(&hotplug_device_info);
}

type_init(hotplug_device_register_types)
