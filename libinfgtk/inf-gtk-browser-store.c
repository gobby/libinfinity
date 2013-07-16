/* libinfinity - a GObject-based infinote implementation
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
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <libinfgtk/inf-gtk-browser-store.h>
#include <libinfgtk/inf-gtk-browser-model.h>
#include <libinfinity/client/infc-browser.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <gtk/gtk.h>

/* The three pointers in GtkTreeIter are used as follows:
 *
 * user_data holds a pointer to the GtkTreeModelItem the iter points to.
 * user_data2 holds the node_id field of the InfBrowserIter, or 0 if the
 * iter points to the toplevel node.
 * user_data3 holds the node field of the InfcBrowser, or NULL if the iter
 * points to the toplevel node. Note that it does not hold the root node of
 * the item's browser (if present) because the iter should remain valid when
 * the browser is removed (we set GTK_TREE_MODEL_ITERS_PERSIST).
 */

typedef struct _InfGtkBrowserStoreItem InfGtkBrowserStoreItem;
struct _InfGtkBrowserStoreItem {
  gchar* name;
  InfDiscovery* discovery;
  InfDiscoveryInfo* info;

  InfBrowser* browser;

  /* Browser node which is currently to be removed. This is required since
   * when node-removed is emitted in InfcBrowser the node is still present,
   * but we need to make sure that the GtkTreeModel functions do as if it
   * wasn't present anymore. */
  gpointer missing;

  /* Running requests */
  GSList* requests;
  /* Saved node errors (during exploration/subscription) */
  GHashTable* node_errors;

  /* TODO: Determine status at run-time? */
  InfGtkBrowserModelStatus status;

  /* Error on toplevel item */
  GError* error;

  /* Link */
  InfGtkBrowserStoreItem* next;
};

typedef struct _InfGtkBrowserStoreRequestData InfGtkBrowserStoreRequestData;
struct _InfGtkBrowserStoreRequestData {
  InfGtkBrowserStore* store;
  InfGtkBrowserStoreItem* item;
};

typedef struct _InfGtkBrowserStorePrivate InfGtkBrowserStorePrivate;
struct _InfGtkBrowserStorePrivate {
  gint stamp;

  InfIo* io;
  InfCommunicationManager* communication_manager;

  GSList* discoveries;
  InfGtkBrowserStoreItem* first_item;
  InfGtkBrowserStoreItem* last_item;
};

enum {
  PROP_0,

  PROP_IO,
  PROP_COMMUNICATION_MANAGER
};

#define INF_GTK_BROWSER_STORE_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_BROWSER_STORE, InfGtkBrowserStorePrivate))

static GObjectClass* parent_class;

/*
 * Utility functions
 */

static InfGtkBrowserStoreItem*
inf_gtk_browser_store_find_item_by_connection(InfGtkBrowserStore* store,
                                              InfXmlConnection* connection)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfcBrowser* browser;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);

  for(item = priv->first_item; item != NULL; item = item->next)
  {
    if(item->browser != NULL)
    {
      browser = INFC_BROWSER(item->browser);
      if(infc_browser_get_connection(browser) == connection)
        return item;
    }
  }

  return NULL;
}

static InfGtkBrowserStoreItem*
inf_gtk_browser_store_find_item_by_browser(InfGtkBrowserStore* store,
                                           InfBrowser* browser)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);

  for(item = priv->first_item; item != NULL; item = item->next)
    if(item->browser != NULL)
      if(item->browser == browser)
        return item;

  return NULL;
}

static InfGtkBrowserStoreItem*
inf_gtk_browser_store_find_item_by_discovery_info(InfGtkBrowserStore* store,
                                                  InfDiscoveryInfo* info)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);

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
inf_gtk_browser_store_browser_notify_status_cb(GObject* object,
                                               GParamSpec* pspec,
                                               gpointer user_data);

static void
inf_gtk_browser_store_browser_error_cb(InfBrowser* browser,
                                       const GError* error,
                                       gpointer user_data);

static void
inf_gtk_browser_store_node_added_cb(InfBrowser* browser,
                                    InfBrowserIter* iter,
                                    gpointer user_data);

static void
inf_gtk_browser_store_node_removed_cb(InfBrowser* browser,
                                      InfBrowserIter* iter,
                                      gpointer user_data);

static void
inf_gtk_browser_store_begin_request_explore_node_cb(InfBrowser* browser,
                                                    InfBrowserIter* iter,
                                                    InfRequest* request,
                                                    gpointer user_data);

static void
inf_gtk_browser_store_begin_request_subscribe_session_cb(InfBrowser* browser,
                                                         InfBrowserIter* iter,
                                                         InfRequest* request,
                                                         gpointer user_data);

static void
inf_gtk_browser_store_request_finished_cb(InfNodeRequest* request,
                                          InfBrowserIter* iter,
                                          const GError* error,
                                          gpointer user_data);

static void
inf_gtk_browser_store_request_unrefed_func(gpointer data,
                                           GObject* where_the_object_was);

/*
 * InfGtkBrowserStoreItem handling
 */

static void
inf_gtk_browser_store_request_data_free(gpointer data,
                                        GClosure* closure)
{
  g_slice_free(InfGtkBrowserStoreRequestData, data);
}

static void
inf_gtk_browser_store_item_request_remove(InfGtkBrowserStoreItem* item,
                                          InfNodeRequest* request)
{
  g_object_weak_unref(
    G_OBJECT(request),
    inf_gtk_browser_store_request_unrefed_func,
    item
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(request),
    G_CALLBACK(inf_gtk_browser_store_request_finished_cb),
    item
  );

  item->requests = g_slist_remove(item->requests, request);
}

static void
inf_gtk_browser_store_item_request_add(InfGtkBrowserStore* store,
                                       InfGtkBrowserStoreItem* item,
                                       InfNodeRequest* request)
{
  InfGtkBrowserStoreRequestData* data;

  g_assert(g_slist_find(item->requests, request) == NULL);
  item->requests = g_slist_prepend(item->requests, request);

  data = g_slice_new(InfGtkBrowserStoreRequestData);
  data->store = store;
  data->item = item;

  g_signal_connect_data(
    G_OBJECT(request),
    "finished",
    G_CALLBACK(inf_gtk_browser_store_request_finished_cb),
    data,
    inf_gtk_browser_store_request_data_free,
    0
  );
  
  g_object_weak_ref(
    G_OBJECT(request),
    inf_gtk_browser_store_request_unrefed_func,
    item
  );
}

