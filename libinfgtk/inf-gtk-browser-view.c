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

#include <libinfgtk/inf-gtk-browser-view.h>
#include <libinfinity/inf-marshal.h>

#include <gtk/gtktreeview.h>
#include <gtk/gtktreeviewcolumn.h>
#include <gtk/gtkcellrendererpixbuf.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcellrendererprogress.h>
#include <gtk/gtkstock.h>

/* TODO: Explore children when a node is expanded */
/* TODO: Explore newly added node when its parent is expanded */
/* TODO: Explore and expand root node when browser gets available */

typedef struct _InfGtkBrowserViewObject InfGtkBrowserViewObject;
struct _InfGtkBrowserViewObject {
  GObject* object;
  GtkTreeRowReference* reference;

  /* This is valid as long as the TreeRowReference above is valid, but we
   * still need the TreeRowReference to know when it becomes invalid */
  GtkTreeIter iter;
};

typedef struct _InfGtkBrowserViewPrivate InfGtkBrowserViewPrivate;
struct _InfGtkBrowserViewPrivate {
  GtkWidget* treeview;
  GtkTreeViewColumn* column;

  /* Note that progress and status_text are never visible at the same time */
  GtkCellRenderer* renderer_icon;
  GtkCellRenderer* renderer_status_icon; /* toplevel only */
  GtkCellRenderer* renderer_name;
  GtkCellRenderer* renderer_progress;
  GtkCellRenderer* renderer_status;

  GArray* browsers;
  GArray* explore_requests;
};

enum {
  PROP_0,

  PROP_MODEL
};

#define INF_GTK_BROWSER_VIEW_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_BROWSER_VIEW, InfGtkBrowserViewPrivate))

/* We do some rather complex stuff here because we don't get the iter when
 * a row is deleted. This would be nice to disconnect browser signals for
 * example (we need the iter to access the browser to disconnect the signals),
 * but it is not possible.
 *
 * Instead, we keep an array of browsers in the model including
 * TreeRowReferences where they are in the tree. When a row is removed, we
 * check which TreeRowReferences got invalid and delete the corresponding
 * browsers from our array. The same holds for explore requests. */

static GObjectClass* parent_class;

/* Lookup the InfGtkBrowserViewObject index in the priv->explore_requests
 * array for the given request. */
gint
inf_gtk_browser_view_explore_request_find(InfGtkBrowserView* view,
                                          InfcExploreRequest* request)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  guint i;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  for(i = 0; i < priv->explore_requests->len; ++ i)
  {
    object = &g_array_index(
      priv->explore_requests,
      InfGtkBrowserViewObject,
      i
    );

    if(object->object == G_OBJECT(request))
      return i;
  }

  return -1;
}

static void
inf_gtk_browser_view_redraw_row(InfGtkBrowserView* view,
                                GtkTreePath* path,
                                GtkTreeIter* iter)
{
  /* TODO: Is there a better way to do this? Calling gtk_tree_model_changed is
   * not good:
   *
   * The actual data in the model has not been changed, otherwise the model
   * would have emitted the signal. What actually has changed is just what we
   * display, for example the progress bar of the exploration of a node. This
   * does not belong to the model because the model does not care about
   * exploration progress, but we want to show it to the user nevertheless.
   * I am not sure whether this is a problem in our design or a limitation
   * in the GTK+ treeview and friends. */
  gtk_tree_model_row_changed(
    gtk_tree_view_get_model(
      GTK_TREE_VIEW(INF_GTK_BROWSER_VIEW_PRIVATE(view)->treeview)
    ),
    path,
    iter
  );
}

