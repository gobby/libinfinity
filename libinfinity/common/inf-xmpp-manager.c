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

#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-tcp-connection.h>

typedef struct _InfXmppManagerKey InfXmppManagerKey;
struct _InfXmppManagerKey {
  InfIpAddress* address; /* owned by connection */
  guint port;
};

typedef struct _InfXmppManagerPrivate InfXmppManagerPrivate;
struct _InfXmppManagerPrivate {
  GTree* connections;
};

#define INF_XMPP_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_XMPP_MANAGER, InfXmppManagerPrivate))

static GObjectClass* parent_class;

static InfXmppManagerKey*
inf_xmpp_manager_key_new(InfXmppConnection* connection)
{
  InfXmppManagerKey* key;
  InfTcpConnection* tcp;

  g_object_get(G_OBJECT(connection), "tcp-connection", &tcp, NULL);
  g_assert(tcp != NULL);

  key = g_slice_new(InfXmppManagerKey);
  key->address = inf_tcp_connection_get_remote_address(tcp);
  key->port = inf_tcp_connection_get_remote_port(tcp);

  g_object_unref(G_OBJECT(tcp));
  return key;
}

static void
inf_xmpp_manager_key_free(gpointer key)
{
  /* address is owned by connection */
  g_slice_free(InfXmppManagerKey, key);
}

static int
inf_xmpp_manager_key_cmp(gconstpointer first,
                         gconstpointer second,
                         G_GNUC_UNUSED gpointer data)
{
  const InfXmppManagerKey* first_key;
  const InfXmppManagerKey* second_key;

  first_key = (const InfXmppManagerKey*)first;
  second_key = (const InfXmppManagerKey*)second;

  if(first_key->port < second_key->port)
    return -1;
  else if(first_key->port > second_key->port)
    return 1;
  else
    return inf_ip_address_collate(first_key->address, second_key->address);
}

static void
inf_xmpp_manager_notify_status_cb(GObject* object,
                                  GParamSpec* pspec,
                                  gpointer user_data)
{
  InfXmppManager* manager;
  InfXmppManagerPrivate* priv;
  InfXmppConnection* connection;
  InfXmlConnectionStatus status;
  InfTcpConnection* tcp;
  InfXmppManagerKey key;

  manager = INF_XMPP_MANAGER(user_data);
  priv = INF_XMPP_MANAGER_PRIVATE(manager);

  connection = INF_XMPP_CONNECTION(object);
  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSING ||
     status == INF_XML_CONNECTION_CLOSED)
  {
    g_object_get(G_OBJECT(connection), "tcp-connection", &tcp, NULL);
    g_assert(tcp != NULL);

    key.address = inf_tcp_connection_get_remote_address(tcp);
    key.port = inf_tcp_connection_get_remote_port(tcp);
    g_tree_remove(priv->connections, &key);
    g_object_unref(G_OBJECT(tcp));

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(inf_xmpp_manager_notify_status_cb),
      manager
    );

    g_object_unref(G_OBJECT(connection));
  }
}

static gboolean
inf_xmpp_manager_dispose_destroy_func(gpointer key,
                                      gpointer value,
                                      gpointer data)
{
  InfXmppManager* manager;
  InfXmppConnection* connection;

  manager = INF_XMPP_MANAGER(data);
  connection = INF_XMPP_CONNECTION(value);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(connection),
    G_CALLBACK(inf_xmpp_manager_notify_status_cb),
    manager
  );

  g_object_unref(G_OBJECT(connection));
  return FALSE;
}

static void
inf_xmpp_manager_init(GTypeInstance* instance,
                      gpointer g_class)
{
  InfXmppManager* manager;
  InfXmppManagerPrivate* priv;

  manager = INF_XMPP_MANAGER(instance);
  priv = INF_XMPP_MANAGER_PRIVATE(manager);

  /* destroy_funcs cannot have data associated, but we need the
   * manager to remove the signal connection. */
  priv->connections = g_tree_new_full(
    inf_xmpp_manager_key_cmp,
    NULL,
    inf_xmpp_manager_key_free,
    NULL
  );
}

static void
inf_xmpp_manager_dispose(GObject* object)
{
  InfXmppManager* manager;
  InfXmppManagerPrivate* priv;

  manager = INF_XMPP_MANAGER(object);
  priv = INF_XMPP_MANAGER_PRIVATE(object);

  g_tree_foreach(
    priv->connections,
    inf_xmpp_manager_dispose_destroy_func,
    manager
  );

  g_tree_destroy(priv->connections);
  priv->connections = NULL;

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_xmpp_manager_class_init(gpointer g_class,
                            gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfXmppManagerPrivate));

  object_class->dispose = inf_xmpp_manager_dispose;
}

