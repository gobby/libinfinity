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

#include <infinoted/infinoted-plugin-manager.h>

#include <gmodule.h>

typedef struct _InfinotedPluginInstance InfinotedPluginInstance;
struct _InfinotedPluginInstance {
  GModule* module;
  const InfinotedPlugin* plugin;
};

typedef struct _InfinotedPluginManagerForeachConnectionData
  InfinotedPluginManagerForeachConnectionData;
struct _InfinotedPluginManagerForeachConnectionData {
  InfinotedPluginManager* manager;
  InfinotedPluginInstance* instance;
};

typedef void(*InfinotedPluginManagerWalkDirectoryFunc)(
  InfinotedPluginManager*,
  InfinotedPluginInstance*,
  const InfBrowserIter*,
  InfSessionProxy*);

static gpointer
infinoted_plugin_manager_hash(gpointer first,
                              gpointer second)
{
  /* This function creates a hash out of two pointer values */
  guintptr hash = 5381;
  hash = hash * 33 + (guintptr)first;
  hash = hash * 33 + (guintptr)second;
  return (gpointer)hash;
}

static void
infinoted_plugin_manager_add_connection(InfinotedPluginManager* manager,
                                        InfinotedPluginInstance* instance,
                                        InfXmlConnection* connection)
{
  gpointer plugin_info;
  gpointer hash;
  gpointer connection_info;

  plugin_info = instance+1;
  hash = infinoted_plugin_manager_hash(plugin_info, connection);
  g_assert(g_hash_table_lookup(manager->connections, hash) == NULL);

  if(instance->plugin->connection_info_size > 0)
  {
    connection_info = g_slice_alloc(instance->plugin->connection_info_size);
    g_hash_table_insert(manager->connections, hash, connection_info);
  }

  if(instance->plugin->on_connection_added != NULL)
  {
    instance->plugin->on_connection_added(
      connection,
      plugin_info,
      connection_info
    );
  }
}

static void
infinoted_plugin_manager_remove_connection(InfinotedPluginManager* manager,
                                           InfinotedPluginInstance* instance,
                                           InfXmlConnection* connection)
{
  gpointer plugin_info;
  gpointer hash;
  gpointer connection_info;

  plugin_info = instance+1;
  hash = infinoted_plugin_manager_hash(plugin_info, connection);
  
  connection_info = g_hash_table_lookup(manager->connections, hash);

  g_assert(
    instance->plugin->connection_info_size == 0 || connection_info != NULL
  );

  if(instance->plugin->on_connection_removed != NULL)
  {
    instance->plugin->on_connection_removed(
      connection,
      plugin_info,
      connection_info
    );
  }

  if(instance->plugin->connection_info_size > 0)
  {
    g_hash_table_remove(manager->connections, hash);
    g_slice_free1(instance->plugin->connection_info_size, connection_info);
  }
}

static gboolean
infinoted_plugin_manager_check_session_type(InfinotedPluginInstance* instance,
                                            InfSessionProxy* proxy)
{
  GType session_type;
  InfSession* session;
  gboolean result;

  if(instance->plugin->session_type == NULL)
    return TRUE;

  /* If the type was not registered yet the passed session cannot have the
   * correct type. */
  session_type = g_type_from_name(instance->plugin->session_type);
  if(session_type == 0)
    return FALSE;

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  result = g_type_is_a(G_TYPE_FROM_INSTANCE(session), session_type);
  g_object_unref(session);

  return result;
}

static void
infinoted_plugin_manager_add_session(InfinotedPluginManager* manager,
                                     InfinotedPluginInstance* instance,
                                     const InfBrowserIter* iter,
                                     InfSessionProxy* proxy)
{
  gpointer plugin_info;
  gpointer hash;
  gpointer session_info;

  if(infinoted_plugin_manager_check_session_type(instance, proxy))
  {
    plugin_info = instance+1;
    hash = infinoted_plugin_manager_hash(plugin_info, proxy);
    g_assert(g_hash_table_lookup(manager->sessions, hash) == NULL);

    if(instance->plugin->session_info_size > 0)
    {
      session_info = g_slice_alloc(instance->plugin->session_info_size);
      g_hash_table_insert(manager->sessions, hash, session_info);
    }

    if(instance->plugin->on_session_added != NULL)
    {
      instance->plugin->on_session_added(
        iter,
        proxy,
        plugin_info,
        session_info
      );
    }
  }
}