static void
inf_gtk_browser_view_redraw_node_for_explore_request(InfGtkBrowserView* view,
                                                     InfcExploreRequest* req)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  GtkTreePath* path;
  gint i;

  /* We could get the iter easily by querying the InfcBrowserIter with
   * infc_browser_iter_from_explore_request and then obtaining the GtkTreeIter
   * with inf_gtk_browser_model_browser_iter_to_tree_iter. However, we do not
   * get the path this way and gtk_tree_model_get_path is expensive.
   * Therefore, we lookup the InfGtkBrowserViewObject which has both iter
   * and path (via the GtkTreeRowReference). */

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  i = inf_gtk_browser_view_explore_request_find(view, req);
  g_assert(i >= 0);

  object = &g_array_index(priv->explore_requests, InfGtkBrowserViewObject, i);
  path = gtk_tree_row_reference_get_path(object->reference);
  g_assert(path != NULL);

  inf_gtk_browser_view_redraw_row(view, path, &object->iter);
  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_view_explore_request_initiated_cb(InfcExploreRequest* request,
                                                  guint total,
                                                  gpointer user_data)
{
  inf_gtk_browser_view_redraw_node_for_explore_request(
    INF_GTK_BROWSER_VIEW(user_data),
    request
  );
}

static void
inf_gtk_browser_view_explore_request_progress_cb(InfcExploreRequest* request,
                                                 guint current,
                                                 guint total,
                                                 gpointer user_data)
{
  inf_gtk_browser_view_redraw_node_for_explore_request(
    INF_GTK_BROWSER_VIEW(user_data),
    request
  );
}

/* Required by inf_gtk_browser_view_explore_request_finished_cb */
static void
inf_gtk_browser_view_explore_request_removed(InfGtkBrowserView* view,
                                             guint i);

static void
inf_gtk_browser_view_explore_request_finished_cb(InfcExploreRequest* request,
                                                 gpointer user_data)
{
  InfGtkBrowserView* view;
  gint i;

  view = INF_GTK_BROWSER_VIEW(user_data);
  i = inf_gtk_browser_view_explore_request_find(view, request);
  g_assert(i >= 0);

  inf_gtk_browser_view_explore_request_removed(view, i);
}

static void
inf_gtk_browser_view_explore_request_added(InfGtkBrowserView* view,
                                           GtkTreePath* path,
                                           GtkTreeIter* iter,
                                           InfcExploreRequest* request)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject object;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  g_assert(inf_gtk_browser_view_explore_request_find(view, request) == -1);
  
  object.object = G_OBJECT(request);
  g_object_ref(G_OBJECT(request));

  object.reference = gtk_tree_row_reference_new_proxy(
    G_OBJECT(priv->column),
    gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview)),
    path
  );

  object.iter = *iter;
  g_array_append_vals(priv->browsers, &object, 1);

  g_signal_connect_after(
    G_OBJECT(request),
    "initiated",
    G_CALLBACK(inf_gtk_browser_view_explore_request_initiated_cb),
    view
  );

  g_signal_connect_after(
    G_OBJECT(request),
    "progress",
    G_CALLBACK(inf_gtk_browser_view_explore_request_progress_cb),
    view
  );

  g_signal_connect_after(
    G_OBJECT(request),
    "finished",
    G_CALLBACK(inf_gtk_browser_view_explore_request_finished_cb),
    view
  );

  inf_gtk_browser_view_redraw_row(view, path, iter);
}

/* Just free data allocated by object */
static void
inf_gtk_browser_view_explore_request_free(InfGtkBrowserView* view,
                                          InfGtkBrowserViewObject* object)
{
  g_assert(INFC_IS_EXPLORE_REQUEST(object->object));

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(object->object),
    G_CALLBACK(inf_gtk_browser_view_explore_request_initiated_cb),
    view
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(object->object),
    G_CALLBACK(inf_gtk_browser_view_explore_request_progress_cb),
    view
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(object->object),
    G_CALLBACK(inf_gtk_browser_view_explore_request_finished_cb),
    view
  );

  gtk_tree_row_reference_free(object->reference);
  g_object_unref(G_OBJECT(object->object));
}

/* Unlink from view */
static void
inf_gtk_browser_view_explore_request_removed(InfGtkBrowserView* view,
                                             guint i)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  GtkTreePath* path;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  object = &g_array_index(priv->explore_requests, InfGtkBrowserViewObject, i);

  /* Redraw if the reference is still valid. Note that if the node is removed
   * while being explored the reference is not valid at this point. */
  path = gtk_tree_row_reference_get_path(object->reference);
  if(path != NULL)
  {
    inf_gtk_browser_view_redraw_row(view, path, &object->iter);
    gtk_tree_path_free(path);
  }

  inf_gtk_browser_view_explore_request_free(view, object);
  g_array_remove_index_fast(priv->explore_requests, i);
}

