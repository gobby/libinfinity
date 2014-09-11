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
 * SECTION:infinoted-plugin-manager
 * @title: InfinotedPluginManager
 * @short_description: Loads and propagates events to infinoted plugins.
 * @include: infinoted/infinoted-plugin-manager.h
 * @stability: Unstable
 *
 * #InfinotedPluginManager handles the loading of plugins for the infinoted
 * server. It initializes and deinitializes plugins, and it makes callbacks
 * when connections or sessions are added or removed. Furthermore, it provides
 * an interface for plugins to obtain and interact with the server itself,
 * most notable its #InfdDirectory instance.
 */

#include <infinoted/infinoted-plugin-manager.h>
#include <libinfinity/inf-signals.h>
#include <libinfinity/inf-i18n.h>

#include <gmodule.h>

typedef struct _InfinotedPluginManagerPrivate InfinotedPluginManagerPrivate;
struct _InfinotedPluginManagerPrivate {
  InfdDirectory* directory;
  InfinotedLog* log;
  InfCertificateCredentials* credentials;
  gchar* path;

  GSList* plugins;

  GHashTable* connections; /* plugin + connection -> PluginConnectionInfo */
  GHashTable* sessions; /* plugin + session -> PluginSessionInfo */
};

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

enum {
  PROP_0,

  PROP_DIRECTORY,
  PROP_LOG,
  PROP_CREDENTIALS,
  PROP_PATH
};

#define INFINOTED_PLUGIN_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFINOTED_TYPE_PLUGIN_MANAGER, InfinotedPluginManagerPrivate))

G_DEFINE_TYPE_WITH_CODE(InfinotedPluginManager, infinoted_plugin_manager, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfinotedPluginManager))

static gpointer
infinoted_plugin_manager_hash(gpointer first,
                              gpointer second)
{
  /* This function creates a hash out of two pointer values */
  /* TODO: Switch to guintptr with glib 2.18 */
  gsize hash = 5381;
  hash = hash * 33 + (gsize)first;
  hash = hash * 33 + (gsize)second;
  return (gpointer)hash;
}

