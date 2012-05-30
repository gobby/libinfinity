/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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

#include <libinfgtk/inf-gtk-browser-model.h>
#include <libinfinity/client/infc-browser.h>
#include <libinfinity/inf-marshal.h>

enum {
  SET_BROWSER,

  LAST_SIGNAL
};

static guint browser_model_signals[LAST_SIGNAL];

static void
inf_gtk_browser_model_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    /**
     * InfGtkBrowserModel::set-browser:
     * @model: The #InfGtkBrowserModel emitting the signal.
     * @path: A #GtkTreePath pointing to the newly created browser.
     * @iter: A #GtkTreeIter pointing to the newly created browser.
     * @browser: The newly created #InfcBrowser.
     *
     * This signal is emitted every time a new browser is inserted to the
     * model. This means either that a completely new item was inserted, or
     * that an item providing only a discovery has been resolved (see
     * inf_gtk_browser_model_resolve()).
     */
    browser_model_signals[SET_BROWSER] = g_signal_new(
      "set-browser",
      INF_GTK_TYPE_BROWSER_MODEL,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfGtkBrowserModelIface, set_browser),
      NULL, NULL,
      inf_marshal_VOID__BOXED_BOXED_OBJECT,
      G_TYPE_NONE,
      3,
      GTK_TYPE_TREE_PATH | G_SIGNAL_TYPE_STATIC_SCOPE,
      GTK_TYPE_TREE_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
      INFC_TYPE_BROWSER
    );

    initialized = TRUE;
  }
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
      }, {
        0,
        NULL,
        NULL
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
inf_gtk_browser_model_get_type()
{
  static GType browser_model_type = 0;

  if(!browser_model_type)
  {
    static const GTypeInfo browser_model_info = {
      sizeof(InfGtkBrowserModelIface),     /* class_size */
      inf_gtk_browser_model_base_init,     /* base_init */
      NULL,                                /* base_finalize */
      NULL,                                /* class_init */
      NULL,                                /* class_finalize */
      NULL,                                /* class_data */
      0,                                   /* instance_size */
      0,                                   /* n_preallocs */
      NULL,                                /* instance_init */
      NULL                                 /* value_table */
    };

    browser_model_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfGtkBrowserModel",
      &browser_model_info,
      0
    );

    g_type_interface_add_prerequisite(
      browser_model_type,
      GTK_TYPE_TREE_MODEL
    );
  }

  return browser_model_type;
}

/**
 * inf_gtk_browser_model_set_browser:
 * @model: A #InfGtkBrowserModel.
 * @path: A #GtkTreePath to a top-level row.
 * @iter: A #GtkTreeIter pointing to the same row.
 * @browser: The new #InfcBrowser to set.
 *
 * Emits the #InfGtkBrowserModel::set-browser signal. This is supposed to only
 * be called by implementations of #InfGtkBrowserModel whenever they set or
 * unset a browser on a row.
 **/
void
inf_gtk_browser_model_set_browser(InfGtkBrowserModel* model,
                                  GtkTreePath* path,
                                  GtkTreeIter* iter,
                                  InfcBrowser* browser)
{
  g_return_if_fail(INF_GTK_IS_BROWSER_MODEL(model));
  g_return_if_fail(path != NULL);
  g_return_if_fail(iter != NULL);
  g_return_if_fail(browser == NULL || INFC_IS_BROWSER(browser));

  g_signal_emit(
    G_OBJECT(model),
    browser_model_signals[SET_BROWSER],
    0,
    path,
    iter,
    browser
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
  InfGtkBrowserModelIface* iface;

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
  InfGtkBrowserModelIface* iface;

  g_return_val_if_fail(INF_GTK_IS_BROWSER_MODEL(model), FALSE);
  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(browser_iter != NULL, FALSE);
  g_return_val_if_fail(tree_iter != NULL, FALSE);

  iface = INF_GTK_BROWSER_MODEL_GET_IFACE(model);
  g_return_val_if_fail(iface->browser_iter_to_tree_iter != NULL, FALSE);

  return iface->browser_iter_to_tree_iter(
    model,
    browser,
    browser_iter,
    tree_iter
  );
}

/* vim:set et sw=2 ts=2: */
