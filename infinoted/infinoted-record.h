/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_RECORD_H__
#define __INFINOTED_RECORD_H__

#include <libinfinity/server/infd-directory.h>
#include <libinfinity/adopted/inf-adopted-session-record.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _InfinotedRecord InfinotedRecord;
struct _InfinotedRecord {
  InfdDirectory* directory;
  GSList* records;
};

InfinotedRecord*
infinoted_record_new(InfdDirectory* directory);

void
infinoted_record_free(InfinotedRecord* record);

InfAdoptedSessionRecord*
infinoted_record_get_for_session(InfinotedRecord* record,
                                 InfAdoptedSession* session);

G_END_DECLS

#endif /* __INFINOTED_RECORD_H__ */

/* vim:set et sw=2 ts=2: */
