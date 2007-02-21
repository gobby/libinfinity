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

#include <libinfinited/infd-directory.h>

#include <libinfinity/inf-net-object.h>

typedef struct _InfdDirectoryPrivate InfdDirectoryPrivate;
struct _InfdDirectoryPrivate {
  InfdDirectoryStorage* storage;
  InfConnectionManager* connection_manager;

  GNetworkServer* server;
};

enum {
  PROP_0,

  PROP_STORAGE,
  PROP_CONNECTION_MANAGER
};

#define INFD_DIRECTORY_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_DIRECTORY, InfdDirectoryPrivate))

static GObjectClass* parent_class;

static void
infd_directory_server_notify_status_cb(GNetworkServer* server,
                                       const gchar* property,
                                       gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  GNetworkServerStatus status;

  directory = INFD_DIRECTORY(user_data);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_object_get(G_OBJECT(server), "status", &status, NULL);

  if(status == GNETWORK_SERVER_OPEN)
  {
    /* TODO: Announce on avahi, if not already */
  }
  else
  {
    /* TODO: Unannounce on avahi, if not already */
  }
}

static void
infd_directory_server_error_cb(GNetworkServer* server,
                               GError* error,
                               gpointer user_data)
{
  InfdDirectory* directory;
  directory = INFD_DIRECTORY(user_data);

  /* Close server on error */
  infd_directory_set_server(directory, NULL);
}

static void
infd_directory_set_storage(InfdDirectory* directory,
                           InfdDirectoryStorage* storage)
{
  InfdDirectoryPrivate* priv;
  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->storage != NULL)
  {
    /* TODO: Store running sessions into storage */
    /* TODO: Close running sessions */
    /* TODO: Clear directory tree */
    /* TODO: Send to connections which opened root folder */
    /* TODO: Remove all entries except root folder for all connections */

    g_object_unref(G_OBJECT(priv->storage));
  }

  priv->storage = storage;

  if(storage != NULL)
  {
    /* TODO: Send root folder to all connections that opened root folder */
    g_object_ref(G_OBJECT(storage));
  }
}

static void
infd_directory_set_connection_manager(InfdDirectory* directory,
                                      InfConnectionManager* manager)
{
  InfdDirectoryPrivate* priv;
  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->connection_manager != NULL)
  {
    /* TODO: call inf_connection_manager_remove_object() for every connection
     * we accepted. */

    g_object_unref(G_OBJECT(priv->connection_manager));
  }

  priv->connection_manager = manager;

  if(manager != NULL)
  {
    /* TODO: Add all connections to the new connection manager. */

    /* TODO: Call inf_connection_manager_add_object() for every connection. */
    g_object_ref(G_OBJECT(manager));
  }
}

static void
infd_directory_init(GTypeInstance* instance,
                    gpointer g_class)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(instance);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  priv->storage = NULL;
  priv->connection_manager = NULL;
  priv->server = NULL;
}

static void
infd_directory_dispose(GObject* object)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(object);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_set_server(directory, NULL);

  /* It is important that the connection manager is unrefed first, otherwise
   * we would call infd_directory_set_storage while we have still connections
   * eshablished and the function would try to tell everyone that all
   * notes disappeared... */
  infd_directory_set_connection_manager(directory, NULL);
  infd_directory_set_storage(directory, NULL);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
infd_directory_set_property(GObject* object,
                            guint prop_id,
                            const GValue* value,
                            GParamSpec* pspec)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(object);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  switch(prop_id)
  {
  case PROP_STORAGE:
    infd_directory_set_storage(
      directory,
      INFD_DIRECTORY_STORAGE(g_value_get_object(value))
    );

    break;
  case PROP_CONNECTION_MANAGER:
    infd_directory_set_connection_manager(
      directory,
      INF_CONNECTION_MANAGER(g_value_get_object(value))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_directory_get_property(GObject* object,
                            guint prop_id,
                            GValue* value,
                            GParamSpec* pspec)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(object);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  switch(prop_id)
  {
  case PROP_STORAGE:
    g_value_set_object(value, G_OBJECT(priv->storage));
    break;
  case PROP_CONNECTION_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->connection_manager));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_directory_net_object_sent(InfNetObject* net_object,
                               GNetworkConnection* connection,
                               const xmlNodePtr node)
{
  /* TODO: Implement */
}

static void
infd_directory_net_object_received(InfNetObject* net_object,
                                   GNetworkConnection* connection,
                                   const xmlNodePtr node)
{
  /* TODO: Implement */
}

static void
infd_directory_class_init(gpointer g_class,
                          gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdDirectoryPrivate));

  object_class->dispose = infd_directory_dispose;
  object_class->set_property = infd_directory_set_property;
  object_class->get_property = infd_directory_get_property;

  g_object_class_install_property(
    object_class,
    PROP_STORAGE,
    g_param_spec_object(
      "storage",
      "Storage backend",
      "The storage backend to use",
      INFD_TYPE_DIRECTORY_STORAGE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CONNECTION_MANAGER,
    g_param_spec_object(
      "connection-manager",
      "Connection manager",
      "The connection manager for the directory",
      INF_TYPE_CONNECTION_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
infd_directory_net_object_init(gpointer g_iface,
                               gpointer iface_data)
{
  InfNetObjectIface* iface;
  iface = (InfNetObjectIface*)g_iface;

  iface->sent = infd_directory_net_object_sent;
  iface->received = infd_directory_net_object_received;
}

GType
infd_directory_get_type(void)
{
  static GType directory_type = 0;

  if(!directory_type)
  {
    static const GTypeInfo directory_type_info = {
      sizeof(InfdDirectoryClass),  /* class_size */
      NULL,                        /* base_init */
      NULL,                        /* base_finalize */
      infd_directory_class_init,   /* class_init */
      NULL,                        /* class_finalize */
      NULL,                        /* class_data */
      sizeof(InfdDirectory),       /* instance_size */
      0,                           /* n_preallocs */
      infd_directory_init,         /* instance_init */
      NULL                         /* value_table */
    };

    static const GInterfaceInfo net_object_info = {
      infd_directory_net_object_init,
      NULL,
      NULL
    };

    directory_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdDirectory",
      &directory_type_info,
      0
    );

    g_type_add_interface_static(
      directory_type,
      INF_TYPE_NET_OBJECT,
      &net_object_info
    );
  }

  return directory_type;
}

/** infd_directory_new:
 *
 * @storage: Storage backend that is used to read/write notes from
 * permanent memory into #InfBuffer objects.
 * @connection_manager: A #InfConnectionManager to register incoming
 * connections to and which forwards incoming data to the directory
 * or running sessions.
 *
 * Creates a new #InfdDirectory.
 *
 * Return Value: A new #InfdDirectory.
 **/
InfdDirectory*
infd_directory_new(InfdDirectoryStorage* storage,
                   InfConnectionManager* connection_manager)
{
  GObject* object;

  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), NULL);
  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(connection_manager), NULL);

  object = g_object_new(
    INFD_TYPE_DIRECTORY,
    "storage", storage,
    "connection-manager", connection_manager,
    NULL
  );

  return INFD_DIRECTORY(object);
}