static void
inf_gtk_browser_view_begin_explore_cb(InfcBrowser* browser,
                                      InfcBrowserIter* iter,
                                      InfcExploreRequest* request,
                                      gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  GtkTreeModel* model;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  gboolean result;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));

  result = inf_gtk_browser_model_browser_iter_to_tree_iter(
    INF_GTK_BROWSER_MODEL(model),
    browser,
    iter,
    &tree_iter
  );

  g_assert(result == TRUE);

  path = gtk_tree_model_get_path(model, &tree_iter);
  inf_gtk_browser_view_explore_request_added(view, path, &tree_iter, request);
  gtk_tree_path_free(path); 
}

/* This function recursively walks down iter and all its children and
 * inserts running explore requests into the view. */
static void
inf_gtk_browser_view_walk_for_explore_requests(InfGtkBrowserView* view,
                                               InfcBrowser* browser,
                                               InfcBrowserIter* iter)
{
  InfGtkBrowserViewPrivate* priv;
  InfcExploreRequest* request;
  GtkTreeModel* model;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  InfcBrowserIter child_iter;
  gboolean result;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  if(infc_browser_iter_get_explored(browser, iter))
  {
    child_iter = *iter;
    for(result = infc_browser_iter_get_child(browser, &child_iter);
        result == TRUE;
        result = infc_browser_iter_get_next(browser, &child_iter))
    {
      inf_gtk_browser_view_walk_for_explore_requests(
        view,
        browser,
        &child_iter
      );
    }
  }

  request = infc_browser_iter_get_explore_request(browser, iter);
  if(request != NULL)
  {
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));

    result = inf_gtk_browser_model_browser_iter_to_tree_iter(
      INF_GTK_BROWSER_MODEL(model),
      browser,
      iter,
      &tree_iter
    );
    
    path = gtk_tree_model_get_path(model, &tree_iter);

    inf_gtk_browser_view_explore_request_added(
      view,
      path,
      &tree_iter,
      request
    );

    gtk_tree_path_free(path);
  }
}

/* Called whenever a browser is added to a node. The browser is expected
 * to be already refed. */
static void
inf_gtk_browser_view_browser_added(InfGtkBrowserView* view,
                                   GtkTreePath* path,
                                   GtkTreeIter* iter,
                                   InfcBrowser* browser)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject object;
  GtkTreeModel* model;
  InfcBrowserIter* browser_iter;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));
  object.object = G_OBJECT(browser);

  object.reference = gtk_tree_row_reference_new_proxy(
    G_OBJECT(priv->column),
    model,
    path
  );

  object.iter = *iter;
  g_array_append_vals(priv->browsers, &object, 1);

  g_signal_connect(
    G_OBJECT(browser),
    "begin-explore",
    G_CALLBACK(inf_gtk_browser_view_begin_explore_cb),
    view
  );

  gtk_tree_model_get(
    model,
    iter,
    INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
    -1
  );

  /* Look for running explore requests, insert into array of running
   * explore requests to show their progress. */
  inf_gtk_browser_view_walk_for_explore_requests(view, browser, browser_iter);
  infc_browser_iter_free(browser_iter);
}

static void
inf_gtk_browser_view_browser_free(InfGtkBrowserView* view,
                                  InfGtkBrowserViewObject* object)
{
  g_signal_handlers_disconnect_by_func(
    G_OBJECT(object->object),
    G_CALLBACK(inf_gtk_browser_view_begin_explore_cb),
    view
  );

  gtk_tree_row_reference_free(object->reference);
  g_object_unref(G_OBJECT(object->object));
}

static void
inf_gtk_browser_view_browser_removed(InfGtkBrowserView* view,
                                     guint index)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  object = &g_array_index(priv->browsers, InfGtkBrowserViewObject, index);

  /* TODO: Also remove any explore requests belonging to this browser */

  inf_gtk_browser_view_browser_free(view, object);
  g_array_remove_index_fast(priv->browsers, index);
}

