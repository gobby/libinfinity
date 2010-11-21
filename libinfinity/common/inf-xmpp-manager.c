/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-tcp-connection.h>

#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-signals.h>

/**
 * SECTION:inf-xmpp-manager
 * @title: InfXmppManager
 * @short_description: Reuse existing connections
 * @include: libinfinity/common/inf-xmpp-manager.h
 * @stability: Unstable
 *
 * #InfXmppManager stores #InfXmppConnection objects and allows to look them
 * up by the IP address and port number of their underlaying
 * #InfTcpConnection<!-- -->s. This can be used to reuse existing network
 * connections instead of creating new ones.
 *
 * Each object which needs to make connections should be passed a
 * #InfXmppManager. Then, when making a connection to a certain address/port
 * pair, it should first look in the XMPP manager whether there is already
 * an existing connection to the destination host, via
 * inf_xmpp_manager_lookup_connection_by_address(). If there is, it should
 * use it (maybe reopen it if it is closed). Otherwise, it should create a
 * new connection and it to the XMPP manager via
 * inf_xmpp_manager_add_connection() for others to use.
 */

typedef struct _InfXmppManagerKey InfXmppManagerKey;
struct _InfXmppManagerKey {
  InfIpAddress* address; /* owned by connection */
  guint port;
};

typedef struct _InfXmppManagerKeyChangedForeachFuncData
  InfXmppManagerKeyChangedForeachFuncData;
struct _InfXmppManagerKeyChangedForeachFuncData {
  InfTcpConnection* connection;
  InfXmppConnection* xmpp;
  const InfXmppManagerKey* key;
};

typedef struct _InfXmppManagerPrivate InfXmppManagerPrivate;
struct _InfXmppManagerPrivate {
  GTree* connections;
};

enum {
  ADD_CONNECTION,
  REMOVE_CONNECTION,

  LAST_SIGNAL
};

#define INF_XMPP_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_XMPP_MANAGER, InfXmppManagerPrivate))

static GObjectClass* parent_class;
static guint xmpp_manager_signals[LAST_SIGNAL];

static InfXmppManagerKey*
inf_xmpp_manager_key_new(InfXmppConnection* connection)
{
  InfXmppManagerKey* key;
  InfTcpConnection* tcp;

  g_object_get(G_OBJECT(connection), "tcp-connection", &tcp, NULL);
  g_assert(tcp != NULL);

  key = g_slice_new(InfXmppManagerKey);
  key->address =
    inf_ip_address_copy(inf_tcp_connection_get_remote_address(tcp));
  key->port = inf_tcp_connection_get_remote_port(tcp);

  g_object_unref(tcp);
  return key;
}

