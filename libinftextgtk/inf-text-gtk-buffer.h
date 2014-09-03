/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_GTK_BUFFER_H__
#define __INF_TEXT_GTK_BUFFER_H__

#include <libinftext/inf-text-user.h>
#include <libinfinity/common/inf-user-table.h>
#include <libinfinity/common/inf-user.h>

#include <gtk/gtk.h>
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

/**
 * InfTextGtkBufferClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfTextGtkBufferClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfTextGtkBuffer:
 *
 * #InfTextGtkBuffer is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfTextGtkBuffer {
  /*< private >*/
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

InfTextUser*
inf_text_gtk_buffer_get_author(InfTextGtkBuffer* buffer,
                               GtkTextIter* location);

InfTextUser*
inf_text_gtk_buffer_get_user_for_tag(InfTextGtkBuffer* buffer,
                                     GtkTextTag* tag);

gboolean
inf_text_gtk_buffer_is_author_toggle(InfTextGtkBuffer* buffer,
                                     const GtkTextIter* iter,
                                     InfTextUser** user_on,
                                     InfTextUser** user_off);

gboolean
inf_text_gtk_buffer_forward_to_author_toggle(InfTextGtkBuffer* buffer,
                                             GtkTextIter* iter,
                                             InfTextUser** user_on,
                                             InfTextUser** user_off);

gboolean
inf_text_gtk_buffer_backward_to_author_toggle(InfTextGtkBuffer* buffer,
                                              GtkTextIter* iter,
                                              InfTextUser** user_on,
                                              InfTextUser** user_off);

void
inf_text_gtk_buffer_set_wake_on_cursor_movement(InfTextGtkBuffer* buffer,
                                                gboolean wake);

gboolean
inf_text_gtk_buffer_get_wake_on_cursor_movement(InfTextGtkBuffer* buffer);

void
inf_text_gtk_buffer_ensure_author_tags_priority(InfTextGtkBuffer* buffer);

void
inf_text_gtk_buffer_set_saturation_value(InfTextGtkBuffer* buffer,
                                         gdouble saturation,
                                         gdouble value);

void
inf_text_gtk_buffer_set_fade(InfTextGtkBuffer* buffer,
                             gdouble alpha);

gdouble
inf_text_gtk_buffer_get_saturation(InfTextGtkBuffer* buffer);

gdouble
inf_text_gtk_buffer_get_value(InfTextGtkBuffer* buffer);

void
inf_text_gtk_buffer_set_show_user_colors(InfTextGtkBuffer* buffer,
                                         gboolean show);

gboolean
inf_text_gtk_buffer_get_show_user_colors(InfTextGtkBuffer* buffer);

void
inf_text_gtk_buffer_show_user_colors(InfTextGtkBuffer* buffer,
                                     gboolean show,
                                     GtkTextIter* start,
                                     GtkTextIter* end);

G_END_DECLS

#endif /* __INF_TEXT_GTK_BUFFER_H__ */

/* vim:set et sw=2 ts=2: */