static void
inf_gtk_browser_store_request_finished_cb(InfNodeRequest* request,
                                          InfBrowserIter* iter,
                                          const GError* error,
                                          gpointer user_data)
{
  InfGtkBrowserStoreRequestData* data;
  InfGtkBrowserStorePrivate* priv;
  InfBrowserIter request_iter;
  gboolean node_exists;
  GtkTreeIter tree_iter;
  GtkTreePath* path;

  data = (InfGtkBrowserStoreRequestData*)user_data;
  priv = INF_GTK_BROWSER_STORE_PRIVATE(data->store);

  g_assert(g_slist_find(data->item->requests, request) != NULL);
  g_assert(data->item->browser != NULL);

  /* TODO: Also remove the request from the store when
   * it has properly finished? */
  if(error != NULL)
  {
    node_exists = inf_browser_iter_from_request(
      data->item->browser,
      request,
      &request_iter
    );

    inf_gtk_browser_store_item_request_remove(data->item, request);

    /* Ignore if node has been removed in the meanwhile */
    if(G_LIKELY(node_exists))
    {
      /* Replace previous error */
      g_hash_table_insert(
        data->item->node_errors,
        GUINT_TO_POINTER(request_iter.node_id),
        g_error_copy(error)
      );

      tree_iter.stamp = priv->stamp;
      tree_iter.user_data = data->item;
      tree_iter.user_data2 = GUINT_TO_POINTER(request_iter.node_id);

      /* Set NULL for root node because it also refers to the store item as
       * such if no browser is set. */
      if(request_iter.node_id == 0)
        tree_iter.user_data3 = NULL;
      else
        tree_iter.user_data3 = request_iter.node;

      path = gtk_tree_model_get_path(GTK_TREE_MODEL(data->store), &tree_iter);
      gtk_tree_model_row_changed(GTK_TREE_MODEL(data->store), path, &tree_iter);
      gtk_tree_path_free(path);
    }
  }
}

static void
inf_gtk_browser_store_request_unrefed_func(gpointer data,
                                           GObject* where_the_object_was)
{
  InfGtkBrowserStoreItem* item;
  item = (InfGtkBrowserStoreItem*)data;

  /* No need to further unregister */
  item->requests = g_slist_remove(item->requests, where_the_object_was);
}

static void
inf_gtk_browser_store_item_set_browser(InfGtkBrowserStore* store,
                                       InfGtkBrowserStoreItem* item,
                                       GtkTreePath* path,
                                       InfBrowser* browser)
{
  GtkTreeIter tree_iter;
  InfGtkBrowserStorePrivate* priv;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);
  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = item;
  tree_iter.user_data2 = GUINT_TO_POINTER(0);
  tree_iter.user_data3 = NULL;

  /* The default signal handler sets the browser in the item and makes the
   * necessary TreeModel notifications. See
   * inf_gtk_browser_store_browser_model_set_browser(). */
  inf_gtk_browser_model_set_browser(
    INF_GTK_BROWSER_MODEL(store),
    path,
    &tree_iter,
    browser
  );
}

/* takes ownership of name */
static InfGtkBrowserStoreItem*
inf_gtk_browser_store_add_item(InfGtkBrowserStore* store,
                               InfDiscovery* discovery,
                               InfDiscoveryInfo* info,
                               InfXmlConnection* connection,
                               gchar* name)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfGtkBrowserStoreItem* cur;
  GtkTreePath* path;
  GtkTreeIter iter;
  guint index;
  InfcBrowser* browser;

  g_assert(
    connection == NULL ||
    inf_gtk_browser_store_find_item_by_connection(store, connection) == NULL
  );

  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);
  item = g_slice_new(InfGtkBrowserStoreItem);
  item->name = name;
  item->discovery = discovery;
  item->info = info;
  item->status = INF_GTK_BROWSER_MODEL_DISCONNECTED;
  if(discovery != NULL && info != NULL)
    item->status = INF_GTK_BROWSER_MODEL_DISCOVERED;
  item->browser = NULL;
  item->missing = NULL;
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

  gtk_tree_model_row_inserted(GTK_TREE_MODEL(store), path, &iter);

  if(connection != NULL)
  {
    browser = infc_browser_new(
      priv->io,
      priv->communication_manager,
      connection
    );

    /* The connection is not set if the browser could not find a "central"
     * method for the connection's network. */
    /* TODO: Set error */
    if(infc_browser_get_connection(browser) != NULL)
    {
      inf_gtk_browser_store_item_set_browser(
        store,
        item,
        path,
        INF_BROWSER(browser)
      );
    }

    g_object_unref(G_OBJECT(browser));
  }

  gtk_tree_path_free(path);
  return item;
}

static void
inf_gtk_browser_store_remove_item(InfGtkBrowserStore* store,
                                  InfGtkBrowserStoreItem* item)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* prev;
  InfGtkBrowserStoreItem* cur;
  GtkTreePath* path;
  guint index;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);

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
   * inf_gtk_browser_store_item_set_browser() requires item still being
   * linked for change notifications. */
  if(item->browser != NULL)
  {
    inf_gtk_browser_store_item_set_browser(store, item, path, NULL);
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

  gtk_tree_model_row_deleted(GTK_TREE_MODEL(store), path);
  gtk_tree_path_free(path);

  if(item->error != NULL)
    g_error_free(item->error);

  g_hash_table_unref(item->node_errors);
  g_free(item->name);
  g_slice_free(InfGtkBrowserStoreItem, item);
}

/*
 * Callbacks and signal handlers
 */

static void
inf_gtk_browser_store_discovered_cb(InfDiscovery* discovery,
                                    InfDiscoveryInfo* info,
                                    gpointer user_data)
{
  inf_gtk_browser_store_add_item(
    INF_GTK_BROWSER_STORE(user_data),
    discovery,
    info,
    NULL,
    inf_discovery_info_get_service_name(discovery, info)
  );
}

static void
inf_gtk_browser_store_undiscovered_cb(InfDiscovery* discovery,
                                      InfDiscoveryInfo* info,
                                      gpointer user_data)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStoreItem* item;

  store = INF_GTK_BROWSER_STORE(user_data);
  item = inf_gtk_browser_store_find_item_by_discovery_info(store, info);

  /* If, after a discovery resove we see that there is already an item for
   * that connection, then we remove the discovery item, and use the other
   * item instead. In that case, item can be NULL here. */
  if(item != NULL)
  {
    if(item->browser == NULL)
    {
      inf_gtk_browser_store_remove_item(store, item);
    }
    else
    {
      g_assert(item->status != INF_GTK_BROWSER_MODEL_DISCOVERED &&
               item->status != INF_GTK_BROWSER_MODEL_RESOLVING);

      item->discovery = NULL;
      item->info = NULL;
    }
  }
}

