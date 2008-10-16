/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_GTK_BROWSER_MODEL_SORT_H__
#define __INF_GTK_BROWSER_MODEL_SORT_H__

#include <libinfgtk/inf-gtk-browser-model.h>
#include <gtk/gtktreemodelsort.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_BROWSER_MODEL_SORT                 (inf_gtk_browser_model_sort_get_type())
#define INF_GTK_BROWSER_MODEL_SORT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_BROWSER_MODEL_SORT, InfGtkBrowserModelSort))
#define INF_GTK_BROWSER_MODEL_SORT_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_BROWSER_MODEL_SORT, InfGtkBrowserModelSortClass))
#define INF_GTK_IS_BROWSER_MODEL_SORT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_BROWSER_MODEL_SORT))
#define INF_GTK_IS_BROWSER_MODEL_SORT_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_BROWSER_MODEL_SORT))
#define INF_GTK_BROWSER_MODEL_SORT_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_BROWSER_MODEL_SORT, InfGtkBrowserModelSortClass))

typedef struct _InfGtkBrowserModelSort InfGtkBrowserModelSort;
typedef struct _InfGtkBrowserModelSortClass InfGtkBrowserModelSortClass;

struct _InfGtkBrowserModelSortClass {
  GtkTreeModelSortClass parent_class;
};

struct _InfGtkBrowserModelSort {
  GtkTreeModelSort parent;
};

GType
inf_gtk_browser_model_sort_get_type(void) G_GNUC_CONST;

InfGtkBrowserModelSort*
inf_gtk_browser_model_sort_new(InfGtkBrowserModel* child_model);

G_END_DECLS

#endif /* __INF_GTK_BROWSER_MODEL_SORT_H__ */

/* vim:set et sw=2 ts=2: */
