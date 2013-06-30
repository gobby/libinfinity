/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_GTK_CHAT_H__
#define __INF_GTK_CHAT_H__

#include <libinfinity/common/inf-chat-session.h>
#include <libinfinity/common/inf-user.h>

#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_CHAT                 (inf_gtk_chat_get_type())
#define INF_GTK_CHAT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_CHAT, InfGtkChat))
#define INF_GTK_CHAT_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_CHAT, InfGtkChatClass))
#define INF_GTK_IS_CHAT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_CHAT))
#define INF_GTK_IS_CHAT_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_CHAT))
#define INF_GTK_CHAT_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_CHAT, InfGtkChatClass))

typedef struct _InfGtkChat InfGtkChat;
typedef struct _InfGtkChatClass InfGtkChatClass;

/**
 * InfGtkChatClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfGtkChatClass {
  /*< private >*/
  GtkVBoxClass parent_class;
};

/**
 * InfGtkChat:
 *
 * #InfGtkChat is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfGtkChat {
  /*< private >*/
  GtkVBox parent;
};

GType
inf_gtk_chat_get_type(void) G_GNUC_CONST;

GtkWidget*
inf_gtk_chat_new(void);

void
inf_gtk_chat_set_session(InfGtkChat* chat,
                         InfChatSession* session);

void
inf_gtk_chat_set_active_user(InfGtkChat* chat,
                             InfUser* user);

InfUser*
inf_gtk_chat_get_active_user(InfGtkChat* chat);

GtkWidget*
inf_gtk_chat_get_entry(InfGtkChat* chat);

G_END_DECLS

#endif /* __INF_GTK_CHAT_H__ */

/* vim:set et sw=2 ts=2: */
