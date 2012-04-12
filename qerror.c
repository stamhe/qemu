/*
 * QError Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "monitor.h"
#include "qjson.h"
#include "qerror.h"
#include "qemu-common.h"

static void qerror_destroy_obj(QObject *obj);

static const QType qerror_type = {
    .code = QTYPE_QERROR,
    .destroy = qerror_destroy_obj,
};

/**
 * The 'desc' parameter is a printf-like string, the format of the format
 * string is:
 *
 * %(KEY)
 *
 * Where KEY is a QDict key, which has to be passed to qerror_from_info().
 *
 * Example:
 *
 * "foo error on device: %(device) slot: %(slot_nr)"
 *
 * A single percent sign can be printed if followed by a second one,
 * for example:
 *
 * "running out of foo: %(foo)%%"
 *
 * Please keep the entries in alphabetical order.
 * Use scripts/check-qerror.sh to check.
 */
static const QErrorStringTable qerror_table[] = {
    {
        .error_fmt = QERR_ADD_CLIENT_FAILED,
        .desc      = "Could not add client",
    },
    {
        .error_fmt = QERR_AMBIGUOUS_PATH,
        .desc      = "Path '%(path)' does not uniquely identify a %(object)"
    },
    {
        .error_fmt = QERR_BAD_BUS_FOR_DEVICE,
        .desc      = "Device '%(device)' can't go on a %(bad_bus_type) bus",
    },
    {
        .error_fmt = QERR_BASE_NOT_FOUND,
        .desc      = "Base '%(base)' not found",
    },
    {
        .error_fmt = QERR_BLOCK_FORMAT_FEATURE_NOT_SUPPORTED,
        .desc      = "Block format '%(format)' used by device '%(name)' does not support feature '%(feature)'",
    },
    {
        .error_fmt = QERR_BUS_NO_HOTPLUG,
        .desc      = "Bus '%(bus)' does not support hotplugging",
    },
    {
        .error_fmt = QERR_BUS_NOT_FOUND,
        .desc      = "Bus '%(bus)' not found",
    },
    {
        .error_fmt = QERR_COMMAND_DISABLED,
        .desc      = "The command %(name) has been disabled for this instance",
    },
    {
        .error_fmt = QERR_COMMAND_NOT_FOUND,
        .desc      = "The command %(name) has not been found",
    },
    {
        .error_fmt = QERR_DEVICE_ENCRYPTED,
        .desc      = "Device '%(device)' is encrypted",
    },
    {
        .error_fmt = QERR_DEVICE_FEATURE_BLOCKS_MIGRATION,
        .desc      = "Migration is disabled when using feature '%(feature)' in device '%(device)'",
    },
    {
        .error_fmt = QERR_DEVICE_HAS_NO_MEDIUM,
        .desc      = "Device '%(device)' has no medium",
    },
    {
        .error_fmt = QERR_DEVICE_INIT_FAILED,
        .desc      = "Device '%(device)' could not be initialized",
    },
    {
        .error_fmt = QERR_DEVICE_IN_USE,
        .desc      = "Device '%(device)' is in use",
    },
    {
        .error_fmt = QERR_DEVICE_IS_READ_ONLY,
        .desc      = "Device '%(device)' is read only",
    },
    {
        .error_fmt = QERR_DEVICE_LOCKED,
        .desc      = "Device '%(device)' is locked",
    },
    {
        .error_fmt = QERR_DEVICE_MULTIPLE_BUSSES,
        .desc      = "Device '%(device)' has multiple child busses",
    },
    {
        .error_fmt = QERR_DEVICE_NO_BUS,
        .desc      = "Device '%(device)' has no child bus",
    },
    {
        .error_fmt = QERR_DEVICE_NO_HOTPLUG,
        .desc      = "Device '%(device)' does not support hotplugging",
    },
    {
        .error_fmt = QERR_DEVICE_NOT_ACTIVE,
        .desc      = "Device '%(device)' has not been activated",
    },
    {
        .error_fmt = QERR_DEVICE_NOT_ENCRYPTED,
        .desc      = "Device '%(device)' is not encrypted",
    },
    {
        .error_fmt = QERR_DEVICE_NOT_FOUND,
        .desc      = "Device '%(device)' not found",
    },
    {
        .error_fmt = QERR_DEVICE_NOT_REMOVABLE,
        .desc      = "Device '%(device)' is not removable",
    },
    {
        .error_fmt = QERR_DUPLICATE_ID,
        .desc      = "Duplicate ID '%(id)' for %(object)",
    },
    {
        .error_fmt = QERR_FD_NOT_FOUND,
        .desc      = "File descriptor named '%(name)' not found",
    },
    {
        .error_fmt = QERR_FD_NOT_SUPPLIED,
        .desc      = "No file descriptor supplied via SCM_RIGHTS",
    },
    {
        .error_fmt = QERR_FEATURE_DISABLED,
        .desc      = "The feature '%(name)' is not enabled",
    },
    {
        .error_fmt = QERR_INVALID_BLOCK_FORMAT,
        .desc      = "Invalid block format '%(name)'",
    },
    {
        .error_fmt = QERR_INVALID_PARAMETER,
        .desc      = "Invalid parameter '%(name)'",
    },
    {
        .error_fmt = QERR_INVALID_PARAMETER_COMBINATION,
        .desc      = "Invalid parameter combination",
    },
    {
        .error_fmt = QERR_INVALID_PARAMETER_TYPE,
        .desc      = "Invalid parameter type for '%(name)', expected: %(expected)",
    },
    {
        .error_fmt = QERR_INVALID_PARAMETER_VALUE,
        .desc      = "Parameter '%(name)' expects %(expected)",
    },
    {
        .error_fmt = QERR_INVALID_PASSWORD,
        .desc      = "Password incorrect",
    },
    {
        .error_fmt = QERR_IO_ERROR,
        .desc      = "An IO error has occurred",
    },
    {
        .error_fmt = QERR_JSON_PARSE_ERROR,
        .desc      = "JSON parse error, %(message)",

    },
    {
        .error_fmt = QERR_JSON_PARSING,
        .desc      = "Invalid JSON syntax",
    },
    {
        .error_fmt = QERR_KVM_MISSING_CAP,
        .desc      = "Using KVM without %(capability), %(feature) unavailable",
    },
    {
        .error_fmt = QERR_MIGRATION_ACTIVE,
        .desc      = "There's a migration process in progress",
    },
    {
        .error_fmt = QERR_MIGRATION_NOT_SUPPORTED,
        .desc      = "State blocked by non-migratable device '%(device)'",
    },
    {
        .error_fmt = QERR_MIGRATION_EXPECTED,
        .desc      = "An incoming migration is expected before this command can be executed",
    },
    {
        .error_fmt = QERR_MISSING_PARAMETER,
        .desc      = "Parameter '%(name)' is missing",
    },
    {
        .error_fmt = QERR_NO_BUS_FOR_DEVICE,
        .desc      = "No '%(bus)' bus found for device '%(device)'",
    },
    {
        .error_fmt = QERR_NOT_SUPPORTED,
        .desc      = "Not supported",
    },
    {
        .error_fmt = QERR_OPEN_FILE_FAILED,
        .desc      = "Could not open '%(filename)'",
    },
    {
        .error_fmt = QERR_PERMISSION_DENIED,
        .desc      = "Insufficient permission to perform this operation",
    },
    {
        .error_fmt = QERR_PROPERTY_NOT_FOUND,
        .desc      = "Property '%(device).%(property)' not found",
    },
    {
        .error_fmt = QERR_PROPERTY_SET_AFTER_REALIZE,
        .desc      = "Property '%(device).%(property)' cannot be set after realize",
    },
    {
        .error_fmt = QERR_PROPERTY_VALUE_BAD,
        .desc      = "Property '%(device).%(property)' doesn't take value '%(value)'",
    },
    {
        .error_fmt = QERR_PROPERTY_VALUE_IN_USE,
        .desc      = "Property '%(device).%(property)' can't take value '%(value)', it's in use",
    },
    {
        .error_fmt = QERR_PROPERTY_VALUE_NOT_FOUND,
        .desc      = "Property '%(device).%(property)' can't find value '%(value)'",
    },
    {
        .error_fmt = QERR_PROPERTY_VALUE_NOT_POWER_OF_2,
        .desc      = "Property '%(device).%(property)' doesn't take "
                     "value '%(value)', it's not a power of 2",
    },
    {
        .error_fmt = QERR_PROPERTY_VALUE_OUT_OF_RANGE,
        .desc      = "Property '%(device).%(property)' doesn't take "
                     "value %(value) (minimum: %(min), maximum: %(max))",
    },
    {
        .error_fmt = QERR_QGA_COMMAND_FAILED,
        .desc      = "Guest agent command failed, error was '%(message)'",
    },
    {
        .error_fmt = QERR_QGA_LOGGING_FAILED,
        .desc      = "Guest agent failed to log non-optional log statement",
    },
    {
        .error_fmt = QERR_QMP_BAD_INPUT_OBJECT,
        .desc      = "Expected '%(expected)' in QMP input",
    },
    {
        .error_fmt = QERR_QMP_BAD_INPUT_OBJECT_MEMBER,
        .desc      = "QMP input object member '%(member)' expects '%(expected)'",
    },
    {
        .error_fmt = QERR_QMP_EXTRA_MEMBER,
        .desc      = "QMP input object member '%(member)' is unexpected",
    },
    {
        .error_fmt = QERR_RESET_REQUIRED,
        .desc      = "Resetting the Virtual Machine is required",
    },
    {
        .error_fmt = QERR_SET_PASSWD_FAILED,
        .desc      = "Could not set password",
    },
    {
        .error_fmt = QERR_TOO_MANY_FILES,
        .desc      = "Too many open files",
    },
    {
        .error_fmt = QERR_UNDEFINED_ERROR,
        .desc      = "An undefined error has occurred",
    },
    {
        .error_fmt = QERR_UNKNOWN_BLOCK_FORMAT_FEATURE,
        .desc      = "'%(device)' uses a %(format) feature which is not "
                     "supported by this qemu version: %(feature)",
    },
    {
        .error_fmt = QERR_UNSUPPORTED,
        .desc      = "this feature or command is not currently supported",
    },
    {
        .error_fmt = QERR_VIRTFS_FEATURE_BLOCKS_MIGRATION,
        .desc      = "Migration is disabled when VirtFS export path '%(path)' "
                     "is mounted in the guest using mount_tag '%(tag)'",
    },
    {
        .error_fmt = QERR_VNC_SERVER_FAILED,
        .desc      = "Could not start VNC server on %(target)",
    },
    {}
};

