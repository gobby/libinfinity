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

#ifndef __INF_GTK_BROWSER_MODEL_H__
#define __INF_GTK_BROWSER_MODEL_H__

#include <libinfinity/client/infc-browser.h>
#include <libinfinity/common/inf-discovery.h>

#include <gtk/gtk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_BROWSER_MODEL                 (inf_gtk_browser_model_get_type())
#define INF_GTK_BROWSER_MODEL(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_BROWSER_MODEL, InfGtkBrowserModel))
#define INF_GTK_IS_BROWSER_MODEL(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_BROWSER_MODEL))
#define INF_GTK_BROWSER_MODEL_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_GTK_TYPE_BROWSER_MODEL, InfGtkBrowserModelInterface))

#define INF_GTK_TYPE_BROWSER_MODEL_STATUS          (inf_gtk_browser_model_status_get_type())

/**
 * InfGtkBrowserModel:
 *
 * #InfGtkBrowserModel is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfGtkBrowserModel InfGtkBrowserModel;
typedef struct _InfGtkBrowserModelInterface InfGtkBrowserModelInterface;

/**
 * InfGtkBrowserModelStatus:
 * @INF_GTK_BROWSER_MODEL_INVALID: Invalid state. A healthy item should not
 * have this status.
 * @INF_GTK_BROWSER_MODEL_DISCOVERED: The item was discovered with a
 * #InfDiscovery, but no attempt has been made at connecting to it.
 * @INF_GTK_BROWSER_MODEL_RESOLVING: The item was discovered with a
 * #InfDiscovery and is currently being resolved, see inf_discovery_resolve().
 * @INF_GTK_BROWSER_MODEL_DISCONNECTED: A connection attempt to the remote
 * site was not successful, or the connection was lost. The connection
 * parameters are known, but currently no connection is established.
 * @INF_GTK_BROWSER_MODEL_CONNECTING: A connection attempt to the remote site
 * is currently in progress.
 * @INF_GTK_BROWSER_MODEL_CONNECTED: A connection to the remote site has been
 * established and its directory can be browsed.
 * @INF_GTK_BROWSER_MODEL_ERROR: An error has occured with this item. The
 * column with index %INF_GTK_BROWSER_MODEL_COL_ERROR has more information
 * about the error that occurred.
 *
 * The different statuses an item in the #InfGtkBrowserModel can have. The
 * status of an item is only defined for toplevel items in the tree.
 */
typedef enum _InfGtkBrowserModelStatus {
  INF_GTK_BROWSER_MODEL_INVALID,
  INF_GTK_BROWSER_MODEL_DISCOVERED,
  INF_GTK_BROWSER_MODEL_RESOLVING,
  INF_GTK_BROWSER_MODEL_DISCONNECTED,
  INF_GTK_BROWSER_MODEL_CONNECTING,
  INF_GTK_BROWSER_MODEL_CONNECTED,
  INF_GTK_BROWSER_MODEL_ERROR
} InfGtkBrowserModelStatus;

/**
 * InfGtkBrowserModelColumn:
 * @INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO: The #InfDiscoveryInfo of a
 * discovered toplevel item, or %NULL if the item was not discovered.
 * @INF_GTK_BROWSER_MODEL_COL_DISCOVERY: The #InfDiscovery object that the
 * item has been discovered with, if any.
 * @INF_GTK_BROWSER_MODEL_COL_BROWSER: The #InfBrowser with which this item
 * is being browsed, or %NULL if no browser is available.
 * @INF_GTK_BROWSER_MODEL_COL_STATUS: The status of this item. This column is
 * only valid for toplevel items, i.e. for connections to directories.
 * @INF_GTK_BROWSER_MODEL_COL_NAME: The name of the item as a simple string.
 * @INF_GTK_BROWSER_MODEL_COL_ERROR: If an error has occurred with the item,
 * for example the connection failed, or a node exploration failed, this
 * column contains a #GError with more error information.
 * @INF_GTK_BROWSER_MODEL_COL_NODE: The #InfBrowserIter pointing to the
 * corresponding node of the #InfBrowser.
 * @INF_GTK_BROWSER_MODEL_NUM_COLS: The total number of columns of a
 * #InfGtkBrowserModel.
 * 
 * The various #GtkTreeModel columns that a tree model implementing
 * #InfGtkBrowserModel must support.
 */
typedef enum _InfGtkBrowserModelColumn {
  INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO,
  INF_GTK_BROWSER_MODEL_COL_DISCOVERY,
  INF_GTK_BROWSER_MODEL_COL_BROWSER,
  INF_GTK_BROWSER_MODEL_COL_STATUS, /* only toplevel */
  INF_GTK_BROWSER_MODEL_COL_NAME,
  INF_GTK_BROWSER_MODEL_COL_ERROR,
  INF_GTK_BROWSER_MODEL_COL_NODE,

  INF_GTK_BROWSER_MODEL_NUM_COLS
} InfGtkBrowserModelColumn;

/**
 * InfGtkBrowserModelInterface:
 * @set_browser: Default signal handler of the
 * #InfGtkBrowserModel::set-browser signal.
 * @resolve: Virtual function for resolving a discovered infinote service.
 * @browser_iter_to_tree_iter: Virtual function for converting a
 * #InfBrowserIter to a #GtkTreeIter.
 *
 * This structure contains virtual functions and signal handlers of the
 * #InfGtkBrowserModel interface.
 */
struct _InfGtkBrowserModelInterface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/

  /* signals */
  void(*set_browser)(InfGtkBrowserModel* model,
                     GtkTreePath* path,
                     GtkTreeIter* iter,
                     InfBrowser* old_browser,
                     InfBrowser* new_browser);

  /* virtual functions */
  void(*resolve)(InfGtkBrowserModel* model,
                 InfDiscovery* discovery,
                 InfDiscoveryInfo* info);

  gboolean(*browser_iter_to_tree_iter)(InfGtkBrowserModel* model,
                                       InfBrowser* browser,
                                       const InfBrowserIter* iter,
                                       GtkTreeIter* tree_iter);
};

GType
inf_gtk_browser_model_status_get_type(void) G_GNUC_CONST;

GType
inf_gtk_browser_model_get_type(void) G_GNUC_CONST;

void
inf_gtk_browser_model_set_browser(InfGtkBrowserModel* model,
                                  GtkTreePath* path,
                                  GtkTreeIter* iter,
                                  InfBrowser* old_browser,
                                  InfBrowser* new_browser);

void
inf_gtk_browser_model_resolve(InfGtkBrowserModel* model,
                              InfDiscovery* discovery,
                              InfDiscoveryInfo* info);

gboolean
inf_gtk_browser_model_browser_iter_to_tree_iter(InfGtkBrowserModel* model,
                                                InfBrowser* browser,
                                                const InfBrowserIter* iter,
                                                GtkTreeIter* tree_iter);

G_END_DECLS

#endif /* __INF_GTK_BROWSER_MODEL_H__ */

/* vim:set et sw=2 ts=2: */
