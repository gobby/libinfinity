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

#ifndef __INF_GTK_BROWSER_VIEW_H__
#define __INF_GTK_BROWSER_VIEW_H__

#include <libinfgtk/inf-gtk-browser-model.h>
#include <gtk/gtkbin.h>

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
  GtkBinClass parent_class;

  void (*set_scroll_adjustments)(InfGtkBrowserView* view,
                                 GtkAdjustment* hadjustment,
                                 GtkAdjustment* vadjustment);

  /* subscribe-session. Note that this is only called for sessions subscribed
   * via this view. If you want to get notified for any sessions a browser
   * subscribes, connect to the "subscribe-session" signal of InfcBrowser. */
};

struct _InfGtkBrowserView {
  GtkBin parent;
};

GType
inf_gtk_browser_view_get_type(void) G_GNUC_CONST;

GtkWidget*
inf_gtk_browser_view_new(void);

GtkWidget*
inf_gtk_browser_view_new_with_model(InfGtkBrowserModel* model);

G_END_DECLS

#endif /* __INF_GTK_BROWSER_VIEW_H__ */

/* vim:set et sw=2 ts=2: */