static void
inf_xmpp_manager_key_free(gpointer key)
{
  inf_ip_address_free( ((InfXmppManagerKey*)key)->address);
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

static gboolean
inf_xmpp_manager_key_changed_foreach_func(gpointer key,
                                          gpointer value,
                                          gpointer data)
{
  InfXmppManagerKeyChangedForeachFuncData* func_data;
  InfTcpConnection* tcp;

  func_data = (InfXmppManagerKeyChangedForeachFuncData*)data;

  g_object_get(G_OBJECT(value), "tcp-connection", &tcp, NULL);
  if(func_data->connection == tcp)
  {
    g_assert(func_data->xmpp == NULL && func_data->key == NULL);
    func_data->xmpp = INF_XMPP_CONNECTION(value);
    func_data->key = key;

    g_object_unref(tcp);
    return TRUE;
  }

  g_object_unref(tcp);
  return FALSE;
}

static void
inf_xmpp_manager_key_changed(InfXmppManager* manager,
                             InfTcpConnection* connection)
{
  InfXmppManagerPrivate* priv;
  InfXmppManagerKeyChangedForeachFuncData data;
  InfXmppConnection* xmpp;
  InfXmppManagerKey key;
  InfXmppManagerKey* new_key;

  priv = INF_XMPP_MANAGER_PRIVATE(manager);
  data.connection = connection;
  data.xmpp = NULL;
  data.key = NULL;

  g_tree_foreach(
    priv->connections,
    inf_xmpp_manager_key_changed_foreach_func,
    &data
  );

  g_assert(data.xmpp != NULL);

  key.address = inf_tcp_connection_get_remote_address(connection);
  key.port = inf_tcp_connection_get_remote_port(connection);
  xmpp = g_tree_lookup(priv->connections, &key);

  if(xmpp != data.xmpp)
  {
    /* Remove old, now invalid entry */
    g_tree_remove(priv->connections, data.key);

    if(xmpp != NULL)
    {
      /* Changed address causes conflict, so remove the
       * conflicting connection (data.xmpp). */

      /* Make room for the old connection, so that when emitting the signal
       * the default signal handler removes the correct connection. */
      g_object_ref(xmpp);
      g_tree_remove(priv->connections, &key);

      /* Insert the conflicting connection at the point of the previous one */
      new_key = inf_xmpp_manager_key_new(data.xmpp);
      g_tree_insert(priv->connections, new_key, data.xmpp);

      /* Now remove it again, emitting a corresponding signal */
      inf_xmpp_manager_remove_connection(manager, data.xmpp);

      /* Finally readd the previous connection. */
      /* TODO: Handle the case when a signal handler did so already. */
      new_key = inf_xmpp_manager_key_new(xmpp);
      g_tree_insert(priv->connections, new_key, xmpp);
      g_object_unref(xmpp);
    }
    else
    {
      /* Readd the connection to the tree with the new key */
      new_key = inf_xmpp_manager_key_new(data.xmpp);
      g_tree_insert(priv->connections, new_key, data.xmpp);
    }
  }
}

static void
inf_xmpp_manager_notify_remote_address_cb(GObject* object,
                                          GParamSpec* pspec,
                                          gpointer user_data)
{
  inf_xmpp_manager_key_changed(
    INF_XMPP_MANAGER(user_data),
    INF_TCP_CONNECTION(object)
  );
}

static void
inf_xmpp_manager_notify_remote_port_cb(GObject* object,
                                       GParamSpec* pspec,
                                       gpointer user_data)
{
  inf_xmpp_manager_key_changed(
    INF_XMPP_MANAGER(user_data),
    INF_TCP_CONNECTION(object)
  );
}

static gboolean
inf_xmpp_manager_dispose_destroy_func(gpointer key,
                                      gpointer value,
                                      gpointer data)
{
  InfXmppManager* manager;
  InfXmppConnection* connection;
  InfTcpConnection* tcp;

  manager = INF_XMPP_MANAGER(data);
  connection = INF_XMPP_CONNECTION(value);
  g_object_get(G_OBJECT(connection), "tcp-connection", &tcp, NULL);
  g_assert(tcp != NULL);

  inf_signal_handlers_disconnect_by_func(
    tcp,
    G_CALLBACK(inf_xmpp_manager_notify_remote_address_cb),
    manager
  );

  inf_signal_handlers_disconnect_by_func(
    tcp,
    G_CALLBACK(inf_xmpp_manager_notify_remote_port_cb),
    manager
  );

  g_object_unref(tcp);
  g_object_unref(connection);
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
inf_xmpp_manager_add_connection_handler(InfXmppManager* xmpp_manager,
                                        InfXmppConnection* connection)
{
  InfXmppManagerPrivate* priv;
  InfXmppManagerKey* key;
  InfTcpConnection* tcp;

  priv = INF_XMPP_MANAGER_PRIVATE(xmpp_manager);
  g_object_get(G_OBJECT(connection), "tcp-connection", &tcp, NULL);
  g_assert(tcp != NULL);

  g_signal_connect(
    tcp,
    "notify::remote-address",
    G_CALLBACK(inf_xmpp_manager_notify_remote_address_cb),
    xmpp_manager
  );

  g_signal_connect(
    tcp,
    "notify::remote-port",
    G_CALLBACK(inf_xmpp_manager_notify_remote_port_cb),
    xmpp_manager
  );

  key = inf_xmpp_manager_key_new(connection);
  g_tree_insert(priv->connections, key, connection);
  g_object_ref(connection);

  g_object_unref(tcp);
}

static void
inf_xmpp_manager_remove_connection_handler(InfXmppManager* xmpp_manager,
                                           InfXmppConnection* connection)
{
  InfXmppManagerPrivate* priv;
  InfTcpConnection* tcp;
  InfXmppManagerKey key;

  priv = INF_XMPP_MANAGER_PRIVATE(xmpp_manager);

  g_object_get(G_OBJECT(connection), "tcp-connection", &tcp, NULL);
  g_assert(tcp != NULL);

  inf_signal_handlers_disconnect_by_func(
    tcp,
    G_CALLBACK(inf_xmpp_manager_notify_remote_address_cb),
    xmpp_manager
  );

  inf_signal_handlers_disconnect_by_func(
    tcp,
    G_CALLBACK(inf_xmpp_manager_notify_remote_port_cb),
    xmpp_manager
  );

  key.address = inf_tcp_connection_get_remote_address(tcp);
  key.port = inf_tcp_connection_get_remote_port(tcp);
  g_tree_remove(priv->connections, &key);
  g_object_unref(tcp);

  g_object_unref(connection);
}

static void
inf_xmpp_manager_class_init(gpointer g_class,
                            gpointer class_data)
{
  GObjectClass* object_class;
  InfXmppManagerClass* xmpp_manager_class;

  object_class = G_OBJECT_CLASS(g_class);
  xmpp_manager_class = INF_XMPP_MANAGER_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfXmppManagerPrivate));

  object_class->dispose = inf_xmpp_manager_dispose;
  xmpp_manager_class->add_connection =
    inf_xmpp_manager_add_connection_handler;
  xmpp_manager_class->remove_connection =
    inf_xmpp_manager_remove_connection_handler;

  /**
   * InfXmppManager::add-connection:
   * @xmpp_manager: The #InfXmppManager emitting the signal.
   * @connection: The #InfXmppConnection that was added to @xmpp_manager.
   *
   * This signal is emitted whenever a new connection has been added to the
   * #InfXmppManager, via inf_xmpp_manager_add_connection().
   */
  xmpp_manager_signals[ADD_CONNECTION] = g_signal_new(
    "add-connection",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfXmppManagerClass, add_connection),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XMPP_CONNECTION
  );

  /**
   * InfXmppManager::remove-connection:
   * @xmpp_manager: The #InfXmppManager emitting the signal.
   * @connection: The #InfXmppConnection that was removed from @xmpp_manager.
   *
   * This signal is emitted whenever a connection has been removed from the
   * #InfXmppManager, via inf_xmpp_manager_remove_connection().
   */
  xmpp_manager_signals[REMOVE_CONNECTION] = g_signal_new(
    "remove-connection",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfXmppManagerClass, remove_connection),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XMPP_CONNECTION
  );
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
 *
 * Creates a new xmpp manager.
 *
 * Returns: A new #InfXmppManager.
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
 * @manager: A #InfXmppManager.
 * @address: The remote #InfIpAddress of the connection to look for.
 * @port: The remote port number of the connection to look for.
 *
 * Looks for a #InfXmppConnection contained in @manager whose underlaying
 * #InfTcpConnection has the given address and port set. Returns %NULL if
 * there is no such connection.
 *
 * This function may also return a closed connection. You can then attempt to
 * reopen it, or remove it from the manager using
 * inf_xmpp_manager_remove_connection() when that fails.
 *
 * Returns: A #InfXmppConnection with the given address and port, or %NULL on
 * error.
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
 * Returns: %TRUE if @connection is contained in @manager, %FALSE
 * otherwise.
 */
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
 * inf_xmpp_manager_contains_connection().
 */
