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

#include <libinfgtk/inf-gtk-browser-model.h>
#include <libinfinity/client/infc-browser.h>
#include <libinfinity/inf-marshal.h>

#include <gtk/gtktreemodel.h>

/* The three pointers in GtkTreeIter are used as follows:
 *
 * user_data holds a pointer to the GtkTreeModelItem the iter points to.
 * user_data2 holds the node_id field of the InfcBrowserIter, or 0 if the
 * iter points to the toplevel node.
 * user_data3 holds the node field of the InfcBrowser, or NULL if the iter
 * points to the toplevel node. Note that it does not hold the root node of
 * the item's browser (if present) because the iter should remain valid when
 * the browser is removed (we set GTK_TREE_MODEL_ITERS_PERSIST).
 */

typedef struct _InfGtkBrowserModelItem InfGtkBrowserModelItem;
struct _InfGtkBrowserModelItem {
  InfDiscovery* discovery;
  InfDiscoveryInfo* info;

 /* This is the same as infc_browser_get_connection, but we need an extra
  * reference on this because we connect to its "error" signal and need to
  * disconnect when the browser already released its reference: */
  InfXmlConnection* connection;
  InfcBrowser* browser;

  /* Running requests */
  GSList* requests;
  /* Saved node errors (during exploration/subscription) */
  GHashTable* node_errors;

  /* TODO: Determine status at run-time? */
  InfGtkBrowserModelStatus status;

  /* Error on toplevel item */
  GError* error;

  /* Link */
  InfGtkBrowserModelItem* next;
};

typedef struct _InfGtkBrowserModelRequestData InfGtkBrowserModelRequestData;
struct _InfGtkBrowserModelRequestData {
  InfGtkBrowserModel* model;
  InfGtkBrowserModelItem* item;
};

typedef struct _InfGtkBrowserModelPrivate InfGtkBrowserModelPrivate;
struct _InfGtkBrowserModelPrivate {
  gint stamp;

  InfIo* io;
  InfConnectionManager* connection_manager;
  InfMethodManager* method_manager;

  GSList* discoveries;
  InfGtkBrowserModelItem* first_item;
  InfGtkBrowserModelItem* last_item;
};

enum {
  PROP_0,

  PROP_IO,
  PROP_CONNECTION_MANAGER,
  PROP_METHOD_MANAGER
};

enum {
  SET_BROWSER,

  LAST_SIGNAL
};

#define INF_GTK_BROWSER_MODEL_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_BROWSER_MODEL, InfGtkBrowserModelPrivate))

static GObjectClass* parent_class;
static guint browser_model_signals[LAST_SIGNAL];

/*
 * Utility functions
 */

static InfGtkBrowserModelItem*
inf_gtk_browser_model_find_item_by_connection(InfGtkBrowserModel* model,
                                              InfXmlConnection* connection)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

  for(item = priv->first_item; item != NULL; item = item->next)
    if(item->connection != NULL)
      if(item->connection == connection)
        return item;

  return NULL;
}

static InfGtkBrowserModelItem*
inf_gtk_browser_model_find_item_by_browser(InfGtkBrowserModel* model,
                                           InfcBrowser* browser)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

  for(item = priv->first_item; item != NULL; item = item->next)
    if(item->browser != NULL)
      if(item->browser == browser)
        return item;

  return NULL;
}

static InfGtkBrowserModelItem*
inf_gtk_browser_model_find_item_by_discovery_info(InfGtkBrowserModel* model,
                                                  InfDiscoveryInfo* info)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

  for(item = priv->first_item; item != NULL; item = item->next)
    if(item->info != NULL)
      if(item->info == info)
        return item;

  return NULL;
}

/*
 * Callback declarations
 */

static void
inf_gtk_browser_model_connection_notify_status_cb(GObject* object,
                                                  GParamSpec* pspec,
                                                  gpointer user_data);

static void
inf_gtk_browser_model_connection_error_cb(InfXmlConnection* connection,
                                          const GError* error,
                                          gpointer user_data);

static void
inf_gtk_browser_model_node_added_cb(InfcBrowser* browser,
                                    InfcBrowserIter* iter,
                                    gpointer user_data);

static void
inf_gtk_browser_model_node_removed_cb(InfcBrowser* browser,
                                      InfcBrowserIter* iter,
                                      gpointer user_data);

static void
inf_gtk_browser_model_begin_explore_cb(InfcBrowser* browser,
                                       InfcBrowserIter* iter,
                                       InfcExploreRequest* request,
                                       gpointer user_data);

static void
inf_gtk_browser_model_begin_subscribe_cb(InfcBrowser* browser,
                                         InfcBrowserIter* iter,
                                         InfcNodeRequest* request,
                                         gpointer user_data);

static void
inf_gtk_browser_model_request_failed_cb(InfcRequest* request,
                                        const GError* error,
                                        gpointer user_data);

static void
inf_gtk_browser_model_request_unrefed_func(gpointer data,
                                           GObject* where_the_object_was);

/*
 * InfGtkBrowserModelItem handling
 */

static void
inf_gtk_browser_model_request_data_free(gpointer data,
                                        GClosure* closure)
{
  g_slice_free(InfGtkBrowserModelRequestData, data);
}

static void
inf_gtk_browser_model_item_request_remove(InfGtkBrowserModelItem* item,
                                          InfcRequest* request)
{
  g_object_weak_unref(
    G_OBJECT(request),
    inf_gtk_browser_model_request_unrefed_func,
    item
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(request),
    G_CALLBACK(inf_gtk_browser_model_request_failed_cb),
    item
  );

  item->requests = g_slist_remove(item->requests, request);
}

static void
inf_gtk_browser_model_item_request_add(InfGtkBrowserModel* model,
                                       InfGtkBrowserModelItem* item,
                                       InfcRequest* request)
{
  InfGtkBrowserModelRequestData* data;

  g_assert(g_slist_find(item->requests, request) == NULL);
  item->requests = g_slist_prepend(item->requests, request);

  data = g_slice_new(InfGtkBrowserModelRequestData);
  data->model = model;
  data->item = item;

  g_signal_connect_data(
    G_OBJECT(request),
    "failed",
    G_CALLBACK(inf_gtk_browser_model_request_failed_cb),
    data,
    inf_gtk_browser_model_request_data_free,
    0
  );
  
  g_object_weak_ref(
    G_OBJECT(request),
    inf_gtk_browser_model_request_unrefed_func,
    item
  );
}

static void
inf_gtk_browser_model_request_failed_cb(InfcRequest* request,
                                        const GError* error,
                                        gpointer user_data)
{
  InfGtkBrowserModelRequestData* data;
  InfGtkBrowserModelPrivate* priv;
  InfcBrowserIter iter;
  gboolean node_exists;
  GtkTreeIter tree_iter;
  GtkTreePath* path;

  data = (InfGtkBrowserModelRequestData*)user_data;
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(data->model);

  g_assert(g_slist_find(data->item->requests, request) != NULL);
  g_assert(data->item->browser != NULL);

  /* TODO: Let explore request derive from node request */
  g_assert(INFC_IS_EXPLORE_REQUEST(request) || INFC_IS_NODE_REQUEST(request));

  if(INFC_IS_EXPLORE_REQUEST(request))
  {
    node_exists = infc_browser_iter_from_explore_request(
      data->item->browser,
      INFC_EXPLORE_REQUEST(request),
      &iter
    );
  }
  else
  {
    node_exists = infc_browser_iter_from_node_request(
      data->item->browser,
      INFC_NODE_REQUEST(request),
      &iter
    );
  }

  inf_gtk_browser_model_item_request_remove(data->item, request);

  /* Ignore if node has been removed in the meanwhile */
  if(G_LIKELY(node_exists))
  {
    /* Replace previous error */
    g_hash_table_insert(
      data->item->node_errors,
      GUINT_TO_POINTER(iter.node_id),
      g_error_copy(error)
    );

    tree_iter.stamp = priv->stamp;
    tree_iter.user_data = data->item;
    tree_iter.user_data2 = GUINT_TO_POINTER(iter.node_id);

    /* Set NULL for root node because it also refers to the model item as
     * such if no browser is set. */
    if(iter.node_id == 0)
      tree_iter.user_data3 = NULL;
    else
      tree_iter.user_data3 = iter.node;

    path = gtk_tree_model_get_path(GTK_TREE_MODEL(data->model), &tree_iter);
    gtk_tree_model_row_changed(GTK_TREE_MODEL(data->model), path, &tree_iter);
    gtk_tree_path_free(path);
  }
}

