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

/**
 * SECTION:inf-gtk-browser-model
 * @title: InfGtkBrowserModel
 * @short_description: Interface for tree models representing infinote
 * directories
 * @include: libinfgtk/inf-gtk-browser-model.h
 * @see_also: #InfGtkBrowserStore
 * @stability: Unstable
 *
 * #InfGtkBrowserModel provides an interface for tree models that are used to
 * show the contents of infinote directories. All implementations of
 * #InfGtkBrowserModel also need to implement #GtkTreeModel and can then by
 * displayed in a #GtkTreeView.
 *
 * #InfGtkBrowserStore is a reference implementation of this interface,
 * allowing to add discoveries and browsers to it which it then manages.
 * Other implementations include #InfGtkBrowserModelSort and
 * #InfGtkBrowserModelFilter which can be used to sort or filter the list.
 */

#include <libinfgtk/inf-gtk-browser-model.h>
#include <libinfinity/client/infc-browser.h>
#include <libinfinity/inf-define-enum.h>

static const GEnumValue inf_gtk_browser_model_status_values[] = {
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
  }, {
    0,
    NULL,
    NULL
  }
};

INF_DEFINE_ENUM_TYPE(InfGtkBrowserModelStatus, inf_gtk_browser_model_status, inf_gtk_browser_model_status_values)
G_DEFINE_INTERFACE(InfGtkBrowserModel, inf_gtk_browser_model, GTK_TYPE_TREE_MODEL)

enum {
  SET_BROWSER,

  LAST_SIGNAL
};

static guint browser_model_signals[LAST_SIGNAL];

static void
inf_gtk_browser_model_default_init(InfGtkBrowserModelInterface* iface)
{
  /**
   * InfGtkBrowserModel::set-browser:
   * @model: The #InfGtkBrowserModel emitting the signal.
   * @path: A #GtkTreePath pointing to the item with a new browesr.
   * @iter: A #GtkTreeIter pointing to the item with a new browser.
   * @old_browser: The previous #InfBrowser.
   * @new_browser: The new #InfBrowser.
   *
   * This signal is emitted every time the #InfBrowser for one of the
   * model's top-level entries change. This means either that a completely
   * new item was inserted, that an item providing only a discovery has
   * been resolved (see inf_gtk_browser_model_resolve()), or that a
   * top-level entry has been removed.
   *
   * During emission of the signal the actual value in the model might
   * either be the old or the new browser.
   */
  browser_model_signals[SET_BROWSER] = g_signal_new(
    "set-browser",
    INF_GTK_TYPE_BROWSER_MODEL,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfGtkBrowserModelInterface, set_browser),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    4,
    GTK_TYPE_TREE_PATH | G_SIGNAL_TYPE_STATIC_SCOPE,
    GTK_TYPE_TREE_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INF_TYPE_BROWSER,
    INF_TYPE_BROWSER
  );
}

/**
 * inf_gtk_browser_model_set_browser:
 * @model: A #InfGtkBrowserModel.
 * @path: A #GtkTreePath to a top-level row.
 * @iter: A #GtkTreeIter pointing to the same row.
 * @old_browser: The browser which was set at this row before.
 * @new_browser: The new #InfBrowser to set for this row.
 *
 * Emits the #InfGtkBrowserModel::set-browser signal. This is supposed to only
 * be called by implementations of #InfGtkBrowserModel whenever they set or
 * unset a browser on a row.
 **/
void
inf_gtk_browser_model_set_browser(InfGtkBrowserModel* model,
                                  GtkTreePath* path,
                                  GtkTreeIter* iter,
                                  InfBrowser* old_browser,
                                  InfBrowser* new_browser)
{
  g_return_if_fail(INF_GTK_IS_BROWSER_MODEL(model));
  g_return_if_fail(path != NULL);
  g_return_if_fail(iter != NULL);
  g_return_if_fail(old_browser == NULL || INF_IS_BROWSER(old_browser));
  g_return_if_fail(new_browser == NULL || INF_IS_BROWSER(new_browser));

  g_signal_emit(
    G_OBJECT(model),
    browser_model_signals[SET_BROWSER],
    0,
    path,
    iter,
    old_browser,
    new_browser
  );
}

/**
 * inf_gtk_browser_model_resolve:
 * @model: A #InfGtkBrowserModel.
 * @discovery: A #InfDiscovery added to @store.
 * @info: A #InfDiscoveryInfo discovered by @discovery.
 *
 * Resolves @info and adds the resulting connection to the model. If that
 * connection is already contained, the original (newly resolved) entry
 * is removed in favor of the existing entry whose browser might already
 * have explored (parts of) the server's directory.
 */
void
inf_gtk_browser_model_resolve(InfGtkBrowserModel* model,
                              InfDiscovery* discovery,
                              InfDiscoveryInfo* info)
{
  InfGtkBrowserModelInterface* iface;

  g_return_if_fail(INF_GTK_IS_BROWSER_MODEL(model));
  g_return_if_fail(INF_IS_DISCOVERY(discovery));
  g_return_if_fail(info != NULL);

  iface = INF_GTK_BROWSER_MODEL_GET_IFACE(model);
  g_return_if_fail(iface->resolve != NULL);

  iface->resolve(model, discovery, info);
}

/**
 * inf_gtk_browser_model_browser_iter_to_tree_iter:
 * @model: A #InfGtkBrowserModel.
 * @browser:  A #InfBrowser.
 * @iter: A #InfBrowserIter pointing into @browser, or %NULL.
 * @tree_iter: (out): A #GtkTreeIter that will be set by this function.
 *
 * Sets @tree_iter to point to the same node @iter refers to
 * within the model. If @browser is not known to @model, i.e. its connection
 * was never added to @model, then the function returns %FALSE and
 * @tree_iter is left untouched.
 *
 * If @iter is %NULL, the function sets @tree_iter to point to the top
 * level entry representing @browser.
 *
 * Returns: Whether @tree_iter was set.
 **/
gboolean
inf_gtk_browser_model_browser_iter_to_tree_iter(InfGtkBrowserModel* model,
                                                InfBrowser* browser,
                                                const InfBrowserIter* iter,
                                                GtkTreeIter* tree_iter)
{
  InfGtkBrowserModelInterface* iface;

  g_return_val_if_fail(INF_GTK_IS_BROWSER_MODEL(model), FALSE);
  g_return_val_if_fail(INF_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(tree_iter != NULL, FALSE);

  iface = INF_GTK_BROWSER_MODEL_GET_IFACE(model);
  g_return_val_if_fail(iface->browser_iter_to_tree_iter != NULL, FALSE);

  return iface->browser_iter_to_tree_iter(model, browser, iter, tree_iter);
}

/* vim:set et sw=2 ts=2: */