void
inf_xmpp_manager_add_connection(InfXmppManager* manager,
                                InfXmppConnection* connection)
{
  g_return_if_fail(INF_IS_XMPP_MANAGER(manager));
  g_return_if_fail(INF_IS_XMPP_CONNECTION(connection));
  g_return_if_fail(
    inf_xmpp_manager_contains_connection(manager, connection) == FALSE
  );

  g_signal_emit(
    G_OBJECT(manager),
    xmpp_manager_signals[ADD_CONNECTION],
    0,
    connection
  );
}
/**
 * inf_xmpp_manager_remove_connection:
 * @manager: A #InfXmppManager.
 * @connection: A #InfXmppConnection contained in @manager.
 *
 * Removes the given connection from @manager.
 */
void
inf_xmpp_manager_remove_connection(InfXmppManager* manager,
                                   InfXmppConnection* connection)
{
  g_return_if_fail(INF_IS_XMPP_MANAGER(manager));
  g_return_if_fail(INF_IS_XMPP_CONNECTION(connection));
  g_return_if_fail(
    inf_xmpp_manager_contains_connection(manager, connection) == TRUE
  );

  g_signal_emit(
    G_OBJECT(manager),
    xmpp_manager_signals[REMOVE_CONNECTION],
    0,
    connection
  );
}

/* vim:set et sw=2 ts=2: */