static void
inf_gtk_browser_model_request_unrefed_func(gpointer data,
                                           GObject* where_the_object_was)
{
  InfGtkBrowserModelItem* item;
  item = (InfGtkBrowserModelItem*)data;

  /* No need to further unregister */
  item->requests = g_slist_remove(item->requests, where_the_object_was);
}

static void
inf_gtk_browser_model_item_set_browser(InfGtkBrowserModel* model,
                                       InfGtkBrowserModelItem* item,
                                       GtkTreePath* path,
                                       InfcBrowser* browser)
{
  GtkTreeIter tree_iter;
  InfGtkBrowserModelPrivate* priv;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = item;
  tree_iter.user_data2 = GUINT_TO_POINTER(0);
  tree_iter.user_data3 = NULL;

  /* The default signal handler sets the browser in the item and makes the
   * necessary TreeModel notifications. See
   * inf_gtk_browser_model_item_set_browser_impl(). */
  g_signal_emit(
    G_OBJECT(model),
    browser_model_signals[SET_BROWSER],
    0,
    path,
    &tree_iter,
    browser
  );
}

static InfGtkBrowserModelItem*
inf_gtk_browser_model_add_item(InfGtkBrowserModel* model,
                               InfDiscovery* discovery,
                               InfDiscoveryInfo* info,
                               InfXmlConnection* connection)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfGtkBrowserModelItem* cur;
  GtkTreePath* path;
  GtkTreeIter iter;
  guint index;
  InfcBrowser* browser;

  g_assert(
    connection == NULL ||
    inf_gtk_browser_model_find_item_by_connection(model, connection) == NULL
  );

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  item = g_slice_new(InfGtkBrowserModelItem);
  item->discovery = discovery;
  item->info = info;
  item->status = INF_GTK_BROWSER_MODEL_DISCOVERED;
  item->browser = NULL;
  item->connection = NULL;
  item->node_errors = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)g_error_free
  );
  item->requests = NULL;
  item->error = NULL;
  item->next = NULL;
  
  index = 0;
  for(cur = priv->first_item; cur != NULL; cur = cur->next)
    ++ index;

  /* Link */
  if(priv->first_item == NULL)
  {
    priv->first_item = item;
    priv->last_item = item;
  }
  else
  {
    priv->last_item->next = item;
    priv->last_item = item;
  }

  path = gtk_tree_path_new_from_indices(index, -1);
  iter.stamp = priv->stamp;
  iter.user_data = item;
  iter.user_data2 = GUINT_TO_POINTER(0);
  iter.user_data3 = NULL;

  gtk_tree_model_row_inserted(GTK_TREE_MODEL(model), path, &iter);

  if(connection != NULL)
  {
    browser = infc_browser_new(
      priv->io,
      priv->connection_manager,
      priv->method_manager,
      connection
    );

    /* The connection is not set if the browser could not find a "central"
     * method for the connection's network. */
    /* TODO: Set error */
    if(infc_browser_get_connection(browser) != NULL)
      inf_gtk_browser_model_item_set_browser(model, item, path, browser);

    g_object_unref(G_OBJECT(browser));
  }

  gtk_tree_path_free(path);
  return item;
}

static void
inf_gtk_browser_model_remove_item(InfGtkBrowserModel* model,
                                  InfGtkBrowserModelItem* item)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* prev;
  InfGtkBrowserModelItem* cur;
  GtkTreePath* path;
  guint index;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

  /* Determine index of item, to build a tree path to it */
  prev = NULL;
  index = 0;

  for(cur = priv->first_item; cur != NULL; cur = cur->next)
  {
    if(cur == item)
      break;

    prev = cur;
    ++ index;
  }

  /* Item was present in list */
  g_assert(cur != NULL);

  path = gtk_tree_path_new_from_indices(index, -1);

  /* Note we need to reset the browser before we unlink because
   * inf_gtk_browser_model_item_set_browser() requires item still being
   * linked for change notifications. */
  if(item->browser != NULL)
  {
    inf_gtk_browser_model_item_set_browser(model, item, path, NULL);
    g_assert(item->browser == NULL); /* Default handler must run */
  }

  /* Unlink */
  if(prev == NULL)
    priv->first_item = item->next;
  else
    prev->next = item->next;

  if(item->next == NULL)
    priv->last_item = prev;

  g_assert(cur != NULL);

  gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);
  gtk_tree_path_free(path);
  
  if(item->error != NULL)
    g_error_free(item->error);

  g_hash_table_unref(item->node_errors);
  g_slice_free(InfGtkBrowserModelItem, item);
}

/*
 * Callbacks and signal handlers
 */

static void
inf_gtk_browser_model_discovered_cb(InfDiscovery* discovery,
                                    InfDiscoveryInfo* info,
                                    gpointer user_data)
{
  inf_gtk_browser_model_add_item(
    INF_GTK_BROWSER_MODEL(user_data),
    discovery,
    info,
    NULL
  );
}

static void
inf_gtk_browser_model_undiscovered_cb(InfDiscovery* discovery,
                                      InfDiscoveryInfo* info,
                                      gpointer user_data)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelItem* item;

  model = INF_GTK_BROWSER_MODEL(user_data);
  item = inf_gtk_browser_model_find_item_by_discovery_info(model, info);
  g_assert(item != NULL);

  /* TODO: Keep if connection exists, just reset discovery and info */
  inf_gtk_browser_model_remove_item(model, item);
}

