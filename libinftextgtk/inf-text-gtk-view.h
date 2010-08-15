/* infinote - Collaborative notetaking application
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __INF_TEXT_GTK_VIEW_H__
#define __INF_TEXT_GTK_VIEW_H__

#include <libinftext/inf-text-user.h>
#include <libinfinity/common/inf-user-table.h>
#include <libinfinity/common/inf-io.h>

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_GTK_TYPE_VIEW                 (inf_text_gtk_view_get_type())
#define INF_TEXT_GTK_VIEW(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_GTK_TYPE_VIEW, InfTextGtkView))
#define INF_TEXT_GTK_VIEW_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_GTK_TYPE_VIEW, InfTextGtkViewClass))
#define INF_TEXT_GTK_IS_VIEW(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_GTK_TYPE_VIEW))
#define INF_TEXT_GTK_IS_VIEW_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_GTK_TYPE_VIEW))
#define INF_TEXT_GTK_VIEW_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_GTK_TYPE_VIEW, InfTextGtkViewClass))

typedef struct _InfTextGtkView InfTextGtkView;
typedef struct _InfTextGtkViewClass InfTextGtkViewClass;

struct _InfTextGtkViewClass {
  GObjectClass parent_class;
};

struct _InfTextGtkView {
  GObject parent;
};

GType
inf_text_gtk_view_get_type(void) G_GNUC_CONST;

InfTextGtkView*
inf_text_gtk_view_new(InfIo* io,
                      GtkTextView* view,
                      InfUserTable* user_table);

GtkTextView*
inf_text_gtk_view_get_text_view(InfTextGtkView* view);

InfUserTable*
inf_text_gtk_view_get_user_table(InfTextGtkView* view);

void
inf_text_gtk_view_set_active_user(InfTextGtkView* view,
                                  InfTextUser* user);

InfTextUser*
inf_text_gtk_view_get_active_user(InfTextGtkView* view);

G_END_DECLS

#endif /* __INF_TEXT_GTK_VIEW_H__ */

/* vim:set et sw=2 ts=2: */
