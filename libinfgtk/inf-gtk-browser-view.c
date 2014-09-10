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

#include <libinfgtk/inf-gtk-browser-view.h>
#include <libinfinity/common/inf-discovery.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#define INF_GTK_BROWSER_VIEW_INITIAL_EXPANSION \
  "inf-gtk-browser-view-initial-exploration"

#define INF_GTK_BROWSER_VIEW_ERROR_COLOR "#db1515"

typedef struct _InfGtkBrowserViewBrowser InfGtkBrowserViewBrowser;
struct _InfGtkBrowserViewBrowser {
  InfGtkBrowserView* view;
  InfBrowser* browser;
  GtkTreeRowReference* reference;

  GSList* explores;
  GSList* syncs;

  /* Whether we expand the root node automatically */
  gboolean initial_root_expansion;
};

typedef struct _InfGtkBrowserViewExplore InfGtkBrowserViewExplore;
struct _InfGtkBrowserViewExplore {
  InfGtkBrowserViewBrowser* view_browser;
  GtkTreeRowReference* reference;

  InfRequest* request;
};

typedef struct _InfGtkBrowserViewSync InfGtkBrowserViewSync;
struct _InfGtkBrowserViewSync {
  InfGtkBrowserViewBrowser* view_browser;
  GtkTreeRowReference* reference;

  InfSessionProxy* proxy;
};

typedef struct _InfGtkBrowserViewPrivate InfGtkBrowserViewPrivate;
struct _InfGtkBrowserViewPrivate {
  GtkTreeViewColumn* column;

  /* Note that progress and status_text are never visible at the same time */
  GtkCellRenderer* renderer_icon;
  GtkCellRenderer* renderer_name;
  GtkCellRenderer* renderer_progress;
  GtkCellRenderer* renderer_status;

  /* This is just bookkeeping because we connect to their signals, and need
   * to disconnect when the view is disposed, or a browser removed. */
  GSList* browsers;

  /* Pending discovery info resolves */
  GSList* info_resolvs;
};

enum {
  PROP_0,

  PROP_MODEL
};

enum {
  ACTIVATE,
  SELECTION_CHANGED,
  POPULATE_POPUP,

  LAST_SIGNAL
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
 * browsers from our array. The same holds for explore requests and
 * session synchronizations. */

static guint view_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(InfGtkBrowserView, inf_gtk_browser_view, GTK_TYPE_TREE_VIEW,
  G_ADD_PRIVATE(InfGtkBrowserView))

/*
 * Utility functions
 */

static InfGtkBrowserViewBrowser*
inf_gtk_browser_view_find_view_browser(InfGtkBrowserView* view,
                                       InfBrowser* browser)
{
  InfGtkBrowserViewPrivate* priv;
  GSList* item;
  InfGtkBrowserViewBrowser* view_browser;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  for(item = priv->browsers; item != NULL; item = item->next)
  {
    view_browser = (InfGtkBrowserViewBrowser*)item->data;
    if(view_browser->browser == browser)
      return view_browser;
  }

  return NULL;
}

static InfGtkBrowserViewExplore*
inf_gtk_browser_view_find_explore(InfGtkBrowserView* view,
                                  InfGtkBrowserViewBrowser* view_browser,
                                  InfRequest* request)
{
  GSList* item;
  InfGtkBrowserViewExplore* explore;

  for(item = view_browser->explores; item != NULL; item = item->next)
  {
    explore = (InfGtkBrowserViewExplore*)item->data;
    if(explore->request == request)
      return explore;
  }

  return NULL;
}

static InfGtkBrowserViewSync*
inf_gtk_browser_view_find_sync(InfGtkBrowserView* view,
                               InfGtkBrowserViewBrowser* view_browser,
                               InfSessionProxy* proxy)
{
  GSList* item;
  InfGtkBrowserViewSync* sync;

  for(item = view_browser->syncs; item != NULL; item = item->next)
  {
    sync = (InfGtkBrowserViewSync*)item->data;
    if(sync->proxy == proxy)
      return sync;
  }

  return NULL;
}

static void
inf_gtk_browser_view_redraw_row(InfGtkBrowserView* view,
                                GtkTreePath* path,
                                GtkTreeIter* iter)
{
  InfGtkBrowserViewPrivate* priv;
  GdkRectangle cell_area;

  /* The actual data in the model has not been changed, otherwise the model
   * would have emitted the row-changed signal and the treeview would redraw
   * itself automatically. What actually has changed is just what we
   * display, for example the progress bar of the exploration of a node. This
   * does not belong to the model because the model does not care about
   * exploration progress, but we want to show it to the user nevertheless.
   * I am not sure whether this is a problem in our design or a limitation
   * in the GTK+ treeview and friends. */
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  if(gtk_widget_get_realized(GTK_WIDGET(view)))
  {
    gtk_tree_view_get_cell_area(
      GTK_TREE_VIEW(view),
      path,
      priv->column,
      &cell_area
    );

    if(cell_area.height != 0)
    {
      gtk_widget_queue_draw_area(
        GTK_WIDGET(view),
        cell_area.x,
        cell_area.y,
        cell_area.width,
        cell_area.height
      );
    }
  }
}

static void
inf_gtk_browser_view_redraw_for_reference(InfGtkBrowserView* view,
                                          GtkTreeRowReference* reference)
{
  InfGtkBrowserViewPrivate* priv;
  GtkTreeModel* model;
  GtkTreePath* path;
  GtkTreeIter iter;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  path = gtk_tree_row_reference_get_path(reference);
  g_assert(path != NULL);

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
  gtk_tree_model_get_iter(model, &iter, path);
  inf_gtk_browser_view_redraw_row(view, path, &iter);
  gtk_tree_path_free(path);
}

/*
 * Callbacks from InfGtkBrowserViewObjects
 */

/* Required by inf_gtk_browser_view_session_synchronization_complete_cb */
static void
inf_gtk_browser_view_sync_removed(InfGtkBrowserView* view,
                                  InfGtkBrowserViewSync* sync);

/* Required by inf_gtk_browser_view_explore_request_finished_cb */
static void
inf_gtk_browser_view_explore_removed(InfGtkBrowserView* view,
                                     InfGtkBrowserViewExplore* expl);

static void
inf_gtk_browser_view_session_synchronization_progress_cb(InfSession* session,
                                                         InfXmlConnection* c,
                                                         gdouble percentage,
                                                         gpointer user_data)
{
  InfGtkBrowserViewSync* sync;
  sync = (InfGtkBrowserViewSync*)user_data;
  
  inf_gtk_browser_view_redraw_for_reference(
    sync->view_browser->view,
    sync->reference
  );
}

static void
inf_gtk_browser_view_session_synchronization_complete_cb(InfSession* session,
                                                         InfXmlConnection* c,
                                                         gpointer user_data)
{
  InfGtkBrowserViewSync* sync;
  sync = (InfGtkBrowserViewSync*)user_data;

  inf_gtk_browser_view_sync_removed(sync->view_browser->view, sync);
}

static void
inf_gtk_browser_view_session_synchronization_failed_cb(InfSession* session,
                                                       InfXmlConnection* c,
                                                       const GError* error,
                                                       gpointer user_data)
{
  /* TODO: Show the error in the view. I am not completely sure how to
   * achieve this. Probably, InfGtkBrowserModel needs to handle this signal
   * and set the error column. */
  InfGtkBrowserViewSync* sync;
  sync = (InfGtkBrowserViewSync*)user_data;

  inf_gtk_browser_view_sync_removed(sync->view_browser->view, sync);
}

static void
inf_gtk_browser_view_explore_request_notify_progress_cb(GObject* request,
                                                        GParamSpec* pspec,
                                                        gpointer user_data)
{
  InfGtkBrowserViewExplore* explore;
  InfGtkBrowserView* view;
  GtkTreeModel* model;
  GtkTreePath* path;
  GtkTreeIter iter;
  GSList* initial_expansion_list;

  explore = (InfGtkBrowserViewExplore*)user_data;
  view = explore->view_browser->view;
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

  path = gtk_tree_row_reference_get_path(explore->reference);
  g_assert(path != NULL);

  gtk_tree_model_get_iter(model, &iter, path);
  inf_gtk_browser_view_redraw_row(view, path, &iter);

  initial_expansion_list = g_object_steal_data(
      G_OBJECT(explore->request),
      INF_GTK_BROWSER_VIEW_INITIAL_EXPANSION
  );

  /*printf("progress-cb on view %p, initial expansion %p\n", explore->view_browser->view, initial_expansion);*/

  /* Expand initial exploration of the root node if we are told to
   * do so. This is the case if we issued the discovery resolv. */
  /* The model might be a filter model in which case the first discovered
   * node might not be contained in the model. We expand as soon as we have
   * any children. If we don't have any, then we can't expand of course. The
   * extra g_object_set_data does not need to be undone since the request
   * most likely vanishes anyway after exploration. */
  if(g_slist_find(initial_expansion_list, view) != NULL &&
     gtk_tree_model_iter_has_child(model, &iter))
  {
    initial_expansion_list = g_slist_remove(
      initial_expansion_list,
      view
    );

    gtk_tree_view_expand_row(GTK_TREE_VIEW(view), path, FALSE);
  }

  g_object_set_data_full(
    G_OBJECT(explore->request),
    INF_GTK_BROWSER_VIEW_INITIAL_EXPANSION,
    initial_expansion_list,
    (GDestroyNotify)g_slist_free
  );

  gtk_tree_path_free(path);
}

static void
inf_gtk_browser_view_explore_request_finished_cb(InfRequest* request,
                                                 const InfRequestResult* res,
                                                 const GError* error,
                                                 gpointer user_data)
{
  InfGtkBrowserViewExplore* explore;
  explore = (InfGtkBrowserViewExplore*)user_data;

  /* Note that InfGtkBrowserStore listens on the request signals as well, and
   * it sets error on the node if there is an error. So we do not need to
   * handle the error here. */
  inf_gtk_browser_view_explore_removed(explore->view_browser->view, explore);
}

/*
 * InfGtkBrowserViewSync, InfGtkBrowserViewExplore
 */

static void
inf_gtk_browser_view_sync_added(InfGtkBrowserView* view,
                                InfBrowser* browser,
                                InfSessionProxy* proxy,
                                GtkTreePath* path,
                                GtkTreeIter* iter)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewBrowser* view_browser;
  InfSession* session;
  InfGtkBrowserViewSync* sync;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  view_browser = inf_gtk_browser_view_find_view_browser(view, browser);
  g_assert(view_browser != NULL);

