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
#ifndef QEMU_RAM_H
#define QEMU_RAM_H

#include "qom/object.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "qemu/option.h"

#define TYPE_MEMORY_BACKEND "host-memory"
#define MEMORY_BACKEND(obj) \
    OBJECT_CHECK(HostMemoryBackend, (obj), TYPE_MEMORY_BACKEND)
#define MEMORY_BACKEND_GET_CLASS(obj) \
    OBJECT_GET_CLASS(HostMemoryBackendClass, (obj), TYPE_MEMORY_BACKEND)
#define MEMORY_BACKEND_CLASS(klass) \
    OBJECT_CLASS_CHECK(HostMemoryBackendClass, (klass), TYPE_MEMORY_BACKEND)

typedef struct HostMemoryBackend HostMemoryBackend;
typedef struct HostMemoryBackendClass HostMemoryBackendClass;

/**
 * HostMemoryBackendClass:
 * @parent_class: opaque parent class container
 * @memory_init: hook for derived classes to perform memory allocation
 * @get_memory: get #MemoryRegion backed by @backend and link @backend
 *   with user @uobj to prevent backend disapearing while @uobj exists.
 *   @uobj must have "backend" link property or @get_memory will fail.
 *   retuns: pointer to intialized #MemoryRegion on success or
 *           NULL on failure with error set in errp
 *
 */
struct HostMemoryBackendClass {
    ObjectClass parent_class;

    void (*memory_init)(HostMemoryBackend *backend, Error **errp);
    MemoryRegion* (*get_memory)(HostMemoryBackend *backend, Error **errp);
};

/**
 * @HostMemoryBackend
 *
 * @parent: opaque parent object container
 * @size: amount of memory backend provides
 * @id: unique identification string in memdev namespace
 * @mr: MemoryRegion representing host memory belonging to backend
 */
struct HostMemoryBackend {
    /* private */
    Object parent;

    /* protected */
    char *id;
    uint64_t size;

    MemoryRegion mr;
};

extern QemuOptsList qemu_memdev_opts;

/**
 * @qmp_memdev_add:
 *   QMP/HMP memdev-add command handler
 * returns 0 on success or -1 on failure
 */
int qmp_memdev_add(Monitor *mon, const QDict *qdict, QObject **ret);

/**
 * @memdev_add:
 *   CLI "-memdev" option parser
 * @opts: options for accossiated with -memdev
 * @errp: returns an error if this function fails
 */
void memdev_add(QemuOpts *opts, Error **errp);

/**
 * @memdev_name:
 * @id: backend identification string
 *
 * returns backend name in format "memdev[id]",
 * caller is responsible for freeing returned value.
 */
char *memdev_name(const char *id);

/* hostmem_compat_ram.c */
/**
 * @TYPE_COMPAT_RAM_MEMORY_BACKEND:
 * name of backend that uses legacy RAM allocation methods
 * implemented by #memory_region_init_ram
 */
#define TYPE_COMPAT_RAM_MEMORY_BACKEND "compat-ram-host-memory"

#endif