static void
inf_gtk_browser_view_row_inserted_cb(GtkTreeModel* model,
                                     GtkTreePath* path,
                                     GtkTreeIter* iter,
                                     gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  GtkTreeIter parent_iter;
  InfcBrowser* browser;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  gtk_tree_row_reference_inserted(G_OBJECT(priv->column), path);

  if(gtk_tree_model_iter_parent(model, &parent_iter, iter) == FALSE)
  {
    /* No parent, so iter is top-level. Check if
     * it has a browser associated. */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER,
      &browser,
      -1
    );

    if(browser != NULL)
      inf_gtk_browser_view_browser_added(view, path, iter, browser);
  }
}

static void
inf_gtk_browser_view_row_changed_cb(GtkTreeModel* model,
                                    GtkTreePath* path,
                                    GtkTreeIter* iter,
                                    gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  GtkTreeIter parent_iter;

  InfcBrowser* browser;
  InfGtkBrowserViewObject* object;
  GtkTreePath* object_path;
  guint i;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  if(gtk_tree_model_iter_parent(model, &parent_iter, iter) == FALSE)
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER,
      &browser,
      -1
    );

    for(i = 0; i < priv->browsers->len; ++ i)
    {
      object = &g_array_index(priv->browsers, InfGtkBrowserViewObject, i);
      object_path = gtk_tree_row_reference_get_path(object->reference);
      if(gtk_tree_path_compare(path, object_path) == 0)
      {
        gtk_tree_path_free(object_path);
        break;
      }

      gtk_tree_path_free(object_path);
    }

    if(browser == NULL && i < priv->browsers->len)
      inf_gtk_browser_view_browser_removed(view, i);
    else if(browser != NULL && i == priv->browsers->len)
      inf_gtk_browser_view_browser_added(view, path, iter, browser);
    else if(browser != NULL) /* already added */
      g_object_unref(G_OBJECT(browser));
  }
}

static void
inf_gtk_browser_view_row_deleted_cb(GtkTreeModel* model,
                                    GtkTreePath* path,
                                    gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  guint i;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  gtk_tree_row_reference_deleted(G_OBJECT(priv->column), path);

  /* Check for references that became invalid */
  if(gtk_tree_path_get_depth(path) == 1)
  {
    /* Toplevel, so browsers may be affected */
    for(i = 0; i < priv->browsers->len; ++ i)
    {
      object = &g_array_index(priv->browsers, InfGtkBrowserViewObject, i);
      if(gtk_tree_row_reference_valid(object->reference) == FALSE)
      {
        /* Browser node was removed */
        inf_gtk_browser_view_browser_removed(view, i);
      }
    }
  }

  /* Explore requests may be affected as well */
  for(i = 0; i < priv->explore_requests->len; ++ i)
  {
    object = &g_array_index(
      priv->explore_requests,
      InfGtkBrowserViewObject,
      i
    );

    if(gtk_tree_row_reference_valid(object->reference) == FALSE)
    {
      inf_gtk_browser_view_explore_request_removed(view, i);
    }
  }
}

static void
inf_gtk_browser_view_rows_reordered_cb(GtkTreeModel* model,
                                       GtkTreePath* path,
                                       GtkTreeIter* iter,
                                       gint* new_order,
                                       gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  gtk_tree_row_reference_reordered(
    G_OBJECT(priv->column),
    path,
    iter,
    new_order
  );
}