/**
 * qerror_new(): Create a new QError
 *
 * Return strong reference.
 */
QError *qerror_new(void)
{
    QError *qerr;

    qerr = g_malloc0(sizeof(*qerr));
    QOBJECT_INIT(qerr, &qerror_type);

    return qerr;
}

static void GCC_FMT_ATTR(2, 3) qerror_abort(const QError *qerr,
                                            const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "qerror: bad call in function '%s':\n", qerr->func);
    fprintf(stderr, "qerror: -> ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\nqerror: call at %s:%d\n", qerr->file, qerr->linenr);
    abort();
}

static void GCC_FMT_ATTR(2, 0) qerror_set_data(QError *qerr,
                                               const char *fmt, va_list *va)
{
    QObject *obj;

    obj = qobject_from_jsonv(fmt, va);
    if (!obj) {
        qerror_abort(qerr, "invalid format '%s'", fmt);
    }
    if (qobject_type(obj) != QTYPE_QDICT) {
        qerror_abort(qerr, "error format is not a QDict '%s'", fmt);
    }

    qerr->error = qobject_to_qdict(obj);

    obj = qdict_get(qerr->error, "class");
    if (!obj) {
        qerror_abort(qerr, "missing 'class' key in '%s'", fmt);
    }
    if (qobject_type(obj) != QTYPE_QSTRING) {
        qerror_abort(qerr, "'class' key value should be a QString");
    }
    
    obj = qdict_get(qerr->error, "data");
    if (!obj) {
        qerror_abort(qerr, "missing 'data' key in '%s'", fmt);
    }
    if (qobject_type(obj) != QTYPE_QDICT) {
        qerror_abort(qerr, "'data' key value should be a QDICT");
    }
}