static void
inf_gtk_browser_model_connection_error_cb(InfXmlConnection* connection,
                                          const GError* error,
                                          gpointer user_data)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelItem* item;
  InfGtkBrowserModelPrivate* priv;
  GtkTreeIter iter;
  GtkTreePath* path;

  model = INF_GTK_BROWSER_MODEL(user_data);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  item = inf_gtk_browser_model_find_item_by_connection(model, connection);
  g_assert(item != NULL);

  /* Overwrite previous error */
  if(item->error != NULL)
    g_error_free(item->error);

  item->error = g_error_copy(error);
  /* Don't set error state, this could be a non-fatal error */

  /* Notify */
  iter.stamp = priv->stamp;
  iter.user_data = item;
  iter.user_data2 = GUINT_TO_POINTER(0);
  iter.user_data3 = NULL;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &iter);
  gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, &iter);
  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_model_connection_notify_status_cb(GObject* object,
                                                  GParamSpec* pspec,
                                                  gpointer user_data)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelPrivate* priv;
  InfXmlConnection* connection;
  InfGtkBrowserModelItem* item;
  InfXmlConnectionStatus status;
  GtkTreeIter iter;
  GtkTreePath* path;

  model = INF_GTK_BROWSER_MODEL(user_data);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(user_data);  
  connection = INF_XML_CONNECTION(object);
  item = inf_gtk_browser_model_find_item_by_connection(model, connection);

  g_assert(item != NULL);
  g_assert(item->status != INF_GTK_BROWSER_MODEL_ERROR);

  iter.stamp = priv->stamp;
  iter.user_data = item;
  iter.user_data2 = GUINT_TO_POINTER(0);
  iter.user_data3 = NULL;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &iter);

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  switch(status)
  {
  case INF_XML_CONNECTION_OPENING:
    item->status = INF_GTK_BROWSER_MODEL_CONNECTING;
    gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, &iter);
    break;
  case INF_XML_CONNECTION_OPEN:
    item->status = INF_GTK_BROWSER_MODEL_CONNECTED;
    gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, &iter);
    break;
  case INF_XML_CONNECTION_CLOSING:
  case INF_XML_CONNECTION_CLOSED:
    item->status = INF_GTK_BROWSER_MODEL_ERROR;

    /* Set a "Disconnected" error if there is not already one set by
     * inf_gtk_browser_model_connection_error_cb() that has a more
     * meaningful error message. */
    if(item->error == NULL)
    {
      g_set_error(
        &item->error,
        g_quark_from_static_string("INF_GTK_BROWSER_MODEL_ERROR"),
        0,
        "Disconnected"
      );
    }

    gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, &iter);

    /* Reset browser, we do not need it anymore since its connection is
     * gone. */
    /* TODO: Keep browser and still allow browsing in explored folders,
     * but reset connection. */
    /* Status cannot change to invalid because we just set error */
    inf_gtk_browser_model_item_set_browser(model, item, path, NULL);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_model_node_added_cb(InfcBrowser* browser,
                                    InfcBrowserIter* iter,
                                    gpointer user_data)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;

  InfcBrowserIter test_iter;
  gboolean test_result;

  model = INF_GTK_BROWSER_MODEL(user_data);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  item = inf_gtk_browser_model_find_item_by_browser(model, browser);

  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = item;
  tree_iter.user_data2 = GUINT_TO_POINTER(iter->node_id);
  tree_iter.user_data3 = iter->node;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &tree_iter);
  gtk_tree_model_row_inserted(GTK_TREE_MODEL(model), path, &tree_iter);

  /* If iter is the only node within its parent, we need to emit the
   * row-has-child-toggled signal. */
  test_iter = *iter;
  test_result = infc_browser_iter_get_parent(browser, &test_iter);
  g_assert(test_result == TRUE);

  /* Let tree_iter point to parent row for possible notification */
  tree_iter.user_data2 = GUINT_TO_POINTER(test_iter.node_id);

  /* Also adjust path */
  gtk_tree_path_up(path);

  if(test_iter.node_id == 0)
    tree_iter.user_data3 = NULL;
  else
    tree_iter.user_data3 = test_iter.node;

  test_result = infc_browser_iter_get_child(browser, &test_iter);
  g_assert(test_result == TRUE);

  if(infc_browser_iter_get_next(browser, &test_iter) == FALSE)
  {
    gtk_tree_model_row_has_child_toggled(
      GTK_TREE_MODEL(model),
      path,
      &tree_iter
    );
  }

  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_model_node_removed_cb(InfcBrowser* browser,
                                      InfcBrowserIter* iter,
                                      gpointer user_data)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  InfcBrowserIter test_iter;
  gboolean test_result;

  model = INF_GTK_BROWSER_MODEL(user_data);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  item = inf_gtk_browser_model_find_item_by_browser(model, browser);

  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = item;
  tree_iter.user_data2 = GUINT_TO_POINTER(iter->node_id);
  tree_iter.user_data3 = iter->node;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &tree_iter);
  gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);

  /* TODO: Remove requests and node errors from nodes below the removed one */

  /* Note that at this point removed node is still in the browser. We have
   * to emit row-has-child-toggled if it is the only one in its
   * subdirectory. */
  test_iter = *iter;
  test_result = infc_browser_iter_get_parent(browser, &test_iter);
  g_assert(test_result == TRUE);

  /* Let tree_iter point to parent row for possible notification */
  tree_iter.user_data2 = GUINT_TO_POINTER(test_iter.node_id);

  /* Also adjust path */
  gtk_tree_path_up(path);

  if(test_iter.node_id == 0)
    tree_iter.user_data3 = NULL;
  else
    tree_iter.user_data3 = test_iter.node;

  test_result = infc_browser_iter_get_child(browser, &test_iter);
  g_assert(test_result == TRUE);

  if(infc_browser_iter_get_next(browser, &test_iter) == FALSE)
  {
    gtk_tree_model_row_has_child_toggled(
      GTK_TREE_MODEL(model),
      path,
      &tree_iter
    );
  }

  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_model_begin_explore_cb(InfcBrowser* browser,
                                       InfcBrowserIter* iter,
                                       InfcExploreRequest* request,
                                       gpointer user_data)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelItem* item;

  model = INF_GTK_BROWSER_MODEL(user_data);
  item = inf_gtk_browser_model_find_item_by_browser(model, browser);

  inf_gtk_browser_model_item_request_add(model, item, INFC_REQUEST(request));
}

static void
inf_gtk_browser_model_begin_subscribe_cb(InfcBrowser* browser,
                                         InfcBrowserIter* iter,
                                         InfcNodeRequest* request,
                                         gpointer user_data)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelItem* item;

  model = INF_GTK_BROWSER_MODEL(user_data);
  item = inf_gtk_browser_model_find_item_by_browser(model, browser);

  inf_gtk_browser_model_item_request_add(model, item, INFC_REQUEST(request));
}

