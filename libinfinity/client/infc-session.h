/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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

#ifndef __INFC_SESSION_H__
#define __INFC_SESSION_H__

#include <libinfinity/client/infc-user-request.h>
#include <libinfinity/common/inf-session.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_SESSION                 (infc_session_get_type())
#define INFC_SESSION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_SESSION, InfcSession))
#define INFC_SESSION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_SESSION, InfcSessionClass))
#define INFC_IS_SESSION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_SESSION))
#define INFC_IS_SESSION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_SESSION))
#define INFC_SESSION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_SESSION, InfcSessionClass))

typedef struct _InfcSession InfcSession;
typedef struct _InfcSessionClass InfcSessionClass;

struct _InfcSessionClass {
  InfSessionClass parent_class;

  GError* (*translate_error)(InfcSession* session,
                             GQuark domain,
                             guint code);

  GHashTable* message_table;
};

struct _InfcSession {
  InfSession parent;
};

typedef gboolean(*InfcSessionMessageFunc)(InfcSession* session,
                                          InfXmlConnection* connection,
                                          xmlNodePtr xml,
                                          GError** error);

GType
infc_session_get_type(void) G_GNUC_CONST;

gboolean
infc_session_class_register_message(InfcSessionClass* session_class,
                                    const gchar* message,
                                    InfcSessionMessageFunc func);

void
infc_session_set_connection(InfcSession* session,
                            InfXmlConnection* connection,
                            const gchar* identifier);

InfcUserRequest*
infc_session_join_user(InfcSession* session,
                       const GParameter* params,
                       guint n_params,
                       GError** error);

InfcUserRequest*
infc_session_leave_user(InfcSession* session,
                        InfUser* user,
                        GError** error);

G_END_DECLS

#endif /* __INFC_SESSION_H__ */

/* vim:set et sw=2 ts=2: */
