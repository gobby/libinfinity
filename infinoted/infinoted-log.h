/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_LOG_H__
#define __INFINOTED_LOG_H__

#include <infinoted/infinoted-options.h>
#include <infinoted/infinoted-record.h>

#include <libinfinity/server/infd-directory.h>
#include <libinfinity/adopted/inf-adopted-request.h>
#include <libinfinity/adopted/inf-adopted-user.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _InfinotedLogSession InfinotedLogSession;
typedef struct _InfinotedLog InfinotedLog;
struct _InfinotedLog {
  FILE* file;

  InfdDirectory* directory;
  InfinotedRecord* record;

  GSList* connections;
  GSList* sessions;

  GLogFunc prev_log_handler;

  InfinotedLogSession* current_session;
  InfAdoptedRequest* current_request;
  InfAdoptedUser* current_user;
};

InfinotedLog*
infinoted_log_new(InfinotedOptions* options,
                  GError** error);

void
infinoted_log_free(InfinotedLog* log);

void
infinoted_log_set_directory(InfinotedLog* log,
                            InfdDirectory* directory);

void
infinoted_log_set_record(InfinotedLog* log,
                         InfinotedRecord* record);

void
infinoted_log_error(InfinotedLog* log, const char* fmt, ...);

void
infinoted_log_warning(InfinotedLog* log, const char* fmt, ...);

void
infinoted_log_info(InfinotedLog* log, const char* fmt, ...);

G_END_DECLS

#endif /* __INFINOTED_LOG_H__ */

/* vim:set et sw=2 ts=2: */
