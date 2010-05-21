/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
 *
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef __INFINOTED_UTIL_H__
#define __INFINOTED_UTIL_H__

#include <libinfinity/inf-config.h>

#include <glib/gtypes.h>
#include <glib/gerror.h>

G_BEGIN_DECLS

gboolean
infinoted_util_create_dirname(const gchar* path,
                              GError** error);

/* TODO: Move this to infinoted-log.[hc] */
void
infinoted_util_log_error(const char* fmt, ...);

void
infinoted_util_log_warning(const char* fmt, ...);

void
infinoted_util_log_info(const char* fmt, ...);

void
infinoted_util_set_errno_error(GError** error,
                               int save_errno,
                               const char* prefix);

#ifdef LIBINFINITY_HAVE_LIBDAEMON
void
infinoted_util_daemon_set_global_pid_file_proc(void);

void
infinoted_util_daemon_set_local_pid_file_proc(void);

int
infinoted_util_daemon_pid_file_kill(int sig);
#endif

G_END_DECLS

#endif /* __INFINOTED_UTIL_H__ */

/* vim:set et sw=2 ts=2: */
