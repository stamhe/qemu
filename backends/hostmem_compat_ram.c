/*
 * QEMU Host Memory Backend
 *
 * Copyright (C) 2013 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "sysemu/hostmem.h"

static void
compat_ram_backend_memory_init(HostMemoryBackend *backend, Error **errp)
{
    if (!memory_region_size(&backend->mr)) {
        memory_region_init_ram(&backend->mr, OBJECT(backend),
                               backend->id, backend->size);
    }
}

static void
compat_ram_backend_class_init(ObjectClass *oc, void *data)
{
    HostMemoryBackendClass *bc = MEMORY_BACKEND_CLASS(oc);

    bc->memory_init = compat_ram_backend_memory_init;
}

static const TypeInfo compat_ram_backend_info = {
    .name = TYPE_COMPAT_RAM_MEMORY_BACKEND,
    .parent = TYPE_MEMORY_BACKEND,
    .class_init = compat_ram_backend_class_init,
};

static void register_types(void)
{
    type_register_static(&compat_ram_backend_info);
}

type_init(register_types);
