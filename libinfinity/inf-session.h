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

#ifndef __INF_SESSION_H__
#define __INF_SESSION_H__

#include <libinfinity/inf-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_SESSION                 (inf_session_get_type())
#define INF_SESSION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_USER, InfSession))
#define INF_SESSION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_USER, InfSessionClass))
#define INF_IS_SESSION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_USER))
#define INF_IS_SESSION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_USER))
#define INF_SESSION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_USER, InfSessionClass))

typedef struct _InfSession InfSession;
typedef struct _InfSessionClass InfSessionClass;

struct _InfSessionClass {
  GObjectClass parent_class;

  /* Signals */
  void(*add_user)(InfSession* session,
                  InfUser* user);

  void(*remove_user)(InfSession* session,
                     InfUser* user);
};

struct _InfSession {
  GObject parent;
};

typedef void(*InfSessionForeachUserFunc)(InfUser* user,
                                         gpointer user_data);

GType
inf_session_get_type(void) G_GNUC_CONST;

void
inf_session_add_user(InfSession* session,
                     InfUser* user);

void
inf_session_remove_user(InfSession* session,
                        InfUser* user);

InfUser*
inf_session_lookup_user_by_id(InfSession* session,
                              guint user_id);

void
inf_session_foreach_user(InfSession* session,
                         InfSessionForeachUserFunc func,
                         gpointer user_data);

G_END_DECLS

#endif /* __INF_SESSION_H__ */