static void
inf_gtk_browser_store_browser_error_cb(InfBrowser* browser,
                                       const GError* error,
                                       gpointer user_data)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStoreItem* item;
  InfGtkBrowserStorePrivate* priv;
  GtkTreeIter iter;
  GtkTreePath* path;

  store = INF_GTK_BROWSER_STORE(user_data);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);
  item = inf_gtk_browser_store_find_item_by_browser(store, browser);
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

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
  gtk_tree_model_row_changed(GTK_TREE_MODEL(store), path, &iter);
  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_store_browser_notify_status_cb(GObject* object,
                                               GParamSpec* pspec,
                                               gpointer user_data)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStorePrivate* priv;
  InfBrowser* browser;
  InfBrowserStatus status;
  InfGtkBrowserStoreItem* item;
  GtkTreeIter iter;
  GtkTreePath* path;

  store = INF_GTK_BROWSER_STORE(user_data);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(user_data);  
  browser = INF_BROWSER(object);
  item = inf_gtk_browser_store_find_item_by_browser(store, browser);

  g_assert(item != NULL);

  iter.stamp = priv->stamp;
  iter.user_data = item;
  iter.user_data2 = GUINT_TO_POINTER(0);
  iter.user_data3 = NULL;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
  g_object_get(G_OBJECT(browser), "status", &status, NULL);

  switch(status)
  {
  case INF_BROWSER_CLOSED:
    /* TODO: Do we want to go to disconnected state when error is not set? */
    item->status = INF_GTK_BROWSER_MODEL_ERROR;

    /* Set a "Disconnected" error if there is not already one set by
     * inf_gtk_browser_store_connection_error_cb() that has a more
     * meaningful error message. */
    if(item->error == NULL)
    {
      item->error = g_error_new_literal(
        g_quark_from_static_string("INF_GTK_BROWSER_STORE_ERROR"),
        0,
        _("Disconnected")
      );
    }

    gtk_tree_model_row_changed(GTK_TREE_MODEL(store), path, &iter);
    break;
  case INF_BROWSER_OPENING:
    if(item->error != NULL) g_error_free(item->error);
    item->error = NULL;
    item->status = INF_GTK_BROWSER_MODEL_CONNECTING;
    gtk_tree_model_row_changed(GTK_TREE_MODEL(store), path, &iter);
    break;
  case INF_BROWSER_OPEN:
    if(item->error != NULL) g_error_free(item->error);
    item->error = NULL;
    item->status = INF_GTK_BROWSER_MODEL_CONNECTED;
    gtk_tree_model_row_changed(GTK_TREE_MODEL(store), path, &iter);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_store_node_added_cb(InfBrowser* browser,
                                    InfBrowserIter* iter,
                                    gpointer user_data)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;

  InfBrowserIter test_iter;
  gboolean test_result;

  store = INF_GTK_BROWSER_STORE(user_data);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);
  item = inf_gtk_browser_store_find_item_by_browser(store, browser);

  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = item;
  tree_iter.user_data2 = GUINT_TO_POINTER(iter->node_id);
  tree_iter.user_data3 = iter->node;

  if(iter->node_id != 0)
  {
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &tree_iter);
    gtk_tree_model_row_inserted(GTK_TREE_MODEL(store), path, &tree_iter);

    /* If iter is the only node within its parent, we need to emit the
     * row-has-child-toggled signal. */
    test_iter = *iter;
    test_result = inf_browser_get_parent(browser, &test_iter);
    g_assert(test_result == TRUE);

    /* Let tree_iter point to parent row for possible notification */
    tree_iter.user_data2 = GUINT_TO_POINTER(test_iter.node_id);

    /* Also adjust path */
    gtk_tree_path_up(path);

    if(test_iter.node_id == 0)
      tree_iter.user_data3 = NULL;
    else
      tree_iter.user_data3 = test_iter.node;

    test_result = inf_browser_get_child(browser, &test_iter);
    g_assert(test_result == TRUE);

    if(inf_browser_get_next(browser, &test_iter) == FALSE)
    {
      gtk_tree_model_row_has_child_toggled(
        GTK_TREE_MODEL(store),
        path,
        &tree_iter
      );
    }

    gtk_tree_path_free(path);
  }
}

static void
inf_gtk_browser_store_node_removed_cb(InfBrowser* browser,
                                      InfBrowserIter* iter,
                                      gpointer user_data)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  InfBrowserIter test_iter;
  gboolean test_result;

  store = INF_GTK_BROWSER_STORE(user_data);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);
  item = inf_gtk_browser_store_find_item_by_browser(store, browser);

  g_assert(item->missing == NULL);

  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = item;
  tree_iter.user_data2 = GUINT_TO_POINTER(iter->node_id);
  tree_iter.user_data3 = iter->node;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &tree_iter);

  /* This is a small hack to have the item removed from the tree
   * model before it is removed from the InfcBrowser. */

  item->missing = iter->node;

  if(iter->node_id != 0)
  {
    gtk_tree_model_row_deleted(GTK_TREE_MODEL(store), path);

  /* TODO: Remove requests and node errors from nodes below the removed one */

    /* Note that at this point removed node is still in the browser. We have
     * to emit row-has-child-toggled if it is the only one in its
     * subdirectory. */
    test_iter = *iter;
    test_result = inf_browser_get_parent(browser, &test_iter);
    g_assert(test_result == TRUE);

    /* Let tree_iter point to parent row for possible notification */
    tree_iter.user_data2 = GUINT_TO_POINTER(test_iter.node_id);

    /* Also adjust path */
    gtk_tree_path_up(path);

    if(test_iter.node_id == 0)
      tree_iter.user_data3 = NULL;
    else
      tree_iter.user_data3 = test_iter.node;

    test_result = inf_browser_get_child(browser, &test_iter);
    g_assert(test_result == TRUE);

    if(inf_browser_get_next(browser, &test_iter) == FALSE)
    {
      gtk_tree_model_row_has_child_toggled(
        GTK_TREE_MODEL(store),
        path,
        &tree_iter
      );
    }
  }
  else
  {
    /* The root node was removed. We don't remove the node from the
     * GtkTreeModel because it still represents the InfBrowser. Remove
     * all the children, however. */
    if(inf_browser_get_explored(browser, iter))
    {
      test_iter = *iter;
      test_result = inf_browser_get_child(browser, &test_iter);
      gtk_tree_path_down(path);

      do
      {
        gtk_tree_model_row_deleted(GTK_TREE_MODEL(store), path);
      } while(inf_browser_get_next(browser, &test_iter));

      if(test_result == TRUE)
      {
        gtk_tree_path_up(path);
        gtk_tree_model_row_has_child_toggled(
          GTK_TREE_MODEL(store),
          path,
          &tree_iter
        );
      }
    }
  }

  item->missing = NULL;
  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_store_begin_request_explore_node_cb(InfBrowser* browser,
                                                    InfBrowserIter* iter,
                                                    InfRequest* request,
                                                    gpointer user_data)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStoreItem* item;

  store = INF_GTK_BROWSER_STORE(user_data);
  item = inf_gtk_browser_store_find_item_by_browser(store, browser);

  g_assert(INF_IS_NODE_REQUEST(request));

  inf_gtk_browser_store_item_request_add(
    store,
    item,
    INF_NODE_REQUEST(request)
  );
}

static void
inf_gtk_browser_store_begin_request_subscribe_session_cb(InfBrowser* browser,
                                                         InfBrowserIter* iter,
                                                         InfRequest* request,
                                                         gpointer user_data)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStoreItem* item;

  /* should not be a chat session, because chat session
   * has type subscribe-chat */
  g_assert(iter != NULL);
  g_assert(INF_IS_NODE_REQUEST(request));

  store = INF_GTK_BROWSER_STORE(user_data);
  item = inf_gtk_browser_store_find_item_by_browser(store, browser);

  inf_gtk_browser_store_item_request_add(
    store,
    item,
    INF_NODE_REQUEST(request)
  );
}

