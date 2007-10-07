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

#ifndef __INF_GTK_BROWSER_MODEL_H__
#define __INF_GTK_BROWSER_MODEL_H__

#include <libinfinity/client/infc-browser.h>
#include <libinfinity/common/inf-discovery.h>
#include <libinfinity/common/inf-connection-manager.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <gtk/gtktreemodel.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_BROWSER_MODEL                 (inf_gtk_browser_model_get_type())
#define INF_GTK_BROWSER_MODEL(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_BROWSER_MODEL, InfGtkBrowserModel))
#define INF_GTK_BROWSER_MODEL_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_BROWSER_MODEL, InfGtkBrowserModelClass))
#define INF_GTK_IS_BROWSER_MODEL(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_BROWSER_MODEL))
#define INF_GTK_IS_BROWSER_MODEL_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_BROWSER_MODEL))
#define INF_GTK_BROWSER_MODEL_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_BROWSER_MODEL, InfGtkBrowserModelClass))

#define INF_GTK_TYPE_BROWSER_MODEL_STATUS          (inf_gtk_browser_model_status_get_type())

typedef struct _InfGtkBrowserModel InfGtkBrowserModel;
typedef struct _InfGtkBrowserModelClass InfGtkBrowserModelClass;

typedef enum _InfGtkBrowserModelStatus {
  INF_GTK_BROWSER_MODEL_INVALID,
  INF_GTK_BROWSER_MODEL_DISCOVERED,
  INF_GTK_BROWSER_MODEL_RESOLVING,
  INF_GTK_BROWSER_MODEL_CONNECTING,
  INF_GTK_BROWSER_MODEL_CONNECTED,
  INF_GTK_BROWSER_MODEL_ERROR
} InfGtkBrowserModelStatus;

/* Note that not all of the columns are valid for all rows */
typedef enum _InfGtkBrowserModelColumn {
  INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO,
  INF_GTK_BROWSER_MODEL_COL_DISCOVERY,
  INF_GTK_BROWSER_MODEL_COL_BROWSER,
  INF_GTK_BROWSER_MODEL_COL_STATUS, /* only toplevel */
  INF_GTK_BROWSER_MODEL_COL_ERROR, /* only toplevel */
  INF_GTK_BROWSER_MODEL_COL_NODE,

  INF_GTK_BROWSER_MODEL_NUM_COLS
} InfGtkBrowserModelSite;

struct _InfGtkBrowserModelClass {
  GObjectClass parent_class;
};

struct _InfGtkBrowserModel {
  GObject parent;
};

GType
inf_gtk_browser_model_status_get_type(void) G_GNUC_CONST;

GType
inf_gtk_browser_model_get_type(void) G_GNUC_CONST;

InfGtkBrowserModel*
inf_gtk_browser_model_new(InfConnectionManager* connection_manager);

void
inf_gtk_browser_model_add_discovery(InfGtkBrowserModel* model,
                                    InfDiscovery* discovery);

void
inf_gtk_browser_model_add_connection(InfGtkBrowserModel* model,
                                     InfXmlConnection* connection);

void
inf_gtk_browser_model_resolve(InfGtkBrowserModel* model,
                              InfDiscovery* discovery,
                              InfDiscoveryInfo* info);

gboolean
inf_gtk_browser_model_browser_iter_to_tree_iter(InfGtkBrowserModel* model,
                                                InfcBrowser* browser,
                                                InfcBrowserIter* browser_iter,
                                                GtkTreeIter* tree_iter);

G_END_DECLS

#endif /* __INF_GTK_BROWSER_MODEL_H__ */

/* vim:set et sw=2 ts=2: */