  g_assert(inf_gtk_browser_view_find_sync(view, view_browser, proxy) == NULL);
  g_assert(
    inf_session_get_synchronization_status(
      session,
      infc_browser_get_connection(INFC_BROWSER(browser))
    ) != INF_SESSION_SYNC_NONE
  );

  sync = g_slice_new(InfGtkBrowserViewSync);
  sync->view_browser = view_browser;
  sync->proxy = proxy;
  g_object_ref(proxy);

  sync->reference = gtk_tree_row_reference_new_proxy(
    G_OBJECT(priv->column),
    gtk_tree_view_get_model(GTK_TREE_VIEW(view)),
    path
  );

  g_assert(sync->reference != NULL);
  view_browser->syncs = g_slist_prepend(view_browser->syncs, sync);

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-progress",
    G_CALLBACK(inf_gtk_browser_view_session_synchronization_progress_cb),
    sync
  );

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-complete",
    G_CALLBACK(inf_gtk_browser_view_session_synchronization_complete_cb),
    sync
  );

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-failed",
    G_CALLBACK(inf_gtk_browser_view_session_synchronization_failed_cb),
    sync
  );

  g_object_unref(session);
  inf_gtk_browser_view_redraw_row(view, path, iter);
}

static void
inf_gtk_browser_view_sync_removed(InfGtkBrowserView* view,
                                  InfGtkBrowserViewSync* sync)
{
  InfGtkBrowserViewPrivate* priv;
  InfSession* session;
  GtkTreePath* path;
  GtkTreeModel* model;
  GtkTreeIter iter;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  g_object_get(G_OBJECT(sync->proxy), "session", &session, NULL);

  /* Redraw if the reference is still valid. Note that if the node is removed
   * while the corresponding session is synchronized, then the reference is
   * not valid at this point. */
  path = gtk_tree_row_reference_get_path(sync->reference);
  if(path != NULL)
  {
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    gtk_tree_model_get_iter(model, &iter, path);
    inf_gtk_browser_view_redraw_row(view, path, &iter);
    gtk_tree_path_free(path);
  }

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(inf_gtk_browser_view_session_synchronization_progress_cb),
    sync
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(inf_gtk_browser_view_session_synchronization_complete_cb),
    sync
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(inf_gtk_browser_view_session_synchronization_failed_cb),
    sync
  );

  g_object_unref(session);

  gtk_tree_row_reference_free(sync->reference);
  g_object_unref(sync->proxy);

  sync->view_browser->syncs = g_slist_remove(sync->view_browser->syncs, sync);
  g_slice_free(InfGtkBrowserViewSync, sync);
}

static void
inf_gtk_browser_view_explore_added(InfGtkBrowserView* view,
                                   InfBrowser* browser,
                                   InfRequest* request,
                                   GtkTreePath* path,
                                   GtkTreeIter* iter)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewBrowser* view_browser;
  InfGtkBrowserViewExplore* explore;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  view_browser = inf_gtk_browser_view_find_view_browser(view, browser);
  g_assert(view_browser != NULL);

  g_assert(
    inf_gtk_browser_view_find_explore(view, view_browser, request) == NULL
  );

  explore = g_slice_new(InfGtkBrowserViewExplore);
  explore->view_browser = view_browser;
  explore->request = request;
  g_object_ref(request);

  explore->reference = gtk_tree_row_reference_new_proxy(
    G_OBJECT(priv->column),
    gtk_tree_view_get_model(GTK_TREE_VIEW(view)),
    path
  );

  g_assert(explore->reference != NULL);
  view_browser->explores = g_slist_prepend(view_browser->explores, explore);

  g_signal_connect_after(
    G_OBJECT(request),
    "notify::progress",
    G_CALLBACK(inf_gtk_browser_view_explore_request_notify_progress_cb),
    explore
  );

  g_signal_connect_after(
    G_OBJECT(request),
    "finished",
    G_CALLBACK(inf_gtk_browser_view_explore_request_finished_cb),
    explore
  );

  /* TODO: Watch failed? */

  inf_gtk_browser_view_redraw_row(view, path, iter);
}

static void
inf_gtk_browser_view_explore_removed(InfGtkBrowserView* view,
                                     InfGtkBrowserViewExplore* expl)
{
  InfGtkBrowserViewPrivate* priv;
  GtkTreePath* path;
  GtkTreeModel* model;
  GtkTreeIter iter;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  /* Redraw if the reference is still valid. Note that if the node is removed
   * while being explored, then the reference is not valid at this point. */
  path = gtk_tree_row_reference_get_path(expl->reference);
  if(path != NULL)
  {
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
    gtk_tree_model_get_iter(model, &iter, path);
    inf_gtk_browser_view_redraw_row(view, path, &iter);
    gtk_tree_path_free(path);
  }

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(expl->request),
    G_CALLBACK(inf_gtk_browser_view_explore_request_notify_progress_cb),
    expl
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(expl->request),
    G_CALLBACK(inf_gtk_browser_view_explore_request_finished_cb),
    expl
  );

  gtk_tree_row_reference_free(expl->reference);
  g_object_unref(expl->request);

  expl->view_browser->explores =
    g_slist_remove(expl->view_browser->explores, expl);
  g_slice_free(InfGtkBrowserViewExplore, expl);
}

/*
 * Callbacks from browser
 */

static void
inf_gtk_browser_view_begin_request_explore_node_cb(InfBrowser* browser,
                                                   InfBrowserIter* iter,
                                                   InfRequest* request,
                                                   gpointer user_data)
{
  InfGtkBrowserViewBrowser* view_browser;
  InfGtkBrowserView* view;
  GtkTreeModel* model;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  gboolean result;

  view_browser = (InfGtkBrowserViewBrowser*)user_data;
  view = view_browser->view;
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

  result = inf_gtk_browser_model_browser_iter_to_tree_iter(
    INF_GTK_BROWSER_MODEL(model),
    browser,
    iter,
    &tree_iter
  );

  /*printf("Begin-explore cb for view %p, result %d\n", view_browser->view, result);*/

  /* The model might be a filter model that does not contain the node
   * being explored, so do not assert here. */
  if(result == TRUE)
  {
    path = gtk_tree_model_get_path(model, &tree_iter);

    inf_gtk_browser_view_explore_added(
      view,
      browser,
      request,
      path,
      &tree_iter
    );

    gtk_tree_path_free(path); 
  }
}