GType
inf_xmpp_manager_get_type(void)
{
  static GType xmpp_manager_type = 0;

  if(!xmpp_manager_type)
  {
    static const GTypeInfo xmpp_manager_type_info = {
      sizeof(InfXmppManagerClass),  /* class_size */
      NULL,                         /* base_init */
      NULL,                         /* base_finalize */
      inf_xmpp_manager_class_init,  /* class_init */
      NULL,                         /* class_finalize */
      NULL,                         /* class_data */
      sizeof(InfXmppManager),       /* instance_size */
      0,                            /* n_preallocs */
      inf_xmpp_manager_init,        /* instance_init */
      NULL                          /* value_table */
    };

    xmpp_manager_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfXmppManager",
      &xmpp_manager_type_info,
      0
    );
  }

  return xmpp_manager_type;
}

/**
 * inf_xmpp_manager_new:
 * Creates a new xmpp manager.
 *
 * Return Value: A new #InfXmppManager.
 **/
InfXmppManager*
inf_xmpp_manager_new(void)
{
  GObject* object;

  object = g_object_new(INF_TYPE_XMPP_MANAGER, NULL);

  return INF_XMPP_MANAGER(object);
}

/**
 * inf_xmpp_manager_lookup_connection_by_address:
 * @manager: A #InfConnectionManager.
 * @address: The remote #InfIpAddress of the connection to look for.
 * @port: The remote port number of the connection to look for.
 *
 * Looks for a #InfXmppConnection contained in @manager whose underlaying
 * #InfTcpConnection is connected to the given address and port. Returns
 * %NULL if there is no such connection.
 **/
InfXmppConnection*
inf_xmpp_manager_lookup_connection_by_address(InfXmppManager* manager,
                                              InfIpAddress* address,
                                              guint port)
{
  InfXmppManagerPrivate* priv;
  InfXmppManagerKey key;

  g_return_val_if_fail(INF_IS_XMPP_MANAGER(manager), NULL);
  g_return_val_if_fail(address != NULL, NULL);

  priv = INF_XMPP_MANAGER_PRIVATE(manager);
  key.address = address;
  key.port = port;
  return INF_XMPP_CONNECTION(g_tree_lookup(priv->connections, &key));
}

/**
 * inf_xmpp_manager_contains_connection:
 * @manager: A #InfXmppManager.
 * @connection: A #InfXmppConnection.
 *
 * Returns whether @connection is contained in @manager.
 *
 * Return Value: %TRUE if @connection is contained in @manager, %FALSE
 * otherwise.
 **/
gboolean
inf_xmpp_manager_contains_connection(InfXmppManager* manager,
                                     InfXmppConnection* connection)
{
  InfXmppManagerPrivate* priv;
  InfTcpConnection* tcp;
  InfXmppManagerKey key;

  g_return_val_if_fail(INF_IS_XMPP_MANAGER(manager), FALSE);
  g_return_val_if_fail(INF_IS_XMPP_CONNECTION(connection), FALSE);

  priv = INF_XMPP_MANAGER_PRIVATE(manager);
  g_object_get(G_OBJECT(connection), "tcp-connection", &tcp, NULL);
  g_assert(tcp != NULL);

  key.address = inf_tcp_connection_get_remote_address(tcp);
  key.port = inf_tcp_connection_get_remote_port(tcp);
  g_object_unref(G_OBJECT(tcp));

  return INF_XMPP_CONNECTION(g_tree_lookup(priv->connections, &key)) != NULL;
}

/**
 * inf_xmpp_manager_add_connection:
 * @manager: A #InfXmppManager.
 * @connection: A #InfXmppConnection not yet contained in @manager.
 *
 * Adds the given connection to @manager so that it is found by
 * inf_xmpp_manager_lookup_connection_by_address() and
 * inf_xmpp_manager_contains_connection(). @connection must not be in
 * state %INF_XML_CONNECTION_CLOSING or %INF_XML_CONNECTION_CLOSED.
 **/
void
inf_xmpp_manager_add_connection(InfXmppManager* manager,
                                InfXmppConnection* connection)
{
  InfXmppManagerPrivate* priv;
  InfXmlConnectionStatus status;
  InfXmppManagerKey* key;

  g_return_if_fail(INF_IS_XMPP_MANAGER(manager));
  g_return_if_fail(INF_IS_XMPP_CONNECTION(connection));
  g_return_if_fail(
    inf_xmpp_manager_contains_connection(manager, connection) == FALSE
  );

  priv = INF_XMPP_MANAGER_PRIVATE(manager);

  g_object_get(
    G_OBJECT(connection),
    "status", &status,
    NULL
  );

  g_return_if_fail(status != INF_XML_CONNECTION_CLOSING &&
                   status != INF_XML_CONNECTION_CLOSED);

  g_signal_connect(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(inf_xmpp_manager_notify_status_cb),
    manager
  );

  key = inf_xmpp_manager_key_new(connection);
  g_tree_insert(priv->connections, key, connection);
  g_object_ref(G_OBJECT(connection));
}

/* vim:set et sw=2 ts=2: */