static void
infinoted_plugin_manager_remove_session(InfinotedPluginManager* manager,
                                        InfinotedPluginInstance* instance,
                                        const InfBrowserIter* iter,
                                        InfSessionProxy* proxy)
{
  gpointer plugin_info;
  gpointer hash;
  gpointer session_info;

  if(infinoted_plugin_manager_check_session_type(instance, proxy))
  {
    plugin_info = instance+1;
    hash = infinoted_plugin_manager_hash(plugin_info, proxy);
    
    session_info = g_hash_table_lookup(manager->sessions, hash);

    g_assert(
      instance->plugin->session_info_size == 0 || session_info != NULL
    );

    if(instance->plugin->on_session_removed != NULL)
    {
      instance->plugin->on_session_removed(
        iter,
        proxy,
        plugin_info,
        session_info
      );
    }

    if(instance->plugin->session_info_size > 0)
    {
      g_hash_table_remove(manager->sessions, hash);
      g_slice_free1(instance->plugin->session_info_size, session_info);
    }
  }
}

static void
infinoted_plugin_manager_walk_directory(
  InfinotedPluginManager* manager,
  const InfBrowserIter* iter,
  InfinotedPluginInstance* instance,
  InfinotedPluginManagerWalkDirectoryFunc func)
{
  /* This function walks the whole directory tree recursively and
   * registers running sessions with the given plugin instance. */
  InfBrowser* browser;
  InfBrowserIter child;
  InfSessionProxy* proxy;

  browser = INF_BROWSER(manager->directory);
  if(inf_browser_is_subdirectory(browser, iter) == TRUE)
  {
    if(inf_browser_get_explored(browser, iter) == TRUE)
    {
      child = *iter;
      inf_browser_get_child(browser, &child);
      do
      {
        infinoted_plugin_manager_walk_directory(manager, &child, instance, func);
      } while(inf_browser_get_next(browser, &child));
    }
  }
  else
  {
    proxy = inf_browser_get_session(browser, iter);
    if(proxy != NULL)
    {
      func(manager, instance, iter, proxy);
    }
  }
}

static void
infinoted_plugin_manager_load_plugin_foreach_connection_func(
  InfXmlConnection* connection,
  gpointer user_data)
{
  InfinotedPluginManagerForeachConnectionData* data;
  data = (InfinotedPluginManagerForeachConnectionData*)user_data;

  infinoted_plugin_manager_add_connection(
    data->manager,
    data->instance,
    connection
  );
}

static void
infinoted_plugin_manager_unload_plugin_foreach_connection_func(
  InfXmlConnection* connection,
  gpointer user_data)
{
  InfinotedPluginManagerForeachConnectionData* data;
  data = (InfinotedPluginManagerForeachConnectionData*)user_data;

  infinoted_plugin_manager_remove_connection(
    data->manager,
    data->instance,
    connection
  );
}

