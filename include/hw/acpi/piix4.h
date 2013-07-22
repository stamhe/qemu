#ifndef HW_ACPI_PIIX4_H
#define HW_ACPI_PIIX4_H

#include "qemu/typedefs.h"

Object *piix4_pm_find(void);
int piix4_mem_hotplug(DeviceState *hotplug_dev, DeviceState *dev,
                      HotplugState state);

#endif