static void qerror_set_desc(QError *qerr, const char *fmt)
{
    int i;

    // FIXME: inefficient loop

    for (i = 0; qerror_table[i].error_fmt; i++) {
        if (strcmp(qerror_table[i].error_fmt, fmt) == 0) {
            qerr->entry = &qerror_table[i];
            return;
        }
    }

    qerror_abort(qerr, "error format '%s' not found", fmt);
}

/**
 * qerror_from_info(): Create a new QError from error information
 *
 * The information consists of:
 *
 * - file   the file name of where the error occurred
 * - linenr the line number of where the error occurred
 * - func   the function name of where the error occurred
 * - fmt    JSON printf-like dictionary, there must exist keys 'class' and
 *          'data'
 * - va     va_list of all arguments specified by fmt
 *
 * Return strong reference.
 */
QError *qerror_from_info(const char *file, int linenr, const char *func,
                         const char *fmt, va_list *va)
{
    QError *qerr;

    qerr = qerror_new();
    loc_save(&qerr->loc);
    qerr->linenr = linenr;
    qerr->file = file;
    qerr->func = func;

    if (!fmt) {
        qerror_abort(qerr, "QDict not specified");
    }

    qerror_set_data(qerr, fmt, va);
    qerror_set_desc(qerr, fmt);

    return qerr;
}

