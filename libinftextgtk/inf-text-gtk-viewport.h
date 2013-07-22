/* infinote - Collaborative notetaking application
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_GTK_VIEWPORT_H__
#define __INF_TEXT_GTK_VIEWPORT_H__

#include <libinftext/inf-text-user.h>
#include <libinfinity/common/inf-user-table.h>

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_GTK_TYPE_VIEWPORT                 (inf_text_gtk_viewport_get_type())
#define INF_TEXT_GTK_VIEWPORT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_GTK_TYPE_VIEWPORT, InfTextGtkViewport))
#define INF_TEXT_GTK_VIEWPORT_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_GTK_TYPE_VIEWPORT, InfTextGtkViewportClass))
#define INF_TEXT_GTK_IS_VIEWPORT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_GTK_TYPE_VIEWPORT))
#define INF_TEXT_GTK_IS_VIEWPORT_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_GTK_TYPE_VIEWPORT))
#define INF_TEXT_GTK_VIEWPORT_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_GTK_TYPE_VIEWPORT, InfTextGtkViewportClass))

typedef struct _InfTextGtkViewport InfTextGtkViewport;
typedef struct _InfTextGtkViewportClass InfTextGtkViewportClass;

/**
 * InfTextGtkViewportClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfTextGtkViewportClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfTextGtkViewport:
 *
 * #InfTextGtkViewport is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfTextGtkViewport {
  /*< private >*/
  GObject parent;
};

GType
inf_text_gtk_viewport_get_type(void) G_GNUC_CONST;

InfTextGtkViewport*
inf_text_gtk_viewport_new(GtkScrolledWindow* scroll,
                          InfUserTable* user_table);

GtkScrolledWindow*
inf_text_gtk_viewport_get_scrolled_window(InfTextGtkViewport* viewport);

InfUserTable*
inf_text_gtk_viewport_get_user_table(InfTextGtkViewport* viewport);

void
inf_text_gtk_viewport_set_active_user(InfTextGtkViewport* viewport,
                                      InfTextUser* user);

InfTextUser*
inf_text_gtk_viewport_get_active_user(InfTextGtkViewport* viewport);

void
inf_text_gtk_viewport_set_show_user_markers(InfTextGtkViewport* viewport,
                                            gboolean show);

G_END_DECLS

#endif /* __INF_TEXT_GTK_VIEWPORT_H__ */

/* vim:set et sw=2 ts=2: */