/** infd_directory_get_storage:
 *
 * @directory: A #InfdDirectory:
 *
 * Returns the storage backend in use by the directory.
 *
 * Return Value: An #InfdDirectoryStorage.
 **/
InfdDirectoryStorage*
infd_directory_get_storage(InfdDirectory* directory)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  return INFD_DIRECTORY_PRIVATE(directory)->storage;
}

/** infd_directory_get_connection_manager:
 *
 * @directory: A #InfdDirectory.
 *
 * Returns the connection manager of the directory.
 *
 * Return Value: An #InfConnectionManager.
 **/
InfConnectionManager*
infd_directory_get_connection_manager(InfdDirectory* directory)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  return INFD_DIRECTORY_PRIVATE(directory)->connection_manager;
}

/** infd_directory_set_server:
 *
 * @directory: A #InfdDirectory.
 * @server: A #GNetworkServer, or %NULL.
 *
 * Makes @directory use @server to listen for incoming connections. If a
 * server was set previously, this one will overwrite the old. If you do not
 * own a reference of the old one anymore, and the "close-children" property
 * of it is set to TRUE, all connections from the old server will be closed.
 *
 * It is your responsibility to open @server. If an error occurs, @server
 * will be unset from @directory. If @server is open, incoming connections
 * are accepted and requests from them are handled by @directory.
 *
 * @server may be %NULL to unset the server which is currently in use.
 **/
void
infd_directory_set_server(InfdDirectory* directory,
                          GNetworkServer* server)
{
  InfdDirectoryPrivate* priv;
  GNetworkServerStatus status;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  g_return_if_fail(server == NULL || GNETWORK_IS_SERVER(server));

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->server != NULL)
  {
    /* TODO: Unannounce on avahi, if not already */

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->server),
      G_CALLBACK(infd_directory_server_notify_status_cb),
      directory
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->server),
      G_CALLBACK(infd_directory_server_error_cb),
      directory
    );

    g_object_unref(G_OBJECT(priv->server));
  }

  priv->server = server;

  if(server != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(server),
      "notify::status",
      G_CALLBACK(infd_directory_server_notify_status_cb),
      directory
    );

    g_signal_connect_after(
      G_OBJECT(server),
      "error",
      G_CALLBACK(infd_directory_server_error_cb),
      directory
    );

    g_object_get(G_OBJECT(server), "status", &status, NULL);

    if(status == GNETWORK_SERVER_OPEN)
    {
      /* TODO: Announce on avahi, if not already */
    }

    g_object_ref(G_OBJECT(server));
  }
}

/** infd_directory_open_server:
 *
 * @directory: A #InfdDirectory.
 * @interface: IP address of interface to bind to, or %NULL.
 * @port: Port number to bind to, or 0.
 *
 * This is a convenience function that creates a new #GNetworkTcpServer
 * object and then calls infd_directory_set_server(). The created server
 * is returned and has a reference count of 1 which belongs to @directory.
 *
 * Return Value: A newly-created #GNetworkTcpServer.
 **/
GNetworkTcpServer*
infd_directory_open_server(InfdDirectory* directory,
                           const gchar* interface,
                           guint port)
{
  GNetworkTcpServer* server;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);

  server = gnetwork_tcp_server_new(interface, port);
  infd_directory_set_server(directory, GNETWORK_SERVER(server));
  g_object_unref(G_OBJECT(server));

  return server;
}
