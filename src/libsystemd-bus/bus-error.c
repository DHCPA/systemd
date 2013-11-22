/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "util.h"
#include "errno-list.h"

#include "sd-bus.h"
#include "bus-error.h"

#define BUS_ERROR_OOM SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_NO_MEMORY, "Out of memory")
#define BUS_ERROR_FAILED SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_FAILED, "Operation failed")

static int bus_error_name_to_errno(const char *name) {
        const char *p;
        int r;

        if (!name)
                return EINVAL;

        p = startswith(name, "Posix.Error.");
        if (p) {
                r = errno_from_name(p);
                if (r <= 0)
                        return EIO;

                return r;
        }

        /* Better replace this with a gperf table */

        if (streq(name, SD_BUS_ERROR_NO_MEMORY))
                return ENOMEM;

        if (streq(name, SD_BUS_ERROR_SERVICE_UNKNOWN))
                return EHOSTUNREACH;

        if (streq(name, SD_BUS_ERROR_NAME_HAS_NO_OWNER))
                return ENXIO;

        if (streq(name, SD_BUS_ERROR_NO_REPLY) ||
            streq(name, SD_BUS_ERROR_TIMEOUT) ||
            streq(name, "org.freedesktop.DBus.Error.TimedOut"))
                return ETIMEDOUT;

        if (streq(name, SD_BUS_ERROR_IO_ERROR))
                return EIO;

        if (streq(name, SD_BUS_ERROR_BAD_ADDRESS))
                return EADDRNOTAVAIL;

        if (streq(name, SD_BUS_ERROR_NOT_SUPPORTED))
                return ENOTSUP;

        if (streq(name, SD_BUS_ERROR_LIMITS_EXCEEDED))
                return ENOBUFS;

        if (streq(name, SD_BUS_ERROR_ACCESS_DENIED) ||
            streq(name, SD_BUS_ERROR_AUTH_FAILED))
                return EACCES;

        if (streq(name, SD_BUS_ERROR_NO_SERVER))
                return EHOSTDOWN;

        if (streq(name, SD_BUS_ERROR_NO_NETWORK))
                return ENONET;

        if (streq(name, SD_BUS_ERROR_ADDRESS_IN_USE))
                return EADDRINUSE;

        if (streq(name, SD_BUS_ERROR_DISCONNECTED))
                return ECONNRESET;

        if (streq(name, SD_BUS_ERROR_INVALID_ARGS) ||
            streq(name, SD_BUS_ERROR_INVALID_SIGNATURE) ||
            streq(name, "org.freedesktop.DBus.Error.MatchRuleInvalid") ||
            streq(name, "org.freedesktop.DBus.Error.InvalidFileContent"))
                return EINVAL;

        if (streq(name, SD_BUS_ERROR_FILE_NOT_FOUND) ||
            streq(name, "org.freedesktop.DBus.Error.MatchRuleNotFound"))
                return ENOENT;

        if (streq(name, SD_BUS_ERROR_FILE_EXISTS))
                return EEXIST;

        if (streq(name, SD_BUS_ERROR_UNKNOWN_METHOD) ||
            streq(name, SD_BUS_ERROR_UNKNOWN_OBJECT) ||
            streq(name, SD_BUS_ERROR_UNKNOWN_INTERFACE) ||
            streq(name, SD_BUS_ERROR_UNKNOWN_PROPERTY))
                return EBADR;

        if (streq(name, SD_BUS_ERROR_PROPERTY_READ_ONLY))
                return EROFS;

        if (streq(name, SD_BUS_ERROR_UNIX_PROCESS_ID_UNKNOWN) ||
            streq(name, "org.freedesktop.DBus.Error.SELinuxSecurityContextUnknown"))
                return ESRCH;

        if (streq(name, SD_BUS_ERROR_INCONSISTENT_MESSAGE))
                return EBADMSG;

        if (streq(name, "org.freedesktop.DBus.Error.ObjectPathInUse"))
                return EBUSY;

        return EIO;
}

