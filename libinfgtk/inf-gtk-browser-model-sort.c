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

#include <libinfinity/inf-signals.h>

#include <libinfgtk/inf-gtk-browser-model-sort.h>

typedef struct _InfGtkBrowserModelSortPrivate InfGtkBrowserModelSortPrivate;
struct _InfGtkBrowserModelSortPrivate {
  InfGtkBrowserModel* child_model;
};

#define INF_GTK_BROWSER_MODEL_SORT_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_BROWSER_MODEL_SORT, InfGtkBrowserModelSortPrivate))

static void inf_gtk_browser_model_sort_browser_model_iface_init(InfGtkBrowserModelInterface* iface);
G_DEFINE_TYPE_WITH_CODE(InfGtkBrowserModelSort, inf_gtk_browser_model_sort, GTK_TYPE_TREE_MODEL_SORT,
  G_ADD_PRIVATE(InfGtkBrowserModelSort)
  G_IMPLEMENT_INTERFACE(INF_GTK_TYPE_BROWSER_MODEL, inf_gtk_browser_model_sort_browser_model_iface_init))

static void
inf_gtk_browser_model_sort_set_browser_cb(InfGtkBrowserModel* model,
                                          GtkTreePath* path,
                                          GtkTreeIter* iter,
                                          InfBrowser* old_browser,
                                          InfBrowser* new_browser,
                                          gpointer user_data)
{
  GtkTreeModelSort* model_sort;
  GtkTreePath* own_path;
  GtkTreeIter own_iter;

  model_sort = GTK_TREE_MODEL_SORT(user_data);

  own_path = gtk_tree_model_sort_convert_child_path_to_path(model_sort, path);
  gtk_tree_model_sort_convert_child_iter_to_iter(model_sort, &own_iter, iter);
 
  inf_gtk_browser_model_set_browser(
    INF_GTK_BROWSER_MODEL(user_data),
    own_path,
    &own_iter,
    old_browser,
    new_browser
  );

  gtk_tree_path_free(own_path);
}

static void
inf_gtk_browser_model_sort_sync_child_model(InfGtkBrowserModelSort* model,
                                            InfGtkBrowserModel* child_model)
{
  InfGtkBrowserModelSortPrivate* priv;
  priv = INF_GTK_BROWSER_MODEL_SORT_PRIVATE(model);

  if(priv->child_model != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      priv->child_model,
      G_CALLBACK(inf_gtk_browser_model_sort_set_browser_cb),
      model
    );

    g_object_unref(priv->child_model);
  }

  priv->child_model = child_model;

  if(child_model != NULL)
  {
    g_object_ref(child_model);

    g_signal_connect_after(
      G_OBJECT(child_model),
      "set-browser",
      G_CALLBACK(inf_gtk_browser_model_sort_set_browser_cb),
      model
    );
  }
}

static void
inf_gtk_browser_model_sort_notify_model_cb(GObject* object,
                                           GParamSpec* pspec,
                                           gpointer user_data)
{
  GtkTreeModel* child_model;

  child_model = gtk_tree_model_sort_get_model(
    GTK_TREE_MODEL_SORT(object)
  );

  inf_gtk_browser_model_sort_sync_child_model(
    INF_GTK_BROWSER_MODEL_SORT(object),
    INF_GTK_BROWSER_MODEL(child_model)
  );
}

/*
 * GObject overrides
 */

static void
inf_gtk_browser_model_sort_init(InfGtkBrowserModelSort* model_sort)
{
  InfGtkBrowserModelSortPrivate* priv;
  priv = INF_GTK_BROWSER_MODEL_SORT_PRIVATE(model_sort);

  /* Keep child model in sync with the one from GtkTreeModelSort */
  g_signal_connect(
    model_sort,
    "notify::model",
    G_CALLBACK(inf_gtk_browser_model_sort_notify_model_cb),
    NULL
  );
}