static gboolean
infinoted_plugin_manager_load_plugin(InfinotedPluginManager* manager,
                                     const gchar* plugin_path,
                                     const gchar* plugin_name,
                                     GKeyFile* key_file,
                                     GError** error)
{
  gchar* plugin_basename;
  gchar* plugin_filename;

  GModule* module;
  const InfinotedPlugin* plugin;
  InfinotedPluginInstance* instance;

  gboolean result;
  GError* local_error;

  InfBrowserIter root;
  InfinotedPluginManagerForeachConnectionData data;

  plugin_basename = g_strdup_printf(
    "libinfinoted-plugin-%s.%s",
    plugin_name,
    G_MODULE_SUFFIX
  );

  plugin_filename = g_build_filename(plugin_path, plugin_basename, NULL);
  g_free(plugin_basename);

  module = g_module_open(plugin_filename, G_MODULE_BIND_LOCAL);
  g_free(plugin_filename);

  if(module == NULL)
  {
    g_set_error(
      error,
      infinoted_plugin_manager_error_quark(),
      INFINOTED_PLUGIN_MANAGER_ERROR_OPEN_FAILED,
      "%s",
      g_module_error()
    );

    
    return FALSE;
  }

  if(g_module_symbol(module, "INFINOTED_PLUGIN", (gpointer*)&plugin) == FALSE)
  {
    g_set_error(
      error,
      infinoted_plugin_manager_error_quark(),
      INFINOTED_PLUGIN_MANAGER_ERROR_NO_ENTRY_POINT,
      "%s",
      g_module_error()
    );
    
    g_module_close(module);
    return FALSE;
  }

  instance = g_malloc(sizeof(InfinotedPluginInstance) + plugin->info_size);
  instance->module = module;
  instance->plugin = plugin;

  /* Call on_info_initialize, allowing the plugin to set default values */
  if(plugin->on_info_initialize != NULL)
    plugin->on_info_initialize(instance+1);

  /* Next, parse options from keyfile */
  if(plugin->options != NULL)
  {
    local_error = NULL;

    result = infinoted_parameter_load_from_key_file(
      plugin->options,
      key_file,
      plugin->name,
      instance+1,
      &local_error
    );
    
    if(result == FALSE)
    {
      g_free(instance);
      g_module_close(module);

      g_propagate_prefixed_error(
        error,
        local_error,
        "Failed to initialize plugin \"%s\": ",
        plugin_name
      );

      return FALSE;
    }
  }

  /* Finally, call on_initialize, which allows the plugin to initialize
   * itself with the plugin options. */
  if(plugin->on_initialize != NULL)
  {
    local_error = NULL;

    result = plugin->on_initialize(manager, instance+1, &local_error);

    if(local_error != NULL)
    {
      g_free(instance);
      g_module_close(module);

      g_propagate_prefixed_error(
        error,
        local_error,
        "Failed to initialize plugin \"%s\": ",
        plugin_name
      );

      return FALSE;
    }
  }

  /* Register initial connections with plugin */
  data.manager = manager;
  data.instance = instance;
  infd_directory_foreach_connection(
    manager->directory,
    infinoted_plugin_manager_load_plugin_foreach_connection_func,
    &data
  );

  /* Register initial sessions with plugin */
  inf_browser_get_root(INF_BROWSER(manager->directory), &root);
  infinoted_plugin_manager_walk_directory(
    manager,
    &root,
    instance,
    infinoted_plugin_manager_add_session
  );

  manager->plugins = g_slist_prepend(manager->plugins, instance);

  return TRUE;
}

static void
infinoted_plugin_manager_unload_plugin(InfinotedPluginManager* manager,
                                       InfinotedPluginInstance* instance)
{
  InfinotedPluginManagerForeachConnectionData data;
  InfBrowserIter root;

  manager->plugins = g_slist_remove(manager->plugins, instance);

  /* Unregister all sessions with the plugin */
  inf_browser_get_root(INF_BROWSER(manager->directory), &root);
  infinoted_plugin_manager_walk_directory(
    manager,
    &root,
    instance,
    infinoted_plugin_manager_remove_session
  );

  /* Unregister all connections with the plugin */
  data.manager = manager;
  data.instance = instance;
  infd_directory_foreach_connection(
    manager->directory,
    infinoted_plugin_manager_unload_plugin_foreach_connection_func,
    &data
  );

  if(instance->plugin->on_deinitialize != NULL)
    instance->plugin->on_deinitialize(instance+1);

  g_module_close(instance->module);
  
  g_free(instance);
}

static void
infinoted_plugin_manager_connection_added_cb(InfdDirectory* directory,
                                             InfXmlConnection* connection,
                                             gpointer user_data)
{
  InfinotedPluginManager* manager;
  GSList* item;

  manager = (InfinotedPluginManager*)user_data;

  for(item = manager->plugins; item != NULL; item = item->next)
  {
    infinoted_plugin_manager_add_connection(
      manager,
      (InfinotedPluginInstance*)item->data,
      connection
    );
  }
}

static void
infinoted_plugin_manager_connection_removed_cb(InfdDirectory* directory,
                                               InfXmlConnection* connection,
                                               gpointer user_data)
{
  InfinotedPluginManager* manager;
  GSList* item;

  manager = (InfinotedPluginManager*)user_data;

  for(item = manager->plugins; item != NULL; item = item->next)
  {
    infinoted_plugin_manager_remove_connection(
      manager,
      (InfinotedPluginInstance*)item->data,
      connection
    );
  }
}

