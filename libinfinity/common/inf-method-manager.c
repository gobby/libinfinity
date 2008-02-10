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

#include <libinfinity/common/inf-method-manager.h>

#include <gmodule.h>

#include <string.h>

typedef struct _InfMethodManagerPrivate InfMethodManagerPrivate;
struct _InfMethodManagerPrivate {
  gchar* search_path;
  GSList* methods;
};

enum {
  PROP_0,

  PROP_SEARCH_PATH
};

#define INF_METHOD_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_METHOD_MANAGER, InfMethodManagerPrivate))

static GObjectClass* parent_class;

static void
inf_method_manager_init(GTypeInstance* instance,
                        gpointer g_class)
{
  InfMethodManager* connection;
  InfMethodManagerPrivate* priv;

  connection = INF_METHOD_MANAGER(instance);
  priv = INF_METHOD_MANAGER_PRIVATE(connection);

  priv->search_path = NULL;
  priv->methods = NULL;
}

static GObject*
inf_method_manager_constructor(GType type,
                               guint n_construct_properties,
                               GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfMethodManager* manager;
  InfMethodManagerPrivate* priv;

  GDir* dir;
  const gchar* filename;
  gchar* path;
  GModule* module;
  InfConnectionManagerMethodDesc* desc;
  const InfConnectionManagerMethodDesc* existing_desc;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  manager = INF_METHOD_MANAGER(object);
  priv = INF_METHOD_MANAGER_PRIVATE(manager);

  if(priv->search_path != NULL)
  {
    /* TODO: Error handling */
    dir = g_dir_open(priv->search_path, 0, NULL);
    if(dir != NULL)
    {
      while( (filename = g_dir_read_name(dir)) != NULL)
      {
        /* Ignore libtool .la files and other uninteresting stuff */
        if(!g_str_has_suffix(filename, G_MODULE_SUFFIX))
          continue;

        path = g_build_filename(priv->search_path, filename, NULL);
        module = g_module_open(path, G_MODULE_BIND_LOCAL);
        if(module != NULL)
        {
          if(g_module_symbol(module, "INF_METHOD_PLUGIN", (gpointer*)&desc))
          {
            existing_desc = inf_method_manager_lookup_method(
              manager,
              desc->network,
              desc->name
            );

            if(existing_desc == NULL)
            {
              inf_method_manager_add_method(manager, desc);
              g_module_make_resident(module);
            }
            else
            {
              g_warning(
                "Failed to load method `%s': Method with network `%s' and "
                "name `%s' already loaded.",
                path,
                existing_desc->network,
                existing_desc->name
              );
            }
          }

          g_module_close(module);
        }

        g_free(path);
      }

      g_dir_close(dir);
    }
  }

  return object;
}

