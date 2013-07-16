/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_TRAFFIC_LOGGER_H__
#define __INFINOTED_TRAFFIC_LOGGER_H__

#include <infinoted/infinoted-log.h>

#include <libinfinity/server/infd-directory.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _InfinotedTrafficLogger InfinotedTrafficLogger;
struct _InfinotedTrafficLogger {
  InfdDirectory* directory;
  InfinotedLog* log;
  gchar* path;

  GSList* connections;
};

InfinotedTrafficLogger*
infinoted_traffic_logger_new(InfdDirectory* directory,
                             InfinotedLog* log,
                             const gchar* path);

void
infinoted_traffic_logger_free(InfinotedTrafficLogger* logger);

G_END_DECLS

#endif /* __INFINOTED_TRAFFIC_LOGGER_H__ */

/* vim:set et sw=2 ts=2: */
