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

#ifndef __INF_GTK_BROWSER_VIEW_H__
#define __INF_GTK_BROWSER_VIEW_H__

#include <libinfgtk/inf-gtk-browser-model.h>

#include <gtk/gtk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_BROWSER_VIEW                 (inf_gtk_browser_view_get_type())
#define INF_GTK_BROWSER_VIEW(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_BROWSER_VIEW, InfGtkBrowserView))
#define INF_GTK_BROWSER_VIEW_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_BROWSER_VIEW, InfGtkBrowserViewClass))
#define INF_GTK_IS_BROWSER_VIEW(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_BROWSER_VIEW))
#define INF_GTK_IS_BROWSER_VIEW_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_BROWSER_VIEW))
#define INF_GTK_BROWSER_VIEW_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_BROWSER_VIEW, InfGtkBrowserViewClass))

#define INF_GTK_TYPE_BROWSER_VIEW_STATUS          (inf_gtk_browser_view_status_get_type())

typedef struct _InfGtkBrowserView InfGtkBrowserView;
typedef struct _InfGtkBrowserViewClass InfGtkBrowserViewClass;

struct _InfGtkBrowserViewClass {
  GtkTreeViewClass parent_class;

  /* signals */
  void (*activate)(InfGtkBrowserView* view,
                   GtkTreeIter* iter);

  void (*selection_changed)(InfGtkBrowserView* view,
                            GtkTreeIter* iter);

  void (*populate_popup)(InfGtkBrowserView* view,
                         GtkMenu* menu);

#if ! GTK_CHECK_VERSION(2, 91, 0)
  void (*set_scroll_adjustments)(InfGtkBrowserView* view,
                                 GtkAdjustment* hadjustment,
                                 GtkAdjustment* vadjustment);
#endif
};

struct _InfGtkBrowserView {
  GtkTreeView parent;
};

GType
inf_gtk_browser_view_get_type(void) G_GNUC_CONST;

GtkWidget*
inf_gtk_browser_view_new(void);

GtkWidget*
inf_gtk_browser_view_new_with_model(InfGtkBrowserModel* model);

gboolean
inf_gtk_browser_view_get_selected(InfGtkBrowserView* view,
                                  GtkTreeIter* iter);

void
inf_gtk_browser_view_set_selected(InfGtkBrowserView* view,
                                  GtkTreeIter* iter);

G_END_DECLS

#endif /* __INF_GTK_BROWSER_VIEW_H__ */

/* vim:set et sw=2 ts=2: */
