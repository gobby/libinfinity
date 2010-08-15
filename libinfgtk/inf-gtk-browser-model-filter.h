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

#ifndef __INF_GTK_BROWSER_MODEL_FILTER_H__
#define __INF_GTK_BROWSER_MODEL_FILTER_H__

#include <libinfgtk/inf-gtk-browser-model.h>

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_BROWSER_MODEL_FILTER                 (inf_gtk_browser_model_filter_get_type())
#define INF_GTK_BROWSER_MODEL_FILTER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_BROWSER_MODEL_FILTER, InfGtkBrowserModelFilter))
#define INF_GTK_BROWSER_MODEL_FILTER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_BROWSER_MODEL_FILTER, InfGtkBrowserModelFilterClass))
#define INF_GTK_IS_BROWSER_MODEL_FILTER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_BROWSER_MODEL_FILTER))
#define INF_GTK_IS_BROWSER_MODEL_FILTER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_BROWSER_MODEL_FILTER))
#define INF_GTK_BROWSER_MODEL_FILTER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_BROWSER_MODEL_FILTER, InfGtkBrowserModelFilterClass))

typedef struct _InfGtkBrowserModelFilter InfGtkBrowserModelFilter;
typedef struct _InfGtkBrowserModelFilterClass InfGtkBrowserModelFilterClass;

struct _InfGtkBrowserModelFilterClass {
  GtkTreeModelFilterClass parent_class;
};

struct _InfGtkBrowserModelFilter {
  GtkTreeModelFilter parent;
};

GType
inf_gtk_browser_model_filter_get_type(void) G_GNUC_CONST;

InfGtkBrowserModelFilter*
inf_gtk_browser_model_filter_new(InfGtkBrowserModel* child_model);

G_END_DECLS

#endif /* __INF_GTK_BROWSER_MODEL_FILTER_H__ */

/* vim:set et sw=2 ts=2: */
