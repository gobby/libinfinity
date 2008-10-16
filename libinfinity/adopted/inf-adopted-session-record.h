/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __INF_ADOPTED_SESSION_RECORD_RECORD_H__
#define __INF_ADOPTED_SESSION_RECORD_RECORD_H__

#include <libinfinity/adopted/inf-adopted-session.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_SESSION_RECORD                 (inf_adopted_session_record_get_type())
#define INF_ADOPTED_SESSION_RECORD(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_SESSION_RECORD, InfAdoptedSessionRecord))
#define INF_ADOPTED_SESSION_RECORD_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_ADOPTED_TYPE_SESSION_RECORD, InfAdoptedSessionRecordClass))
#define INF_ADOPTED_IS_SESSION_RECORD(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_SESSION_RECORD))
#define INF_ADOPTED_IS_SESSION_RECORD_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_ADOPTED_TYPE_SESSION_RECORD))
#define INF_ADOPTED_SESSION_RECORD_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_ADOPTED_TYPE_SESSION_RECORD, InfAdoptedSessionRecordClass))

typedef struct _InfAdoptedSessionRecord InfAdoptedSessionRecord;
typedef struct _InfAdoptedSessionRecordClass InfAdoptedSessionRecordClass;

/**
 * InfAdoptedSessionRecordClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfAdoptedSessionRecordClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfAdoptedSessionRecord:
 *
 * #InfAdoptedSessionRecord is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfAdoptedSessionRecord {
  /*< private >*/
  GObject parent;
};

GType
inf_adopted_session_record_get_type(void);

InfAdoptedSessionRecord*
inf_adopted_session_record_new(InfAdoptedSession* session);

gboolean
inf_adopted_session_record_start_recording(InfAdoptedSessionRecord* record,
                                           const gchar* filename,
                                           GError** error);

gboolean
inf_adopted_session_record_stop_recording(InfAdoptedSessionRecord* record,
                                          GError** error);

gboolean
inf_adopted_session_record_is_recording(InfAdoptedSessionRecord* record);

G_END_DECLS

#endif /* __INF_ADOPTED_SESSION_RECORD_H__ */

/* vim:set et sw=2 ts=2: */
