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
#ifndef HOTPLUG_H
#define HOTPLUG_H

#include "hw/qdev-core.h"

#define TYPE_HOTPLUG_DEVICE "hotplug-device"

#define HOTPLUG_DEVICE_CLASS(klass) \
     OBJECT_CLASS_CHECK(HotplugDeviceClass, (klass), TYPE_HOTPLUG_DEVICE)
#define HOTPLUG_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(HotplugDeviceClass, (obj), TYPE_HOTPLUG_DEVICE)

/**
 * hotplug_fn:
 * @hotplug_dev: a device performing hotplug/uplug action
 * @hotplugged_dev: a device that has been hotplugged
 * @errp: returns an error if this function fails
 */
typedef void (*hotplug_fn)(DeviceState *hotplug_dev,
                           DeviceState *hotplugged_dev, Error **errp);

/**
 * HotplugDeviceClass:
 *
 * Interface to be implemented by a device performing
 * hardware hotplug/unplug functions.
 *
 * @parent: Opaque parent interface.
 * @hotplug: hotplug callback.
 * @hot_unplug: hot unplug callback.
 */
typedef struct HotplugDeviceClass {
    InterfaceClass parent;

    hotplug_fn hotplug;
    hotplug_fn hot_unplug;
} HotplugDeviceClass;

#endif
