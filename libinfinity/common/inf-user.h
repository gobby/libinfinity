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

#ifndef __INF_USER_H__
#define __INF_USER_H__

#include <glib-object.h>

#include <libinfinity/common/inf-xml-connection.h>

G_BEGIN_DECLS

#define INF_TYPE_USER                 (inf_user_get_type())
#define INF_USER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_USER, InfUser))
#define INF_USER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_USER, InfUserClass))
#define INF_IS_USER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_USER))
#define INF_IS_USER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_USER))
#define INF_USER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_USER, InfUserClass))

#define INF_TYPE_USER_STATUS          (inf_user_status_get_type())
#define INF_TYPE_USER_FLAGS           (inf_user_flags_get_type())

typedef struct _InfUser InfUser;
typedef struct _InfUserClass InfUserClass;

typedef enum _InfUserStatus {
  INF_USER_ACTIVE,
  INF_USER_INACTIVE,
  INF_USER_UNAVAILABLE
} InfUserStatus;

typedef enum InfUserFlags {
  INF_USER_LOCAL = 1 << 0
} InfUserFlags;

struct _InfUserClass {
  GObjectClass parent_class;

  void (*set_status)(InfUser* user,
                     InfUserStatus status);
};

struct _InfUser {
  GObject parent;
  gpointer priv;
};

GType
inf_user_status_get_type(void) G_GNUC_CONST;

GType
inf_user_flags_get_type(void) G_GNUC_CONST;

GType
inf_user_get_type(void) G_GNUC_CONST;

guint
inf_user_get_id(InfUser* user);

const gchar*
inf_user_get_name(InfUser* user);

InfUserStatus
inf_user_get_status(InfUser* user);

InfUserFlags
inf_user_get_flags(InfUser* user);

InfXmlConnection*
inf_user_get_connection(InfUser* user);

const gchar*
inf_user_status_to_string(InfUserStatus status);

gboolean
inf_user_status_from_string(const gchar* string,
                            InfUserStatus* status,
                            GError** error);

G_END_DECLS

#endif /* __INF_USER_H__ */

/* vim:set et sw=2 ts=2: */