static sd_bus_error errno_to_bus_error_const(int error) {

        if (error < 0)
                error = -error;

        switch (error) {

        case ENOMEM:
                return BUS_ERROR_OOM;

        case EPERM:
        case EACCES:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_ACCESS_DENIED, "Access denied");

        case EINVAL:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INVALID_ARGS, "Invalid argument");

        case ESRCH:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_UNIX_PROCESS_ID_UNKNOWN, "No such process");

        case ENOENT:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_FILE_NOT_FOUND, "File not found");

        case EEXIST:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_FILE_EXISTS, "File exists");

        case ETIMEDOUT:
        case ETIME:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_TIMEOUT, "Timed out");

        case EIO:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_IO_ERROR, "Input/output error");

        case ENETRESET:
        case ECONNABORTED:
        case ECONNRESET:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_DISCONNECTED, "Disconnected");

        case ENOTSUP:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_NOT_SUPPORTED, "Not supported");

        case EADDRNOTAVAIL:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_BAD_ADDRESS, "Address not available");

        case ENOBUFS:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_LIMITS_EXCEEDED, "Limits exceeded");

        case EADDRINUSE:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_ADDRESS_IN_USE, "Address in use");

        case EBADMSG:
                return SD_BUS_ERROR_MAKE_CONST(SD_BUS_ERROR_INCONSISTENT_MESSAGE, "Inconsistent message");
        }

        return SD_BUS_ERROR_NULL;
}

static int errno_to_bus_error_name_new(int error, char **ret) {
        const char *name;
        char *n;

        if (error < 0)
                error = -error;

        name = errno_to_name(error);
        if (!name)
                return 0;

        n = strappend("Posix.Error.", name);
        if (!n)
                return -ENOMEM;

        *ret = n;
        return 1;
}

bool bus_error_is_dirty(sd_bus_error *e) {
        if (!e)
                return false;

        return e->name || e->message || e->need_free;
}

_public_ void sd_bus_error_free(sd_bus_error *e) {
        if (!e)
                return;

        if (e->need_free) {
                free((void*) e->name);
                free((void*) e->message);
        }

        e->name = e->message = NULL;
        e->need_free = false;
}

_public_ int sd_bus_error_set(sd_bus_error *e, const char *name, const char *message) {

        if (!name)
                return 0;
        if (!e)
                goto finish;

        assert_return(!bus_error_is_dirty(e), -EINVAL);

        e->name = strdup(name);
        if (!e->name) {
                *e = BUS_ERROR_OOM;
                return -ENOMEM;
        }

        if (message)
                e->message = strdup(message);

        e->need_free = true;

finish:
        return -bus_error_name_to_errno(name);
}

int bus_error_setfv(sd_bus_error *e, const char *name, const char *format, va_list ap) {

        if (!name)
                return 0;
        if (!e)
                goto finish;

        assert_return(!bus_error_is_dirty(e), -EINVAL);

        e->name = strdup(name);
        if (!e->name) {
                *e = BUS_ERROR_OOM;
                return -ENOMEM;
        }

        if (format)
                vasprintf((char**) &e->message, format, ap);

        e->need_free = true;

finish:
        return -bus_error_name_to_errno(name);
}

_public_ int sd_bus_error_setf(sd_bus_error *e, const char *name, const char *format, ...) {

        if (format) {
                int r;
                va_list ap;

                va_start(ap, format);
                r = bus_error_setfv(e, name, format, ap);
                va_end(ap);

                return r;
        }

        return sd_bus_error_set(e, name, NULL);
}

_public_ int sd_bus_error_copy(sd_bus_error *dest, const sd_bus_error *e) {

        if (!sd_bus_error_is_set(e))
                return 0;
        if (!dest)
                goto finish;

        assert_return(!bus_error_is_dirty(dest), -EINVAL);

        if (!e->need_free)
                *dest = *e;
        else {
                dest->name = strdup(e->name);
                if (!dest->name) {
                        *dest = BUS_ERROR_OOM;
                        return -ENOMEM;
                }

                if (e->message)
                        dest->message = strdup(e->message);

                dest->need_free = true;
        }

finish:
        return -bus_error_name_to_errno(e->name);
}

_public_ int sd_bus_error_set_const(sd_bus_error *e, const char *name, const char *message) {
        if (!name)
                return 0;
        if (!e)
                goto finish;

        assert_return(!bus_error_is_dirty(e), -EINVAL);

        *e = SD_BUS_ERROR_MAKE_CONST(name, message);

finish:
        return -bus_error_name_to_errno(name);
}

_public_ int sd_bus_error_is_set(const sd_bus_error *e) {
        if (!e)
                return 0;

        return !!e->name;
}

_public_ int sd_bus_error_has_name(const sd_bus_error *e, const char *name) {
        if (!e)
                return 0;

        return streq_ptr(e->name, name);
}

_public_ int sd_bus_error_get_errno(const sd_bus_error* e) {
        if (!e)
                return 0;

        return bus_error_name_to_errno(e->name);
}