static void
inf_method_manager_dispose(GObject* object)
{
  InfMethodManager* connection;
  InfMethodManagerPrivate* priv;

  connection = INF_METHOD_MANAGER(object);
  priv = INF_METHOD_MANAGER_PRIVATE(connection);

  g_slist_free(priv->methods);
  priv->methods = NULL;

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_method_manager_finalize(GObject* object)
{
  InfMethodManager* connection;
  InfMethodManagerPrivate* priv;

  connection = INF_METHOD_MANAGER(object);
  priv = INF_METHOD_MANAGER_PRIVATE(connection);

  g_free(priv->search_path);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_method_manager_set_property(GObject* object,
                                guint prop_id,
                                const GValue* value,
                                GParamSpec* pspec)
{
  InfMethodManager* manager;
  InfMethodManagerPrivate* priv;

  manager = INF_METHOD_MANAGER(object);
  priv = INF_METHOD_MANAGER_PRIVATE(manager);

  switch(prop_id)
  {
  case PROP_SEARCH_PATH:
    g_free(priv->search_path);
    priv->search_path = g_value_dup_string(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_method_manager_get_property(GObject* object,
                                guint prop_id,
                                GValue* value,
                                GParamSpec* pspec)
{
  InfMethodManager* manager;
  InfMethodManagerPrivate* priv;

  manager = INF_METHOD_MANAGER(object);
  priv = INF_METHOD_MANAGER_PRIVATE(manager);

  switch(prop_id)
  {
  case PROP_SEARCH_PATH:
    g_value_set_string(value, priv->search_path);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_method_manager_class_init(gpointer g_class,
                              gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfMethodManagerPrivate));

  object_class->constructor = inf_method_manager_constructor;
  object_class->dispose = inf_method_manager_dispose;
  object_class->finalize = inf_method_manager_finalize;
  object_class->set_property = inf_method_manager_set_property;
  object_class->get_property = inf_method_manager_get_property;

  g_object_class_install_property(
    object_class,
    PROP_SEARCH_PATH,
    g_param_spec_string(
      "search-path",
      "Search path",
      "Path to search for modules",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

GType
inf_method_manager_get_type(void)
{
  static GType method_manager_type = 0;

  if(!method_manager_type)
  {
    static const GTypeInfo method_manager_type_info = {
      sizeof(InfMethodManagerClass),  /* class_size */
      NULL,                           /* base_init */
      NULL,                           /* base_finalize */
      inf_method_manager_class_init,  /* class_init */
      NULL,                           /* class_finalize */
      NULL,                           /* class_data */
      sizeof(InfMethodManager),       /* instance_size */
      0,                              /* n_preallocs */
      inf_method_manager_init,        /* instance_init */
      NULL                            /* value_table */
    };

    method_manager_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfMethodManager",
      &method_manager_type_info,
      0
    );
  }

  return method_manager_type;
}

/** inf_method_manager_get_default:
 *
 * Returns the default method manager with a standard search path.
 *
 * Returns: A #InfMethodManager.
 **/
InfMethodManager*
inf_method_manager_get_default(void)
{
  static InfMethodManager* manager = NULL;

  /* TODO: Thread safety */
  if(manager == NULL)
    manager = inf_method_manager_new(METHODS_PATH);

  return manager;
}

/** inf_method_manager_new:
 *
 * @search_path: The path to search for method modules, or %NULL.
 *
 * Creates a new #InfMethodManager loading its modules from @search_path.
 * @search_path might be %NULL in which case no method modules are loaded.
 * You can use inf_method_manager_add_method() in this case to add your own
 * methods lateron.
 *
 * Returns: A #InfMethodManager.
 **/
InfMethodManager*
inf_method_manager_new(const gchar* search_path)
{
  GObject* object;
  object = g_object_new(
    INF_TYPE_METHOD_MANAGER,
    "search-path", search_path,
    NULL
  );

  return INF_METHOD_MANAGER(object);
}

/** inf_method_manager_add_method:
 *
 * @manager: A #InfMethodManager.
 * @method: A #InfConnectionManagerMethodDesc.
 *
 * Adds a new communication method to @manager. Its name must be unique within
 * its network.
 **/
void
inf_method_manager_add_method(InfMethodManager* manager,
                              const InfConnectionManagerMethodDesc* method)
{
  InfMethodManagerPrivate* priv;

  g_return_if_fail(INF_IS_METHOD_MANAGER(manager));
  g_return_if_fail(method != NULL);
  g_return_if_fail(
    inf_method_manager_lookup_method(
      manager,
      method->network,
      method->name
    ) == NULL
  );

  priv = INF_METHOD_MANAGER_PRIVATE(manager);
  priv->methods = g_slist_prepend(priv->methods, (gpointer)method);
}

/** inf_connection_manager_lookup_method:
 *
 * @manager: A #InfMethodManager.
 * @network: The network for which to find a method.
 * @method_name: The name of the method to look up.
 *
 * Returns the method with the given name for the given network, or %NULL if
 * there is no such method.
 *
 * Returns: A #InfConnectionManagerMethodDesc, or %NULL.
 **/
const InfConnectionManagerMethodDesc*
inf_method_manager_lookup_method(InfMethodManager* manager,
                                 const gchar* network,
                                 const gchar* method_name)
{
  InfMethodManagerPrivate* priv;
  InfConnectionManagerMethodDesc* desc;
  GSList* item;

  g_return_val_if_fail(INF_IS_METHOD_MANAGER(manager), NULL);
  g_return_val_if_fail(network != NULL, NULL);
  g_return_val_if_fail(method_name != NULL, NULL);

  priv = INF_METHOD_MANAGER_PRIVATE(manager);

  for(item = priv->methods; item != NULL; item = g_slist_next(item))
  {
    desc = (InfConnectionManagerMethodDesc*)item->data;
    if(strcmp(desc->network, network) == 0 &&
       strcmp(desc->name, method_name) == 0)
    {
      return desc;
    }
  }

  return NULL;
}

/** inf_connection_manager_list_methods_with_name:
 *
 * @manager: A #InfMethodManager.
 * @name: A method name.
 *
 * Returns a list of all methods with the given name. All these methods have
 * a different network.
 *
 * Returns: A list of all methods with name @name. Free with g_slist_free().
 **/
GSList*
inf_method_manager_list_methods_with_name(InfMethodManager* manager,
                                          const gchar* name)
{
  InfMethodManagerPrivate* priv;
  InfConnectionManagerMethodDesc* desc;
  GSList* item;
  GSList* result;

  g_return_val_if_fail(INF_IS_METHOD_MANAGER(manager), NULL);
  g_return_val_if_fail(name != NULL, NULL);

  priv = INF_METHOD_MANAGER_PRIVATE(manager);
  result = NULL;

  for(item = priv->methods; item != NULL; item = g_slist_next(item))
  {
    desc = (InfConnectionManagerMethodDesc*)item->data;
    if(strcmp(desc->name, name) == 0)
      result = g_slist_prepend(result, desc);
  }

  return result;
}

/** inf_connection_manager_list_methods_with_network:
 *
 * @manager: A #InfMethodManager.
 * @network: A network name.
 *
 * Returns a list of all methods with the given network. All these methods
 * have a different name.
 *
 * Returns: A list of all methods with network @network.
 * Free with g_slist_free().
 **/
GSList*
inf_method_manager_list_methods_with_network(InfMethodManager* manager,
                                             const gchar* network)
{
  InfMethodManagerPrivate* priv;
  InfConnectionManagerMethodDesc* desc;
  GSList* item;
  GSList* result;

  g_return_val_if_fail(INF_IS_METHOD_MANAGER(manager), NULL);
  g_return_val_if_fail(network != NULL, NULL);

  priv = INF_METHOD_MANAGER_PRIVATE(manager);
  result = NULL;

  for(item = priv->methods; item != NULL; item = g_slist_next(item))
  {
    desc = (InfConnectionManagerMethodDesc*)item->data;
    if(strcmp(desc->network, network) == 0)
      result = g_slist_prepend(result, desc);
  }

  return result;
}

/** inf_connection_manager_list_all_methods:
 *
 * @manager: A #InfMethodManager.
 *
 * List all available methods.
 *
 * Returns: A list of all methods. It is owned by the #InfMethodManager, so
 * you do not need to free it.
 **/
GSList*
inf_method_manager_list_all_methods(InfMethodManager* manager)
{
  g_return_val_if_fail(INF_IS_METHOD_MANAGER(manager), NULL);
  return INF_METHOD_MANAGER_PRIVATE(manager)->methods;
}

/* vim:set et sw=2 ts=2: */