static void
infinoted_plugin_manager_add_connection(InfinotedPluginManager* manager,
                                        InfinotedPluginInstance* instance,
                                        InfXmlConnection* connection)
{
  InfinotedPluginManagerPrivate* priv;
  gpointer plugin_info;
  gpointer hash;
  gpointer connection_info;

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  plugin_info = instance+1;
  hash = infinoted_plugin_manager_hash(plugin_info, connection);
  g_assert(g_hash_table_lookup(priv->connections, hash) == NULL);

  if(instance->plugin->connection_info_size > 0)
  {
    connection_info = g_slice_alloc(instance->plugin->connection_info_size);
    g_hash_table_insert(priv->connections, hash, connection_info);
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
  InfinotedPluginManagerPrivate* priv;
  gpointer plugin_info;
  gpointer hash;
  gpointer connection_info;

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  plugin_info = instance+1;
  hash = infinoted_plugin_manager_hash(plugin_info, connection);
  
  connection_info = g_hash_table_lookup(priv->connections, hash);

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
    g_hash_table_remove(priv->connections, hash);
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
  InfinotedPluginManagerPrivate* priv;
  gpointer plugin_info;
  gpointer hash;
  gpointer session_info;

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  if(infinoted_plugin_manager_check_session_type(instance, proxy))
  {
    plugin_info = instance+1;
    hash = infinoted_plugin_manager_hash(plugin_info, proxy);
    g_assert(g_hash_table_lookup(priv->sessions, hash) == NULL);

    if(instance->plugin->session_info_size > 0)
    {
      session_info = g_slice_alloc(instance->plugin->session_info_size);
      g_hash_table_insert(priv->sessions, hash, session_info);
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
  InfinotedPluginManagerPrivate* priv;
  gpointer plugin_info;
  gpointer hash;
  gpointer session_info;

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  if(infinoted_plugin_manager_check_session_type(instance, proxy))
  {
    plugin_info = instance+1;
    hash = infinoted_plugin_manager_hash(plugin_info, proxy);
    
    session_info = g_hash_table_lookup(priv->sessions, hash);

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
      g_hash_table_remove(priv->sessions, hash);
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
  InfinotedPluginManagerPrivate* priv;
  InfBrowser* browser;
  InfBrowserIter child;
  InfSessionProxy* proxy;

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);
  browser = INF_BROWSER(priv->directory);
  if(inf_browser_is_subdirectory(browser, iter) == TRUE)
  {
    if(inf_browser_get_explored(browser, iter) == TRUE)
    {
      child = *iter;
      if(inf_browser_get_child(browser, &child))
      {
        do
        {
          infinoted_plugin_manager_walk_directory(
            manager,
            &child,
            instance,
            func
          );
        } while(inf_browser_get_next(browser, &child));
      }
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
  InfinotedPluginManagerPrivate* priv;
  gchar* plugin_basename;
  gchar* plugin_filename;

  GModule* module;
  const InfinotedPlugin* plugin;
  InfinotedPluginInstance* instance;

  gboolean result;
  GError* local_error;

  InfBrowserIter root;
  InfinotedPluginManagerForeachConnectionData data;

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

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
    g_set_error_literal(
      error,
      infinoted_plugin_manager_error_quark(),
      INFINOTED_PLUGIN_MANAGER_ERROR_OPEN_FAILED,
      g_module_error()
    );

    
    return FALSE;
  }

  if(g_module_symbol(module, "INFINOTED_PLUGIN", (gpointer*)&plugin) == FALSE)
  {
    g_set_error_literal(
      error,
      infinoted_plugin_manager_error_quark(),
      INFINOTED_PLUGIN_MANAGER_ERROR_NO_ENTRY_POINT,
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
      if(instance->plugin->on_deinitialize != NULL)
        instance->plugin->on_deinitialize(instance+1);

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
    priv->directory,
    infinoted_plugin_manager_load_plugin_foreach_connection_func,
    &data
  );

  /* Register initial sessions with plugin */
  inf_browser_get_root(INF_BROWSER(priv->directory), &root);
  infinoted_plugin_manager_walk_directory(
    manager,
    &root,
    instance,
    infinoted_plugin_manager_add_session
  );

  infinoted_log_info(
    priv->log,
    _("Loaded plugin \"%s\" from \"%s\""),
    plugin_name,
    g_module_name(module)
  );

  priv->plugins = g_slist_prepend(priv->plugins, instance);

  return TRUE;
}

static void
infinoted_plugin_manager_unload_plugin(InfinotedPluginManager* manager,
                                       InfinotedPluginInstance* instance)
{
  InfinotedPluginManagerPrivate* priv;
  InfinotedPluginManagerForeachConnectionData data;
  InfBrowserIter root;

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);
  priv->plugins = g_slist_remove(priv->plugins, instance);

  /* Unregister all sessions with the plugin */
  inf_browser_get_root(INF_BROWSER(priv->directory), &root);
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
    priv->directory,
    infinoted_plugin_manager_unload_plugin_foreach_connection_func,
    &data
  );

  if(instance->plugin->on_deinitialize != NULL)
    instance->plugin->on_deinitialize(instance+1);

  infinoted_log_info(
    priv->log,
    _("Unloaded plugin \"%s\" from \"%s\""),
    instance->plugin->name,
    g_module_name(instance->module)
  );

  g_module_close(instance->module);
  g_free(instance);
}

static void
infinoted_plugin_manager_connection_added_cb(InfdDirectory* directory,
                                             InfXmlConnection* connection,
                                             gpointer user_data)
{
  InfinotedPluginManager* manager;
  InfinotedPluginManagerPrivate* priv;
  GSList* item;

  manager = (InfinotedPluginManager*)user_data;
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  for(item = priv->plugins; item != NULL; item = item->next)
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
  InfinotedPluginManagerPrivate* priv;
  GSList* item;

  manager = (InfinotedPluginManager*)user_data;
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  for(item = priv->plugins; item != NULL; item = item->next)
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
  InfinotedPluginManagerPrivate* priv;
  GSList* item;

  manager = (InfinotedPluginManager*)user_data;
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  for(item = priv->plugins; item != NULL; item = item->next)
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
  InfinotedPluginManagerPrivate* priv;
  GSList* item;

  manager = (InfinotedPluginManager*)user_data;
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  for(item = priv->plugins; item != NULL; item = item->next)
  {
    infinoted_plugin_manager_remove_session(
      manager,
      (InfinotedPluginInstance*)item->data,
      iter,
      proxy
    );
  }
}

static void
infinoted_plugin_manager_set_directory(InfinotedPluginManager* manager,
                                       InfdDirectory* directory)
{
  InfinotedPluginManagerPrivate* priv;
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  /* Directory can only be changed while no plugins are loaded. */
  g_assert(priv->plugins == NULL);

  if(priv->directory != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->directory),
      G_CALLBACK(infinoted_plugin_manager_connection_added_cb),
      manager
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->directory),
      G_CALLBACK(infinoted_plugin_manager_connection_removed_cb),
      manager
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->directory),
      G_CALLBACK(infinoted_plugin_manager_subscribe_session_cb),
      manager
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->directory),
      G_CALLBACK(infinoted_plugin_manager_unsubscribe_session_cb),
      manager
    );

    g_object_unref(priv->directory);
  }

  priv->directory = directory;

  if(directory != NULL)
  {
    g_object_ref(priv->directory);

    g_signal_connect_after(
      G_OBJECT(directory),
      "connection-added",
      G_CALLBACK(infinoted_plugin_manager_connection_added_cb),
      manager
    );

    g_signal_connect_after(
      G_OBJECT(directory),
      "connection-removed",
      G_CALLBACK(infinoted_plugin_manager_connection_removed_cb),
      manager
    );

    g_signal_connect_after(
      G_OBJECT(directory),
      "subscribe-session",
      G_CALLBACK(infinoted_plugin_manager_subscribe_session_cb),
      manager
    );

    g_signal_connect_after(
      G_OBJECT(directory),
      "unsubscribe-session",
      G_CALLBACK(infinoted_plugin_manager_unsubscribe_session_cb),
      manager
    );
  }

  g_object_notify(G_OBJECT(manager), "directory");
}

