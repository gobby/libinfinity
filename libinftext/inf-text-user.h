/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_USER_H__
#define __INF_TEXT_USER_H__

#include <libinfinity/adopted/inf-adopted-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_USER                 (inf_text_user_get_type())
#define INF_TEXT_USER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_USER, InfTextUser))
#define INF_TEXT_USER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_TYPE_USER, InfTextUserClass))
#define INF_TEXT_IS_USER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_USER))
#define INF_TEXT_IS_USER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_TYPE_USER))
#define INF_TEXT_USER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_TYPE_USER, InfTextUserClass))

typedef struct _InfTextUser InfTextUser;
typedef struct _InfTextUserClass InfTextUserClass;

struct _InfTextUserClass {
  InfAdoptedUserClass parent_class;

  void(*selection_changed)(InfTextUser* user,
                           guint position,
                           guint length,
                           gboolean by_request);
};

struct _InfTextUser {
  InfAdoptedUser parent;
};

GType
inf_text_user_get_type(void) G_GNUC_CONST;

InfTextUser*
inf_text_user_new(guint id,
                  const gchar* name,
                  InfAdoptedStateVector* vector,
                  double hue);

guint
inf_text_user_get_caret_position(InfTextUser* user);

gint
inf_text_user_get_selection_length(InfTextUser* user);

void
inf_text_user_set_selection(InfTextUser* user,
                            guint position,
                            gint length,
                            gboolean by_request);

gdouble
inf_text_user_get_hue(InfTextUser* user);

G_END_DECLS

#endif /* __INF_TEXT_USER_H__ */

/* vim:set et sw=2 ts=2: */