static void
inf_gtk_browser_store_resolv_complete_func(InfDiscoveryInfo* info,
                                           InfXmlConnection* connection,
                                           gpointer user_data)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* new_item;
  InfGtkBrowserStoreItem* old_item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  InfcBrowser* browser;

  InfGtkBrowserStoreItem* cur;
  InfGtkBrowserStoreItem* prev;
  InfGtkBrowserStoreItem* prev_new;
  InfGtkBrowserStoreItem* prev_old;
  gint* order;
  guint count;
  guint new_pos;
  guint old_pos;
  guint i;

  store = INF_GTK_BROWSER_STORE(user_data);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);
  new_item = inf_gtk_browser_store_find_item_by_discovery_info(store, info);
  old_item = inf_gtk_browser_store_find_item_by_connection(store, connection);

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

    inf_gtk_browser_store_remove_item(store, new_item);
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
      gtk_tree_model_rows_reordered(GTK_TREE_MODEL(store), path, NULL, order);
      gtk_tree_path_free(path);

      /* TODO: Perhaps we should emit a signal so that the view can
       * highlight and scroll to the existing item. And also so that it can
       * over initial root exploration. */

      g_free(order);
    }
  }
  else
  {
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &tree_iter);

    browser = infc_browser_new(
      priv->io,
      priv->communication_manager,
      connection
    );

    /* The connection is not set if the browser could not find a "central"
     * method for the connection's network. */
    /* TODO: Set error */
    if(infc_browser_get_connection(browser) != NULL)
    {
      inf_gtk_browser_store_item_set_browser(
        store,
        new_item,
        path,
        INF_BROWSER(browser)
      );
    }

    g_object_unref(G_OBJECT(browser));
    gtk_tree_path_free(path);
  }
}

static void
inf_gtk_browser_store_resolv_error_func(InfDiscoveryInfo* info,
                                        const GError* error,
                                        gpointer user_data)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;

  store = INF_GTK_BROWSER_STORE(user_data);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);
  item = inf_gtk_browser_store_find_item_by_discovery_info(store, info);

  g_assert(item != NULL);
  g_assert(item->status == INF_GTK_BROWSER_MODEL_RESOLVING);
  item->status = INF_GTK_BROWSER_MODEL_ERROR;
  item->error = g_error_copy(error);

  tree_iter.stamp = priv->stamp;
  tree_iter.user_data = item;
  tree_iter.user_data2 = GUINT_TO_POINTER(0);
  tree_iter.user_data3 = NULL;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &tree_iter);
  gtk_tree_model_row_changed(GTK_TREE_MODEL(store), path, &tree_iter);
  gtk_tree_path_free(path);
}

/*
 * GObject overrides
 */

static void
inf_gtk_browser_store_init(GTypeInstance* instance,
                           gpointer g_class)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStorePrivate* priv;

  store = INF_GTK_BROWSER_STORE(instance);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);

  priv->stamp = g_random_int();
  priv->io = NULL;
  priv->communication_manager = NULL;
  priv->discoveries = NULL;
  priv->first_item = NULL;
  priv->last_item = NULL;
}

static void
inf_gtk_browser_store_dispose(GObject* object)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStorePrivate* priv;
  GSList* item;

  store = INF_GTK_BROWSER_STORE(object);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);

  while(priv->first_item != NULL)
    inf_gtk_browser_store_remove_item(store, priv->first_item);
  g_assert(priv->last_item == NULL);

  for(item = priv->discoveries; item != NULL; item = g_slist_next(item))
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(item->data),
      G_CALLBACK(inf_gtk_browser_store_discovered_cb),
      store
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(item->data),
      G_CALLBACK(inf_gtk_browser_store_undiscovered_cb),
      store
    );

    g_object_unref(G_OBJECT(item->data));
  }

  g_slist_free(priv->discoveries);
  priv->discoveries = NULL;

  if(priv->communication_manager != NULL)
  {
    g_object_unref(priv->communication_manager);
    priv->communication_manager = NULL;
  }

  if(priv->io != NULL)
  {
    g_object_unref(G_OBJECT(priv->io));
    priv->io = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_gtk_browser_store_set_property(GObject* object,
                                   guint prop_id,
                                   const GValue* value,
                                   GParamSpec* pspec)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStorePrivate* priv;

  store = INF_GTK_BROWSER_STORE(object);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct only */
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_COMMUNICATION_MANAGER: 
    g_assert(priv->communication_manager == NULL); /* construct only */
    priv->communication_manager =
      INF_COMMUNICATION_MANAGER(g_value_dup_object(value));

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_gtk_browser_store_get_property(GObject* object,
                                   guint prop_id,
                                   GValue* value,
                                   GParamSpec* pspec)
{
  InfGtkBrowserStore* store;
  InfGtkBrowserStorePrivate* priv;

  store = INF_GTK_BROWSER_STORE(object);
  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, priv->io);
    break;
  case PROP_COMMUNICATION_MANAGER:
    g_value_set_object(value, priv->communication_manager);
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
inf_gtk_browser_store_tree_model_get_flags(GtkTreeModel* model)
{
  return GTK_TREE_MODEL_ITERS_PERSIST;
}

static gint
inf_gtk_browser_store_tree_model_get_n_columns(GtkTreeModel* model)
{
  return INF_GTK_BROWSER_MODEL_NUM_COLS;
}

static GType
inf_gtk_browser_store_tree_model_get_column_type(GtkTreeModel* model,
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
  case INF_GTK_BROWSER_MODEL_COL_NAME:
    return G_TYPE_STRING;
  case INF_GTK_BROWSER_MODEL_COL_ERROR:
    return G_TYPE_POINTER;
  case INF_GTK_BROWSER_MODEL_COL_NODE:
    return INF_TYPE_BROWSER_ITER;
  default:
    g_assert_not_reached();
    return G_TYPE_INVALID;
  }
}

