#ifndef HW_PCIHOST_PIIX_H
#define HW_PCIHOST_PIIX_H

#include "exec/memory.h"
#include "hw/qdev.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/pci-host/pam.h"
#include "hw/mem/dimm.h"

#define TYPE_I440FX_PCI_DEVICE "i440FX"
#define I440FX_PCI_DEVICE(obj) \
    OBJECT_CHECK(PCII440FXState, (obj), TYPE_I440FX_PCI_DEVICE)

struct PCII440FXState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion *system_memory;
    MemoryRegion *pci_address_space;
    MemoryRegion *ram_memory;
    PAMMemoryRegion pam_regions[13];
    MemoryRegion smram_region;
    uint8_t smm_enabled;
    DimmBus hotplug_mem_bus;
};

#endif
