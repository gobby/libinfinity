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

#ifndef __INFD_SESSION_H__
#define __INFD_SESSION_H__

#include <libinfinity/inf-session.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_SESSION                 (infd_session_get_type())
#define INFD_SESSION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_SESSION, InfdSession))
#define INFD_SESSION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_SESSION, InfdSessionClass))
#define INFD_IS_SESSION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_SESSION))
#define INFD_IS_SESSION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_SESSION))
#define INFD_SESSION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_SESSION, InfdSessionClass))

typedef struct _InfdSession InfdSession;
typedef struct _InfdSessionClass InfdSessionClass;

struct _InfdSessionClass {
  InfSessionClass parent_class;

  GHashTable* message_table;
};

struct _InfdSession {
  InfSession parent;
};

typedef gboolean(*InfdSessionMessageFunc)(InfdSession* session,
                                          InfXmlConnection* connection,
					  const xmlNodePtr xml,
					  GError** error);

GType
infd_session_get_type(void) G_GNUC_CONST;

gboolean
infd_session_class_register_message(InfdSessionClass* session_class,
                                    const gchar* message,
                                    InfdSessionMessageFunc func);

InfUser*
infd_session_add_user(InfdSession* session,
                      const GParameter* params,
                      guint n_params,
                      GError** error);

void
infd_session_subscribe_to(InfdSession* session,
                          InfXmlConnection* connection,
                          const gchar* identifier);

void
infd_session_send_to_subscriptions(InfdSession* session,
                                   InfXmlConnection* exclude,
                                   xmlNodePtr xml);

G_END_DECLS

#endif /* __INFD_SESSION_H__ */