static void
infinoted_plugin_manager_init(InfinotedPluginManager* manager)
{
  InfinotedPluginManagerPrivate* priv;
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  priv->directory = NULL;
  priv->log = NULL;
  priv->credentials = NULL;
  priv->path = NULL;
  priv->plugins = NULL;
  priv->connections = g_hash_table_new(NULL, NULL);
  priv->sessions = g_hash_table_new(NULL, NULL);
}

static void
infinoted_plugin_manager_dispose(GObject* object)
{
  InfinotedPluginManager* manager;
  InfinotedPluginManagerPrivate* priv;

  manager = INFINOTED_PLUGIN_MANAGER(object);
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  while(priv->plugins != NULL)
  {
    infinoted_plugin_manager_unload_plugin(
      manager,
      (InfinotedPluginInstance*)priv->plugins->data
    );
  }

  if(priv->directory != NULL)
    infinoted_plugin_manager_set_directory(manager, NULL);

  if(priv->log != NULL)
  {
    g_object_unref(priv->log);
    priv->log = NULL;
  }

  if(priv->credentials != NULL)
  {
    inf_certificate_credentials_unref(priv->credentials);
    priv->credentials = NULL;
  }

  G_OBJECT_CLASS(infinoted_plugin_manager_parent_class)->dispose(object);
}

static void
infinoted_plugin_manager_finalize(GObject* object)
{
  InfinotedPluginManager* manager;
  InfinotedPluginManagerPrivate* priv;

  manager = INFINOTED_PLUGIN_MANAGER(object);
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  g_assert(g_hash_table_size(priv->connections) == 0);
  g_assert(g_hash_table_size(priv->sessions) == 0);

  g_hash_table_unref(priv->connections);
  g_hash_table_unref(priv->sessions);

  G_OBJECT_CLASS(infinoted_plugin_manager_parent_class)->finalize(object);
}

