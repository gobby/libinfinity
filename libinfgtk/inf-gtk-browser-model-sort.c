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

#include <libinfgtk/inf-gtk-browser-model-sort.h>

static GObjectClass* parent_class;

typedef struct _InfGtkBrowserModelSortPrivate InfGtkBrowserModelSortPrivate;
struct _InfGtkBrowserModelSortPrivate {
  InfGtkBrowserModel* child_model;
};

#define INF_GTK_BROWSER_MODEL_SORT_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_BROWSER_MODEL_SORT, InfGtkBrowserModelSortPrivate))

static void
inf_gtk_browser_model_sort_set_browser_cb(InfGtkBrowserModel* model,
                                          GtkTreePath* path,
                                          GtkTreeIter* iter,
                                          InfcBrowser* browser,
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
    browser
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
    g_signal_handlers_disconnect_by_func(
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

    g_signal_connect(
      G_OBJECT(child_model),
      "set-browser",
      G_CALLBACK(inf_gtk_browser_model_sort_set_browser_cb),
      child_model
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
inf_gtk_browser_model_sort_init(GTypeInstance* instance,
                                gpointer g_class)
{
  InfGtkBrowserModelSort* model_sort;
  InfGtkBrowserModelSortPrivate* priv;

  model_sort = INF_GTK_BROWSER_MODEL_SORT(instance);
  priv = INF_GTK_BROWSER_MODEL_SORT_PRIVATE(model_sort);

  /* Keep child model in sync with the one from GtkTreeModelSort */
  g_signal_connect(
    instance,
    "notify::model",
    G_CALLBACK(inf_gtk_browser_model_sort_notify_model_cb),
    NULL
  );

  /* TODO: Check whether we get notified if the property is set during
   * construction, and sync in constructor if not. */
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

  g_signal_handlers_disconnect_by_func(
    object,
    G_CALLBACK(inf_gtk_browser_model_sort_notify_model_cb),
    NULL
  );

  G_OBJECT_CLASS(parent_class)->dispose(object);
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
                                                     InfcBrowser* browser,
                                                     InfcBrowserIter* iter,
                                                     GtkTreeIter* tree_iter)
{
  GtkTreeModel* child_model;
  GtkTreeIter child_iter;
  gboolean result;

  child_model = gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(mdl));

  result = inf_gtk_browser_model_browser_iter_to_tree_iter(
    INF_GTK_BROWSER_MODEL(child_model),
    browser,
    iter,
    &child_iter
  );

  if(result == TRUE)
  {
    gtk_tree_model_sort_convert_child_iter_to_iter(
      GTK_TREE_MODEL_SORT(child_model),
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
inf_gtk_browser_model_sort_class_init(gpointer g_class,
                                      gpointer class_data)
{
  GObjectClass* object_class;
  InfGtkBrowserModelSortClass* browser_model_sort_class;

  object_class = G_OBJECT_CLASS(g_class);
  browser_model_sort_class = INF_GTK_BROWSER_MODEL_SORT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkBrowserModelSortPrivate));

  object_class->dispose = inf_gtk_browser_model_sort_dispose;
}

static void
inf_gtk_browser_model_sort_browser_model_init(gpointer g_iface,
                                              gpointer iface_data)
{
  InfGtkBrowserModelIface* iface;
  iface = (InfGtkBrowserModelIface*)g_iface;

  iface->set_browser = NULL;
  iface->resolve = inf_gtk_browser_model_sort_resolve;
  /* inf_gtk_browser_model_sort_browser_model_browser_iter_to_tree_iter would
   * be consistent, but a _bit_ too long to fit properly into 80 chars ;) */
  iface->browser_iter_to_tree_iter =
    inf_gtk_browser_model_sort_browser_iter_to_tree_iter;
}

GType
inf_gtk_browser_model_sort_get_type(void)
{
  static GType browser_model_sort_type = 0;

  if(!browser_model_sort_type)
  {
    static const GTypeInfo browser_model_sort_type_info = {
      sizeof(InfGtkBrowserModelSortClass),    /* class_size */
      NULL,                                   /* base_init */
      NULL,                                   /* base_finalize */
      inf_gtk_browser_model_sort_class_init,  /* class_init */
      NULL,                                   /* class_finalize */
      NULL,                                   /* class_data */
      sizeof(InfGtkBrowserModelSort),         /* instance_size */
      0,                                      /* n_preallocs */
      inf_gtk_browser_model_sort_init,        /* instance_init */
      NULL                                    /* value_table */
    };

    static const GInterfaceInfo browser_model_info = {
      inf_gtk_browser_model_sort_browser_model_init,
      NULL,
      NULL
    };

    browser_model_sort_type = g_type_register_static(
      GTK_TYPE_TREE_MODEL_SORT,
      "InfGtkBrowserModelSort",
      &browser_model_sort_type_info,
      0
    );

    g_type_add_interface_static(
      browser_model_sort_type,
      INF_GTK_TYPE_BROWSER_MODEL,
      &browser_model_info
    );
  }

  return browser_model_sort_type;
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