static gboolean
inf_gtk_browser_store_tree_model_get_iter(GtkTreeModel* model,
                                          GtkTreeIter* iter,
                                          GtkTreePath* path)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfBrowserIter browser_iter;
  gint* indices;

  guint i;
  guint n;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  if(gtk_tree_path_get_depth(path) == 0) return FALSE;

  indices = gtk_tree_path_get_indices(path);
  n = indices[0];

  i = 0;
  for(item = priv->first_item; item != NULL && i < n; item = item->next)
    ++i;

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
  inf_browser_get_root(item->browser, &browser_iter);

  for(n = 1; n < (guint)gtk_tree_path_get_depth(path); ++ n)
  {
    if(inf_browser_get_explored(item->browser, &browser_iter) == FALSE)
      return FALSE;

    if(inf_browser_get_child(item->browser, &browser_iter) == FALSE)
      return FALSE;

    /* skip missing */
    if(browser_iter.node == item->missing)
      ++indices[n];

    for(i = 0; i < (guint)indices[n]; ++ i)
    {
      if(inf_browser_get_next(item->browser, &browser_iter) == FALSE)
        return FALSE;

      /* skip missing */
      if(browser_iter.node == item->missing)
        ++indices[n];
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
inf_gtk_browser_store_tree_model_get_path_impl(InfGtkBrowserStore* store,
                                               InfGtkBrowserStoreItem* item,
                                               InfBrowserIter* iter,
                                               GtkTreePath* path)
{
  InfGtkBrowserStorePrivate* priv;
  InfBrowserIter cur_iter;
  InfGtkBrowserStoreItem* cur;
  gboolean result;
  guint n;

  cur_iter = *iter;
  if(inf_browser_get_parent(item->browser, &cur_iter) == FALSE)
  {
    priv = INF_GTK_BROWSER_STORE_PRIVATE(store);

    /* We are on top level, but still need to find the item index */
    n = 0;
    for(cur = priv->first_item; cur != item; cur = cur->next)
      ++n;

    gtk_tree_path_append_index(path, n);
  }
  else
  {
    inf_gtk_browser_store_tree_model_get_path_impl(
      store,
      item,
      &cur_iter,
      path
    );

    result = inf_browser_get_child(item->browser, &cur_iter);
    g_assert(result == TRUE);

    /* skip missing */
    if(cur_iter.node == item->missing)
    {
      result = inf_browser_get_next(item->browser, &cur_iter);
      g_assert(result == TRUE);
    }

    n = 0;
    while(cur_iter.node_id != iter->node_id)
    {
      result = inf_browser_get_next(item->browser, &cur_iter);
      g_assert(result == TRUE);

      /* skip missing */
      if(cur_iter.node == item->missing)
      {
        result = inf_browser_get_next(item->browser, &cur_iter);
        g_assert(result == TRUE);
      }

      ++n;
    }

    gtk_tree_path_append_index(path, n);
  }
}

static GtkTreePath*
inf_gtk_browser_store_tree_model_get_path(GtkTreeModel* model,
                                          GtkTreeIter* iter)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfGtkBrowserStoreItem* cur;
  GtkTreePath* path;
  InfBrowserIter browser_iter;
  guint n;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  g_assert(iter->stamp == priv->stamp);
  g_assert(iter->user_data != NULL);

  item = (InfGtkBrowserStoreItem*)iter->user_data;

  path = gtk_tree_path_new();
  browser_iter.node_id = GPOINTER_TO_UINT(iter->user_data2);
  browser_iter.node = iter->user_data3;

  if(browser_iter.node != NULL)
  {
    g_assert(item->browser != NULL);

    inf_gtk_browser_store_tree_model_get_path_impl(
      INF_GTK_BROWSER_STORE(model),
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
      ++n;

    gtk_tree_path_append_index(path, n);
  }

  return path;
}

static void
inf_gtk_browser_store_tree_model_get_value(GtkTreeModel* model,
                                           GtkTreeIter* iter,
                                           gint column,
                                           GValue* value)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfBrowserIter browser_iter;
  InfBrowserStatus browser_status;
  GError* error;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  g_assert(iter->stamp == priv->stamp);

  item = (InfGtkBrowserStoreItem*)iter->user_data;
  browser_iter.node_id = GPOINTER_TO_UINT(iter->user_data2);
  browser_iter.node = iter->user_data3;
  g_assert(item->missing == NULL || browser_iter.node != item->missing);

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
  case INF_GTK_BROWSER_MODEL_COL_NAME:
    g_value_init(value, G_TYPE_STRING);
    if(browser_iter.node == NULL)
    {
      g_value_set_string(value, item->name);
    }
    else
    {
      g_value_set_string(
        value,
        inf_browser_get_node_name(INF_BROWSER(item->browser), &browser_iter)
      );
    }
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
        g_object_get(
          G_OBJECT(item->browser),
          "status",
          &browser_status,
          NULL
        );

        if(browser_status == INF_BROWSER_OPEN)
        {
          inf_browser_get_root(item->browser, &browser_iter);
          error = g_hash_table_lookup(
            item->node_errors,
            GUINT_TO_POINTER(browser_iter.node_id)
          );
        }
        else
        {
          /* Browser exists but is closed, but there is no connection error
           * set: no error */
          error = NULL;
        }
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

      g_object_get(G_OBJECT(item->browser), "status", &browser_status, NULL);
      g_assert(browser_status == INF_BROWSER_OPEN);

      error = g_hash_table_lookup(
        item->node_errors,
        GUINT_TO_POINTER(browser_iter.node_id)
      );
    }

    g_value_init(value, G_TYPE_POINTER);
    g_value_set_pointer(value, error);
    break;
  case INF_GTK_BROWSER_MODEL_COL_NODE:
    g_assert(item->browser != NULL);

    g_object_get(G_OBJECT(item->browser), "status", &browser_status, NULL);
    g_assert(browser_status == INF_BROWSER_OPEN);

    if(browser_iter.node == NULL)
      inf_browser_get_root(item->browser, &browser_iter);

    g_value_init(value, INF_TYPE_BROWSER_ITER);
    g_value_set_boxed(value, &browser_iter);
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static gboolean
inf_gtk_browser_store_tree_model_iter_next(GtkTreeModel* model,
                                           GtkTreeIter* iter)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfBrowserIter browser_iter;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  g_assert(iter->stamp == priv->stamp);

  item = (InfGtkBrowserStoreItem*)iter->user_data;
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
    g_assert(browser_iter.node != item->missing);

    if(inf_browser_get_next(item->browser, &browser_iter) == FALSE)
      return FALSE;

    /* skip missing */
    if(browser_iter.node == item->missing)
      if(inf_browser_get_next(item->browser, &browser_iter) == FALSE)
        return FALSE;

    iter->user_data2 = GUINT_TO_POINTER(browser_iter.node_id);
    iter->user_data3 = browser_iter.node;
    return TRUE;
  }
}

static gboolean
inf_gtk_browser_store_tree_model_iter_children(GtkTreeModel* model,
                                               GtkTreeIter* iter,
                                               GtkTreeIter* parent)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfBrowserStatus browser_status;
  InfBrowserIter browser_iter;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);

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
    g_assert(parent->stamp == priv->stamp);

    item = (InfGtkBrowserStoreItem*)parent->user_data;
    if(item->browser == NULL)
      return FALSE;

    g_object_get(G_OBJECT(item->browser), "status", &browser_status, NULL);
    if(browser_status != INF_BROWSER_OPEN)
      return FALSE;

    browser_iter.node_id = GPOINTER_TO_UINT(parent->user_data2);
    if(browser_iter.node_id == 0)
      inf_browser_get_root(item->browser, &browser_iter);
    else
      browser_iter.node = parent->user_data3;

    g_assert(item->missing == NULL || browser_iter.node != item->missing);

    if(!inf_browser_is_subdirectory(item->browser, &browser_iter))
      return FALSE;

    if(!inf_browser_get_explored(item->browser, &browser_iter))
      return FALSE;

    if(!inf_browser_get_child(item->browser, &browser_iter))
      return FALSE;

    if(browser_iter.node == item->missing)
      if(!inf_browser_get_next(item->browser, &browser_iter))
        return FALSE;

    iter->stamp = priv->stamp;
    iter->user_data = item;
    iter->user_data2 = GUINT_TO_POINTER(browser_iter.node_id);
    iter->user_data3 = browser_iter.node;
    return TRUE;
  }
}

static gboolean
inf_gtk_browser_store_tree_model_iter_has_child(GtkTreeModel* model,
                                                GtkTreeIter* iter)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfBrowserIter browser_iter;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  g_assert(iter->stamp == priv->stamp);

  item = (InfGtkBrowserStoreItem*)iter->user_data;
  if(item->browser == NULL) return FALSE;

  browser_iter.node_id = GPOINTER_TO_UINT(iter->user_data2);
  browser_iter.node = iter->user_data3;
  g_assert(item->missing == NULL || browser_iter.node != item->missing);

  if(browser_iter.node == NULL)
    inf_browser_get_root(item->browser, &browser_iter);

  if(inf_browser_is_subdirectory(item->browser, &browser_iter) == FALSE)
    return FALSE;

  if(inf_browser_get_explored(item->browser, &browser_iter) == FALSE)
    return FALSE;

  if(!inf_browser_get_child(item->browser, &browser_iter))
    return FALSE;

  if(browser_iter.node == item->missing)
    if(!inf_browser_get_next(item->browser, &browser_iter))
      return FALSE;

  return TRUE;
}