static void
inf_gtk_browser_view_subscribe_session_cb(InfBrowser* browser,
                                          InfBrowserIter* iter,
                                          InfSessionProxy* proxy,
                                          InfRequest* request,
                                          gpointer user_data)
{
  InfGtkBrowserViewBrowser* view_browser;
  InfGtkBrowserView* view;
  InfSession* session;
  GtkTreeModel* model;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  gboolean result;

  view_browser = (InfGtkBrowserViewBrowser*)user_data;
  view = view_browser->view;
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  /* Note that we do not check sync-ins here. This is because sync-ins can
   * only be created along with new nodes, in which case we already add the
   * synchronization in row_inserted_cb(). Perhaps we could still
   * double-check here, just to be sure, though... */
  if(iter != NULL &&
     inf_session_get_status(session) == INF_SESSION_SYNCHRONIZING)
  {
    result = inf_gtk_browser_model_browser_iter_to_tree_iter(
      INF_GTK_BROWSER_MODEL(model),
      browser,
      iter,
      &tree_iter
    );

    /* The model might be a filter model that does not contain the session
     * being synchronized, so do not assert here. */
    if(result == TRUE)
    {
      path = gtk_tree_model_get_path(model, &tree_iter);
      inf_gtk_browser_view_sync_added(
        view,
        browser,
        proxy,
        path,
        &tree_iter
      );
      gtk_tree_path_free(path); 
    }
  }

  g_object_unref(session);
}

static void
inf_gtk_browser_view_acl_changed_cb(InfBrowser* browser,
                                    const InfBrowserIter* iter,
                                    const InfAclSheetSet* sheet_set,
                                    InfRequest* request,
                                    gpointer user_data)
{
  InfGtkBrowserViewBrowser* view_browser;
  InfGtkBrowserView* view;
  GtkTreeModel* model;
  GtkTreeIter tree_iter;
  GtkTreeIter parent_iter;
  GtkTreePath* path;
  gboolean result;
  const InfAclAccount* account;
  InfAclAccountId account_id;
  InfAclMask mask;
  InfRequest* pending_request;

  view_browser = (InfGtkBrowserViewBrowser*)user_data;
  view = view_browser->view;
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

  result = inf_gtk_browser_model_browser_iter_to_tree_iter(
    INF_GTK_BROWSER_MODEL(model),
    browser,
    iter,
    &tree_iter
  );

  /* The model might be a filter model that does not contain the session
   * being synchronized, so do not assert here. */
  if(result == TRUE)
  {
    /* If this node is a subdirectory node, then explore it if its parent
     * is expanded. If it is the root node, then always explore it. */
    if(gtk_tree_model_iter_parent(model, &parent_iter, &tree_iter) == TRUE)
    {
      path = gtk_tree_model_get_path(model, &parent_iter);
      result = gtk_tree_view_row_expanded(GTK_TREE_VIEW(view), path);
      gtk_tree_path_free(path);
    }
    else
    {
      result = TRUE;
    }

    account = inf_browser_get_acl_local_account(browser);
    inf_acl_mask_set1(&mask, INF_ACL_CAN_EXPLORE_NODE);

    account_id = 0;
    if(account != NULL) account_id = account->id;

    if(result == TRUE && /* row expanded or root node */
       inf_browser_is_subdirectory(browser, iter) == TRUE &&
       inf_browser_get_explored(browser, iter) == FALSE &&
       inf_browser_check_acl(browser, iter, account_id, &mask, NULL))
    {
      pending_request = inf_browser_get_pending_request(
        browser,
        iter,
        "explore-node"
      );

      if(pending_request == NULL)
        inf_browser_explore(browser, iter, NULL, NULL);
    }

    /* Redraw to show the new ACL. Since the ACL might propagate recursively,
     * simply redraw the whole widget. */
    gtk_widget_queue_draw(GTK_WIDGET(view));
  }
}

/*
 * Browser management
 */

/* This function recursively walks down iter and all its children and
 * inserts running explore requests and synchronizations into the view. */
static void
inf_gtk_browser_view_walk_requests(InfGtkBrowserView* view,
                                   InfBrowser* browser,
                                   InfBrowserIter* iter)
{
  InfGtkBrowserViewPrivate* priv;
  InfRequest* request;
  GObject* object;
  InfSessionProxy* proxy;
  InfSession* session;
  GtkTreeModel* model;
  GtkTreeIter tree_iter;
  GtkTreePath* path;
  InfBrowserIter child_iter;
  InfXmlConnection* connection;
  gboolean result;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  /* TODO: Carry both path and iter through the recursion, so we do not need
   * to make expensive gtk_tree_model_get_path calls */
  /* Hm. Perhaps this isn't a good idea after all, since normally there are
   * not too much ongoing syncs/explores. */
  if(inf_browser_is_subdirectory(browser, iter))
  {
    if(inf_browser_get_explored(browser, iter))
    {
      child_iter = *iter;
      if(inf_browser_get_child(browser, &child_iter))
      {
        do
        {
          inf_gtk_browser_view_walk_requests(view, browser, &child_iter);
        } while(inf_browser_get_next(browser, &child_iter));
      }
    }

    request = inf_browser_get_pending_request(browser, iter, "explore-node");
    if(request != NULL)
    {
      model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

      result = inf_gtk_browser_model_browser_iter_to_tree_iter(
        INF_GTK_BROWSER_MODEL(model),
        browser,
        iter,
        &tree_iter
      );

      /* The model might be a filter model that does not contain the node
       * being explored, so do not assert here. */
      if(result == TRUE)
      {
        path = gtk_tree_model_get_path(model, &tree_iter);
        inf_gtk_browser_view_explore_added(
          view,
          browser,
          request,
          path,
          &tree_iter
        );
        gtk_tree_path_free(path);
      }
    }
  }
  else if(INFC_IS_BROWSER(browser))
  {
    /* Show synchronization status on client side only, on server side
     * there is nothing to report. */
    proxy = INF_SESSION_PROXY(
      infc_browser_iter_get_sync_in(INFC_BROWSER(browser), iter)
    );

    if(proxy == NULL)
      proxy = inf_browser_get_session(browser, iter);

    if(proxy != NULL)
    {
      g_object_get(G_OBJECT(proxy), "session", &session, NULL);
      connection = infc_browser_get_connection(INFC_BROWSER(browser));
      g_assert(connection != NULL);

      if(inf_session_get_synchronization_status(session, connection) !=
         INF_SESSION_SYNC_NONE)
      {
        model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

        result = inf_gtk_browser_model_browser_iter_to_tree_iter(
          INF_GTK_BROWSER_MODEL(model),
          browser,
          iter,
          &tree_iter
        );

        /* The model might be a filter model that does not contain the session
         * being synchronized, so do not assert here. */
        if(result == TRUE)
        {
          path = gtk_tree_model_get_path(model, &tree_iter);
          inf_gtk_browser_view_sync_added(
            view,
            browser,
            proxy,
            path,
            &tree_iter
          );
          gtk_tree_path_free(path);
        }
      }

      g_object_unref(session);
    }
  }
}