static void
inf_gtk_browser_view_set_model(InfGtkBrowserView* view,
                               InfGtkBrowserModel* model)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewObject* object;
  GtkTreeModel* current_model;
  GtkTreeIter iter;
  guint i;
  InfcBrowser* browser;
  GtkTreePath* path;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  current_model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

  if(current_model != NULL)
  {
    for(i = 0; i < priv->explore_requests->len; ++ i)
    {
      object = &g_array_index(
        priv->explore_requests,
        InfGtkBrowserViewObject,
        i
      );

      inf_gtk_browser_view_explore_request_free(view, object);
    }

    g_array_remove_range(
      priv->explore_requests,
      0,
      priv->explore_requests->len
    );

    for(i = 0; i < priv->browsers->len; ++ i)
    {
      object = &g_array_index(priv->browsers, InfGtkBrowserViewObject, i);
      inf_gtk_browser_view_browser_free(view, object);
    }

    g_array_remove_range(priv->browsers, 0, priv->browsers->len);

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(model),
      G_CALLBACK(inf_gtk_browser_view_row_inserted_cb),
      view
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(model),
      G_CALLBACK(inf_gtk_browser_view_row_deleted_cb),
      view
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(model),
      G_CALLBACK(inf_gtk_browser_view_row_changed_cb),
      view
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(model),
      G_CALLBACK(inf_gtk_browser_view_rows_reordered_cb),
      view
    );
  }

  gtk_tree_view_set_model(
    GTK_TREE_VIEW(priv->treeview),
    GTK_TREE_MODEL(model)
  );

  if(model != NULL)
  {
    /* Add initial browsers */
    if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter) == TRUE)
    {
      path = gtk_tree_path_new_first();

      do
      {
        gtk_tree_model_get(
          GTK_TREE_MODEL(model),
          &iter,
          INF_GTK_BROWSER_MODEL_COL_BROWSER,
          &browser,
          -1
        );

        if(browser != NULL)
          inf_gtk_browser_view_browser_added(view, path, &iter, browser);

        gtk_tree_path_next(path);
      } while(gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter) == TRUE);
    }

    g_signal_connect(
      G_OBJECT(model),
      "row-inserted",
      G_CALLBACK(inf_gtk_browser_view_row_inserted_cb),
      view
    );

    g_signal_connect(
      G_OBJECT(model),
      "row-deleted",
      G_CALLBACK(inf_gtk_browser_view_row_deleted_cb),
      view
    );

    g_signal_connect(
      G_OBJECT(model),
      "row-changed",
      G_CALLBACK(inf_gtk_browser_view_row_changed_cb),
      view
    );

    g_signal_connect(
      G_OBJECT(model),
      "rows-reordered",
      G_CALLBACK(inf_gtk_browser_view_rows_reordered_cb),
      view
    );
  }
}

static void
inf_gtk_browser_view_icon_data_func(GtkTreeViewColumn* column,
                                    GtkCellRenderer* renderer,
                                    GtkTreeModel* model,
                                    GtkTreeIter* iter,
                                    gpointer user_data)
{
  GtkTreeIter iter_parent;
  InfDiscovery* discovery;
  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;

  if(gtk_tree_model_iter_parent(model, &iter_parent, iter))
  {
    /* Inner node */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    /* TODO: Set icon depending on note type, perhaps also on whether
     * we are subscribed or not. */
    if(infc_browser_iter_is_subdirectory(browser, browser_iter))
      g_object_set(G_OBJECT(renderer), "stock-id", GTK_STOCK_DIRECTORY, NULL);
    else
      g_object_set(G_OBJECT(renderer), "stock-id", GTK_STOCK_FILE, NULL);

    infc_browser_iter_free(browser_iter);
    g_object_unref(G_OBJECT(browser));
  }
  else
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY, &discovery,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      -1
    );

    /* TODO: Set icon depending on discovery type (LAN, jabber, direct) */
    g_object_set(G_OBJECT(renderer), "stock-id", GTK_STOCK_NETWORK, NULL);

    if(discovery != NULL) g_object_unref(G_OBJECT(discovery));
    if(browser != NULL) g_object_unref(G_OBJECT(browser));
  }
}