static void
inf_gtk_browser_model_resolv_complete_func(InfDiscoveryInfo* info,
                                           InfXmlConnection* connection,
                                           gpointer user_data)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* new_item;
  InfGtkBrowserModelItem* old_item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  InfcBrowser* browser;

  InfGtkBrowserModelItem* cur;
  InfGtkBrowserModelItem* prev;
  InfGtkBrowserModelItem* prev_new;
  InfGtkBrowserModelItem* prev_old;
  gint* order;
  guint count;
  guint new_pos;
  guint old_pos;
  guint i;

  model = INF_GTK_BROWSER_MODEL(user_data);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  new_item = inf_gtk_browser_model_find_item_by_discovery_info(model, info);
  old_item = inf_gtk_browser_model_find_item_by_connection(model, connection);
  
  g_assert(new_item != NULL);
  g_assert(new_item->status == INF_GTK_BROWSER_MODEL_RESOLVING);

  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = new_item;
  tree_iter.user_data2 = GUINT_TO_POINTER(0);
  tree_iter.user_data3 = NULL;

  if(old_item != NULL)
  {
    g_assert(old_item != new_item);

    /* There is already an item with the same connection. This is perhaps from
     * another discovery or was inserted directly. We remove the current item
     * and move the existing one to the place of it. */

    count = 0;
    prev = NULL;

    for(cur = priv->first_item; cur != NULL; cur = cur->next)
    {
      if(cur == old_item) { old_pos = count; prev_old = prev; }
      if(cur == new_item) { new_pos = count; prev_new = prev; }
      ++ count;
      prev = cur;
    }

    inf_gtk_browser_model_remove_item(model, new_item);
    if(old_pos > new_pos) -- old_pos;
    else -- new_pos;
    -- count;

    /* Reorder list if the two items were not adjacent */
    if(new_pos != old_pos)
    {
      /* old item is last element, but it is moved elsewhere */
      if(old_item->next == NULL)
        priv->last_item = prev_old;

      /* Unlink old_item */
      if(prev_old != NULL)
        prev_old->next = old_item->next;
      else
        priv->first_item = old_item->next;

      /* Relink */
      old_item->next = prev_new->next;

      if(prev_new != NULL)
        prev_new->next = old_item;
      else
        priv->first_item = old_item;

      /* old_item has been moved to end of list */
      if(old_item->next == NULL)
        priv->last_item = old_item;

      order = g_malloc(sizeof(gint) * count);
      if(new_pos < old_pos)
      {
        for(i = 0; i < new_pos; ++ i)
          order[i] = i;
        order[new_pos] = old_pos;
        for(i = new_pos + 1; i <= old_pos; ++ i)
          order[i] = i - 1;
        for(i = old_pos + 1; i < count; ++ i)
          order[i] = i;
      }
      else
      {
        for(i = 0; i < old_pos; ++ i)
          order[i] = i;
        for(i = old_pos; i < new_pos; ++ i)
          order[i] = i + 1;
        order[new_pos] = old_pos;
        for(i = new_pos + 1; i < count; ++ i)
          order[i] = i;
      }

      path = gtk_tree_path_new();
      gtk_tree_model_rows_reordered(GTK_TREE_MODEL(model), path, NULL, order);
      gtk_tree_path_free(path);
      
      g_free(order);
    }
  }
  else
  {
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &tree_iter);

    browser = infc_browser_new(
      priv->io,
      priv->connection_manager,
      priv->method_manager,
      connection
    );

    /* The connection is not set if the browser could not find a "central"
     * method for the connection's network. */
    /* TODO: Set error */
    if(infc_browser_get_connection(browser) != NULL)
      inf_gtk_browser_model_item_set_browser(model, new_item, path, browser);

    g_object_unref(G_OBJECT(browser));
    gtk_tree_path_free(path);
  }
}

static void
inf_gtk_browser_model_resolv_error_func(InfDiscoveryInfo* info,
                                        const GError* error,
                                        gpointer user_data)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;

  model = INF_GTK_BROWSER_MODEL(user_data);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  item = inf_gtk_browser_model_find_item_by_discovery_info(model, info);

  g_assert(item != NULL);
  g_assert(item->status == INF_GTK_BROWSER_MODEL_RESOLVING);
  item->status = INF_GTK_BROWSER_MODEL_ERROR;
  item->error = g_error_copy(error);

  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = item;
  tree_iter.user_data2 = GUINT_TO_POINTER(0);
  tree_iter.user_data3 = NULL;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &tree_iter);
  gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, &tree_iter);
  gtk_tree_path_free(path);
}

/*
 * GObject overrides
 */

static void
inf_gtk_browser_model_init(GTypeInstance* instance,
                           gpointer g_class)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelPrivate* priv;

  model = INF_GTK_BROWSER_MODEL(instance);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

  priv->stamp = g_random_int();
  priv->io = NULL;
  priv->connection_manager = NULL;
  priv->method_manager = NULL;
  priv->discoveries = NULL;
  priv->first_item = NULL;
  priv->last_item = NULL;
}

static void
inf_gtk_browser_model_dispose(GObject* object)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelPrivate* priv;
  GSList* item;

  model = INF_GTK_BROWSER_MODEL(object);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

  while(priv->first_item != NULL)
    inf_gtk_browser_model_remove_item(model, priv->first_item);
  g_assert(priv->last_item == NULL);

  for(item = priv->discoveries; item != NULL; item = g_slist_next(item))
  {
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(item->data),
      G_CALLBACK(inf_gtk_browser_model_discovered_cb),
      model
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(item->data),
      G_CALLBACK(inf_gtk_browser_model_undiscovered_cb),
      model
    );

    g_object_unref(G_OBJECT(item->data));
  }

  g_slist_free(priv->discoveries);
  priv->discoveries = NULL;

  if(priv->method_manager != NULL)
  {
    g_object_unref(priv->method_manager);
    priv->method_manager = NULL;
  }

  if(priv->connection_manager != NULL)
  {
    g_object_unref(G_OBJECT(priv->connection_manager));
    priv->connection_manager = NULL;
  }

  if(priv->io != NULL)
  {
    g_object_unref(G_OBJECT(priv->io));
    priv->io = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_gtk_browser_model_set_property(GObject* object,
                                   guint prop_id,
                                   const GValue* value,
                                   GParamSpec* pspec)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelPrivate* priv;

  model = INF_GTK_BROWSER_MODEL(object);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct only */
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_CONNECTION_MANAGER: 
    g_assert(priv->connection_manager == NULL); /* construct only */
    priv->connection_manager =
      INF_CONNECTION_MANAGER(g_value_dup_object(value));
  
    break;
  case PROP_METHOD_MANAGER:
    g_assert(priv->method_manager == NULL); /* construct/only */
    priv->method_manager = INF_METHOD_MANAGER(g_value_dup_object(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_browser_model_get_property(GObject* object,
                                   guint prop_id,
                                   GValue* value,
                                   GParamSpec* pspec)
{
  InfGtkBrowserModel* model;
  InfGtkBrowserModelPrivate* priv;

  model = INF_GTK_BROWSER_MODEL(object);
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_CONNECTION_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->connection_manager));
    break;
  case PROP_METHOD_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->method_manager));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * GtkTreeModel implementation
 */

static GtkTreeModelFlags
inf_gtk_browser_model_tree_model_get_flags(GtkTreeModel* model)
{
  return GTK_TREE_MODEL_ITERS_PERSIST;
}

static gint
inf_gtk_browser_model_tree_model_get_n_columns(GtkTreeModel* model)
{
  return INF_GTK_BROWSER_MODEL_NUM_COLS;
}

static GType
inf_gtk_browser_model_tree_model_get_column_type(GtkTreeModel* model,
                                                 gint index)
{
  switch(index)
  {
  case INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO:
    return G_TYPE_POINTER;
  case INF_GTK_BROWSER_MODEL_COL_DISCOVERY:
    return INF_TYPE_DISCOVERY;
  case INF_GTK_BROWSER_MODEL_COL_BROWSER:
    return INFC_TYPE_BROWSER;
  case INF_GTK_BROWSER_MODEL_COL_STATUS:
    return INF_GTK_TYPE_BROWSER_MODEL_STATUS;
  case INF_GTK_BROWSER_MODEL_COL_ERROR:
    return G_TYPE_POINTER;
  case INF_GTK_BROWSER_MODEL_COL_NODE:
    return INFC_TYPE_BROWSER_ITER;
  default:
    g_assert_not_reached();
    return G_TYPE_INVALID;
  }
}

static gboolean
inf_gtk_browser_model_tree_model_get_iter(GtkTreeModel* model,
                                          GtkTreeIter* iter,
                                          GtkTreePath* path)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfcBrowserIter browser_iter;
  gint* indices;

  guint i;
  guint n;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  if(gtk_tree_path_get_depth(path) == 0) return FALSE;

  indices = gtk_tree_path_get_indices(path);
  n = indices[0];

  i = 0;
  for(item = priv->first_item; item != NULL && i < n; item = item->next)
    ++ i;

  if(item == NULL) return FALSE;

  /* Depth 1 */
  if(gtk_tree_path_get_depth(path) == 1)
  {
    iter->stamp = priv->stamp;
    iter->user_data = item;
    iter->user_data2 = GUINT_TO_POINTER(0);
    iter->user_data3 = NULL;
    return TRUE;
  }

  if(item->browser == NULL) return FALSE;
  infc_browser_iter_get_root(item->browser, &browser_iter);
  
  for(n = 1; n < (guint)gtk_tree_path_get_depth(path); ++ n)
  {
    if(infc_browser_iter_get_explored(item->browser, &browser_iter) == FALSE)
      return FALSE;

    if(infc_browser_iter_get_child(item->browser, &browser_iter) == FALSE)
      return FALSE;

    for(i = 0; i < (guint)indices[n]; ++ i)
    {
      if(infc_browser_iter_get_next(item->browser, &browser_iter) == FALSE)
        return FALSE;
    }
  }

  iter->stamp = priv->stamp;
  iter->user_data = item;
  iter->user_data2 = GUINT_TO_POINTER(browser_iter.node_id);
  iter->user_data3 = browser_iter.node;
  return TRUE;
}

