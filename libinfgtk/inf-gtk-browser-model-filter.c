/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <libinfgtk/inf-gtk-browser-model-filter.h>

static GObjectClass* parent_class;

typedef struct _InfGtkBrowserModelFilterPrivate
  InfGtkBrowserModelFilterPrivate;
struct _InfGtkBrowserModelFilterPrivate {
  InfGtkBrowserModel* child_model;
};

#define INF_GTK_BROWSER_MODEL_FILTER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_GTK_TYPE_BROWSER_MODEL_FILTER, InfGtkBrowserModelFilterPrivate))

static void
inf_gtk_browser_model_filter_set_browser_cb(InfGtkBrowserModel* model,
                                            GtkTreePath* path,
                                            GtkTreeIter* iter,
                                            InfcBrowser* browser,
                                            gpointer user_data)
{
  GtkTreeModelFilter* model_filter;
  GtkTreePath* own_path;
  GtkTreeIter own_iter;
  gboolean result;

  model_filter = GTK_TREE_MODEL_FILTER(user_data);

  result = gtk_tree_model_filter_convert_child_iter_to_iter(
    model_filter,
    &own_iter,
    iter
  );

  if(result == TRUE)
  {
    own_path = gtk_tree_model_filter_convert_child_path_to_path(
      model_filter,
      path
    );
    g_assert(own_path != NULL);

    inf_gtk_browser_model_set_browser(
      INF_GTK_BROWSER_MODEL(user_data),
      own_path,
      &own_iter,
      browser
    );

    gtk_tree_path_free(own_path);
  }
}

static void
inf_gtk_browser_model_filter_sync_child_model(InfGtkBrowserModelFilter* model,
                                              InfGtkBrowserModel* child_model)
{
  InfGtkBrowserModelFilterPrivate* priv;
  priv = INF_GTK_BROWSER_MODEL_FILTER_PRIVATE(model);

  if(priv->child_model != NULL)
  {
    g_signal_handlers_disconnect_by_func(
      priv->child_model,
      G_CALLBACK(inf_gtk_browser_model_filter_set_browser_cb),
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
      G_CALLBACK(inf_gtk_browser_model_filter_set_browser_cb),
      model
    );
  }
}

static void
inf_gtk_browser_model_filter_notify_model_cb(GObject* object,
                                             GParamSpec* pspec,
                                             gpointer user_data)
{
  GtkTreeModel* child_model;

  child_model = gtk_tree_model_filter_get_model(
    GTK_TREE_MODEL_FILTER(object)
  );

  inf_gtk_browser_model_filter_sync_child_model(
    INF_GTK_BROWSER_MODEL_FILTER(object),
    INF_GTK_BROWSER_MODEL(child_model)
  );
}

/*
 * GObject overrides
 */

static void
inf_gtk_browser_model_filter_init(GTypeInstance* instance,
                                  gpointer g_class)
{
  InfGtkBrowserModelFilter* model_sort;
  InfGtkBrowserModelFilterPrivate* priv;

  model_sort = INF_GTK_BROWSER_MODEL_FILTER(instance);
  priv = INF_GTK_BROWSER_MODEL_FILTER_PRIVATE(model_sort);

  /* Keep child model in sync with the one from GtkTreeModelFilter */
  g_signal_connect(
    instance,
    "notify::model",
    G_CALLBACK(inf_gtk_browser_model_filter_notify_model_cb),
    NULL
  );
}

static GObject*
inf_gtk_browser_model_filter_constructor(GType type,
                                         guint n_construct_properties,
                                         GObjectConstructParam* properties)
{
  GObject* object;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    properties
  );

  /* Set initial model, we do not get notified for this */
  inf_gtk_browser_model_filter_sync_child_model(
    INF_GTK_BROWSER_MODEL_FILTER(object),
    INF_GTK_BROWSER_MODEL(
      gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(object))
    )
  );

  return object;
}