static void
infinoted_plugin_manager_set_property(GObject* object,
                                      guint prop_id,
                                      const GValue* value,
                                      GParamSpec* pspec)
{
  InfinotedPluginManager* manager;
  InfinotedPluginManagerPrivate* priv;

  manager = INFINOTED_PLUGIN_MANAGER(object);
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  switch(prop_id)
  {
  case PROP_DIRECTORY:
    g_assert(priv->directory == NULL); /* construct only */

    infinoted_plugin_manager_set_directory(
      manager,
      INFD_DIRECTORY(g_value_get_object(value))
    );

    break;
  case PROP_LOG:
    g_assert(priv->log == NULL); /* construct only */
    priv->log = INFINOTED_LOG(g_value_dup_object(value));
    break;
  case PROP_CREDENTIALS:
    g_assert(priv->credentials == NULL); /* construct only */
    priv->credentials = (InfCertificateCredentials*)g_value_dup_boxed(value);
    break;
  case PROP_PATH:
    /* read only */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infinoted_plugin_manager_get_property(GObject* object,
                                      guint prop_id,
                                      GValue* value,
                                      GParamSpec* pspec)
{
  InfinotedPluginManager* manager;
  InfinotedPluginManagerPrivate* priv;

  manager = INFINOTED_PLUGIN_MANAGER(object);
  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  switch(prop_id)
  {
  case PROP_DIRECTORY:
    g_value_set_object(value, priv->directory);
    break;
  case PROP_LOG:
    g_value_set_object(value, priv->log);
    break;
  case PROP_CREDENTIALS:
    g_value_set_boxed(value, priv->credentials);
    break;
  case PROP_PATH:
    g_value_set_string(value, priv->path);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infinoted_plugin_manager_class_init(
  InfinotedPluginManagerClass* manager_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(manager_class);

  object_class->dispose = infinoted_plugin_manager_dispose;
  object_class->finalize = infinoted_plugin_manager_finalize;
  object_class->set_property = infinoted_plugin_manager_set_property;
  object_class->get_property = infinoted_plugin_manager_get_property;

  g_object_class_install_property(
    object_class,
    PROP_DIRECTORY,
    g_param_spec_object(
      "directory",
      "Directory",
      "The infinote directory served by the server",
      INFD_TYPE_DIRECTORY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_LOG,
    g_param_spec_object(
      "log",
      "Log",
      "The log object into which to write log messages",
      INFINOTED_TYPE_LOG,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CREDENTIALS,
    g_param_spec_boxed(
      "credentials",
      "Credentials",
      "The server's TLS credentials",
      INF_TYPE_CERTIFICATE_CREDENTIALS,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_PATH,
    g_param_spec_string(
      "path",
      "Path",
      "The path from which plugins are loaded",
      NULL,
      G_PARAM_READABLE
    )
  );
}

/**
 * infinoted_plugin_manager_new: (constructor)
 * @directory: The #InfdDirectory on which plugins should operate.
 * @log: The #InfinotedLog to write log messages to.
 * @creds: (allow-none): The #InfCertificateCredentials used to secure data
 * transfer with the clients, or %NULL.
 *
 * Creates a new #InfinotedPluginManager with the given directory, log
 * and credentials. These three objects will be available for plugins
 * to enhance the infinoted functionality. Plugins can be loaded
 * with infinoted_plugin_manager_load().
 *
 * Returns: (transfer full): A new #InfinotedPluginManager.
 */
InfinotedPluginManager*
infinoted_plugin_manager_new(InfdDirectory* directory,
                             InfinotedLog* log,
                             InfCertificateCredentials* creds)
{
  GObject* object;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  g_return_val_if_fail(INFINOTED_IS_LOG(log), NULL);

  object = g_object_new(
    INFINOTED_TYPE_PLUGIN_MANAGER,
    "directory", directory,
    "log", log,
    "credentials", creds,
    NULL
  );

  return INFINOTED_PLUGIN_MANAGER(object);
}

/**
 * infinoted_plugin_manager_load:
 * @manager: A #InfinotedPluginManager.
 * @plugin_path: The path from which to load plugins.
 * @plugins: (array zero-terminated=1) (allow-none): A list of plugins to
 * load, or %NULL.
 * @options: A #GKeyFile with configuration options for the plugins.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Loads all plugins specified in @plugins from the location at @plugin_path.
 * If loading one of the module fails the function sets @error and returns
 * %FALSE, and the object ends up with no plugins loaded. If @plugins is
 * %NULL, no plugins are loaded.
 * 
 * If this function is called while there are already plugins loaded, all
 * existing plugins are unloaded first.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
infinoted_plugin_manager_load(InfinotedPluginManager* manager,
                              const gchar* plugin_path,
                              const gchar* const* plugins,
                              GKeyFile* options,
                              GError** error)
{
  InfinotedPluginManagerPrivate* priv;
  const gchar* const* plugin;
  gboolean result;

  g_return_val_if_fail(INFINOTED_IS_PLUGIN_MANAGER(manager), FALSE);
  g_return_val_if_fail(plugin_path != NULL, FALSE);
  g_return_val_if_fail(options != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);

  /* Unload existing plugins */
  g_free(priv->path);
  while(priv->plugins != NULL)
  {
    infinoted_plugin_manager_unload_plugin(
      manager,
      (InfinotedPluginInstance*)priv->plugins->data
    );
  }

  /* Load new plugins */
  priv->path = g_strdup(plugin_path);

  if(plugins != NULL)
  {
    for(plugin = plugins; *plugin != NULL; ++plugin)
    {
      result = infinoted_plugin_manager_load_plugin(
        manager,
        plugin_path,
        *plugin,
        options,
        error
      );

      if(result == FALSE)
      {
        while(priv->plugins != NULL)
        {
          infinoted_plugin_manager_unload_plugin(
            manager,
            (InfinotedPluginInstance*)priv->plugins->data
          );
        }

        return FALSE;
      }
    }
  }

  return TRUE;
}

/**
 * infinoted_plugin_manager_get_directory:
 * @manager: A #InfinotedPluginManager.
 *
 * Returns the #InfdDirectory used by the plugin manager.
 *
 * Returns: (transfer none): A #InfdDirectory owned by the plugin manager.
 */
InfdDirectory*
infinoted_plugin_manager_get_directory(InfinotedPluginManager* manager)
{
  g_return_val_if_fail(INFINOTED_IS_PLUGIN_MANAGER(manager), NULL);
  return INFINOTED_PLUGIN_MANAGER_PRIVATE(manager)->directory;
}

/**
 * infinoted_plugin_manager_get_io:
 * @manager: A #InfinotedPluginManager.
 *
 * Returns the #InfIo of the #InfdDirectory used by the plugin manager.
 *
 * Returns: (transfer none): A #InfIo owned by the plugin manager.
 */
InfIo*
infinoted_plugin_manager_get_io(InfinotedPluginManager* manager)
{
  InfinotedPluginManagerPrivate* priv;

  g_return_val_if_fail(INFINOTED_IS_PLUGIN_MANAGER(manager), NULL);

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(manager);
  return infd_directory_get_io(priv->directory);
}

/**
 * infinoted_plugin_manager_get_log:
 * @manager: A #InfinotedPluginManager.
 *
 * Returns the #InfinotedLog that the plugin manager and the plugins do
 * write log messages to.
 *
 * Returns: (transfer none): A #InfinotedLog owned by the plugin manager.
 */
InfinotedLog*
infinoted_plugin_manager_get_log(InfinotedPluginManager* manager)
{
  g_return_val_if_fail(INFINOTED_IS_PLUGIN_MANAGER(manager), NULL);
  return INFINOTED_PLUGIN_MANAGER_PRIVATE(manager)->log;
}

/**
 * infinoted_plugin_manager_get_credentials:
 * @manager: A #InfinotedPluginManager.
 *
 * Returns the #InfCertificateCredentials used for securing the data transfer
 * with all clients.
 *
 * Returns: (transfer none): A #InfCertificateCredentials object owned by the
 * plugin manager.
 */
InfCertificateCredentials*
infinoted_plugin_manager_get_credentials(InfinotedPluginManager* manager)
{
  g_return_val_if_fail(INFINOTED_IS_PLUGIN_MANAGER(manager), NULL);
  return INFINOTED_PLUGIN_MANAGER_PRIVATE(manager)->credentials;
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
 * Returns: (transfer none) (allow-none): A pointer to the connection-specific
 * plugin data, or %NULL.
 */
gpointer
infinoted_plugin_manager_get_connection_info(InfinotedPluginManager* mgr,
                                             gpointer plugin_info,
                                             InfXmlConnection* connection)
{
  InfinotedPluginManagerPrivate* priv;

  g_return_val_if_fail(INFINOTED_IS_PLUGIN_MANAGER(mgr), NULL);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), NULL);

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(mgr);

  return g_hash_table_lookup(
    priv->connections,
    infinoted_plugin_manager_hash(plugin_info, connection)
  );
}

/**
 * infinoted_plugin_manager_get_session_info:
 * @mgr: A #InfinotedPluginManager.
 * @plugin_info: The @plugin_info pointer of a plugin instance.
 * @proxy: The #InfSessionProxy for which to retrieve plugin data.
 *
 * Queries the session-specfic plugin data for the plugin instance
 * @plugin_info. Returns %NULL if no such object exists, i.e. when the
 * plugin's @session_info_size is set to 0.
 *
 * Returns: (transfer none) (allow-none): A pointer to the session-specific
 * plugin data, or %NULL.
 */
gpointer
infinoted_plugin_manager_get_session_info(InfinotedPluginManager* mgr,
                                          gpointer plugin_info,
                                          InfSessionProxy* proxy)
{
  InfinotedPluginManagerPrivate* priv;

  g_return_val_if_fail(INFINOTED_IS_PLUGIN_MANAGER(mgr), NULL);
  g_return_val_if_fail(INF_IS_SESSION_PROXY(proxy), NULL);

  priv = INFINOTED_PLUGIN_MANAGER_PRIVATE(mgr);

  return g_hash_table_lookup(
    priv->sessions,
    infinoted_plugin_manager_hash(plugin_info, proxy)
  );
}

/* vim:set et sw=2 ts=2: */