static gint
inf_gtk_browser_store_tree_model_iter_n_children(GtkTreeModel* model,
                                                 GtkTreeIter* iter)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfGtkBrowserStoreItem* cur;
  InfBrowserIter browser_iter;
  gboolean result;
  guint n;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  g_assert(iter == NULL || iter->stamp == priv->stamp);

  if(iter == NULL)
  {
    n = 0;
    for(cur = priv->first_item; cur != NULL; cur = cur->next)
      ++n;

    return n;
  }
  else
  {
    item = (InfGtkBrowserStoreItem*)iter->user_data;
    browser_iter.node_id = GPOINTER_TO_UINT(iter->user_data2);
    browser_iter.node = iter->user_data3;
    g_assert(item->missing == NULL || browser_iter.node != item->missing);

    if(browser_iter.node == NULL)
      inf_browser_get_root(item->browser, &browser_iter);

    if(inf_browser_get_explored(item->browser, &browser_iter) == FALSE)
      return 0;

    n = 0;
    for(result = inf_browser_get_child(item->browser, &browser_iter);
        result == TRUE;
        result = inf_browser_get_next(item->browser, &browser_iter))
    {
      if(browser_iter.node != item->missing)
        ++n;
    }

    return n;
  }
}

static gboolean
inf_gtk_browser_store_tree_model_iter_nth_child(GtkTreeModel* model,
                                                GtkTreeIter* iter,
                                                GtkTreeIter* parent,
                                                gint n)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfGtkBrowserStoreItem* cur;
  InfBrowserIter browser_iter;
  guint i;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);

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

    item = (InfGtkBrowserStoreItem*)parent->user_data;
    browser_iter.node_id = GPOINTER_TO_UINT(parent->user_data2);

    if(browser_iter.node_id == 0)
      inf_browser_get_root(item->browser, &browser_iter);
    else
      browser_iter.node = parent->user_data3;
    g_assert(item->missing == NULL || browser_iter.node != item->missing);

    if(inf_browser_get_explored(item->browser, &browser_iter) == FALSE)
      return FALSE;

    if(inf_browser_get_child(item->browser, &browser_iter) == FALSE)
      return FALSE;

    /* skip missing */
    if(browser_iter.node == item->missing)
      ++n;

    for(i = 0; i < (guint)n; ++ i)
    {
      if(inf_browser_get_next(item->browser, &browser_iter) == FALSE)
        return FALSE;

      if(browser_iter.node == item->missing)
        ++n;
    }

    iter->stamp = priv->stamp;
    iter->user_data = item;
    iter->user_data2 = GUINT_TO_POINTER(browser_iter.node_id);
    iter->user_data3 = browser_iter.node;
    return TRUE;
  }
}

static gboolean
inf_gtk_browser_store_tree_model_iter_parent(GtkTreeModel* model,
                                             GtkTreeIter* iter,
                                             GtkTreeIter* child)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfBrowserIter browser_iter;
  gboolean result;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  g_assert(child->stamp == priv->stamp);

  item = (InfGtkBrowserStoreItem*)child->user_data;
  browser_iter.node_id = GPOINTER_TO_UINT(child->user_data2);
  browser_iter.node = child->user_data3;

  if(browser_iter.node == NULL)
    return FALSE;

  g_assert(browser_iter.node != item->missing);

  result = inf_browser_get_parent(item->browser, &browser_iter);
  g_assert(result == TRUE);
  g_assert(browser_iter.node != item->missing);

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
 * InfGtkBrowserModel implementation.
 */

static void
inf_gtk_browser_store_browser_model_set_browser(InfGtkBrowserModel* model,
                                                GtkTreePath* path,
                                                GtkTreeIter* tree_iter,
                                                InfBrowser* browser)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;

  InfBrowserIter iter;
  guint n;
  gboolean had_children;
  InfBrowserStatus status;

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  had_children = FALSE;

  item = (InfGtkBrowserStoreItem*)tree_iter->user_data;
  /* cannot set browser in non-toplevel entries */
  g_assert(tree_iter->user_data3 == NULL);

  if(item->browser != NULL)
  {
    g_object_get(G_OBJECT(item->browser), "status", &status, NULL);
    if(status == INF_BROWSER_OPEN)
    {
      /* Notify about deleted rows. Notify in reverse order so that indexing
       * continues to work. Remember whether we had children to emit
       * row-has-child-toggled later. */
      inf_browser_get_root(item->browser, &iter);
      if(inf_browser_get_explored(item->browser, &iter) &&
         inf_browser_get_child(item->browser, &iter))
      {
        n = 1;
        while(inf_browser_get_next(item->browser, &iter))
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
    }

    while(item->requests != NULL)
    {
      inf_gtk_browser_store_item_request_remove(
        item,
        INF_NODE_REQUEST(item->requests->data)
      );
    }

    g_hash_table_remove_all(item->node_errors);

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_store_browser_error_cb),
      model
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_store_browser_notify_status_cb),
      model
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_store_node_added_cb),
      model
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_store_node_removed_cb),
      model
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_store_begin_request_explore_node_cb),
      model
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(item->browser),
      G_CALLBACK(inf_gtk_browser_store_begin_request_subscribe_session_cb),
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

  /* Set up new browser */
  item->browser = browser;

  if(browser != NULL)
  {
    g_object_ref(browser);

    g_signal_connect(
      G_OBJECT(item->browser),
      "error",
      G_CALLBACK(inf_gtk_browser_store_browser_error_cb),
      model
    );
      
    g_signal_connect(
      G_OBJECT(item->browser),
      "notify::status",
      G_CALLBACK(inf_gtk_browser_store_browser_notify_status_cb),
      model
    );

    g_signal_connect_after(
      G_OBJECT(item->browser),
      "node-added",
      G_CALLBACK(inf_gtk_browser_store_node_added_cb),
      model
    );

    g_signal_connect_after(
      G_OBJECT(item->browser),
      "node-removed",
      G_CALLBACK(inf_gtk_browser_store_node_removed_cb),
      model
    );

    g_signal_connect_after(
      G_OBJECT(item->browser),
      "begin-request::explore-node",
      G_CALLBACK(inf_gtk_browser_store_begin_request_explore_node_cb),
      model
    );

    g_signal_connect_after(
      G_OBJECT(item->browser),
      "begin-request::subscribe-session",
      G_CALLBACK(inf_gtk_browser_store_begin_request_subscribe_session_cb),
      model
    );

    /* TODO: Walk browser for requests */
  }

  /* Set status to invalid if there aren't any connection information anymore.
   * Keep the item if an error is set, so it can be displayed. */
  if(item->browser == NULL && item->info == NULL && item->error == NULL)
  {
    item->status = INF_GTK_BROWSER_MODEL_INVALID;
    /* TODO: What's up with this? Can this happen? Can we assert() here? */
  }
  else if(item->status != INF_GTK_BROWSER_MODEL_ERROR)
  {
    /* Set item status according to browser status if there is no
     * error set. */
    if(item->browser != NULL)
    {
      g_object_get(G_OBJECT(item->browser), "status", &status, NULL);
      switch(status)
      {
      case INF_BROWSER_CLOSED:
        item->status = INF_GTK_BROWSER_MODEL_DISCONNECTED;
        break;
      case INF_BROWSER_OPENING:
        item->status = INF_GTK_BROWSER_MODEL_CONNECTING;
        break;
      case INF_BROWSER_OPEN:
        item->status = INF_GTK_BROWSER_MODEL_CONNECTED;
        break;
      default:
        g_assert_not_reached();
        break;
      }
    }
    else
    {
      /* No browser available. Discovery needs to be set now, otherwise
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

  /* TODO: Emit row_inserted for the whole tree in browser, and
   * row-has-child-toggled where appropriate. */
  gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, tree_iter);
}

