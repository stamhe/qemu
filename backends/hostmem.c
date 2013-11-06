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
#include "sysemu/sysemu.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qerror.h"
#include "qemu/config-file.h"

QemuOptsList qemu_memdev_opts = {
    .name = "memdev",
    .implied_opt_name = "type",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_memdev_opts.head),
    .desc = {
        { /* end of list */ }
    },
};

static int set_object_prop(const char *name, const char *value, void *opaque)
{
    Error *local_err = NULL;
    Object *obj = OBJECT(opaque);

    object_property_parse(obj, value, name, &local_err);
    if (error_is_set(&local_err)) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }
    return 0;
}

void memdev_add(QemuOpts *opts, Error **errp)
{
    Error *local_err = NULL;
    HostMemoryBackendClass *bc;
    Object *obj = NULL;
    ObjectClass *oc;
    const char *type;

    type = qemu_opt_get(opts, "type");
    if (!type) {
        type = TYPE_COMPAT_RAM_MEMORY_BACKEND;
    }

    oc = object_class_by_name(type);
    if (!oc) {
        error_setg(&local_err, "Unknown memdev type: %s", type);
        goto out;
    }
    if (object_class_is_abstract(oc)) {
        error_setg(&local_err, "Can't create abstract memdev type: %s", type);
        goto out;
    }

    obj = object_new(type);
    if (!object_dynamic_cast(obj, TYPE_MEMORY_BACKEND)) {
        error_setg(&local_err, "Invalid memdev type: %s", type);
        goto out;
    }

    if (qemu_opt_foreach(opts, set_object_prop, obj, true)) {
        error_setg(&local_err, "failed to create memdev");
        goto out;
    }
    object_property_parse(obj, qemu_opts_id(opts), "id", &local_err);
    if (error_is_set(&local_err)) {
        goto out;
    }

    /* verify properties correctnes and initialize backend */
    bc = MEMORY_BACKEND_GET_CLASS(obj);
    if (bc->get_memory) {
        HostMemoryBackend *backend = MEMORY_BACKEND(obj);
        if (!bc->get_memory(backend, &local_err)) {
            goto out;
        }
    }

    /* make backend available to the world via QOM tree */
    object_property_add_child(container_get(qemu_get_backend(), "/memdev"),
                              qemu_opts_id(opts), obj, &local_err);

out:
    if (error_is_set(&local_err)) {
        error_propagate(errp, local_err);
        if (obj) {
            object_unref(obj);
        }
    }
}

int qmp_memdev_add(Monitor *mon, const QDict *qdict, QObject **ret)
{
    Error *local_err = NULL;
    QemuOptsList *opts_list;
    QemuOpts *opts;

    opts_list = qemu_find_opts_err("memdev", &local_err);
    if (error_is_set(&local_err)) {
        goto out;
    }

    opts = qemu_opts_from_qdict(opts_list, qdict, &local_err);
    if (error_is_set(&local_err)) {
        goto out;
    }

    memdev_add(opts, &local_err);

out:
    qemu_opts_del(opts);
    if (error_is_set(&local_err)) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }
    return 0;
}

static void
hostmemory_backend_get_size(Object *obj, Visitor *v, void *opaque,
                            const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint64_t value = backend->size;

    visit_type_size(v, &value, name, errp);
}

static void
hostmemory_backend_set_size(Object *obj, Visitor *v, void *opaque,
                            const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint64_t value;

    visit_type_size(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    if (!value) {
        error_setg(errp, "Property '%s.%s' doesn't take value '%" PRIu64 "'",
                   object_get_typename(obj), name , value);
        return;
    }
    backend->size = value;
}

static void
hostmemory_backend_get_id(Object *obj, Visitor *v, void *opaque,
                          const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    visit_type_str(v, &backend->id, name, errp);
}

static void
hostmemory_backend_set_id(Object *obj, Visitor *v, void *opaque,
                          const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    Error *local_err = NULL;
    Object *dup_obj;
    char *str;

    visit_type_str(v, &str, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    dup_obj = object_resolve_path(str, NULL);
    if (dup_obj) {
        error_setg(errp, "Duplicate property [%s.%s] value: '%s'",
                   object_get_typename(obj), name, str);
        error_propagate(errp, local_err);
        g_free(str);
        return;
    }

    if (backend->id) {
        g_free(backend->id);
    }
    backend->id = str;
}

static void hostmemory_backend_initfn(Object *obj)
{
    object_property_add(obj, "id", "string",
                        hostmemory_backend_get_id,
                        hostmemory_backend_set_id, NULL, NULL, NULL);
    object_property_add(obj, "size", "int",
                        hostmemory_backend_get_size,
                        hostmemory_backend_set_size, NULL, NULL, NULL);
}

static void hostmemory_backend_finalize(Object *obj)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    g_free(backend->id);
    if (memory_region_size(&backend->mr)) {
        memory_region_destroy(&backend->mr);
    }
}

static void
hostmemory_backend_memory_init(HostMemoryBackend *backend, Error **errp)
{
    error_setg(errp, "memory_init is not implemented for type [%s]",
               object_get_typename(OBJECT(backend)));
}

static MemoryRegion *
hostmemory_backend_get_memory(HostMemoryBackend *backend, Error **errp)
{
    HostMemoryBackendClass *bc = MEMORY_BACKEND_GET_CLASS(backend);
    Object *obj = OBJECT(backend);
    char *id = backend->id;

    if (!id || (*id == '\0')) {
        error_setg(errp, "Invalid property [%s.id] value: '%s'",
                   object_get_typename(obj), id ? id : "");
        return NULL;
    }

    if (!backend->size) {
        error_setg(errp, "Invalid property [%s.size] value: %" PRIu64,
                   object_get_typename(obj), backend->size);
        return NULL;
    }

    bc->memory_init(backend, errp);

    return memory_region_size(&backend->mr) ? &backend->mr : NULL;
}

static void
hostmemory_backend_class_init(ObjectClass *oc, void *data)
{
    HostMemoryBackendClass *bc = MEMORY_BACKEND_CLASS(oc);

    bc->memory_init = hostmemory_backend_memory_init;
    bc->get_memory = hostmemory_backend_get_memory;
}

static const TypeInfo hostmemory_backend_info = {
    .name = TYPE_MEMORY_BACKEND,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(HostMemoryBackendClass),
    .class_init = hostmemory_backend_class_init,
    .instance_size = sizeof(HostMemoryBackend),
    .instance_init = hostmemory_backend_initfn,
    .instance_finalize = hostmemory_backend_finalize,
};

static void register_types(void)
{
    type_register_static(&hostmemory_backend_info);
}

type_init(register_types);
