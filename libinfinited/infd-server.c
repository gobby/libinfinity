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

#include <libinfinited/infd-server.h>

#include <libinfinity/inf-net-object.h>

typedef struct _InfdServerPrivate InfdServerPrivate;
struct _InfdServerPrivate {
  InfdDirectory* directory;
  GNetworkServer* server;

  /* TODO: Service publishing */
};

enum {
  PROP_0,

  PROP_DIRECTORY,
  PROP_SERVER
};

#define INFD_SERVER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_SERVER, InfdServerPrivate))

static GObjectClass* parent_class;

static void
infd_server_server_notify_status_cb(GNetworkServer* gnetwork_server,
                                    const gchar* property,
                                    gpointer user_data)
{
  InfdServer* server;
  InfdServerPrivate* priv;
  GNetworkServerStatus status;

  server = INFD_SERVER(user_data);
  priv = INFD_SERVER_PRIVATE(server);

  g_object_get(G_OBJECT(gnetwork_server), "status", &status, NULL);

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
infd_server_server_error_cb(GNetworkServer* gnetwork_server,
                            GError* error,
                            gpointer user_data)
{
  InfdServer* server;
  InfdServerPrivate* priv;

  server = INFD_SERVER(user_data);
  priv = INFD_SERVER_PRIVATE(server);

  /* Perhaps another signal handler already replaced the server by
   * another one. */
  if(priv->server == gnetwork_server)
  {
    /* Close server on error */
    infd_server_set_server(server, NULL);
  }
}

static void
infd_server_server_new_connection_cb(GNetworkServer* gnetwork_server,
                                     GNetworkConnection* connection,
                                     gpointer user_data)
{
  InfdServer* server;
  InfdServerPrivate* priv;

  server = INFD_SERVER(user_data);
  priv = INFD_SERVER_PRIVATE(server);

  if(priv->directory != NULL)
    infd_directory_add_connection(priv->directory, connection);
}

static void
infd_server_init(GTypeInstance* instance,
                 gpointer g_class)
{
  InfdServer* server;
  InfdServerPrivate* priv;

  server = INFD_SERVER(instance);
  priv = INFD_SERVER_PRIVATE(server);

  priv->directory = NULL;
  priv->server = NULL;
}

static void
infd_server_dispose(GObject* object)
{
  InfdServer* server;
  InfdServerPrivate* priv;

  server = INFD_SERVER(object);
  priv = INFD_SERVER_PRIVATE(server);

  infd_server_set_server(server, NULL);
  g_object_unref(G_OBJECT(priv->directory));

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
infd_server_set_property(GObject* object,
                         guint prop_id,
                         const GValue* value,
                         GParamSpec* pspec)
{
  InfdServer* server;
  InfdServerPrivate* priv;

  server = INFD_SERVER(object);
  priv = INFD_SERVER_PRIVATE(server);

  switch(prop_id)
  {
  case PROP_DIRECTORY:
    if(priv->directory != NULL) g_object_unref(G_OBJECT(priv->directory));
    priv->directory = INFD_DIRECTORY(g_value_dup_object(value));
    break;
  case PROP_SERVER:
    infd_server_set_server(
      server,
      GNETWORK_SERVER(g_value_get_object(value))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_server_get_property(GObject* object,
                         guint prop_id,
                         GValue* value,
                         GParamSpec* pspec)
{
  InfdServer* server;
  InfdServerPrivate* priv;

  server = INFD_SERVER(object);
  priv = INFD_SERVER_PRIVATE(server);

  switch(prop_id)
  {
  case PROP_DIRECTORY:
    g_value_set_object(value, G_OBJECT(priv->directory));
    break;
  case PROP_SERVER:
    g_value_set_object(value, G_OBJECT(priv->server));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_server_class_init(gpointer g_class,
                       gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdServerPrivate));

  object_class->dispose = infd_server_dispose;
  object_class->set_property = infd_server_set_property;
  object_class->get_property = infd_server_get_property;

  g_object_class_install_property(
    object_class,
    PROP_DIRECTORY,
    g_param_spec_object(
      "directory",
      "Directory",
      "The directory to which to register incoming connections to",
      INFD_TYPE_DIRECTORY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SERVER,
    g_param_spec_object(
      "server",
      "Server",
      "The server to accept connections from",
      GNETWORK_TYPE_SERVER,
      G_PARAM_READWRITE
    )
  );
}

GType
infd_server_get_type(void)
{
  static GType server_type = 0;

  if(!server_type)
  {
    static const GTypeInfo server_type_info = {
      sizeof(InfdServerClass),  /* class_size */
      NULL,                        /* base_init */
      NULL,                        /* base_finalize */
      infd_server_class_init,   /* class_init */
      NULL,                        /* class_finalize */
      NULL,                        /* class_data */
      sizeof(InfdServer),       /* instance_size */
      0,                           /* n_preallocs */
      infd_server_init,         /* instance_init */
      NULL                         /* value_table */
    };

    server_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdServer",
      &server_type_info,
      0
    );
  }

  return server_type;
}

/** infd_server_new:
 *
 * @directory: A #InfdDirectory to which to add incoming connections.
 * @connection_manager: A #GNetworkTcpServer from which to accept incoming
 * connections, or %NULL.
 *
 * Creates a new #InfdServer.
 *
 * Return Value: A new #InfdServer.
 **/
InfdServer*
infd_server_new(InfdDirectory* directory,
                GNetworkServer* server)
{
  GObject* object;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  g_return_val_if_fail(server == NULL || GNETWORK_IS_SERVER(server), NULL);

  object = g_object_new(
    INFD_TYPE_SERVER,
    "directory", directory,
    "server", server,
    NULL
  );

  return INFD_SERVER(object);
}

/** infd_server_get_server:
 *
 * @server: A #InfdServer.
 *
 * Returns the #GNetworkServer @server uses.
 *
 * Return Value: A #GNetworkServer.
 **/
GNetworkServer*
infd_server_get_server(InfdServer* server)
{
  g_return_val_if_fail(INFD_IS_SERVER(server), NULL);
  return INFD_SERVER_PRIVATE(server)->server;
}

/** infd_server_set_server:
 *
 * @server: A #InfdServer.
 * @gnetwork_server: A #GNetworkServer, or %NULL.
 *
 * Makes @server use @gnetwork_server to listen for incoming connections. If a
 * server was set previously, this one will overwrite the old. If you do not
 * own a reference of the old one anymore, and the "close-children" property
 * of it is set to TRUE, all connections from the old server will be closed.
 *
 * It is your responsibility to open @gnetwork_server. If an error occurs,
 * @gnetwork_server will be unset from @server. If @gnetwork_server is open,
 * incoming connections are accepted and requests from them are handled by
 * the directory associated to @server.
 *
 * @gnetwork_server may be %NULL to unset the server which is currently in
 * use.
 **/
void
infd_server_set_server(InfdServer* server,
                       GNetworkServer* gnetwork_server)
{
  InfdServerPrivate* priv;
  GNetworkServerStatus status;

  g_return_if_fail(INFD_IS_SERVER(server));

  g_return_if_fail(
    gnetwork_server == NULL || GNETWORK_IS_SERVER(gnetwork_server)
  );

  priv = INFD_SERVER_PRIVATE(server);

  if(priv->server != NULL)
  {
    /* TODO: Unannounce on avahi, if not already */

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->server),
      G_CALLBACK(infd_server_server_notify_status_cb),
      server
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->server),
      G_CALLBACK(infd_server_server_error_cb),
      server
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->server),
      G_CALLBACK(infd_server_server_new_connection_cb),
      server
    );

    g_object_unref(G_OBJECT(priv->server));
  }

  priv->server = gnetwork_server;

  if(gnetwork_server != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(gnetwork_server),
      "notify::status",
      G_CALLBACK(infd_server_server_notify_status_cb),
      server
    );

    g_signal_connect_after(
      G_OBJECT(gnetwork_server),
      "error",
      G_CALLBACK(infd_server_server_error_cb),
      server
    );

    g_signal_connect_after(
      G_OBJECT(gnetwork_server),
      "new-connection",
      G_CALLBACK(infd_server_server_new_connection_cb),
      server
    );

    g_object_get(G_OBJECT(gnetwork_server), "status", &status, NULL);

    if(status == GNETWORK_SERVER_OPEN)
    {
      /* TODO: Announce on avahi, if not already */
    }

    g_object_ref(G_OBJECT(gnetwork_server));
  }
}

/** infd_server_open_server:
 *
 * @server: A #InfdServer.
 * @interface: IP address of interface to bind to, or %NULL.
 * @port: Port number to bind to, or 0.
 *
 * This is a convenience function that creates a new #GNetworkTcpServer
 * object and then calls infd_server_set_server(). The created server
 * is returned and has a reference count of 1 which belongs to @server.
 *
 * Return Value: A newly-created #GNetworkTcpServer.
 **/
GNetworkTcpServer*
infd_server_open_server(InfdServer* server,
                        const gchar* interface,
                        guint port)
{
  GNetworkTcpServer* gnetwork_server;

  g_return_val_if_fail(INFD_IS_SERVER(server), NULL);

  gnetwork_server = gnetwork_tcp_server_new(interface, port);
  infd_server_set_server(server, GNETWORK_SERVER(gnetwork_server));
  g_object_unref(G_OBJECT(gnetwork_server));

  return gnetwork_server;
}