static void
inf_gtk_browser_store_browser_model_resolve(InfGtkBrowserModel* model,
                                            InfDiscovery* discovery,
                                            InfDiscoveryInfo* info)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  GtkTreeIter tree_iter;
  GtkTreePath* path;

  g_assert(INF_GTK_IS_BROWSER_STORE(model));

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  item = inf_gtk_browser_store_find_item_by_discovery_info(
    INF_GTK_BROWSER_STORE(model),
    info
  );

  g_assert(item != NULL);
  g_assert(
    item->status == INF_GTK_BROWSER_MODEL_DISCOVERED ||
    item->status == INF_GTK_BROWSER_MODEL_ERROR
  );

  if(item->status == INF_GTK_BROWSER_MODEL_ERROR)
  {
    g_assert(item->error != NULL);
    g_error_free(item->error);
    item->error = NULL;

    item->status = INF_GTK_BROWSER_MODEL_RESOLVING;
  }
  else
  {
    item->status = INF_GTK_BROWSER_MODEL_RESOLVING;
  }

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
    inf_gtk_browser_store_resolv_complete_func,
    inf_gtk_browser_store_resolv_error_func,
    model
  );
}

static gboolean
inf_gtk_browser_store_browser_iter_to_tree_iter(InfGtkBrowserModel* model,
                                                InfBrowser* browser,
                                                const InfBrowserIter* iter,
                                                GtkTreeIter* tree_iter)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;

  g_assert(INF_GTK_IS_BROWSER_STORE(model));

  priv = INF_GTK_BROWSER_STORE_PRIVATE(model);
  item = inf_gtk_browser_store_find_item_by_browser(
    INF_GTK_BROWSER_STORE(model),
    browser
  );
  if(item == NULL || item->missing == iter->node) return FALSE;

  tree_iter->stamp = priv->stamp;
  tree_iter->user_data = item;
  tree_iter->user_data2 = GUINT_TO_POINTER(iter->node_id);
  tree_iter->user_data3 = iter->node;

  /* Root node */
  if(iter->node_id == 0)
    tree_iter->user_data3 = NULL;

  return TRUE;
}

/*
 * GType registration
 */

static void
inf_gtk_browser_store_class_init(gpointer g_class,
                                 gpointer class_data)
{
  GObjectClass* object_class;
  InfGtkBrowserStoreClass* browser_store_class;

  object_class = G_OBJECT_CLASS(g_class);
  browser_store_class = INF_GTK_BROWSER_STORE_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkBrowserStorePrivate));

  object_class->dispose = inf_gtk_browser_store_dispose;
  object_class->set_property = inf_gtk_browser_store_set_property;
  object_class->get_property = inf_gtk_browser_store_get_property;

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
    PROP_COMMUNICATION_MANAGER,
    g_param_spec_object(
      "communication-manager",
      "Communication manager", 
      "The communication manager used for browsing remote directories",
      INF_COMMUNICATION_TYPE_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
inf_gtk_browser_store_tree_model_init(gpointer g_iface,
                                      gpointer iface_data)
{
  GtkTreeModelIface* iface;
  iface = (GtkTreeModelIface*)g_iface;

  iface->get_flags = inf_gtk_browser_store_tree_model_get_flags;
  iface->get_n_columns = inf_gtk_browser_store_tree_model_get_n_columns;
  iface->get_column_type = inf_gtk_browser_store_tree_model_get_column_type;
  iface->get_iter = inf_gtk_browser_store_tree_model_get_iter;
  iface->get_path = inf_gtk_browser_store_tree_model_get_path;
  iface->get_value = inf_gtk_browser_store_tree_model_get_value;
  iface->iter_next = inf_gtk_browser_store_tree_model_iter_next;
  iface->iter_children = inf_gtk_browser_store_tree_model_iter_children;
  iface->iter_has_child = inf_gtk_browser_store_tree_model_iter_has_child;
  iface->iter_n_children = inf_gtk_browser_store_tree_model_iter_n_children;
  iface->iter_nth_child = inf_gtk_browser_store_tree_model_iter_nth_child;
  iface->iter_parent = inf_gtk_browser_store_tree_model_iter_parent;
}

static void
inf_gtk_browser_store_browser_model_init(gpointer g_iface,
                                         gpointer iface_data)
{
  InfGtkBrowserModelIface* iface;
  iface = (InfGtkBrowserModelIface*)g_iface;

  iface->set_browser = inf_gtk_browser_store_browser_model_set_browser;
  iface->resolve = inf_gtk_browser_store_browser_model_resolve;
  /* inf_gtk_browser_store_browser_model_browser_iter_to_tree_iter would be
   * consistent, but a _bit_ too long to fit properly into 80 chars ;) */
  iface->browser_iter_to_tree_iter =
    inf_gtk_browser_store_browser_iter_to_tree_iter;
}

GType
inf_gtk_browser_store_get_type(void)
{
  static GType browser_store_type = 0;

  if(!browser_store_type)
  {
    static const GTypeInfo browser_store_type_info = {
      sizeof(InfGtkBrowserStoreClass),    /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      inf_gtk_browser_store_class_init,   /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof(InfGtkBrowserStore),         /* instance_size */
      0,                                  /* n_preallocs */
      inf_gtk_browser_store_init,         /* instance_init */
      NULL                                /* value_table */
    };

    static const GInterfaceInfo tree_model_info = {
      inf_gtk_browser_store_tree_model_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo browser_model_info = {
      inf_gtk_browser_store_browser_model_init,
      NULL,
      NULL
    };

    browser_store_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfGtkBrowserStore",
      &browser_store_type_info,
      0
    );

    g_type_add_interface_static(
      browser_store_type,
      GTK_TYPE_TREE_MODEL,
      &tree_model_info
    );

    g_type_add_interface_static(
      browser_store_type,
      INF_GTK_TYPE_BROWSER_MODEL,
      &browser_model_info
    );
  }

  return browser_store_type;
}

/*
 * Public API.
 */

/**
 * inf_gtk_browser_store_new:
 * @io: A #InfIo object for the created #InfcBrowser to schedule timeouts.
 * @comm_manager: The #InfCommunicationManager with which to explore
 * remote directories.
 *
 * Creates a new #InfGtkBrowserStore.
 *
 * Return Value: A new #InfGtkBrowserStore.
 **/
InfGtkBrowserStore*
inf_gtk_browser_store_new(InfIo* io,
                          InfCommunicationManager* comm_manager)
{
  GObject* object;

  object = g_object_new(
    INF_GTK_TYPE_BROWSER_STORE,
    "io", io,
    "communication-manager", comm_manager,
    NULL
  );

  return INF_GTK_BROWSER_STORE(object);
}

/**
 * inf_gtk_browser_store_add_discovery:
 * @store: A #InfGtkBrowserStore.
 * @discovery: A #InfDiscovery not yet added to @model.
 *
 * Adds @discovery to @model. The model will then show up discovered
 * servers.
 **/
void
inf_gtk_browser_store_add_discovery(InfGtkBrowserStore* store,
                                    InfDiscovery* discovery)
{
  InfGtkBrowserStorePrivate* priv;
  GSList* discovered;
  GSList* item;
  InfDiscoveryInfo* info;

  g_return_if_fail(INF_GTK_IS_BROWSER_STORE(store));
  g_return_if_fail(INF_IS_DISCOVERY(discovery));

  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);
  g_return_if_fail(g_slist_find(priv->discoveries, discovery) == NULL);

  g_object_ref(G_OBJECT(discovery));
  priv->discoveries = g_slist_prepend(priv->discoveries, discovery);

  g_signal_connect(
    G_OBJECT(discovery),
    "discovered",
    G_CALLBACK(inf_gtk_browser_store_discovered_cb),
    store
  );

  g_signal_connect(
    G_OBJECT(discovery),
    "undiscovered",
    G_CALLBACK(inf_gtk_browser_store_undiscovered_cb),
    store
  );

  discovered = inf_discovery_get_discovered(discovery, "_infinote._tcp");
  for(item = discovered; item != NULL; item = g_slist_next(item))
  {
    info = (InfDiscoveryInfo*)item->data;

    inf_gtk_browser_store_add_item(
      store,
      discovery,
      info,
      NULL,
      inf_discovery_info_get_service_name(discovery, info)
    );
  }
  g_slist_free(discovered);

  inf_discovery_discover(discovery, "_infinote._tcp");
}