static void
inf_gtk_browser_view_initial_root_explore(InfGtkBrowserView* view,
                                          GtkTreePath* path,
                                          GtkTreeIter* iter)
{
  InfGtkBrowserViewPrivate* priv;
  InfRequest* request;
  GSList* initial_expansion_list;
  InfGtkBrowserViewBrowser* view_browser;
  GtkTreeModel* model;
  InfBrowser* browser;
  InfBrowserIter* browser_iter;
  InfBrowserStatus browser_status;
  const InfAclAccount* account;
  InfAclAccountId acc_id;
  InfAclMask mask;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

  gtk_tree_model_get(
    model,
    iter,
    INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
    INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
    -1
  );

  view_browser = inf_gtk_browser_view_find_view_browser(view, browser);
  g_assert(view_browser != NULL);

  g_object_get(G_OBJECT(browser), "status", &browser_status, NULL);
  if(browser_status == INF_BROWSER_OPEN)
  {
    if(inf_browser_get_explored(browser, browser_iter) == FALSE)
    {
      request = inf_browser_get_pending_request(
        INF_BROWSER(browser),
        browser_iter,
        "explore-node"
      );

      /* Explore root node if it is not already explored */
      inf_acl_mask_set1(&mask, INF_ACL_CAN_EXPLORE_NODE);
      account = inf_browser_get_acl_local_account(browser);

      acc_id = 0;
      if(account != NULL) acc_id = account->id;

      if(request == NULL &&
         inf_browser_check_acl(browser, browser_iter, acc_id, &mask, NULL))
      {
        request = inf_browser_explore(browser, browser_iter, NULL, NULL);
      }

      if(view_browser->initial_root_expansion == TRUE)
      {
        /* Expand the root node */
        if(!gtk_tree_model_iter_has_child(model, iter))
        {
          /* The root node does not yet have a child. If it is not yet fully
           * explored (i.e., request != NULL), then remember to do it as soon
           * as the first note is created. Otherwise, there is nothing to
           * expand. */
          if(request != NULL)
          {
            /* Remember to do initial root expansion when the node has been
             * explored. */
            initial_expansion_list = g_object_steal_data(
              G_OBJECT(request),
              INF_GTK_BROWSER_VIEW_INITIAL_EXPANSION
            );

            if(g_slist_find(initial_expansion_list, view) == NULL)
            {
              initial_expansion_list = g_slist_prepend(
                initial_expansion_list,
                view
              );
            }

            g_object_set_data_full(
              G_OBJECT(request),
              INF_GTK_BROWSER_VIEW_INITIAL_EXPANSION,
              initial_expansion_list,
              (GDestroyNotify)g_slist_free
            );
          }
        }
        else
        {
          /* We can expand it right away */
          gtk_tree_view_expand_row(GTK_TREE_VIEW(view), path, FALSE);
        }

        /* Handled expansion flag, so unset, could otherwise lead to another
         * try of expanding the root node. */
        view_browser->initial_root_expansion = FALSE;
      }
    }
    else
    {
      if(view_browser->initial_root_expansion == TRUE)
      {
        gtk_tree_view_expand_row(GTK_TREE_VIEW(view), path, FALSE);

        /* Handled expansion flag, so unset, could otherwise lead to another
         * try of expanding the root node. */
        view_browser->initial_root_expansion = FALSE;
      }
    }
  }

  inf_browser_iter_free(browser_iter);
  g_object_unref(browser);
}

static void
inf_gtk_browser_view_browser_added(InfGtkBrowserView* view,
                                   InfBrowser* browser,
                                   GtkTreePath* path,
                                   GtkTreeIter* iter)
{
  InfGtkBrowserViewPrivate* priv;
  InfGtkBrowserViewBrowser* view_browser;
  GtkTreeModel* model;
  InfBrowserStatus browser_status;
  InfBrowserIter* browser_iter;
  InfDiscoveryInfo* info;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

  view_browser = g_slice_new(InfGtkBrowserViewBrowser);
  view_browser->view = view;
  view_browser->browser = browser;
  g_object_ref(browser);

  view_browser->explores = NULL;
  view_browser->syncs = NULL;
  view_browser->initial_root_expansion = TRUE;

  view_browser->reference = gtk_tree_row_reference_new_proxy(
    G_OBJECT(priv->column),
    model,
    path
  );

  priv->browsers = g_slist_prepend(priv->browsers, view_browser);

  g_signal_connect(
    G_OBJECT(browser),
    "begin-request::explore-node",
    G_CALLBACK(inf_gtk_browser_view_begin_request_explore_node_cb),
    view_browser
  );

  g_signal_connect_after(
    G_OBJECT(browser),
    "subscribe-session",
    G_CALLBACK(inf_gtk_browser_view_subscribe_session_cb),
    view_browser
  );

  g_signal_connect_after(
    G_OBJECT(browser),
    "acl-changed",
    G_CALLBACK(inf_gtk_browser_view_acl_changed_cb),
    view_browser
  );

  /* TODO: Watch a signal to be notified when a sync-in begins. */

  gtk_tree_model_get(
    model,
    iter,
    INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO, &info,
    -1
  );

  /* Initially expand the root node in this view if we resolved it. */
  if(info != NULL && g_slist_find(priv->info_resolvs, info) != NULL)
  {
    /*printf("Set initial root expansion for view %p\n", view);*/
    /* TODO: Remember to unset the flag when an error happens or the
     * corresponding browser is disconnected for another reason before we
     * actually get to explore anything. */
    view_browser->initial_root_expansion = TRUE;
    priv->info_resolvs = g_slist_remove(priv->info_resolvs, info);
  }

  /* Initial explore if connection is already open */
  g_object_get(G_OBJECT(browser), "status", &browser_status, NULL);
  if(browser_status == INF_BROWSER_OPEN)
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    /* Look for running explore requests, insert into array of running
     * explore requests to show their progress. */
    /* TODO: We do not need this anymore when we get insertion callbacks
     * from the model for each node in the newly added browser. See
     * inf_gtk_browser_store_set_browser_impl() in inf-gtk-browser-store.c. */
    inf_gtk_browser_view_walk_requests(view, browser, browser_iter);

    /* Explore root node initially if not already explored */
    inf_gtk_browser_view_initial_root_explore(view, path, iter);

    inf_browser_iter_free(browser_iter);
  }
}

static void
inf_gtk_browser_view_browser_removed(InfGtkBrowserView* view,
                                     InfGtkBrowserViewBrowser* view_browser)
{
  InfGtkBrowserViewPrivate* priv;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  while(view_browser->explores != NULL)
    inf_gtk_browser_view_explore_removed(view, view_browser->explores->data);
  while(view_browser->syncs != NULL)
    inf_gtk_browser_view_sync_removed(view, view_browser->syncs->data);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(view_browser->browser),
    G_CALLBACK(inf_gtk_browser_view_begin_request_explore_node_cb),
    view_browser
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(view_browser->browser),
    G_CALLBACK(inf_gtk_browser_view_subscribe_session_cb),
    view_browser
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(view_browser->browser),
    G_CALLBACK(inf_gtk_browser_view_acl_changed_cb),
    view_browser
  );

  gtk_tree_row_reference_free(view_browser->reference);
  g_object_unref(view_browser->browser);

  priv->browsers = g_slist_remove(priv->browsers, view_browser);
  g_slice_free(InfGtkBrowserViewBrowser, view_browser);
}

/*
 * TreeModel callbacks
 */

static void
inf_gtk_browser_view_set_browser_cb(InfGtkBrowserModel* model,
                                    GtkTreePath* path,
                                    GtkTreeIter* iter,
                                    InfBrowser* old_browser,
                                    InfBrowser* new_browser,
                                    gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewBrowser* view_browser;

  view = INF_GTK_BROWSER_VIEW(user_data);

  /* Find the old browser for this item, if there is any, and remove it. */
  view_browser = inf_gtk_browser_view_find_view_browser(view, old_browser);
  if(view_browser != NULL)
    inf_gtk_browser_view_browser_removed(view, view_browser);

  /* Add a view browser for the new item */
  if(new_browser != NULL)
    inf_gtk_browser_view_browser_added(view, new_browser, path, iter);
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
  InfBrowser* browser;
  InfBrowserIter* browser_iter;
  InfRequest* request;
  InfGtkBrowserViewBrowser* view_browser;
  InfGtkBrowserViewExplore* explore;
  gboolean explored;
  GtkTreePath* parent_path;
  const InfAclAccount* account;
  InfAclAccountId acc_id;
  InfAclMask mask;

  GObject* object;
  InfSessionProxy* proxy;
  InfSession* session;
  InfXmlConnection* connection;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  gtk_tree_row_reference_inserted(G_OBJECT(priv->column), path);

  if(gtk_tree_model_iter_parent(model, &parent_iter, iter) == TRUE)
  {
    /* Inner node. Explore if the parent node is expanded. */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    g_assert(browser != NULL);

    if(inf_browser_is_subdirectory(browser, browser_iter))
    {
      /* Perhaps some other code already explored this. */
      request = inf_browser_get_pending_request(
        INF_BROWSER(browser),
        browser_iter,
        "explore-node"
      );

      if(request == NULL)
      {
        explored = inf_browser_get_explored(browser, browser_iter);
        inf_acl_mask_set1(&mask, INF_ACL_CAN_EXPLORE_NODE);

        account = inf_browser_get_acl_local_account(browser);
        acc_id = 0;
        if(account != NULL) acc_id = account->id;

        if(explored == FALSE &&
           inf_browser_check_acl(browser, browser_iter, acc_id, &mask, NULL))
        {
          parent_path = gtk_tree_path_copy(path);
          gtk_tree_path_up(parent_path);

          if(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view), parent_path))
          {
            inf_browser_explore(browser, browser_iter, NULL, NULL);
          }

          gtk_tree_path_free(parent_path);
        }
      }
      else
      {
        view_browser = inf_gtk_browser_view_find_view_browser(view, browser);
        g_assert(view_browser != NULL);

        explore = inf_gtk_browser_view_find_explore(
          view,
          view_browser,
          request
        );

        /* TODO: The correct way to do this would probably be to ignore
         * the begin-explore signal of the browser if row-inserted for the
         * row being explored has not yet been received by the view. We
         * could then omit this nasty check. */
        if(explore == NULL)
        {
          inf_gtk_browser_view_explore_added(
            view,
            browser,
            request,
            path,
            iter
          );
        }
      }
    }
    else if(INFC_IS_BROWSER(browser))
    {
      /* Show synchronization status on client side only, on server side
       * there is nothing to report. */
      proxy = INF_SESSION_PROXY(
        infc_browser_iter_get_sync_in(INFC_BROWSER(browser), browser_iter)
      );

      if(proxy == NULL)
        proxy = inf_browser_get_session(browser, browser_iter);

      if(proxy != NULL)
      {
        g_object_get(G_OBJECT(proxy), "session", &session, NULL);
        connection = infc_browser_get_connection(INFC_BROWSER(browser));
        g_assert(connection != NULL);

        if(inf_session_get_synchronization_status(session, connection) !=
           INF_SESSION_SYNC_NONE)
        {
          inf_gtk_browser_view_sync_added(view, browser, proxy, path, iter);
        }

        g_object_unref(session);
      }
    }

    inf_browser_iter_free(browser_iter);
    g_object_unref(G_OBJECT(browser));
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
  InfGtkBrowserModelStatus status;
  InfDiscoveryInfo* info;
  InfBrowser* browser;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  if(gtk_tree_model_iter_parent(model, &parent_iter, iter) == FALSE)
  {
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_STATUS, &status,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO, &info,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      -1
    );

    /* Explore root node as soon as the connection is ready. The second
     * condition is required since this might be called before
     * set_browser_cb. */
    if(status == INF_GTK_BROWSER_MODEL_CONNECTED &&
       inf_gtk_browser_view_find_view_browser(view, browser) != NULL)
    {
      inf_gtk_browser_view_initial_root_explore(view, path, iter);
    }

    /* Remove pending resolv when there was an error while resolving. On
     * success, a browser will be created and we remove the pending resolve
     * in the set-browser signal handler. */
    if(info != NULL && status == INF_GTK_BROWSER_MODEL_ERROR)
      priv->info_resolvs = g_slist_remove(priv->info_resolvs, info);

    if(browser != NULL)
      g_object_unref(browser);
  }
}