static void
infinoted_plugin_manager_subscribe_session_cb(InfBrowser* browser,
                                              const InfBrowserIter* iter,
                                              InfSessionProxy* proxy,
                                              InfRequest* request,
                                              gpointer user_data)
{
  InfinotedPluginManager* manager;
  GSList* item;

  manager = (InfinotedPluginManager*)user_data;

  for(item = manager->plugins; item != NULL; item = item->next)
  {
    infinoted_plugin_manager_add_session(
      manager,
      (InfinotedPluginInstance*)item->data,
      iter,
      proxy
    );
  }
}

static void
infinoted_plugin_manager_unsubscribe_session_cb(InfBrowser* browser,
                                                const InfBrowserIter* iter,
                                                InfSessionProxy* proxy,
                                                InfRequest* request,
                                                gpointer user_data)
{
  InfinotedPluginManager* manager;
  GSList* item;

  manager = (InfinotedPluginManager*)user_data;

  for(item = manager->plugins; item != NULL; item = item->next)
  {
    infinoted_plugin_manager_remove_session(
      manager,
      (InfinotedPluginInstance*)item->data,
      iter,
      proxy
    );
  }
}

/**
 * infinoted_plugin_manager_new:
 * @directory: The #InfdDirectory on which plugins should operate.
 * @log: The #InfinotedLog to write log messages to.
 * @plugin_path: A path to the plugin modules.
 * @plugins: A list of plugins to load, or %NULL.
 * @options: A #GKeyFile with configuration options for the plugins.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Creates a new #InfinotedPluginManager and loads all plugins specified
 * in @plugins from the location at @plugin_path. If loading one of the
 * module fails the function sets @error and returns %NULL. If @plugins is
 * %NULL, no plugins are initially loaded.
 *
 * Returns: A new #InfinotedPluginManager, or %NULL on error. Free with
 * infinoted_plugin_manager_free() when no longer needed.
 */
InfinotedPluginManager*
infinoted_plugin_manager_new(InfdDirectory* directory,
                             InfinotedLog* log,
                             const gchar* plugin_path,
                             const gchar* const* plugins,
                             GKeyFile* options,
                             GError** error)
{
  InfinotedPluginManager* plugin_manager;
  const gchar* const* plugin;
  gboolean result;

  plugin_manager = g_slice_new(InfinotedPluginManager);

  plugin_manager->directory = directory;
  plugin_manager->log = log;
  plugin_manager->path = g_strdup(plugin_path);
  plugin_manager->plugins = NULL;
  plugin_manager->connections = g_hash_table_new(NULL, NULL);
  plugin_manager->sessions = g_hash_table_new(NULL, NULL);

  g_object_ref(directory);
  g_object_ref(log);

  g_signal_connect_after(
    G_OBJECT(directory),
    "connection-added",
    G_CALLBACK(infinoted_plugin_manager_connection_added_cb),
    plugin_manager
  );

  g_signal_connect_after(
    G_OBJECT(directory),
    "connection-removed",
    G_CALLBACK(infinoted_plugin_manager_connection_removed_cb),
    plugin_manager
  );

  g_signal_connect_after(
    G_OBJECT(directory),
    "subscribe-session",
    G_CALLBACK(infinoted_plugin_manager_subscribe_session_cb),
    plugin_manager
  );

  g_signal_connect_after(
    G_OBJECT(directory),
    "unsubscribe-session",
    G_CALLBACK(infinoted_plugin_manager_unsubscribe_session_cb),
    plugin_manager
  );

  if(plugins != NULL)
  {
    for(plugin = plugins; *plugin != NULL; ++plugin)
    {
      result = infinoted_plugin_manager_load_plugin(
        plugin_manager,
        plugin_path,
        *plugin,
        options,
        error
      );

      if(result == FALSE)
      {
        infinoted_plugin_manager_free(plugin_manager);
        return NULL;
      }
    }
  }

  return plugin_manager;
}

/**
 * infinoted_plugin_manager_free:
 * @manager: A #InfinotedPluginManager.
 *
 * Unloads all plugins and releases all resources associated with @manager.
 */