static void bus_error_strerror(sd_bus_error *e, int error) {
        size_t k = 64;
        char *m;

        assert(e);

        for (;;) {
                char *x;

                m = new(char, k);
                if (!m)
                        return;

                errno = 0;
                x = strerror_r(error, m, k);
                if (errno == ERANGE || strlen(x) >= k - 1) {
                        free(m);
                        k *= 2;
                        continue;
                }

                if (!x || errno) {
                        free(m);
                        return;
                }

                if (x == m) {
                        if (e->need_free) {
                                /* Error is already dynamic, let's just update the message */
                                free((char*) e->message);
                                e->message = x;

                        } else {
                                char *t;
                                /* Error was const so far, let's make it dynamic, if we can */

                                t = strdup(e->name);
                                if (!t) {
                                        free(m);
                                        return;
                                }

                                e->need_free = true;
                                e->name = t;
                                e->message = x;
                        }
                } else {
                        free(m);

                        if (e->need_free) {
                                char *t;

                                /* Error is dynamic, let's hence make the message also dynamic */
                                t = strdup(x);
                                if (!t)
                                        return;

                                free((char*) e->message);
                                e->message = t;
                        } else {
                                /* Error is const, hence we can just override */
                                e->message = x;
                        }
                }

                return;
        }
}

_public_ int sd_bus_error_set_errno(sd_bus_error *e, int error) {

        if (error < 0)
                error = -error;

        if (!e)
                return -error;
        if (error == 0)
                return -error;

        assert_return(!bus_error_is_dirty(e), -EINVAL);

        /* First, try a const translation */
        *e = errno_to_bus_error_const(error);

        if (!sd_bus_error_is_set(e)) {
                int k;

                /* If that didn't work, try a dynamic one. */

                k = errno_to_bus_error_name_new(error, (char**) &e->name);
                if (k > 0)
                        e->need_free = true;
                else if (k < 0) {
                        *e = BUS_ERROR_OOM;
                        return -error;
                } else
                        *e = BUS_ERROR_FAILED;
        }

        /* Now, fill in the message from strerror() if we can */
        bus_error_strerror(e, error);
        return -error;
}

int bus_error_set_errnofv(sd_bus_error *e, int error, const char *format, va_list ap) {
        int r;

        if (error < 0)
                error = -error;

        if (!e)
                return -error;
        if (error == 0)
                return 0;

        assert_return(!bus_error_is_dirty(e), -EINVAL);

        /* First, try a const translation */
        *e = errno_to_bus_error_const(error);

        if (!sd_bus_error_is_set(e)) {
                int k;

                /* If that didn't work, try a dynamic one */

                k = errno_to_bus_error_name_new(error, (char**) &e->name);
                if (k > 0)
                        e->need_free = true;
                else if (k < 0) {
                        *e = BUS_ERROR_OOM;
                        return -ENOMEM;
                } else
                        *e = BUS_ERROR_FAILED;
        }

        if (format) {
                char *m;

                /* First, let's try to fill in the supplied message */

                r = vasprintf(&m, format, ap);
                if (r >= 0) {

                        if (!e->need_free) {
                                char *t;

                                t = strdup(e->name);
                                if (t) {
                                        e->need_free = true;
                                        e->name = t;
                                        e->message = m;
                                        return -error;
                                }

                                free(m);
                        } else {
                                free((char*) e->message);
                                e->message = m;
                                return -error;
                        }
                }
        }

        /* If that didn't work, use strerror() for the message */
        bus_error_strerror(e, error);
        return -error;
}

_public_ int sd_bus_error_set_errnof(sd_bus_error *e, int error, const char *format, ...) {
        int r;

        if (error < 0)
                error = -error;

        if (!e)
                return -error;
        if (error == 0)
                return 0;

        assert_return(!bus_error_is_dirty(e), -EINVAL);

        if (format) {
                va_list ap;

                va_start(ap, format);
                r = bus_error_set_errnofv(e, error, format, ap);
                va_end(ap);

                return r;
        }

        return sd_bus_error_set_errno(e, error);
}

const char *bus_error_message(const sd_bus_error *e, int error) {

        if (e) {
                /* Sometimes the D-Bus server is a little bit too verbose with
                 * its error messages, so let's override them here */
                if (sd_bus_error_has_name(e, SD_BUS_ERROR_ACCESS_DENIED))
                        return "Access denied";

                if (e->message)
                        return e->message;
        }

        if (error < 0)
                error = -error;

        return strerror(error);
}