static void
inf_gtk_browser_view_row_deleted_cb(GtkTreeModel* model,
                                    GtkTreePath* path,
                                    gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  GSList* top_item;
  InfGtkBrowserViewBrowser* view_browser;
  GtkTreePath* browser_path;
  GSList* item;
  InfGtkBrowserViewExplore* explore;
  InfGtkBrowserViewSync* sync;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  gtk_tree_row_reference_deleted(G_OBJECT(priv->column), path);

  for(top_item = priv->browsers; top_item != NULL; )
  {
    view_browser = (InfGtkBrowserViewBrowser*)top_item->data;
    top_item = top_item->next;

    if(gtk_tree_row_reference_valid(view_browser->reference) == FALSE)
    {
      inf_gtk_browser_view_browser_removed(view, view_browser);
    }
    else
    {
      /* If a child of this browser was removed, then explores and syncs
       * of that browser might be affected. */
      browser_path = gtk_tree_row_reference_get_path(view_browser->reference);
      g_assert(browser_path != NULL);

      if(gtk_tree_path_is_descendant(path, browser_path))
      {
        for(item = view_browser->explores; item != NULL; )
        {
          explore = (InfGtkBrowserViewExplore*)item->data;
          item = item->next;

          if(gtk_tree_row_reference_valid(explore->reference) == FALSE)
            inf_gtk_browser_view_explore_removed(view, explore);
        }
        
        for(item = view_browser->syncs; item != NULL; )
        {
          sync = (InfGtkBrowserViewSync*)item->data;
          item = item->next;

          if(gtk_tree_row_reference_valid(sync->reference) == FALSE)
            inf_gtk_browser_view_sync_removed(view, sync);
        }
      }

      gtk_tree_path_free(browser_path);
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

/*
 * TreeModel management
 */

static void
inf_gtk_browser_view_set_model(InfGtkBrowserView* view,
                               InfGtkBrowserModel* model)
{
  InfGtkBrowserViewPrivate* priv;
  GtkTreeModel* current_model;
  GtkTreeIter iter;
  InfBrowser* browser;
  GtkTreePath* path;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);
  current_model = gtk_tree_view_get_model(GTK_TREE_VIEW(view));

  if(current_model != NULL)
  {
    while(priv->browsers != NULL)
      inf_gtk_browser_view_browser_removed(view, priv->browsers->data);

    /* We are no longer waiting for resolvs from that model */
    g_slist_free(priv->info_resolvs);
    priv->info_resolvs = NULL;

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(current_model),
      G_CALLBACK(inf_gtk_browser_view_row_inserted_cb),
      view
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(current_model),
      G_CALLBACK(inf_gtk_browser_view_row_deleted_cb),
      view
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(current_model),
      G_CALLBACK(inf_gtk_browser_view_row_changed_cb),
      view
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(current_model),
      G_CALLBACK(inf_gtk_browser_view_rows_reordered_cb),
      view
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(current_model),
      G_CALLBACK(inf_gtk_browser_view_set_browser_cb),
      view
    );
  }

  gtk_tree_view_set_model(
    GTK_TREE_VIEW(view),
    GTK_TREE_MODEL(model)
  );

  if(model != NULL)
  {
    gtk_tree_view_set_search_column(
      GTK_TREE_VIEW(view),
      INF_GTK_BROWSER_MODEL_COL_NAME
    );

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
        {
          inf_gtk_browser_view_browser_added(view, browser, path, &iter);
          g_object_unref(browser);
        }

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

    g_signal_connect_after(
      G_OBJECT(model),
      "set-browser",
      G_CALLBACK(inf_gtk_browser_view_set_browser_cb),
      view
    );
  }
}

/*
 * TreeView callbacks
 */

static void
inf_gtk_browser_view_row_expanded(GtkTreeView* tree_view,
                                  GtkTreeIter* iter,
                                  GtkTreePath* path)
{
  GtkTreeModel* model;
  InfBrowser* browser;
  InfBrowserIter* browser_iter;
  InfRequest* pending_request;
  const InfAclAccount* account;
  InfAclAccountId acc_id;
  InfAclMask mask;
  GtkTreeViewClass* parent_class;

  model = gtk_tree_view_get_model(tree_view);

  gtk_tree_model_get(
    model,
    iter,
    INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
    INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
    -1
  );

  g_assert(browser != NULL);

  /* Explore all child nodes that are not yet explored */
  if(inf_browser_get_child(browser, browser_iter))
  {
    account = inf_browser_get_acl_local_account(browser);
    inf_acl_mask_set1(&mask, INF_ACL_CAN_EXPLORE_NODE);

    acc_id = 0;
    if(account != NULL) acc_id = account->id;

    do
    {
      if(inf_browser_is_subdirectory(browser, browser_iter) == TRUE &&
         inf_browser_get_explored(browser, browser_iter) == FALSE &&
         inf_browser_check_acl(browser, browser_iter, acc_id, &mask, NULL))
      {
        pending_request = inf_browser_get_pending_request(
          browser,
          browser_iter,
          "explore-node"
        );

        if(pending_request == NULL)
          inf_browser_explore(browser, browser_iter, NULL, NULL);
      }
    } while(inf_browser_get_next(browser, browser_iter));
  }

  inf_browser_iter_free(browser_iter);
  g_object_unref(browser);

  parent_class = GTK_TREE_VIEW_CLASS(inf_gtk_browser_view_parent_class);
  if(parent_class->row_expanded != NULL)
    parent_class->row_expanded(tree_view, iter, path);
}

static void
inf_gtk_browser_view_row_activated(GtkTreeView* tree_view,
                                   GtkTreePath* path,
                                   GtkTreeViewColumn* column)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  GtkTreeModel* model;
  InfGtkBrowserModelStatus status;
  InfDiscovery* discovery;
  InfDiscoveryInfo* info;
  GtkTreeIter iter;

  InfBrowser* browser;
  InfBrowserIter* browser_iter;
  InfBrowserStatus browser_status;
  InfXmlConnection* connection;
  InfXmlConnectionStatus xml_status;
  GError* error;
  InfGtkBrowserViewBrowser* view_browser;
  GtkTreeViewClass* parent_class;

  view = INF_GTK_BROWSER_VIEW(tree_view);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  /* Connect to host, if not already */
  if(gtk_tree_path_get_depth(path) == 1)
  {
    model = gtk_tree_view_get_model(tree_view);
    gtk_tree_model_get_iter(model, &iter, path);

    gtk_tree_model_get(
      model,
      &iter,
      INF_GTK_BROWSER_MODEL_COL_STATUS, &status,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY, &discovery,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY_INFO, &info,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      -1
    );

    if(browser != NULL && INFC_IS_BROWSER(browser))
    {
      g_assert(infc_browser_get_connection(INFC_BROWSER(browser)) != NULL);
      g_object_get(G_OBJECT(browser), "status", &browser_status, NULL);
      if(browser_status == INF_BROWSER_CLOSED)
      {
        connection = infc_browser_get_connection(INFC_BROWSER(browser));
        g_assert(connection != NULL);
        g_object_get(G_OBJECT(connection), "status", &xml_status, NULL);
        if(xml_status == INF_XML_CONNECTION_CLOSED)
        {
          error = NULL;
          if(!inf_xml_connection_open(connection, &error))
          {
            /* TODO: We can't properly report error here. Actually, we should
             * not do this, but just emit signal activate here, for others
             * to open the connection if necessary. */
            g_warning("Failed to reconnect: %s\n", error->message);
            g_error_free(error);
          }

          view_browser =
            inf_gtk_browser_view_find_view_browser(view, browser);
          g_assert(view_browser != NULL);
          view_browser->initial_root_expansion = TRUE;
        }
      }
    }
    else if(discovery != NULL)
    {
      if(status == INF_GTK_BROWSER_MODEL_DISCOVERED ||
         status == INF_GTK_BROWSER_MODEL_ERROR)
      {
        /* TODO: This method should not exist. Instead, we should just
         * emit ACTIVATE and make others resolve stuff there. */
        inf_gtk_browser_model_resolve(
          INF_GTK_BROWSER_MODEL(model),
          discovery,
          info
        );

        /* Remember that we resolved that info, to do the initial root node
         * expansion. */
        priv->info_resolvs = g_slist_prepend(priv->info_resolvs, info);
        /*printf("Add info %p to info resolvs of view %p\n", info, view);*/
      }
    }

    if(discovery != NULL)
      g_object_unref(G_OBJECT(discovery));

    if(browser != NULL)
      g_object_unref(browser);
  }
  else
  {
    model = gtk_tree_view_get_model(tree_view);
    gtk_tree_model_get_iter(model, &iter, path);

    gtk_tree_model_get(
      model,
      &iter,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
      -1
    );

    if(inf_browser_is_subdirectory(browser, browser_iter))
    {
      gtk_tree_view_expand_row(tree_view, path, FALSE);
    }
    else
    {
      /* Notify */
      g_signal_emit(
        G_OBJECT(view),
        view_signals[ACTIVATE],
        0,
        &iter
      );
    }

    inf_browser_iter_free(browser_iter);
    g_object_unref(G_OBJECT(browser));
  }

  parent_class = GTK_TREE_VIEW_CLASS(inf_gtk_browser_view_parent_class);
  if(parent_class->row_activated != NULL)
    parent_class->row_activated(tree_view, path, column);
}