static void
inf_gtk_browser_model_filter_dispose(GObject* object)
{
  InfGtkBrowserModelFilter* model_sort;
  InfGtkBrowserModelFilterPrivate* priv;

  model_sort = INF_GTK_BROWSER_MODEL_FILTER(object);
  priv = INF_GTK_BROWSER_MODEL_FILTER_PRIVATE(model_sort);

  /* Release own child model, since we won't get notified anymore when the
   * parent's dispose runs. We disconnect the signal handler before chaining
   * up. */
  inf_gtk_browser_model_filter_sync_child_model(model_sort, NULL);

  g_signal_handlers_disconnect_by_func(
    object,
    G_CALLBACK(inf_gtk_browser_model_filter_notify_model_cb),
    NULL
  );

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

/*
 * InfGtkBrowserModel implementation.
 */

static void
inf_gtk_browser_model_filter_resolve(InfGtkBrowserModel* model,
                                     InfDiscovery* discovery,
                                     InfDiscoveryInfo* info)
{
  GtkTreeModel* child_model;

  child_model = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

  inf_gtk_browser_model_resolve(
    INF_GTK_BROWSER_MODEL(child_model),
    discovery,
    info
  );
}

static gboolean
inf_gtk_browser_model_filter_browser_iter_to_tree_iter(InfGtkBrowserModel* m,
                                                       InfcBrowser* browser,
                                                       InfcBrowserIter* iter,
                                                       GtkTreeIter* tree_iter)
{
  GtkTreeModel* child_model;
  GtkTreeIter child_iter;
  gboolean result;

  child_model = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(m));

  result = inf_gtk_browser_model_browser_iter_to_tree_iter(
    INF_GTK_BROWSER_MODEL(child_model),
    browser,
    iter,
    &child_iter
  );

  if(result == TRUE)
  {
    return gtk_tree_model_filter_convert_child_iter_to_iter(
      GTK_TREE_MODEL_FILTER(m),
      tree_iter,
      &child_iter
    );
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
inf_gtk_browser_model_filter_class_init(gpointer g_class,
                                        gpointer class_data)
{
  GObjectClass* object_class;
  InfGtkBrowserModelFilterClass* browser_model_filter_class;

  object_class = G_OBJECT_CLASS(g_class);
  browser_model_filter_class = INF_GTK_BROWSER_MODEL_FILTER_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfGtkBrowserModelFilterPrivate));

  object_class->constructor = inf_gtk_browser_model_filter_constructor;
  object_class->dispose = inf_gtk_browser_model_filter_dispose;
}

static void
inf_gtk_browser_model_filter_browser_model_init(gpointer g_iface,
                                                gpointer iface_data)
{
  InfGtkBrowserModelIface* iface;
  iface = (InfGtkBrowserModelIface*)g_iface;

  iface->set_browser = NULL;
  iface->resolve = inf_gtk_browser_model_filter_resolve;
  /* inf_gtk_browser_model_filter_browser_model_browser_iter_to_tree_iter
   * would be consistent, but a _bit_ too long to fit properly into 80
   * chars ;) */
  iface->browser_iter_to_tree_iter =
    inf_gtk_browser_model_filter_browser_iter_to_tree_iter;
}

GType
inf_gtk_browser_model_filter_get_type(void)
{
  static GType browser_model_filter_type = 0;

  if(!browser_model_filter_type)
  {
    static const GTypeInfo browser_model_filter_type_info = {
      sizeof(InfGtkBrowserModelFilterClass),    /* class_size */
      NULL,                                     /* base_init */
      NULL,                                     /* base_finalize */
      inf_gtk_browser_model_filter_class_init,  /* class_init */
      NULL,                                     /* class_finalize */
      NULL,                                     /* class_data */
      sizeof(InfGtkBrowserModelFilter),         /* instance_size */
      0,                                        /* n_preallocs */
      inf_gtk_browser_model_filter_init,        /* instance_init */
      NULL                                      /* value_table */
    };

    static const GInterfaceInfo browser_model_info = {
      inf_gtk_browser_model_filter_browser_model_init,
      NULL,
      NULL
    };

    browser_model_filter_type = g_type_register_static(
      GTK_TYPE_TREE_MODEL_FILTER,
      "InfGtkBrowserModelFilter",
      &browser_model_filter_type_info,
      0
    );

    g_type_add_interface_static(
      browser_model_filter_type,
      INF_GTK_TYPE_BROWSER_MODEL,
      &browser_model_info
    );
  }

  return browser_model_filter_type;
}

/*
 * Public API.
 */

/**
 * inf_gtk_browser_model_filter_new:
 * @child_model: A #InfGtkBrowserModel.
 *
 * Creates a new #InfGtkBrowserModelFilter, filtering @child_model.
 *
 * Return Value: A new #InfGtkBrowserModelFilter.
 **/
InfGtkBrowserModelFilter*
inf_gtk_browser_model_filter_new(InfGtkBrowserModel* child_model)
{
  GObject* object;

  object = g_object_new(
    INF_GTK_TYPE_BROWSER_MODEL_FILTER,
    "child-model", child_model,
    NULL
  );

  return INF_GTK_BROWSER_MODEL_FILTER(object);
}

/* vim:set et sw=2 ts=2: */
