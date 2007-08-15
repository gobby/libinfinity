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

#ifndef __INF_USER_H__
#define __INF_USER_H__

#include <glib-object.h>

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

struct _InfUserClass {
  GObjectClass parent_class;
};

struct _InfUser {
  GObject parent;
};

typedef enum _InfUserStatus {
  INF_USER_AVAILABLE,
  INF_USER_UNAVAILABLE
  /* TODO: Add further status (AWAY, ...) */
} InfUserStatus;

typedef enum InfUserFlags {
  INF_USER_LOCAL = 1 << 0
} InfUserFlags;

GType
inf_user_status_get_type(void) G_GNUC_CONST;

GType
inf_user_flags_get_type(void) G_GNUC_CONST;

GType
inf_user_get_type(void) G_GNUC_CONST;

guint
inf_user_get_id(const InfUser* user);

const gchar*
inf_user_get_name(const InfUser* user);

InfUserStatus
inf_user_get_status(const InfUser* user);

InfUserFlags
inf_user_get_flags(const InfUser* user);

G_END_DECLS

#endif /* __INF_USER_H__ */