static void
inf_gtk_browser_view_selection_changed_cb(GtkTreeSelection* selection,
                                          gpointer user_data)
{
  InfGtkBrowserView* view;
  GtkTreeIter selected_iter;

  view = INF_GTK_BROWSER_VIEW(user_data);
  if(gtk_tree_selection_get_selected(selection, NULL, &selected_iter))
    g_signal_emit(view, view_signals[SELECTION_CHANGED], 0, &selected_iter);
  else
    g_signal_emit(view, view_signals[SELECTION_CHANGED], 0, NULL);
}

/*
 * Popup menu
 */

static void
inf_gtk_browser_view_popup_menu_detach_func(GtkWidget* attach_widget,
                                            GtkMenu* menu)
{
}

static void
inf_gtk_browser_view_popup_menu_position_func(GtkMenu* menu,
                                              gint* x,
                                              gint* y,
                                              gboolean* push_in,
                                              gpointer user_data)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;
  GdkWindow* bin_window;
  GdkScreen* screen;
  GtkRequisition menu_req;
  GdkRectangle monitor;
  gint monitor_num;
  gint orig_x;
  gint orig_y;
  gint height;

  GtkTreeSelection* selection;
  GtkTreeModel* model;
  GtkTreeIter selected_iter;
  GtkTreePath* selected_path;
  GdkRectangle cell_area;

  view = INF_GTK_BROWSER_VIEW(user_data);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  /* Place menu below currently selected row */

  bin_window = gtk_tree_view_get_bin_window(GTK_TREE_VIEW(view));
  gdk_window_get_origin(bin_window, &orig_x, &orig_y);

  screen = gtk_widget_get_screen(GTK_WIDGET(view));
  monitor_num = gdk_screen_get_monitor_at_window(screen, bin_window);
  if(monitor_num < 0) monitor_num = 0;
  gtk_menu_set_monitor(menu, monitor_num);

  gdk_screen_get_monitor_geometry(screen, monitor_num, &monitor);
  gtk_widget_get_preferred_size(GTK_WIDGET(menu), NULL, &menu_req);

  height = gdk_window_get_height(bin_window);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  gtk_tree_selection_get_selected(selection, &model, &selected_iter);
  selected_path = gtk_tree_model_get_path(model, &selected_iter);
  gtk_tree_view_get_cell_area(
    GTK_TREE_VIEW(view),
    selected_path,
    priv->column,
    &cell_area
  );
  gtk_tree_path_free(selected_path);

  g_assert(cell_area.height > 0);

  if(gtk_widget_get_direction(GTK_WIDGET(view)) == GTK_TEXT_DIR_LTR)
    *x = orig_x + cell_area.x + cell_area.width - menu_req.width;
  else
    *x = orig_x + cell_area.x;

  *y = orig_y + cell_area.y + cell_area.height;

  /* Keep within widget */
  if(*y < orig_y)
    *y = orig_y;
  if(*y > orig_y + height)
    *y = orig_y + height;

  /* Keep on screen */
  if(*y + menu_req.height > monitor.y + monitor.height)
    *y = monitor.y + monitor.height - menu_req.height;
  if(*y < monitor.y)
    *y = monitor.y;

  *push_in = FALSE;
}

static gboolean
inf_gtk_browser_view_show_popup(InfGtkBrowserView* view,
                                guint button, /* 0 if triggered by keyboard */
                                guint32 time)
{
  GtkWidget* menu;
  GList* children;
  gboolean result;

  menu = gtk_menu_new();

  gtk_menu_attach_to_widget(
    GTK_MENU(menu),
    GTK_WIDGET(view),
    inf_gtk_browser_view_popup_menu_detach_func
  );

  g_signal_emit(view, view_signals[POPULATE_POPUP], 0, menu);

  /* Only show menu if items have been added to it */
  children = gtk_container_get_children(GTK_CONTAINER(menu));
  if(children != NULL)
  {
    result = TRUE;

    if(button)
    {
      gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, button, time);
    }
    else
    {
      gtk_menu_popup(
        GTK_MENU(menu),
        NULL,
        NULL,
        inf_gtk_browser_view_popup_menu_position_func,
        view,
        button,
        time
      );

      gtk_menu_shell_select_first(GTK_MENU_SHELL(menu), FALSE);
    }
  }
  else
  {
    result = FALSE;
    gtk_widget_destroy(menu);
  }

  g_list_free(children);
  return result;
}

static gboolean
inf_gtk_browser_view_button_press_event(GtkWidget* treeview,
                                        GdkEventButton* event)
{
  GtkTreePath* path;
  gboolean has_path;
  GtkWidgetClass* parent_class;

  if(event->button == 3 &&
     event->window == gtk_tree_view_get_bin_window(GTK_TREE_VIEW(treeview)))
  {
    has_path = gtk_tree_view_get_path_at_pos(
      GTK_TREE_VIEW(treeview),
      event->x,
      event->y,
      &path,
      NULL,
      NULL,
      NULL
    );

    if(has_path)
    {
      gtk_tree_selection_select_path(
        gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
        path
      );

      gtk_tree_path_free(path);

      return inf_gtk_browser_view_show_popup(
        INF_GTK_BROWSER_VIEW(treeview),
        event->button,
        event->time
      );
    }
  }

  parent_class = GTK_WIDGET_CLASS(inf_gtk_browser_view_parent_class);
  return parent_class->button_press_event(treeview, event);
}

