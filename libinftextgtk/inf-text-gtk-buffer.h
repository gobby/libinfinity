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

#ifndef __INF_TEXT_GTK_BUFFER_H__
#define __INF_TEXT_GTK_BUFFER_H__

#include <libinftext/inf-text-user.h>
#include <libinfinity/common/inf-user-table.h>
#include <libinfinity/common/inf-user.h>

#include <gtk/gtktextbuffer.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_GTK_TYPE_BUFFER                 (inf_text_gtk_buffer_get_type())
#define INF_TEXT_GTK_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_GTK_TYPE_BUFFER, InfTextGtkBuffer))
#define INF_TEXT_GTK_BUFFER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_GTK_TYPE_BUFFER, InfTextGtkBufferClass))
#define INF_TEXT_GTK_IS_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_GTK_TYPE_BUFFER))
#define INF_TEXT_GTK_IS_BUFFER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_GTK_TYPE_BUFFER))
#define INF_TEXT_GTK_BUFFER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_GTK_TYPE_BUFFER, InfTextGtkBufferClass))

typedef struct _InfTextGtkBuffer InfTextGtkBuffer;
typedef struct _InfTextGtkBufferClass InfTextGtkBufferClass;

struct _InfTextGtkBufferClass {
  GObjectClass parent_class;
};

struct _InfTextGtkBuffer {
  GObject parent;
};

GType
inf_text_gtk_buffer_get_type(void) G_GNUC_CONST;

InfTextGtkBuffer*
inf_text_gtk_buffer_new(GtkTextBuffer* buffer,
                        InfUserTable* user_table);

GtkTextBuffer*
inf_text_gtk_buffer_get_text_buffer(InfTextGtkBuffer* buffer);

void
inf_text_gtk_buffer_set_active_user(InfTextGtkBuffer* buffer,
                                    InfTextUser* user);

InfTextUser*
inf_text_gtk_buffer_get_active_user(InfTextGtkBuffer* buffer);

void
inf_text_gtk_buffer_set_wake_on_cursor_movement(InfTextGtkBuffer* buffer,
                                                gboolean wake);

gboolean
inf_text_gtk_buffer_get_wake_on_cursor_movement(InfTextGtkBuffer* buffer);

void
inf_text_gtk_buffer_ensure_author_tags_priority(InfTextGtkBuffer* buffer);

G_END_DECLS

#endif /* __INF_TEXT_GTK_BUFFER_H__ */

/* vim:set et sw=2 ts=2: */