void
infinoted_plugin_manager_free(InfinotedPluginManager* manager)
{
  GSList* item;

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(manager->directory),
    G_CALLBACK(infinoted_plugin_manager_connection_added_cb),
    manager
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(manager->directory),
    G_CALLBACK(infinoted_plugin_manager_connection_removed_cb),
    manager
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(manager->directory),
    G_CALLBACK(infinoted_plugin_manager_subscribe_session_cb),
    manager
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(manager->directory),
    G_CALLBACK(infinoted_plugin_manager_unsubscribe_session_cb),
    manager
  );

  while(manager->plugins != NULL)
  {
    infinoted_plugin_manager_unload_plugin(
      manager,
      (InfinotedPluginInstance*)manager->plugins->data
    );
  }

  g_assert(g_hash_table_size(manager->connections) == 0);
  g_hash_table_unref(manager->connections);

  g_assert(g_hash_table_size(manager->sessions) == 0);
  g_hash_table_unref(manager->sessions);

  g_free(manager->path);

  g_object_unref(manager->directory);
  g_object_unref(manager->log);

  g_slice_free(InfinotedPluginManager, manager);
}

/**
 * infinoted_plugin_manager_get_directory:
 * @manager: A #InfinotedPluginManager.
 *
 * Returns the #InfdDirectory used by the plugin manager.
 *
 * Returns: A #InfdDirectory owned by the plugin manager.
 */
InfdDirectory*
infinoted_plugin_manager_get_directory(InfinotedPluginManager* manager)
{
  return manager->directory;
}

/**
 * infinoted_plugin_manager_get_io:
 * @manager: A #InfinotedPluginManager.
 *
 * Returns the #InfIo of the #InfdDirectory used by the plugin manager.
 *
 * Returns: A #InfIo owned by the plugin manager.
 */
InfIo*
infinoted_plugin_manager_get_io(InfinotedPluginManager* manager)
{
  return infd_directory_get_io(manager->directory);
}

/**
 * infinoted_plugin_manager_get_log:
 * @manager: A #InfinotedPluginManager.
 *
 * Returns the #InfinotedLog that the plugin manager and the plugins do
 * write log messages to.
 *
 * Returns: A #InfinotedLog owned by the plugin manager.
 */
InfinotedLog*
infinoted_plugin_manager_get_log(InfinotedPluginManager* manager)
{
  return manager->log;
}

/**
 * infinoted_plugin_manager_error_quark:
 *
 * Returns the #GQuark for errors from the InfinotedPluginManager module.
 *
 * Returns: The error domain for the InfinotedPluginManager module.
 */
GQuark
infinoted_plugin_manager_error_quark(void)
{
  return g_quark_from_static_string("INFINOTED_PLUGIN_MANAGER_ERROR");
}

/**
 * infinoted_plugin_manager_get_connection_info:
 * @mgr: A #InfinotedPluginManager.
 * @plugin_info: The @plugin_info pointer of a plugin instance.
 * @connection: The #InfXmlConnection for which to retrieve plugin data.
 *
 * Queries the connection-specfic plugin data for the plugin instance
 * @plugin_info. Returns %NULL if no such object exists, i.e. when the
 * plugin's @connection_info_size is set to 0.
 *
 * Returns: A pointer to the connection-specific plugin data, or %NULL.
 */
gpointer
infinoted_plugin_manager_get_connection_info(InfinotedPluginManager* mgr,
                                             gpointer plugin_info,
                                             InfXmlConnection* connection)
{
  return g_hash_table_lookup(
    mgr->connections,
    infinoted_plugin_manager_hash(plugin_info, connection)
  );
}

/**
 * infinoted_plugin_manager_get_connection_info:
 * @mgr: A #InfinotedPluginManager.
 * @plugin_info: The @plugin_info pointer of a plugin instance.
 * @proxy: The #InfSessionProxy for which to retrieve plugin data.
 *
 * Queries the session-specfic plugin data for the plugin instance
 * @plugin_info. Returns %NULL if no such object exists, i.e. when the
 * plugin's @session_info_size is set to 0.
 *
 * Returns: A pointer to the session-specific plugin data, or %NULL.
 */
gpointer
infinoted_plugin_manager_get_session_info(InfinotedPluginManager* mgr,
                                          gpointer plugin_info,
                                          InfSessionProxy* proxy)
{
  return g_hash_table_lookup(
    mgr->sessions,
    infinoted_plugin_manager_hash(plugin_info, proxy)
  );
}

/* vim:set et sw=2 ts=2: */