static gboolean
inf_gtk_browser_view_key_press_event(GtkWidget* treeview,
                                     GdkEventKey* event)
{
  GtkTreeSelection* selection;
  GtkTreeIter iter;
  GtkWidgetClass* parent_class;

  if(event->keyval == GDK_KEY_Menu)
  {
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if(gtk_tree_selection_get_selected(selection, NULL, &iter))
    {
      return inf_gtk_browser_view_show_popup(
        INF_GTK_BROWSER_VIEW(treeview),
        0,
        event->time
      );
    }
  }

  parent_class = GTK_WIDGET_CLASS(inf_gtk_browser_view_parent_class);
  return parent_class->key_press_event(treeview, event);
}

/*
 * CellDataFuncs
 */

static void
inf_gtk_browser_view_icon_data_func(GtkTreeViewColumn* column,
                                    GtkCellRenderer* renderer,
                                    GtkTreeModel* model,
                                    GtkTreeIter* iter,
                                    gpointer user_data)
{
  GtkTreeIter iter_parent;
  InfGtkBrowserModelStatus status;
  InfDiscovery* discovery;
  InfBrowser* browser;
  InfBrowserIter* browser_iter;
  InfAclMask mask;
  const InfAclAccount* account;
  InfAclAccountId acc_id;
  const gchar* icon_name;

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

    /* TODO: Set error icon if an error occured? */

    /* TODO: Set icon depending on note type, perhaps also on whether
     * we are subscribed or not. */
    account = inf_browser_get_acl_local_account(browser);
    acc_id = 0;
    if(account != NULL) acc_id = account->id;

    if(inf_browser_is_subdirectory(browser, browser_iter))
    {
      inf_acl_mask_set1(&mask, INF_ACL_CAN_EXPLORE_NODE);
      if(inf_browser_check_acl(browser, browser_iter, acc_id, &mask, NULL))
        icon_name = "folder";
      else
        /* Would be nice to have a more appropriate icon for this */
        icon_name = "dialog-password";
      g_object_set(G_OBJECT(renderer), "icon-name", icon_name, NULL);
    }
    else
    {
      inf_acl_mask_set1(&mask, INF_ACL_CAN_SUBSCRIBE_SESSION);
      if(inf_browser_check_acl(browser, browser_iter, acc_id, &mask, NULL))
        icon_name = "text-x-generic"; /* appropriate? */
      else
        /* Would be nice to have a more appropriate icon for this */
        icon_name = "dialog-password";
      g_object_set(G_OBJECT(renderer), "icon-name", icon_name, NULL);
    }

    inf_browser_iter_free(browser_iter);
    g_object_unref(G_OBJECT(browser));
  }
  else
  {
    /* toplevel */

    /* TODO: Set icon depending on discovery type (LAN, jabber, direct) */
    /*gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_DISCOVERY, &discovery,
      INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
      -1
    );*/

    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_STATUS, &status,
      -1
    );

    switch(status)
    {
    case INF_GTK_BROWSER_MODEL_DISCONNECTED:
    case INF_GTK_BROWSER_MODEL_DISCOVERED:
    case INF_GTK_BROWSER_MODEL_RESOLVING:
    case INF_GTK_BROWSER_MODEL_CONNECTING:
      icon_name = "network-offline";
      break;
    case INF_GTK_BROWSER_MODEL_CONNECTED:
      /* TODO: Could choose to show network-transmit and/or
       * network-transmit-received when there is activitiy */
      icon_name = "network-idle";
      break;
    case INF_GTK_BROWSER_MODEL_ERROR:
      icon_name = "network-error";
      break;
    default:
      g_assert_not_reached();
      break;
    }
    
    g_object_set(G_OBJECT(renderer), "icon-name", icon_name, NULL);

    /*if(discovery != NULL) g_object_unref(G_OBJECT(discovery));
    if(browser != NULL) g_object_unref(G_OBJECT(browser));*/
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
  InfcBrowser* browser;
  InfBrowserIter* browser_iter;
  const gchar* name;
  gchar* top_name;

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
    
    /* TODO: Use another foreground color (or even background color?) when
     * we are subscribed or have sent a subscription request. */

    name = inf_browser_get_node_name(INF_BROWSER(browser), browser_iter);
    g_object_set(G_OBJECT(renderer), "text", name, NULL);

    inf_browser_iter_free(browser_iter);
    g_object_unref(G_OBJECT(browser));
  }
  else
  {
    /* Toplevel */
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_NAME, &top_name,
      -1
    );

    g_object_set(G_OBJECT(renderer), "text", top_name, NULL);
    g_free(top_name);
  }
}

static void
inf_gtk_browser_view_progress_data_func(GtkTreeViewColumn* column,
                                        GtkCellRenderer* renderer,
                                        GtkTreeModel* model,
                                        GtkTreeIter* iter,
                                        gpointer user_data)
{
  InfBrowser* browser;
  InfBrowserStatus browser_status;
  InfBrowserIter* browser_iter;
  InfRequest* request;
  GObject* object;
  InfSessionProxy* proxy;
  InfSession* session;
  InfXmlConnection* connection;
  gdouble progress;
  gboolean progress_set;

  progress_set = FALSE;

  gtk_tree_model_get(
    model,
    iter,
    INF_GTK_BROWSER_MODEL_COL_BROWSER, &browser,
    -1
  );

  if(browser != NULL)
  {
    g_object_get(G_OBJECT(browser), "status", &browser_status, NULL);
    if(browser_status == INF_BROWSER_OPEN)
    {
      gtk_tree_model_get(
        model,
        iter,
        INF_GTK_BROWSER_MODEL_COL_NODE, &browser_iter,
        -1
      );

      if(inf_browser_is_subdirectory(browser, browser_iter))
      {
        request = inf_browser_get_pending_request(
          browser,
          browser_iter,
          "explore-node"
        );

        if(request != NULL)
        {
          g_object_get(
            G_OBJECT(request),
            "progress", &progress,
            NULL
          );

          /* Progress can be at 1.0 if the all nodes have been explored but
           * the request has not finished yet, since the <explore-end> tag by
           * the server has not yet arrived. In that case we still don't show
           * the progress bar anymore, since from the client's perspective
           * everything has finished and all explored nodes are usable. */
          if(progress < 1.0)
          {
            g_object_set(
              G_OBJECT(renderer),
              "visible", TRUE,
              "value", (gint)(progress * 100 + 0.5),
              "text", _("Exploring..."),
              NULL
            );

            progress_set = TRUE;
          }
        }
      }
      else if(INFC_IS_BROWSER(browser))
      {
        /* Show progress of either sync-in or synchronization
         * due to subscription. Note that we only do this for
         * InfcBrowser objects, not InfdDirectory objects. For an
         * InfdDirectory during sync-in the node is not yet created,
         * and synchronization to individual clients we cannot show
         * easily in the GUI. */
        proxy = INF_SESSION_PROXY(
          infc_browser_iter_get_sync_in(INFC_BROWSER(browser), browser_iter));
        if(proxy == NULL)
          proxy = inf_browser_get_session(browser, browser_iter);

        if(proxy != NULL)
        {
          connection = infc_browser_get_connection(INFC_BROWSER(browser));
          g_assert(connection != NULL);

          g_object_get(G_OBJECT(proxy), "session", &session, NULL);
          if(inf_session_get_synchronization_status(session, connection) !=
             INF_SESSION_SYNC_NONE)
          {
            progress = inf_session_get_synchronization_progress(
              session,
              connection
            );

            g_object_set(
              G_OBJECT(renderer),
              "visible", TRUE,
              "value", (gint)(progress * 100 + 0.5),
              "text", _("Synchronizing..."),
              NULL
            );

            progress_set = TRUE;
          }

          g_object_unref(session);
        }
      }

      inf_browser_iter_free(browser_iter);
    }

    g_object_unref(browser);
  }

  if(!progress_set)
  {
    g_object_set(
      G_OBJECT(renderer),
      "visible", FALSE,
      NULL
    );
  }
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
    gtk_tree_model_get(
      model,
      iter,
      INF_GTK_BROWSER_MODEL_COL_ERROR, &error,
      -1
    );

    if(error != NULL)
    {
      g_object_set(
        G_OBJECT(renderer),
        "text", error->message,
        "foreground", INF_GTK_BROWSER_VIEW_ERROR_COLOR,
        "visible", TRUE,
        NULL
      );
    }
    else
    {
      g_object_set(G_OBJECT(renderer), "visible", FALSE, NULL);
    }
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
    case INF_GTK_BROWSER_MODEL_DISCONNECTED:
      g_object_set(
        G_OBJECT(renderer),
        "text", _("Not connected"),
        "foreground", "black",
        "visible", FALSE, /* Don't show */
        NULL
      );

      break;
    case INF_GTK_BROWSER_MODEL_RESOLVING:
    case INF_GTK_BROWSER_MODEL_CONNECTING:
      g_object_set(
        G_OBJECT(renderer),
        "text", _("Connecting..."),
        "foreground", "black",
        "visible", TRUE,
        NULL
      );

      break;
    case INF_GTK_BROWSER_MODEL_CONNECTED:
      g_object_set(
        G_OBJECT(renderer),
        "text", _("Connected"),
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
        "foreground", INF_GTK_BROWSER_VIEW_ERROR_COLOR,
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

/*
 * GObject overrides
 */

static void
inf_gtk_browser_view_init(InfGtkBrowserView* view)
{
  InfGtkBrowserViewPrivate* priv;
  GtkTreeSelection* selection;

  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  priv->column = gtk_tree_view_column_new();
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  
  priv->renderer_icon = gtk_cell_renderer_pixbuf_new();
  priv->renderer_name = gtk_cell_renderer_text_new();
  priv->renderer_progress = gtk_cell_renderer_progress_new();
  priv->renderer_status = gtk_cell_renderer_text_new();

  priv->browsers = NULL;
  priv->info_resolvs = NULL;

  g_object_set(G_OBJECT(priv->renderer_status), "xpad", 10, NULL);

  gtk_tree_view_column_pack_start(priv->column, priv->renderer_icon, FALSE);

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

  g_signal_connect(
    selection,
    "changed",
    G_CALLBACK(inf_gtk_browser_view_selection_changed_cb),
    view
  );

  gtk_tree_view_append_column(GTK_TREE_VIEW(view), priv->column);
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
}

static void
inf_gtk_browser_view_dispose(GObject* object)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  inf_gtk_browser_view_set_model(view, NULL);

  G_OBJECT_CLASS(inf_gtk_browser_view_parent_class)->dispose(object);
}

static void
inf_gtk_browser_view_finalize(GObject* object)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  g_assert(priv->browsers == NULL);

  G_OBJECT_CLASS(inf_gtk_browser_view_parent_class)->finalize(object);
}