/* TODO: We can also use gtk_tree_path_prepend_index and do tail
 * recursion. We should find out which is faster. */
static void
inf_gtk_browser_model_tree_model_get_path_impl(InfGtkBrowserModel* model,
                                               InfGtkBrowserModelItem* item,
                                               InfcBrowserIter* iter,
                                               GtkTreePath* path)
{
  InfGtkBrowserModelPrivate* priv;
  InfcBrowserIter cur_iter;
  InfGtkBrowserModelItem* cur;
  gboolean result;
  guint n;
  
  cur_iter = *iter;
  if(infc_browser_iter_get_parent(item->browser, &cur_iter) == FALSE)
  {
    priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

    /* We are on top level, but still need to find the item index */
    n = 0;
    for(cur = priv->first_item; cur != item; cur = cur->next)
      ++ n;

    gtk_tree_path_append_index(path, n);
  }
  else
  {
    inf_gtk_browser_model_tree_model_get_path_impl(
      model,
      item,
      &cur_iter,
      path
    );

    result = infc_browser_iter_get_child(item->browser, &cur_iter);
    g_assert(result == TRUE);

    n = 0;
    while(cur_iter.node_id != iter->node_id)
    {
      result = infc_browser_iter_get_next(item->browser, &cur_iter);
      g_assert(result == TRUE);
      ++ n;
    }

    gtk_tree_path_append_index(path, n);
  }
}

static GtkTreePath*
inf_gtk_browser_model_tree_model_get_path(GtkTreeModel* model,
                                          GtkTreeIter* iter)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfGtkBrowserModelItem* cur;
  GtkTreePath* path;
  InfcBrowserIter browser_iter;
  guint n;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  g_assert(iter->stamp == priv->stamp);
  g_assert(iter->user_data != NULL);
  
  item = (InfGtkBrowserModelItem*)iter->user_data;

  path = gtk_tree_path_new();
  browser_iter.node_id = GPOINTER_TO_UINT(iter->user_data2);
  browser_iter.node = iter->user_data3;

  if(browser_iter.node != NULL)
  {
    g_assert(item->browser != NULL);

    inf_gtk_browser_model_tree_model_get_path_impl(
      INF_GTK_BROWSER_MODEL(model),
      item,
      &browser_iter,
      path
    );
  }
  else
  {
    /* toplevel */
    n = 0;
    for(cur = priv->first_item; cur != item; cur = cur->next)
      ++ n;

    gtk_tree_path_append_index(path, n);
  }

  return path;
}

static void
inf_gtk_browser_model_tree_model_get_value(GtkTreeModel* model,
                                           GtkTreeIter* iter,
                                           gint column,
                                           GValue* value)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfcBrowserIter browser_iter;
  GError* error;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  g_assert(iter->stamp == priv->stamp);

  item = (InfGtkBrowserModelItem*)iter->user_data;
  browser_iter.node_id = GPOINTER_TO_UINT(iter->user_data2);
  browser_iter.node = iter->user_data3;
  
  switch(column)
  {
  case INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO:
    g_value_init(value, G_TYPE_POINTER);
    g_value_set_pointer(value, item->info);
    break;
  case INF_GTK_BROWSER_MODEL_COL_DISCOVERY:
    g_value_init(value, G_TYPE_OBJECT);
    g_value_set_object(value, item->discovery);
    break;
  case INF_GTK_BROWSER_MODEL_COL_BROWSER:
    g_value_init(value, INFC_TYPE_BROWSER);
    g_value_set_object(value, G_OBJECT(item->browser));
    break;
  case INF_GTK_BROWSER_MODEL_COL_STATUS:
    g_assert(browser_iter.node == NULL); /* only toplevel */
    g_value_init(value, INF_GTK_TYPE_BROWSER_MODEL_STATUS);
    g_value_set_enum(value, item->status);
    break;
  case INF_GTK_BROWSER_MODEL_COL_ERROR:
    if(browser_iter.node == NULL)
    {
      /* toplevel */
      if(item->error != NULL)
      {
        /* not a node related error, perhaps connection error */
        error = item->error;
      }
      else if(item->browser != NULL)
      {
        /* error on root node */
        infc_browser_iter_get_root(item->browser, &browser_iter);
        error = g_hash_table_lookup(
          item->node_errors,
          GUINT_TO_POINTER(browser_iter.node_id)
        );
      }
      else
      {
        /* Neither error nor browser set: no error */
        error = NULL;
      }
    }
    else
    {
      g_assert(item->browser != NULL);

      error = g_hash_table_lookup(
        item->node_errors,
        GUINT_TO_POINTER(browser_iter.node_id)
      );
    }

    g_value_init(value, G_TYPE_POINTER);
    g_value_set_pointer(value, error);
    break;
  case INF_GTK_BROWSER_MODEL_COL_NODE:
    if(browser_iter.node == NULL)
    {
      /* get root node, if available */
      g_assert(item->browser != NULL);
      infc_browser_iter_get_root(item->browser, &browser_iter);
    }

    g_value_init(value, INFC_TYPE_BROWSER_ITER);
    g_value_set_boxed(value, &browser_iter);
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static gboolean
inf_gtk_browser_model_tree_model_iter_next(GtkTreeModel* model,
                                           GtkTreeIter* iter)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfcBrowserIter browser_iter;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  g_assert(iter->stamp == priv->stamp);

  item = (InfGtkBrowserModelItem*)iter->user_data;
  browser_iter.node_id = GPOINTER_TO_UINT(iter->user_data2);
  browser_iter.node = iter->user_data3;

  if(browser_iter.node == NULL)
  {
    if(item->next == NULL)
      return FALSE;

    iter->user_data = item->next;
    return TRUE;
  }
  else
  {
    if(infc_browser_iter_get_next(item->browser, &browser_iter) == FALSE)
      return FALSE;

    iter->user_data2 = GUINT_TO_POINTER(browser_iter.node_id);
    iter->user_data3 = browser_iter.node;
    return TRUE;
  }
}

static gboolean
inf_gtk_browser_model_tree_model_iter_children(GtkTreeModel* model,
                                               GtkTreeIter* iter,
                                               GtkTreeIter* parent)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfcBrowserIter browser_iter;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  g_assert(parent->stamp == priv->stamp);

  if(parent == NULL)
  {
    if(priv->first_item == NULL)
      return FALSE;

    iter->stamp = priv->stamp;
    iter->user_data = priv->first_item;
    iter->user_data2 = GUINT_TO_POINTER(0);
    iter->user_data3 = NULL;
    return TRUE;
  }
  else
  {
    item = (InfGtkBrowserModelItem*)parent->user_data;
    if(item->browser == NULL)
      return FALSE;

    browser_iter.node_id = GPOINTER_TO_UINT(parent->user_data2);
    if(browser_iter.node_id == 0)
      infc_browser_iter_get_root(item->browser, &browser_iter);
    else
      browser_iter.node = parent->user_data3;

    if(!infc_browser_iter_is_subdirectory(item->browser, &browser_iter))
      return FALSE;

    if(!infc_browser_iter_get_explored(item->browser, &browser_iter))
      return FALSE;

    if(!infc_browser_iter_get_child(item->browser, &browser_iter))
      return FALSE;

    iter->stamp = priv->stamp;
    iter->user_data = item;
    iter->user_data2 = GUINT_TO_POINTER(browser_iter.node_id);
    iter->user_data3 = browser_iter.node;
    return TRUE;
  }
}