static void
inf_gtk_browser_view_status_icon_data_func(GtkTreeViewColumn* column,
                                           GtkCellRenderer* renderer,
                                           GtkTreeModel* model,
                                           GtkTreeIter* iter,
                                           gpointer user_data)
{
  GtkTreeIter iter_parent;
  InfGtkBrowserModelStatus status;
  const gchar* stock_id;

  if(gtk_tree_model_iter_parent(model, &iter_parent, iter))
  {
    /* inner node, ignore */
    g_object_set(G_OBJECT(renderer), "visible", FALSE, NULL);
  }
  else
  {
    /* toplevel */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_STATUS, &status,
      -1
    );

    switch(status)
    {
    case INF_GTK_BROWSER_MODEL_DISCOVERED:
    case INF_GTK_BROWSER_MODEL_RESOLVING:
    case INF_GTK_BROWSER_MODEL_CONNECTING:
      stock_id = GTK_STOCK_DISCONNECT;
      break;
    case INF_GTK_BROWSER_MODEL_CONNECTED:
      stock_id = GTK_STOCK_CONNECT;
      break;
    case INF_GTK_BROWSER_MODEL_ERROR:
      stock_id = GTK_STOCK_DIALOG_ERROR;
      break;
    default:
      g_assert_not_reached();
      break;
    }
    
    g_object_set(
      G_OBJECT(renderer),
      "visible", TRUE,
      "stock-id", stock_id,
      NULL
    );
  }
}

static void
inf_gtk_browser_view_name_data_func(GtkTreeViewColumn* column,
                                    GtkCellRenderer* renderer,
                                    GtkTreeModel* model,
                                    GtkTreeIter* iter,
                                    gpointer user_data)
{
  GtkTreeIter iter_parent;
  InfDiscovery* discovery;
  InfDiscoveryInfo* info;
  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;
  const gchar* name;

  if(gtk_tree_model_iter_parent(model, &iter_parent, iter))
  {
    /* Inner node */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    name = infc_browser_iter_get_name(browser, browser_iter);
    g_object_set(G_OBJECT(renderer), "text", name, NULL);

    infc_browser_iter_free(browser_iter);
    g_object_unref(G_OBJECT(browser));
  }
  else
  {
    /* Toplevel */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY, &discovery,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO, &info,
      -1
    );
    
    if(discovery != NULL)
    {
      g_assert(info != NULL);

      name = inf_discovery_info_get_service_name(discovery, info);
      g_object_set(G_OBJECT(renderer), "text", name, NULL);

      g_object_unref(G_OBJECT(discovery));
    }
    else
    {
      /* TODO: Display remote address */
      g_object_set(G_OBJECT(renderer), "text", "Direct connection", NULL);
    }
  }
}

static void
inf_gtk_browser_view_progress_data_func(GtkTreeViewColumn* column,
                                        GtkCellRenderer* renderer,
                                        GtkTreeModel* model,
                                        GtkTreeIter* iter,
                                        gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserIter* browser_iter;
  InfcExploreRequest* request;
  guint current;
  guint total;

  /* TODO: This could later also show synchronization progress */

  gtk_tree_model_get(
    model,
    iter,
    INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
    -1
  );

  if(browser != NULL)
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    if(infc_browser_iter_is_subdirectory(browser, browser_iter))
    {
      request = infc_browser_iter_get_explore_request(browser, browser_iter);
      if(request != NULL)
      {
        if(infc_explore_request_get_finished(request) == FALSE)
        {
          if(infc_explore_request_get_initiated(request) == FALSE)
          {
            current = 0;
            total = 1;
          }
          else
          {
            g_object_get(
              G_OBJECT(request),
              "current", &current,
              "total", &total,
              NULL
            );
          }
          
          g_object_set(
            G_OBJECT(renderer),
            "visible", TRUE,
            "value", current * 100 / total,
            "text", "Exploring...",
            NULL
          );

          return;
        }
      }
    }

    g_object_unref(G_OBJECT(browser));
  }
  
  g_object_set(
    G_OBJECT(renderer),
    "visible", FALSE,
    NULL
  );
}