static void parse_error(const QErrorStringTable *entry, int c)
{
    fprintf(stderr, "expected '%c' in '%s'", c, entry->desc);
    abort();
}

static const char *append_field(QDict *error, QString *outstr,
                                const QErrorStringTable *entry,
                                const char *start)
{
    QObject *obj;
    QDict *qdict;
    QString *key_qs;
    const char *end, *key;

    if (*start != '%')
        parse_error(entry, '%');
    start++;
    if (*start != '(')
        parse_error(entry, '(');
    start++;

    end = strchr(start, ')');
    if (!end)
        parse_error(entry, ')');

    key_qs = qstring_from_substr(start, 0, end - start - 1);
    key = qstring_get_str(key_qs);

    qdict = qobject_to_qdict(qdict_get(error, "data"));
    obj = qdict_get(qdict, key);
    if (!obj) {
        abort();
    }

    switch (qobject_type(obj)) {
        case QTYPE_QSTRING:
            qstring_append(outstr, qdict_get_str(qdict, key));
            break;
        case QTYPE_QINT:
            qstring_append_int(outstr, qdict_get_int(qdict, key));
            break;
        default:
            abort();
    }

    QDECREF(key_qs);
    return ++end;
}

static QString *qerror_format_desc(QDict *error,
                                   const QErrorStringTable *entry)
{
    QString *qstring;
    const char *p;

    assert(entry != NULL);

    qstring = qstring_new();

    for (p = entry->desc; *p != '\0';) {
        if (*p != '%') {
            qstring_append_chr(qstring, *p++);
        } else if (*(p + 1) == '%') {
            qstring_append_chr(qstring, '%');
            p += 2;
        } else {
            p = append_field(error, qstring, entry, p);
        }
    }

    return qstring;
}

QString *qerror_format(const char *fmt, QDict *error)
{
    const QErrorStringTable *entry = NULL;
    int i;

    for (i = 0; qerror_table[i].error_fmt; i++) {
        if (strcmp(qerror_table[i].error_fmt, fmt) == 0) {
            entry = &qerror_table[i];
            break;
        }
    }

    return qerror_format_desc(error, entry);
}

/**
 * qerror_human(): Format QError data into human-readable string.
 */
QString *qerror_human(const QError *qerror)
{
    return qerror_format_desc(qerror->error, qerror->entry);
}

/**
 * qerror_print(): Print QError data
 *
 * This function will print the member 'desc' of the specified QError object,
 * it uses error_report() for this, so that the output is routed to the right
 * place (ie. stderr or Monitor's device).
 */
void qerror_print(QError *qerror)
{
    QString *qstring = qerror_human(qerror);
    loc_push_restore(&qerror->loc);
    error_report("%s", qstring_get_str(qstring));
    loc_pop(&qerror->loc);
    QDECREF(qstring);
}

void qerror_report_internal(const char *file, int linenr, const char *func,
                            const char *fmt, ...)
{
    va_list va;
    QError *qerror;

    va_start(va, fmt);
    qerror = qerror_from_info(file, linenr, func, fmt, &va);
    va_end(va);

    if (monitor_cur_is_qmp()) {
        monitor_set_error(cur_mon, qerror);
    } else {
        qerror_print(qerror);
        QDECREF(qerror);
    }
}

/* Evil... */
struct Error
{
    QDict *obj;
    const char *fmt;
    char *msg;
};

void qerror_report_err(Error *err)
{
    QError *qerr;
    int i;

    qerr = qerror_new();
    loc_save(&qerr->loc);
    QINCREF(err->obj);
    qerr->error = err->obj;

    for (i = 0; qerror_table[i].error_fmt; i++) {
        if (strcmp(qerror_table[i].error_fmt, err->fmt) == 0) {
            qerr->entry = &qerror_table[i];
            break;
        }
    }

    if (monitor_cur_is_qmp()) {
        monitor_set_error(cur_mon, qerr);
    } else {
        qerror_print(qerr);
        QDECREF(qerr);
    }
}

void assert_no_error(Error *err)
{
    if (err) {
        qerror_report_err(err);
        abort();
    }
}

/**
 * qobject_to_qerror(): Convert a QObject into a QError
 */
QError *qobject_to_qerror(const QObject *obj)
{
    if (qobject_type(obj) != QTYPE_QERROR) {
        return NULL;
    }

    return container_of(obj, QError, base);
}

/**
 * qerror_destroy_obj(): Free all memory allocated by a QError
 */
static void qerror_destroy_obj(QObject *obj)
{
    QError *qerr;

    assert(obj != NULL);
    qerr = qobject_to_qerror(obj);

    QDECREF(qerr->error);
    g_free(qerr);
}