/**
 * inf_gtk_browser_store_add_connection:
 * @store: A #InfGtkBrowserStore.
 * @connection: A #InfXmlConnection.
 * @name: Name for the item, or %NULL.
 *
 * This function adds a connection to the @store. @store will show up
 * an item for the connection if there is not already one. This allows to
 * browse the explored parts of the directory of the remote site. If @name
 * is %NULL, then the #InfXmlConnection:remote-id of the connection will be
 * used.
 **/
void
inf_gtk_browser_store_add_connection(InfGtkBrowserStore* store,
                                     InfXmlConnection* connection,
                                     const gchar* name)
{
  InfGtkBrowserStorePrivate* priv;
  InfGtkBrowserStoreItem* item;
  InfXmlConnectionStatus status;
  gchar* remote_id;

  g_return_if_fail(INF_GTK_IS_BROWSER_STORE(store));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  priv = INF_GTK_BROWSER_STORE_PRIVATE(store);
  item = inf_gtk_browser_store_find_item_by_connection(store, connection);

  if(item == NULL)
  {
    if(name == NULL)
    {
      g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);
      inf_gtk_browser_store_add_item(
        store,
        NULL,
        NULL,
        connection,
        remote_id
      );
    }
    else
    {
      inf_gtk_browser_store_add_item(
        store,
        NULL,
        NULL,
        connection,
        g_strdup(name)
      );
    }
  }
}

/**
 * inf_gtk_browser_store_remove_connection:
 * @store: A #InfGtkBrowserStore.
 * @connection: A #InfXmlConnection contained in @store.
 *
 * This function removes the entry for the given connection from @store.
 */
void
inf_gtk_browser_store_remove_connection(InfGtkBrowserStore* store,
                                        InfXmlConnection* connection)
{
  InfGtkBrowserStoreItem* item;

  g_return_if_fail(INF_GTK_IS_BROWSER_STORE(store));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  item = inf_gtk_browser_store_find_item_by_connection(store, connection);
  g_return_if_fail(item != NULL);

  inf_gtk_browser_store_remove_item (store, item);
}

/**
 * inf_gtk_browser_store_clear_connection_error:
 * @store: A #InfGtkBrowserStore.
 * @connection: A #InfXmlConnection contained in @store.
 *
 * This function clears the error for the entry which belongs to the given
 * connection.
 */
void
inf_gtk_browser_store_clear_connection_error(InfGtkBrowserStore* store,
                                             InfXmlConnection* connection)
{
  InfGtkBrowserStoreItem* item;
  InfBrowserStatus status;
  GtkTreeIter iter;
  GtkTreePath* path;

  g_return_if_fail(INF_GTK_IS_BROWSER_STORE(store));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  item = inf_gtk_browser_store_find_item_by_connection(store, connection);
  g_return_if_fail(item != NULL);

  if(item->status == INF_GTK_BROWSER_MODEL_ERROR)
  {
    g_assert(item->error != NULL);

    g_error_free(item->error);
    item->error = NULL;

    if(item->browser != NULL)
    {
      g_object_get(G_OBJECT(item->browser), "status", &status, NULL);
      switch(status)
      {
      case INF_BROWSER_CLOSED:
        item->status = INF_GTK_BROWSER_MODEL_DISCONNECTED;
        break;
      case INF_BROWSER_OPENING:
        item->status = INF_GTK_BROWSER_MODEL_CONNECTING;
        break;
      case INF_BROWSER_OPEN:
        item->status = INF_GTK_BROWSER_MODEL_CONNECTED;
        break;
      default:
        g_assert_not_reached();
        break;
      }
    }
    else if(item->info != NULL)
    {
      item->status = INF_GTK_BROWSER_MODEL_DISCOVERED;
    }
    else
    {
      g_assert_not_reached();
    }

    iter.stamp = INF_GTK_BROWSER_STORE_PRIVATE(store)->stamp;
    iter.user_data = item;
    iter.user_data2 = GUINT_TO_POINTER(0);
    iter.user_data3 = NULL;

    path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
    gtk_tree_model_row_changed(GTK_TREE_MODEL(store), path, &iter);
    gtk_tree_path_free (path);
  }
}

/**
 * inf_gtk_browser_store_set_connection_name:
 * @store: A #InfGtkBrowserStore.
 * @connection: A #InfXmlConnection contained in @store.
 * @name: The new name to set for the connection.
 *
 * This function sets the name of @connection.
 **/
void
inf_gtk_browser_store_set_connection_name(InfGtkBrowserStore* store,
                                          InfXmlConnection* connection,
                                          const gchar* name)
{
  InfGtkBrowserStoreItem* item;
  GtkTreeIter iter;
  GtkTreePath* path;

  g_return_if_fail(INF_GTK_IS_BROWSER_STORE(store));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(name != NULL);

  item = inf_gtk_browser_store_find_item_by_connection(store, connection);
  g_return_if_fail(item != NULL);

  g_free (item->name);
  item->name = g_strdup(name);

  iter.stamp = INF_GTK_BROWSER_STORE_PRIVATE(store)->stamp;
  iter.user_data = item;
  iter.user_data2 = GUINT_TO_POINTER(0);
  iter.user_data3 = NULL;

  path = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
  gtk_tree_model_row_changed(GTK_TREE_MODEL(store), path, &iter);
  gtk_tree_path_free(path);
}

/* vim:set et sw=2 ts=2: */