static void
inf_gtk_browser_view_status_data_func(GtkTreeViewColumn* column,
                                      GtkCellRenderer* renderer,
                                      GtkTreeModel* model,
                                      GtkTreeIter* iter,
                                      gpointer user_data)
{
  GtkTreeIter iter_parent;
  InfGtkBrowserModelStatus status;
  GError* error;

  if(gtk_tree_model_iter_parent(model, &iter_parent, iter))
  {
    /* Status is currrently only shown for toplevel items */
    g_object_set(G_OBJECT(renderer), "visible", FALSE, NULL);
  }
  else
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_STATUS, &status,
      INF_GTK_BROWSER_MODEL_COL_ERROR, &error,
      -1
    );

    switch(status)
    {
    case INF_GTK_BROWSER_MODEL_DISCOVERED:
      g_object_set(
        G_OBJECT(renderer),
        "text", "Not connected",
        "foreground", "black",
        "visible", FALSE, /* Don't show */
        NULL
      );

      break;
    case INF_GTK_BROWSER_MODEL_RESOLVING:
    case INF_GTK_BROWSER_MODEL_CONNECTING:
      g_object_set(
        G_OBJECT(renderer),
        "text", "Connecting...",
        "foreground", "black",
        "visible", TRUE,
        NULL
      );

      break;
    case INF_GTK_BROWSER_MODEL_CONNECTED:
      g_object_set(
        G_OBJECT(renderer),
        "text", "Connected",
        "foreground", "black",
        "visible", FALSE, /* Don't show */
        NULL
      );

      break;
    case INF_GTK_BROWSER_MODEL_ERROR:
      g_assert(error != NULL);

      g_object_set(
        G_OBJECT(renderer),
        "text", error->message,
        "foreground", "dark red",
        "visible", TRUE,
        NULL
      );

      break;
    default:
      g_assert_not_reached();
      break;
    }
  }
}

static void
inf_gtk_browser_view_init(GTypeInstance* instance,
                          gpointer g_class)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(instance);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  priv->treeview = gtk_tree_view_new();
  priv->column = gtk_tree_view_column_new();
  
  priv->renderer_icon = gtk_cell_renderer_pixbuf_new();
  priv->renderer_name = gtk_cell_renderer_text_new();
  priv->renderer_progress = gtk_cell_renderer_progress_new();
  priv->renderer_status = gtk_cell_renderer_text_new();

  priv->browsers =
    g_array_new(FALSE, FALSE, sizeof(InfGtkBrowserViewObject));
  priv->explore_requests =
    g_array_new(FALSE, FALSE, sizeof(InfGtkBrowserViewObject));

  g_object_set(
    G_OBJECT(priv->renderer_status),
    "ellipsize", PANGO_ELLIPSIZE_END,
    "xpad", 10,
    NULL
  );

  gtk_tree_view_column_pack_start(priv->column, priv->renderer_icon, FALSE);

  gtk_tree_view_column_pack_start(
    priv->column,
    priv->renderer_status_icon,
    FALSE
  );

  gtk_tree_view_column_pack_start(priv->column, priv->renderer_name, FALSE);
  gtk_tree_view_column_pack_start(
    priv->column,
    priv->renderer_progress,
    FALSE
  );

  gtk_tree_view_column_pack_start(priv->column, priv->renderer_status, TRUE);

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_icon,
    inf_gtk_browser_view_icon_data_func,
    NULL,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_status_icon,
    inf_gtk_browser_view_status_icon_data_func,
    NULL,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_name,
    inf_gtk_browser_view_name_data_func,
    NULL,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_progress,
    inf_gtk_browser_view_progress_data_func,
    NULL,
    NULL
  );

  gtk_tree_view_column_set_cell_data_func(
    priv->column,
    priv->renderer_status,
    inf_gtk_browser_view_status_data_func,
    NULL,
    NULL
  );

  gtk_tree_view_append_column(GTK_TREE_VIEW(priv->treeview), priv->column);
  gtk_container_add(GTK_CONTAINER(view), priv->treeview);
}

