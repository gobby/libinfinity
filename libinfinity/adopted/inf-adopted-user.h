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

#ifndef __INF_ADOPTED_USER_H__
#define __INF_ADOPTED_USER_H__

#include <libinfinity/adopted/inf-adopted-state-vector.h>
#include <libinfinity/common/inf-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_USER                 (inf_adopted_user_get_type())
#define INF_ADOPTED_USER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_USER, InfAdoptedUser))
#define INF_ADOPTED_USER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_ADOPTED_TYPE_USER, InfAdoptedUserClass))
#define INF_ADOPTED_IS_USER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_USER))
#define INF_ADOPTED_IS_USER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_ADOPTED_TYPE_USER))
#define INF_ADOPTED_USER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_ADOPTED_TYPE_USER, InfAdoptedUserClass))

typedef struct _InfAdoptedUser InfAdoptedUser;
typedef struct _InfAdoptedUserClass InfAdoptedUserClass;

struct _InfAdoptedUserClass {
  InfUserClass parent_class;
};

struct _InfAdoptedUser {
  InfUser parent;
};

GType
inf_adopted_user_get_type(void) G_GNUC_CONST;

guint
inf_adopted_user_get_component(InfAdoptedUser* user,
                               InfUser* component);

InfAdoptedStateVector*
inf_adopted_user_get_vector(InfAdoptedUser* user);

void
inf_adopted_user_set_vector(InfAdoptedUser* user,
                            InfAdoptedStateVector* vec);

G_END_DECLS

#endif /* __INF_ADOPTED_USER_H__ */

/* vim:set et sw=2 ts=2: */