static gboolean
inf_gtk_browser_model_tree_model_iter_has_child(GtkTreeModel* model,
                                                GtkTreeIter* iter)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfcBrowserIter browser_iter;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  g_assert(iter->stamp == priv->stamp);

  item = (InfGtkBrowserModelItem*)iter->user_data;
  if(item->browser == NULL) return FALSE;

  browser_iter.node_id = GPOINTER_TO_UINT(iter->user_data2);
  browser_iter.node = iter->user_data3;

  if(browser_iter.node == NULL)
    infc_browser_iter_get_root(item->browser, &browser_iter);

  if(infc_browser_iter_is_subdirectory(item->browser, &browser_iter) == FALSE)
    return FALSE;

  if(infc_browser_iter_get_explored(item->browser, &browser_iter) == FALSE)
    return FALSE;

  return infc_browser_iter_get_child(item->browser, &browser_iter);
}

static gint
inf_gtk_browser_model_tree_model_iter_n_children(GtkTreeModel* model,
                                                 GtkTreeIter* iter)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfGtkBrowserModelItem* cur;
  InfcBrowserIter browser_iter;
  gboolean result;
  guint n;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  g_assert(iter->stamp == priv->stamp);

  if(iter == NULL)
  {
    n = 0;
    for(cur = priv->first_item; cur != NULL; cur = cur->next)
      ++ n;

    return n;
  }
  else
  {
    item = (InfGtkBrowserModelItem*)iter->user_data;
    browser_iter.node_id = GPOINTER_TO_UINT(iter->user_data2);
    browser_iter.node = iter->user_data3;

    if(browser_iter.node == NULL)
      infc_browser_iter_get_root(item->browser, &browser_iter);

    if(infc_browser_iter_get_explored(item->browser, &browser_iter) == FALSE)
      return 0;

    n = 0;
    for(result = infc_browser_iter_get_child(item->browser, &browser_iter);
        result == TRUE;
        result = infc_browser_iter_get_next(item->browser, &browser_iter))
    {
      ++ n;
    }

    return n;
  }
}

static gboolean
inf_gtk_browser_model_tree_model_iter_nth_child(GtkTreeModel* model,
                                                GtkTreeIter* iter,
                                                GtkTreeIter* parent,
                                                gint n)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfGtkBrowserModelItem* cur;
  InfcBrowserIter browser_iter;
  guint i;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);

  if(parent == NULL)
  {
    cur = priv->first_item;
    if(cur == NULL) return FALSE;

    for(i = 0; i < (guint)n; ++ i)
    {
      cur = cur->next;
      if(cur == NULL) return FALSE;
    }

    iter->stamp = priv->stamp;
    iter->user_data = cur;
    iter->user_data2 = GUINT_TO_POINTER(0);
    iter->user_data3 = NULL;
    return TRUE;
  }
  else
  {
    g_assert(parent->stamp == priv->stamp);

    item = (InfGtkBrowserModelItem*)parent->user_data;
    browser_iter.node_id = GPOINTER_TO_UINT(parent->user_data2);

    if(browser_iter.node_id == 0)
      infc_browser_iter_get_root(item->browser, &browser_iter);
    else
      browser_iter.node = parent->user_data3;

    if(infc_browser_iter_get_explored(item->browser, &browser_iter) == FALSE)
      return FALSE;
                
    if(infc_browser_iter_get_child(item->browser, &browser_iter) == FALSE)
      return FALSE;

    for(i = 0; i < (guint)n; ++ i)
    {
      if(infc_browser_iter_get_next(item->browser, &browser_iter) == FALSE)
        return FALSE;
    }

    iter->stamp = priv->stamp;
    iter->user_data = item;
    iter->user_data2 = GUINT_TO_POINTER(browser_iter.node_id);
    iter->user_data3 = browser_iter.node;
    return TRUE;
  }
}

static gboolean
inf_gtk_browser_model_tree_model_iter_parent(GtkTreeModel* model,
                                             GtkTreeIter* iter,
                                             GtkTreeIter* child)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfcBrowserIter browser_iter;
  gboolean result;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  g_assert(child->stamp == priv->stamp);

  item = (InfGtkBrowserModelItem*)child->user_data;
  browser_iter.node_id = GPOINTER_TO_UINT(child->user_data2);
  browser_iter.node = child->user_data3;

  if(browser_iter.node == NULL)
    return FALSE;

  result = infc_browser_iter_get_parent(item->browser, &browser_iter);
  g_assert(result == TRUE);

  iter->stamp = priv->stamp;
  iter->user_data = item;
  iter->user_data2 = GUINT_TO_POINTER(browser_iter.node_id);
  iter->user_data3 = browser_iter.node;

  /* Root node */
  if(browser_iter.node_id == 0)
    iter->user_data3 = NULL;

  return TRUE;
}

/*
 * Default signal handlers
 */