static void
inf_gtk_browser_view_set_property(GObject* object,
                                  guint prop_id,
                                  const GValue* value,
                                  GParamSpec* pspec)
{
  InfGtkBrowserView* view;

  view = INF_GTK_BROWSER_VIEW(object);

  switch(prop_id)
  {
  case PROP_MODEL:
    if (g_value_get_object(value) != NULL)
      g_assert(INF_GTK_IS_BROWSER_MODEL(g_value_get_object(value)));

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

  view = INF_GTK_BROWSER_VIEW(object);

  switch(prop_id)
  {
  case PROP_MODEL:
    g_value_set_object(
      value,
      G_OBJECT(gtk_tree_view_get_model(GTK_TREE_VIEW(view)))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * GtkObject / GtkWidget overrides */

static void
inf_gtk_browser_view_destroy(GtkWidget* object)
{
  InfGtkBrowserView* view;
  InfGtkBrowserViewPrivate* priv;

  view = INF_GTK_BROWSER_VIEW(object);
  priv = INF_GTK_BROWSER_VIEW_PRIVATE(view);

  inf_gtk_browser_view_set_model(view, NULL);

  if(GTK_WIDGET_CLASS(inf_gtk_browser_view_parent_class)->destroy)
    GTK_WIDGET_CLASS(inf_gtk_browser_view_parent_class)->destroy(object);
}

/*
 * GType registration
 */

static void
inf_gtk_browser_view_class_init(InfGtkBrowserViewClass* view_class)
{
  GObjectClass* object_class;
  GtkWidgetClass* widget_class;
  GtkTreeViewClass* tree_class;

  object_class = G_OBJECT_CLASS(view_class);
  widget_class = GTK_WIDGET_CLASS(view_class);
  tree_class = GTK_TREE_VIEW_CLASS(view_class);

  object_class->dispose = inf_gtk_browser_view_dispose;
  object_class->finalize = inf_gtk_browser_view_finalize;
  object_class->set_property = inf_gtk_browser_view_set_property;
  object_class->get_property = inf_gtk_browser_view_get_property;

  widget_class->destroy = inf_gtk_browser_view_destroy;
  widget_class->button_press_event = inf_gtk_browser_view_button_press_event;
  widget_class->key_press_event = inf_gtk_browser_view_key_press_event;

  tree_class->row_expanded = inf_gtk_browser_view_row_expanded;
  tree_class->row_activated = inf_gtk_browser_view_row_activated;

  view_class->activate = NULL;
  view_class->selection_changed = NULL;
  view_class->populate_popup = NULL;

  g_object_class_override_property(object_class, PROP_MODEL, "model");

  view_signals[ACTIVATE] = g_signal_new(
    "activate",
    G_TYPE_FROM_CLASS(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfGtkBrowserViewClass, activate),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    GTK_TYPE_TREE_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
  );

  view_signals[SELECTION_CHANGED] = g_signal_new(
    "selection-changed",
    G_TYPE_FROM_CLASS(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfGtkBrowserViewClass, selection_changed),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    GTK_TYPE_TREE_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
  );

  view_signals[POPULATE_POPUP] = g_signal_new(
    "populate-popup",
    G_TYPE_FROM_CLASS(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfGtkBrowserViewClass, populate_popup),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    GTK_TYPE_MENU
  );
}

/*
 * Public API.
 */

/**
 * inf_gtk_browser_view_new: (constructor)
 *
 * Creates a new #InfGtkBrowserView.
 *
 * Returns: (transfer floating): A new #InfGtkBrowserView.
 **/
GtkWidget*
inf_gtk_browser_view_new(void)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_BROWSER_VIEW, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_gtk_browser_view_new_with_model: (constructor)
 * @model: A #InfGtkBrowserModel.
 *
 * Creates a new #InfGtkBrowserView showing @model.
 *
 * Returns: (transfer floating): A new #InfGtkBrowserView.
 **/
GtkWidget*
inf_gtk_browser_view_new_with_model(InfGtkBrowserModel* model)
{
  GObject* object;
  object = g_object_new(INF_GTK_TYPE_BROWSER_VIEW, "model", model, NULL);
  return GTK_WIDGET(object);
}

/**
 * inf_gtk_browser_view_get_selected:
 * @view: A #InfGtkBrowserView.
 * @iter: (out): An uninitialized #GtkTreeIter.
 *
 * Sets @iter to point to the currently selected row in the browser view. If
 * no row is selected, @iter is left untouched and %FALSE is returned.
 *
 * Returns: Whether @iter was set.
 **/
gboolean
inf_gtk_browser_view_get_selected(InfGtkBrowserView* view,
                                  GtkTreeIter* iter)
{
  GtkTreeSelection* selection;

  g_return_val_if_fail(INF_GTK_IS_BROWSER_VIEW(view), FALSE);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
  return gtk_tree_selection_get_selected(selection, NULL, iter);
}

/**
 * inf_gtk_browser_view_set_selected:
 * @view: A #InfGtkBrowserView.
 * @iter: (transfer none): A #GtkTreeIter pointing to a row in @view.
 *
 * Sets the currently selected row to be @iter. If necessary, rows will be
 * expanded so that @iter is visible.
 */
void
inf_gtk_browser_view_set_selected(InfGtkBrowserView* view,
                                  GtkTreeIter* iter)
{
  GtkTreeSelection* selection;
  GtkTreePath* path;

  g_return_if_fail(INF_GTK_IS_BROWSER_VIEW(view));
  g_return_if_fail(iter != NULL);

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));

  path = gtk_tree_model_get_path(
    gtk_tree_view_get_model(GTK_TREE_VIEW(view)),
    iter
  );
  g_assert(path != NULL);

  gtk_tree_view_expand_to_path(GTK_TREE_VIEW(view), path);
  gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(view), path, NULL, FALSE, 0.0f, 0.0f);
  gtk_tree_path_free(path);

  gtk_tree_selection_select_iter(selection, iter);
}

/* vim:set et sw=2 ts=2: */