static GObject*
inf_gtk_browser_model_sort_constructor(GType type,
                                       guint n_construct_properties,
                                       GObjectConstructParam* properties)
{
  GObject* object;

  object = G_OBJECT_CLASS(inf_gtk_browser_model_sort_parent_class)->constructor(
    type,
    n_construct_properties,
    properties
  );

  /* Set initial model, we do not get notified for this */
  inf_gtk_browser_model_sort_sync_child_model(
    INF_GTK_BROWSER_MODEL_SORT(object),
    INF_GTK_BROWSER_MODEL(
      gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(object))
    )
  );

  return object;
}

static void
inf_gtk_browser_model_sort_dispose(GObject* object)
{
  InfGtkBrowserModelSort* model_sort;
  InfGtkBrowserModelSortPrivate* priv;

  model_sort = INF_GTK_BROWSER_MODEL_SORT(object);
  priv = INF_GTK_BROWSER_MODEL_SORT_PRIVATE(model_sort);

  /* Release own child model, since we won't get notified anymore when the
   * parent's dispose runs. We disconnect the signal handler before chaining
   * up. */
  inf_gtk_browser_model_sort_sync_child_model(model_sort, NULL);

  inf_signal_handlers_disconnect_by_func(
    object,
    G_CALLBACK(inf_gtk_browser_model_sort_notify_model_cb),
    NULL
  );

  G_OBJECT_CLASS(inf_gtk_browser_model_sort_parent_class)->dispose(object);
}

/*
 * InfGtkBrowserModel implementation.
 */

static void
inf_gtk_browser_model_sort_resolve(InfGtkBrowserModel* model,
                                   InfDiscovery* discovery,
                                   InfDiscoveryInfo* info)
{
  GtkTreeModel* child_model;

  child_model = gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(model));

  inf_gtk_browser_model_resolve(
    INF_GTK_BROWSER_MODEL(child_model),
    discovery,
    info
  );
}

static gboolean
inf_gtk_browser_model_sort_browser_iter_to_tree_iter(InfGtkBrowserModel* mdl,
                                                     InfBrowser* browser,
                                                     const InfBrowserIter* it,
                                                     GtkTreeIter* tree_iter)
{
  GtkTreeModel* child_model;
  GtkTreeIter child_iter;
  gboolean result;

  child_model = gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(mdl));

  result = inf_gtk_browser_model_browser_iter_to_tree_iter(
    INF_GTK_BROWSER_MODEL(child_model),
    browser,
    it,
    &child_iter
  );

  if(result == TRUE)
  {
    gtk_tree_model_sort_convert_child_iter_to_iter(
      GTK_TREE_MODEL_SORT(mdl),
      tree_iter,
      &child_iter
    );

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/*
 * GType registration
 */

static void
inf_gtk_browser_model_sort_class_init(
  InfGtkBrowserModelSortClass* browser_model_sort_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(browser_model_sort_class);

  object_class->constructor = inf_gtk_browser_model_sort_constructor;
  object_class->dispose = inf_gtk_browser_model_sort_dispose;
}

static void
inf_gtk_browser_model_sort_browser_model_iface_init(
  InfGtkBrowserModelInterface* iface)
{
  iface->set_browser = NULL;
  iface->resolve = inf_gtk_browser_model_sort_resolve;
  /* inf_gtk_browser_model_sort_browser_model_browser_iter_to_tree_iter would
   * be consistent, but a _bit_ too long to fit properly into 80 chars ;) */
  iface->browser_iter_to_tree_iter =
    inf_gtk_browser_model_sort_browser_iter_to_tree_iter;
}

/*
 * Public API.
 */

/**
 * inf_gtk_browser_model_sort_new:
 * @child_model: A #InfGtkBrowserModel.
 *
 * Creates a new #InfGtkBrowserModelSort, sorting @child_model.
 *
 * Return Value: A new #InfGtkBrowserModelSort.
 **/
InfGtkBrowserModelSort*
inf_gtk_browser_model_sort_new(InfGtkBrowserModel* child_model)
{
  GObject* object;

  object = g_object_new(
    INF_GTK_TYPE_BROWSER_MODEL_SORT,
    "model", child_model,
    NULL
  );

  return INF_GTK_BROWSER_MODEL_SORT(object);
}

/* vim:set et sw=2 ts=2: */