static void
inf_gtk_browser_model_set_browser_impl(InfGtkBrowserModel* model,
                                       GtkTreePath* path,
                                       GtkTreeIter* tree_iter,
                                       InfcBrowser* browser)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  InfXmlConnectionStatus status;

  InfcBrowserIter iter;
  guint n;
  gboolean had_children;

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  had_children = FALSE;

  item = (InfGtkBrowserModelItem*)tree_iter->user_data;
  /* cannot set browser in non-toplevel entries */
  g_assert(tree_iter->user_data3 == NULL);

  if(item->browser != NULL)
  {
    /* Notify about deleted rows. Notify in reverse order so that indexing
     * continues to work. Remember whether we had children to emit
     * row-has-child-toggled later. */
    infc_browser_iter_get_root(item->browser, &iter);
    if(infc_browser_iter_get_explored(item->browser, &iter) &&
       infc_browser_iter_get_child(item->browser, &iter))
    {
      n = 1;
      while(infc_browser_iter_get_next(item->browser, &iter))
        ++ n;

      gtk_tree_path_append_index(path, n);

      for(; n > 0; -- n)
      {
        had_children = TRUE;
        gtk_tree_path_prev(path);
        gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);
      }

      gtk_tree_path_up(path);
    }

    if(item->connection != NULL)
    {
      g_signal_handlers_disconnect_by_func(
        G_OBJECT(item->connection),
        G_CALLBACK(inf_gtk_browser_model_connection_error_cb),
        model
      );

      g_signal_handlers_disconnect_by_func(
        G_OBJECT(item->connection),
        G_CALLBACK(inf_gtk_browser_model_connection_notify_status_cb),
        model
      );

      g_object_unref(G_OBJECT(item->connection));
    }

    while(item->requests != NULL)
    {
      inf_gtk_browser_model_item_request_remove(
        item,
        INFC_REQUEST(item->requests->data)
      );
    }

    g_hash_table_remove_all(item->node_errors);

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_model_node_added_cb),
      model
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_model_node_removed_cb),
      model
    );
    
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_model_begin_explore_cb),
      model
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_model_begin_subscribe_cb),
      model
    );

    g_object_unref(G_OBJECT(item->browser));
  }

  /* Reset browser for emitting row-has-child-toggled */
  item->browser = NULL;
  if(had_children)
  {
    gtk_tree_model_row_has_child_toggled(
      GTK_TREE_MODEL(model),
      path,
      tree_iter
    );
  }

  /* Set up new browser and connection */
  item->browser = browser;
  if(browser != NULL)
    item->connection = infc_browser_get_connection(browser);
  else
    item->connection = NULL;

  if(browser != NULL)
  {
    if(item->connection != NULL)
    {
      g_object_ref(G_OBJECT(item->connection));

      g_signal_connect(
        G_OBJECT(item->connection),
        "error",
        G_CALLBACK(inf_gtk_browser_model_connection_error_cb),
        model
      );
      
      g_signal_connect(
        G_OBJECT(item->connection),
        "notify::status",
        G_CALLBACK(inf_gtk_browser_model_connection_notify_status_cb),
        model
      );
    }

    g_object_ref(G_OBJECT(browser));

    g_signal_connect_after(
      G_OBJECT(item->browser),
      "node-added",
      G_CALLBACK(inf_gtk_browser_model_node_added_cb),
      model
    );

    g_signal_connect_after(
      G_OBJECT(item->browser),
      "node-removed",
      G_CALLBACK(inf_gtk_browser_model_node_removed_cb),
      model
    );

    g_signal_connect_after(
      G_OBJECT(item->browser),
      "begin-explore",
      G_CALLBACK(inf_gtk_browser_model_begin_explore_cb),
      model
    );

    g_signal_connect_after(
      G_OBJECT(item->browser),
      "begin-subscribe",
      G_CALLBACK(inf_gtk_browser_model_begin_subscribe_cb),
      model
    );

    /* TODO: Walk browser for requests */
  }

  /* TODO: Emit row_inserted for the whole tree in browser, and
   * row-has-child-toggled where appropriate. */
  gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, tree_iter);

  /* Set status to invalid if there aren't any connection information anymore.
   * Keep the item if an error is set, so it can be displayed. */
  if(item->browser == NULL && item->info == NULL && item->error == NULL)
  {
    item->status = INF_GTK_BROWSER_MODEL_INVALID;
  }
  else if(item->status != INF_GTK_BROWSER_MODEL_ERROR)
  {
    /* Set item status according to connection status if there is no error
     * set. */
    if(item->connection != NULL)
    {
      g_object_get(G_OBJECT(item->connection), "status", &status, NULL);
      switch(status)
      {
      case INF_XML_CONNECTION_OPENING:
        item->status = INF_GTK_BROWSER_MODEL_CONNECTING;
        break;
      case INF_XML_CONNECTION_OPEN:
        item->status = INF_GTK_BROWSER_MODEL_CONNECTED;
        break;
      case INF_XML_CONNECTION_CLOSING:
      case INF_XML_CONNECTION_CLOSED:
        /* Browser drops connection if in one of those states */
      default:
        g_assert_not_reached();
        break;
      }
    }
    else
    {
      /* No connection available. Discovery needs to be set now, otherwise
       * we would have set the status to invalid above. */
      g_assert(item->info != NULL);
      item->status = INF_GTK_BROWSER_MODEL_DISCOVERED;
    }
  }
  else
  {
    /* Error needs to be set in error status */
    g_assert(item->error != NULL);
  }
}

/*
 * GType registration
 */

static void
inf_gtk_browser_model_class_init(gpointer g_class,
                                 gpointer class_data)
{
  GObjectClass* object_class;
  InfGtkBrowserModelClass* browser_model_class;

  object_class = G_OBJECT_CLASS(g_class);
  browser_model_class = INF_GTK_BROWSER_MODEL_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkBrowserModelPrivate));

  object_class->dispose = inf_gtk_browser_model_dispose;
  object_class->set_property = inf_gtk_browser_model_set_property;
  object_class->get_property = inf_gtk_browser_model_get_property;

  browser_model_class->set_browser = inf_gtk_browser_model_set_browser_impl;

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "The IO object used for the created browsers to schedule timeouts",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CONNECTION_MANAGER,
    g_param_spec_object(
      "connection-manager",
      "Connection manager", 
      "The connection manager used for browsing remote directories",
      INF_TYPE_CONNECTION_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_METHOD_MANAGER,
    g_param_spec_object(
      "method-manager",
      "Method manager",
      "The method manager used for browsing remote directories",
      INF_TYPE_METHOD_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  browser_model_signals[SET_BROWSER] = g_signal_new(
    "set-browser",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfGtkBrowserModelClass, set_browser),
    NULL, NULL,
    inf_marshal_VOID__BOXED_BOXED_OBJECT,
    G_TYPE_NONE,
    3,
    GTK_TYPE_TREE_PATH | G_SIGNAL_TYPE_STATIC_SCOPE,
    GTK_TYPE_TREE_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INFC_TYPE_BROWSER
  );
}

static void
inf_gtk_browser_model_tree_model_init(gpointer g_iface,
                                      gpointer iface_data)
{
  GtkTreeModelIface* iface;
  iface = (GtkTreeModelIface*)g_iface;

  iface->get_flags = inf_gtk_browser_model_tree_model_get_flags;
  iface->get_n_columns = inf_gtk_browser_model_tree_model_get_n_columns;
  iface->get_column_type = inf_gtk_browser_model_tree_model_get_column_type;
  iface->get_iter = inf_gtk_browser_model_tree_model_get_iter;
  iface->get_path = inf_gtk_browser_model_tree_model_get_path;
  iface->get_value = inf_gtk_browser_model_tree_model_get_value;
  iface->iter_next = inf_gtk_browser_model_tree_model_iter_next;
  iface->iter_children = inf_gtk_browser_model_tree_model_iter_children;
  iface->iter_has_child = inf_gtk_browser_model_tree_model_iter_has_child;
  iface->iter_n_children = inf_gtk_browser_model_tree_model_iter_n_children;
  iface->iter_nth_child = inf_gtk_browser_model_tree_model_iter_nth_child;
  iface->iter_parent = inf_gtk_browser_model_tree_model_iter_parent;
}

GType
inf_gtk_browser_model_status_get_type(void)
{
  static GType browser_model_status_type = 0;

  if(!browser_model_status_type)
  {
    static const GEnumValue browser_model_status_values[] = {
      {
        INF_GTK_BROWSER_MODEL_DISCOVERED,
        "INF_GTK_BROWSER_MODEL_DISCOVERED",
        "discovered"
      }, {
        INF_GTK_BROWSER_MODEL_RESOLVING,
        "INF_GTK_BROWSER_MODEL_RESOLVING",
        "resolving"
      }, {
        INF_GTK_BROWSER_MODEL_CONNECTING,
        "INF_GTK_BROWSER_MODEL_CONNECTING",
        "connecting"
      }, {
        INF_GTK_BROWSER_MODEL_CONNECTED,
        "INF_GTK_BROWSER_MODEL_CONNECTED",
        "connected"
      }, {
        INF_GTK_BROWSER_MODEL_ERROR,
        "INF_GTK_BROWSER_MODEL_ERROR",
        "error"
      }
    };

    browser_model_status_type = g_enum_register_static(
      "InfGtkBrowserModelStatus",
      browser_model_status_values
    );
  }

  return browser_model_status_type;
}