static void
inf_gtk_browser_view_dispose(GObject* object)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  inf_gtk_browser_view_set_model(view, NULL);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_gtk_browser_view_finalize(GObject* object)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  g_assert(priv->browsers->len == 0);
  g_assert(priv->explore_requests->len == 0);

  g_array_free(priv->browsers, TRUE);
  g_array_free(priv->explore_requests, TRUE);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_gtk_browser_view_set_property(GObject* object,
                                  guint prop_id,
                                  const GValue* value,
                                  GParamSpec* pspec)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_MODEL:
    inf_gtk_browser_view_set_model(
      view,
      INF_GTK_BROWSER_MODEL(g_value_get_object(value))
    );
  
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_browser_view_get_property(GObject* object,
                                  guint prop_id,
                                  GValue* value,
                                  GParamSpec* pspec)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  switch(prop_id)
  {
  case PROP_MODEL:
    g_value_set_object(
      value,
      G_OBJECT(gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview)))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_browser_view_set_scroll_adjustments(InfGtkBrowserView* view,
                                            GtkAdjustment* hadj,
                                            GtkAdjustment* vadj)
{
  InfGtkBrowserViewPrivate* priv;
  GtkWidgetClass* klass;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  klass = GTK_WIDGET_GET_CLASS(priv->treeview);

  /* Delegate to TreeView */
  g_assert(klass->set_scroll_adjustments_signal);
  g_signal_emit(
    G_OBJECT(priv->treeview),
    klass->set_scroll_adjustments_signal,
    0,
    hadj,
    vadj
  );
}

/*
 * GType registration
 */

static void
inf_gtk_browser_view_class_init(gpointer g_class,
                                gpointer class_data)
{
  GObjectClass* object_class;
  GtkWidgetClass* widget_class;
  InfGtkBrowserViewClass* view_class;

  object_class = G_OBJECT_CLASS(g_class);
  widget_class = GTK_WIDGET_CLASS(g_class);
  view_class = INF_GTK_BROWSER_VIEW_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkBrowserViewPrivate));

  object_class->dispose = inf_gtk_browser_view_dispose;
  object_class->finalize = inf_gtk_browser_view_finalize;
  object_class->set_property = inf_gtk_browser_view_set_property;
  object_class->get_property = inf_gtk_browser_view_get_property;

  view_class->set_scroll_adjustments =
    inf_gtk_browser_view_set_scroll_adjustments;

  g_object_class_install_property(
    object_class,
    PROP_MODEL,
    g_param_spec_object(
      "model",
      "Model", 
      "The model to display",
      INF_GTK_TYPE_BROWSER_MODEL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  widget_class->set_scroll_adjustments_signal = g_signal_new(
    "set-scroll-adjustments",
    G_TYPE_FROM_CLASS(object_class),
    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
    G_STRUCT_OFFSET(InfGtkBrowserViewClass, set_scroll_adjustments),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_OBJECT,
    G_TYPE_NONE,
    2,
    GTK_TYPE_ADJUSTMENT,
    GTK_TYPE_ADJUSTMENT
  );
}

GType
inf_gtk_browser_view_get_type(void)
{
  static GType browser_view_type = 0;

  if(!browser_view_type)
  {
    static const GTypeInfo browser_view_type_info = {
      sizeof(InfGtkBrowserViewClass),    /* class_size */
      NULL,                              /* base_init */
      NULL,                              /* base_finalize */
      inf_gtk_browser_view_class_init,   /* class_init */
      NULL,                              /* class_finalize */
      NULL,                              /* class_data */
      sizeof(InfGtkBrowserView),         /* instance_size */
      0,                                 /* n_preallocs */
      inf_gtk_browser_view_init,         /* instance_init */
      NULL                               /* value_table */
    };

    browser_view_type = g_type_register_static(
      GTK_TYPE_BIN,
      "InfGtkBrowserView",
      &browser_view_type_info,
      0
    );
  }

  return browser_view_type;
}

/*
 * Public API.
 */

/** inf_gtk_browser_view_new:
 *
 * Creates a new #InfGtkBrowserView.
 *
 * Return Value: A new #InfGtkBrowserView.
 **/
GtkWidget*
inf_gtk_browser_view_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_BROWSER_VIEW, NULL);
  return GTK_WIDGET(object);
}

/** inf_gtk_browser_view_new_with_model:
 *
 * @model: A #InfGtkBrowserModel.
 *
 * Creates a new #InfGtkBrowserView showing @model.
 *
 * Return Value: A new #InfGtkBrowserView.
 **/
GtkWidget*
inf_gtk_browser_view_new_with_model(InfGtkBrowserModel* model)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_BROWSER_VIEW, "model", model, NULL);
  return GTK_WIDGET(object);
}

/* vim:set et sw=2 ts=2: */