GType
inf_gtk_browser_model_get_type(void)
{
  static GType browser_model_type = 0;

  if(!browser_model_type)
  {
    static const GTypeInfo browser_model_type_info = {
      sizeof(InfGtkBrowserModelClass),    /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      inf_gtk_browser_model_class_init,   /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof(InfGtkBrowserModel),         /* instance_size */
      0,                                  /* n_preallocs */
      inf_gtk_browser_model_init,         /* instance_init */
      NULL                                /* value_table */
    };

    static const GInterfaceInfo tree_model_info = {
      inf_gtk_browser_model_tree_model_init,
      NULL,
      NULL
    };

    browser_model_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfGtkBrowserModel",
      &browser_model_type_info,
      0
    );

    g_type_add_interface_static(
      browser_model_type,
      GTK_TYPE_TREE_MODEL,
      &tree_model_info
    );
  }

  return browser_model_type;
}

/*
 * Public API.
 */

/** inf_gtk_browser_model_new:
 *
 * @io: A #InfIo object for the created #InfcBrowser to schedule timeouts.
 * @connection_manager: The #InfConnectionManager with which to explore
 * remote directories.
 * @method_manager: The #InfMethodManager with which to explore remote
 * directories, or %NULL to use the default method manager.
 *
 * Creates a new #InfGtkBrowserModel.
 *
 * Return Value: A new #InfGtkBrowserModel.
 **/
InfGtkBrowserModel*
inf_gtk_browser_model_new(InfIo* io,
                          InfConnectionManager* connection_manager,
                          InfMethodManager* method_manager)
{
  GObject* object;

  object = g_object_new(
    INF_GTK_TYPE_BROWSER_MODEL,
    "io", io,
    "connection-manager", connection_manager,
    "method-manager", method_manager,
    NULL
  );

  return INF_GTK_BROWSER_MODEL(object);
}

/** inf_gtk_browser_model_add_discovery:
 *
 * @model: A #InfGtkBrowserModel.
 * @discovery: A #InfDiscovery not yet added to @model.
 *
 * Adds @discovery to @model. The model will then show up discovered
 * servers.
 **/
void
inf_gtk_browser_model_add_discovery(InfGtkBrowserModel* model,
                                    InfDiscovery* discovery)
{
  InfGtkBrowserModelPrivate* priv;
  GSList* discovered;
  GSList* item;

  g_return_if_fail(INF_GTK_IS_BROWSER_MODEL(model));
  g_return_if_fail(INF_IS_DISCOVERY(discovery));

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  g_return_if_fail(g_slist_find(priv->discoveries, discovery) == NULL);

  g_object_ref(G_OBJECT(discovery));
  priv->discoveries = g_slist_prepend(priv->discoveries, discovery);

  g_signal_connect(
    G_OBJECT(discovery),
    "discovered",
    G_CALLBACK(inf_gtk_browser_model_discovered_cb),
    model
  );

  g_signal_connect(
    G_OBJECT(discovery),
    "undiscovered",
    G_CALLBACK(inf_gtk_browser_model_undiscovered_cb),
    model
  );

  discovered = inf_discovery_get_discovered(discovery, "_infinote._tcp");
  for(item = discovered; item != NULL; item = g_slist_next(item))
  {
    inf_gtk_browser_model_add_item(
      model,
      discovery,
      (InfDiscoveryInfo*)item->data,
      NULL
    );
  }
  g_slist_free(discovered);

  inf_discovery_discover(discovery, "_infinote._tcp");
}

/** inf_gtk_browser_model_add_connection:
 *
 * @model: A #InfGtkBrowserModel.
 * @connection: A #InfXmlConnection.
 *
 * This function adds a connection to the @model. @model will show up
 * an item for the connection if there is not already one. This allows to
 * browse the explored parts of the directory of the remote site.
 *
 * @connection must be in %INF_XML_CONNECTION_OPEN or
 * %INF_XML_CONNECTION_OPENING status.
 **/
void
inf_gtk_browser_model_add_connection(InfGtkBrowserModel* model,
                                     InfXmlConnection* connection)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;

  g_return_if_fail(INF_GTK_IS_BROWSER_MODEL(model));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  item = inf_gtk_browser_model_find_item_by_connection(model, connection);

  if(item == NULL)
    inf_gtk_browser_model_add_item(model, NULL, NULL, connection);
}

/** inf_gtk_browser_model_resolve:
 *
 * @model: A #InfGtkBrowserModel.
 * @discovery: A #InfDiscovery added to @model.
 * @info: A #InfDiscoveryInfo discovered by @discovery.
 *
 * Resolves @info and adds the resulting connection to the model. If that
 * connection is already contained, the original (newly resolved) entry
 * is removed in favor of the existing entry whose browser might already
 * have explored (parts of) the server's directory. */
void
inf_gtk_browser_model_resolve(InfGtkBrowserModel* model,
                              InfDiscovery* discovery,
                              InfDiscoveryInfo* info)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;

  g_return_if_fail(INF_GTK_IS_BROWSER_MODEL(model));
  g_return_if_fail(INF_IS_DISCOVERY(discovery));
  g_return_if_fail(info != NULL);
  
  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  item = inf_gtk_browser_model_find_item_by_discovery_info(model, info);

  g_return_if_fail(item != NULL);

  /* TODO: Also allow resolve in error status to retry. Clear error before
   * retry though. */
  g_return_if_fail(item->status == INF_GTK_BROWSER_MODEL_DISCOVERED);

  item->status = INF_GTK_BROWSER_MODEL_RESOLVING;
  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = item;
  tree_iter.user_data2 = GUINT_TO_POINTER(0);
  tree_iter.user_data3 = NULL;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &tree_iter);
  gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, &tree_iter);
  gtk_tree_path_free(path);

  inf_discovery_resolve(
    discovery,
    info,
    inf_gtk_browser_model_resolv_complete_func,
    inf_gtk_browser_model_resolv_error_func,
    model
  );
}

/** inf_gtk_browser_model_browser_iter_to_tree_iter:
 *
 * @model: A #InfGtkBrowserModel.
 * @browser:  A #InfcBrowser.
 * @browser_iter: A #InfcBrowserIter pointing into @browser.
 * @tree_iter: A #GtkTreeIter that will be set by this function.
 *
 * Sets @tree_iter to point to the same node @browser_iter refers to
 * within the model. If @browser is not known to @model, i.e. its connection
 * was never added to @model, then the function returns %FALSE and
 * @tree_iter is left untouched.
 *
 * Return Value: Whether @tree_iter was set.
 **/
gboolean
inf_gtk_browser_model_browser_iter_to_tree_iter(InfGtkBrowserModel* model,
                                                InfcBrowser* browser,
                                                InfcBrowserIter* browser_iter,
                                                GtkTreeIter* tree_iter)
{
  InfGtkBrowserModelPrivate* priv;
  InfGtkBrowserModelItem* item;

  g_return_val_if_fail(INF_GTK_IS_BROWSER_MODEL(model), FALSE);
  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(browser_iter != NULL, FALSE);
  g_return_val_if_fail(tree_iter != NULL, FALSE);

  priv = INF_GTK_BROWSER_MODEL_PRIVATE(model);
  item = inf_gtk_browser_model_find_item_by_browser(model, browser);
  if(item == NULL) return FALSE;

  tree_iter->stamp = priv->stamp;
  tree_iter->user_data = item;
  tree_iter->user_data2 = GUINT_TO_POINTER(browser_iter->node_id);
  tree_iter->user_data3 = browser_iter->node;

  /* Root node */
  if(browser_iter->node_id == 0)
    tree_iter->user_data3 = NULL;

  return TRUE;
}

/* vim:set et sw=2 ts=2: */
