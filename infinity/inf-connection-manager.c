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

#include <infinity/inf-connection-manager.h>
#include <infinity/inf-xml-stream.h>

typedef struct _InfConnectionManagerPrivate InfConnectionManagerPrivate;
struct _InfConnectionManagerPrivate {
  GSList* connections;
};

typedef struct _InfConnectionManagerConnection InfConnectionManagerConnection;
struct _InfConnectionManagerConnection {
  /* These hash tables map NetObjects to their identifiers and vice versa. */
  GHashTable* objects;
  GHashTable* identifiers;

  InfXmlStream* stream_received;
  InfXmlStream* stream_sent;
};

#define INF_CONNECTION_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_CONNECTION_MANAGER, InfConnectionManagerPrivate))

static GObjectClass* parent_class;
static GQuark connection_quark;

static InfNetObject*
inf_connection_manager_connection_grab(InfConnectionManagerConnection* conn,
                                       xmlNodePtr message)
{
  xmlChar* identifier;
  InfNetObject* object;

  object = NULL;
  if(strcmp((const char*)message->name, "message") == 0)
  {
    identifier = xmlGetProp(message, (const xmlChar*)"to");
    if(identifier != NULL)
    {
      object = g_hash_table_lookup(conn->objects, (const gchar*)identifier);
      xmlFree(identifier);
    }
  }

  return object;
}

static void
inf_connection_manager_connection_sent_cb(GNetworkConnection* gnetwork_conn,
                                          gpointer data,
                                          gulong len,
                                          gpointer user_data)
{
  InfConnectionManagerConnection* conn;
  InfNetObject* object;
  GError* error;

  xmlNodePtr message;
  xmlNodePtr child;
  gsize bytes_read;
  gsize bytes_cur;

  conn = (InfConnectionManagerConnection*)user_data;

  error = NULL;
  bytes_read = 0;

  while(bytes_read < len)
  {
    message = inf_xml_stream_parse(
      conn->stream_sent,
      (gchar*)data + bytes_read,
      len - bytes_read,
      &bytes_cur,
      &error
    );

    /* We really should not get an error here because this is XML we produced
     * in inf_connection_manager_send_to_object(). */
    g_assert(error != NULL);

    bytes_read += bytes_cur;
    if(message != NULL)
    {
      object = inf_connection_manager_connection_grab(conn, message);

      /* It may happen that a NetObject sends things but is removed until
       * the data is really sent out, so do not assert here. */
      if(object != NULL)
      {
        for(child = message->children; child != NULL; child = child->next)
        {
          inf_net_object_received(object, gnetwork_conn, child);
        }
      }

      xmlFreeNode(message);
    }
  }
}

static void
inf_connection_manager_connection_received_cb(GNetworkConnection* gnetwork_conn,
                                              gpointer data,
                                              gulong len,
                                              gpointer user_data)
{
  InfConnectionManagerConnection* conn;
  InfNetObject* object;
  gchar* address;
  GError* error;

  xmlNodePtr message;
  xmlNodePtr child;
  guint bytes_read;
  guint bytes_cur;

  conn = (InfConnectionManagerConnection*)user_data;

  error = NULL;
  bytes_read = 0;

  while(bytes_read < len)
  {
    message = inf_xml_stream_parse(
      conn->stream_received,
      (gchar*)data + bytes_read,
      len - bytes_read,
      &bytes_cur,
      &error
    );

    if(error != NULL)
    {
      g_object_get(G_OBJECT(gnetwork_conn), "address", &address, NULL);

      fprintf(
        stderr,
        "Received bad XML from %s: %s\n",
        address,
        error->message
      );

      gnetwork_connection_close(gnetwork_conn);
      g_error_free(error);
      g_free(address);

      return;
    }
    else
    {
      bytes_read += bytes_cur;
      if(message != NULL)
      {
        object = inf_connection_manager_connection_grab(conn, message);
        if(object != NULL)
        {
          for(child = message->children; child != NULL; child = child->next)
          {
            inf_net_object_received(object, gnetwork_conn, child);
          }
        }

        xmlFreeNode(message);
      }
    }
  }
}

static void
inf_connection_manager_connection_weak_notify_func(gpointer data,
                                                   GObject* object)
{
  InfConnectionManagerConnection* connection;
  gchar* name;
  gboolean result;

  connection = data;

  name = g_hash_table_lookup(connection->identifiers, object);

  result = g_hash_table_remove(connection->identifiers, object);
  g_assert(result == TRUE);

  g_hash_table_remove(connection->objects, name);
  g_assert(result == TRUE);

  g_free(name);
}

static void
inf_connection_manager_connection_unassoc_foreach_func(gpointer key,
                                                       gpointer value,
                                                       gpointer data)
{
  g_free(key);

  g_object_weak_unref(
    G_OBJECT(value),
    inf_connection_manager_connection_weak_notify_func,
    (InfConnectionManagerConnection*)data
  );
}

/* Note that this function does not free the given GNetworkConnection but
 * the associated InfConnectionManagerConnection. */
static void
inf_connection_manager_connection_unassoc(GNetworkConnection* gnetwork_conn)
{
  InfConnectionManagerConnection* conn;
  conn = g_object_steal_qdata(G_OBJECT(gnetwork_conn), connection_quark);

  g_return_if_fail(conn != NULL);

  /* It is enough to run foreach over one of the two hash tables because
   * both refer to the same data. */
  g_hash_table_foreach(
    conn->objects,
    inf_connection_manager_connection_unassoc_foreach_func,
    conn
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(gnetwork_conn),
    G_CALLBACK(inf_connection_manager_connection_received_cb),
    conn
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(gnetwork_conn),
    G_CALLBACK(inf_connection_manager_connection_sent_cb),
    conn
  );

  g_hash_table_destroy(conn->objects);
  g_hash_table_destroy(conn->identifiers);

  g_object_unref(G_OBJECT(conn->stream_received));
  g_object_unref(G_OBJECT(conn->stream_sent));

  g_free(conn);
}

static InfConnectionManagerConnection*
inf_connection_manager_connection_assoc(GNetworkConnection* gnetwork_conn)
{
  InfConnectionManagerConnection* conn;

  conn = g_object_get_qdata(G_OBJECT(gnetwork_conn), connection_quark);
  g_return_val_if_fail(conn == NULL, NULL);

  conn = g_new(InfConnectionManagerConnection, 1);

  g_object_set_qdata(
    G_OBJECT(gnetwork_conn),
    connection_quark,
    conn
  );

  g_signal_connect(
    G_OBJECT(gnetwork_conn),
    "received",
    G_CALLBACK(inf_connection_manager_connection_received_cb),
    conn
  );

  g_signal_connect(
    G_OBJECT(gnetwork_conn),
    "sent",
    G_CALLBACK(inf_connection_manager_connection_sent_cb),
    conn
  );

  /* These hash tables convert an object's identifier to the
   * InfNetObject and back. Both operate on the same string (the same
   * as in the char poniters point to the same memory) and the same
   * NetObject. */
  conn->objects = g_hash_table_new(g_str_hash, g_str_equal);
  conn->identifiers = g_hash_table_new(NULL, NULL);

  conn->stream_received = inf_xml_stream_new();
  conn->stream_sent = inf_xml_stream_new();

  return conn;
}

/* Required by inf_connection_manager_connection_notify_status_cb to
 * remove the gnetwork connection if it has been closed. */
static void
inf_connection_manager_free_connection(InfConnectionManager* manager,
                                       GNetworkConnection* gnetwork_conn);

static void
inf_connection_manager_connection_notify_status_cb(GNetworkConnection* conn,
                                                   GParamSpec* pspec,
                                                   gpointer user_data)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;
  GNetworkConnectionStatus status;

  g_object_get(G_OBJECT(conn), "status", &status, NULL);

  /* Remove the connection from the list of connections if it has
   * been closed. */
  if(status == GNETWORK_CONNECTION_CLOSED)
  {
    manager = INF_CONNECTION_MANAGER(user_data);
    priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

    inf_connection_manager_free_connection(manager, conn);
    priv->connections = g_slist_remove(priv->connections, conn);
  }
}

static void
inf_connection_manager_connection_error_cb(GNetworkConnection* conn,
                                           GError* error,
                                           gpointer user_data)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;

  manager = INF_CONNECTION_MANAGER(user_data);
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  /* Remove connection on error */
  inf_connection_manager_free_connection(manager, conn);
  priv->connections = g_slist_remove(priv->connections, conn);
}

static void
inf_connection_manager_free_connection(InfConnectionManager* manager,
                                       GNetworkConnection* gnetwork_conn)
{
  inf_connection_manager_connection_unassoc(gnetwork_conn);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(gnetwork_conn),
    G_CALLBACK(inf_connection_manager_connection_notify_status_cb),
    manager
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(gnetwork_conn),
    G_CALLBACK(inf_connection_manager_connection_error_cb),
    manager
  );

  g_object_unref(G_OBJECT(gnetwork_conn));
}

static void
inf_connection_manager_init(GTypeInstance* instance,
                            gpointer g_class)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;

  manager = INF_CONNECTION_MANAGER(instance);
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  priv->connections = NULL;
}

static void
inf_connection_manager_dispose(GObject* object)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;
  GNetworkConnection* gnetwork_conn;
  GSList* item;

  manager = INF_CONNECTION_MANAGER(object);
  priv = INF_CONNECTION_MANAGER_PRIVATE(object);

  for(item = priv->connections; item != NULL; item = g_slist_next(item))
  {
    gnetwork_conn = GNETWORK_CONNECTION(item->data);
    inf_connection_manager_free_connection(manager, gnetwork_conn);
  }

  g_slist_free(priv->connections);
  priv->connections = NULL;
}

static void
inf_connection_manager_class_init(gpointer g_class,
                                  gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfConnectionManagerPrivate));

  connection_quark = g_quark_from_static_string(
    "inf-connection-manager-connection"
  );

  object_class->dispose = inf_connection_manager_dispose;
}

GType
inf_connection_manager_get_type(void)
{
  static GType connection_manager_type = 0;

  if(!connection_manager_type)
  {
    static const GTypeInfo connection_manager_type_info = {
      sizeof(InfConnectionManagerClass),  /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      inf_connection_manager_class_init,  /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof(InfConnectionManager),       /* instance_size */
      0,                                  /* n_preallocs */
      inf_connection_manager_init,        /* instance_init */
      NULL                                /* value_table */
    };

    connection_manager_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfConnectionManager",
      &connection_manager_type_info,
      0
    );
  }

  return connection_manager_type;
}

/** inf_connection_manager_new:
 *
 * Creates a new connection manager.
 *
 * Return value: A new #InfConnectionManager.
 **/
InfConnectionManager*
inf_connection_manager_new(void)
{
  GObject* object;

  object = g_object_new(INF_TYPE_CONNECTION_MANAGER, NULL);

  return INF_CONNECTION_MANAGER(object);
}

/** inf_connection_manager_add_connection:
 *
 * @manager A #InfConnectionManager.
 * @connection A #GNetworkConnection that is not yet added to the manager.
 *
 * This function adds a new connection to the connection manager. It holds
 * a reference on the connection until the connection is closed or the
 * connection manager is finalized. Incoming data is forwarded to any
 * associated #InfNetObject objects associated with this connection
 * (see inf_connection_manager_add_object()).
 **/
void
inf_connection_manager_add_connection(InfConnectionManager* manager,
                                      GNetworkConnection* connection)
{
  InfConnectionManagerPrivate* priv;
  GSList* item;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(GNETWORK_IS_CONNECTION(connection));

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  item = g_slist_find(priv->connections, connection);

  g_return_if_fail(item == NULL);

  inf_connection_manager_connection_assoc(connection);
  priv->connections = g_slist_prepend(priv->connections, connection);

  g_signal_connect_after(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(inf_connection_manager_connection_notify_status_cb),
    manager
  );

  g_signal_connect_after(
    G_OBJECT(connection),
    "error",
    G_CALLBACK(inf_connection_manager_connection_error_cb),
    manager
  );
}

/** inf_connection_manager_get_by_address:
 *
 * @manager A #InfConnectionManager.
 * @address The IP address to which to fetch a connection
 * @port The port to which to fetch a connection
 *
 * This function looks for a connection to the given host and port in the
 * currently open connections the manager manages. If none has been found,
 * a new connection is created. The returned connection might not yet be
 * fully established but yet being opened.
 **/
GNetworkTcpConnection*
inf_connection_manager_get_by_address(InfConnectionManager* manager,
                                      const GNetworkIpAddress* address,
                                      guint port)
{
  InfConnectionManagerPrivate* priv;
  GNetworkTcpConnection* tcp;
  GNetworkIpAddress list_address;
  guint list_port;
  gchar* addr_str;
  GSList* item;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(address != NULL, NULL);
  g_return_val_if_fail(port < 65536, NULL);

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  for(item = priv->connections; item != NULL; item = g_slist_next(item))
  {
    if(GNETWORK_IS_TCP_CONNECTION(item->data))
    {
      tcp = GNETWORK_TCP_CONNECTION(item->data);

      g_object_get(
        G_OBJECT(tcp),
        "ip-address", &list_address,
        "port", &list_port,
        NULL
      );

      if(port == list_port)
        if(gnetwork_ip_address_collate(address, &list_address) == 0)
          return tcp;
    }
  }

  /* No result until now, so try with stringified IP address as hostname.
   * This will either return a connection attempt to the same address that
   * has not been translated into a GNetworkIpAddress yet or will attempt
   * a new connection to the remote host. */
  addr_str = gnetwork_ip_address_to_string(address);
  tcp = inf_connection_manager_get_by_hostname(manager, addr_str, port);
  g_free(addr_str);

  return tcp;
}

/** inf_connection_manager_get_by_hostname:
 *
 * @manager A #InfConnectionManager
 * @hostname The name of the host to which to fetch a connection
 * @port The port to which to fetch a connection
 *
 * This function looks for a connection to the given host and port in the
 * currently open connections the manager manages. If none has been found,
 * a new connection is created. The returned connection might not yet be
 * fully established but yet being opened.
 */
GNetworkTcpConnection*
inf_connection_manager_get_by_hostname(InfConnectionManager* manager,
                                       const gchar* hostname,
                                       guint port)
{
  InfConnectionManagerPrivate* priv;
  GNetworkTcpConnection* tcp;
  gchar* list_hostname;
  guint list_port;
  GSList* item;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(hostname != NULL, NULL);
  g_return_val_if_fail(port < 65536, NULL);

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  for(item = priv->connections; item != NULL; item = g_slist_next(item))
  {
    if(GNETWORK_IS_TCP_CONNECTION(item->data))
    {
      tcp = GNETWORK_TCP_CONNECTION(item->data);

      g_object_get(
        G_OBJECT(tcp),
        "address", &list_hostname,
        "port", &list_port,
        NULL
      );

      if(port == list_port)
      {
        if(strcmp(hostname, list_hostname) == 0)
        {
          g_free(list_hostname);
          return tcp;
        }
      }

      g_free(list_hostname);
    }
  }

  /* No connection found, so establish new one */
  tcp = GNETWORK_TCP_CONNECTION(
    g_object_new(
      GNETWORK_TYPE_TCP_CONNECTION,
      "address", hostname,
      "port", port,
      NULL
    )
  );

  inf_connection_manager_add_connection(manager, GNETWORK_CONNECTION(tcp));
  return tcp;
}

/** inf_connection_manager_add_object:
 *
 * @manager A #InfConnectionManager
 * @gnetwork_conn A #GNetworkConnection that is managed by @manager
 * @object An object implementing #InfNetObject
 * @identifier A unique identifier for @object
 *
 * Adds a #InfNetObject to the given #GNetworkConnection. This allows that
 * messages may be sent to the remote site where a #InfNetObject with the
 * same identifier should be registered. Vice-versa, incoming messages
 * addressed to this #InfNetObject are delivered to @object. The object
 * is automatically removed again when it gets finalized.
 **/
void
inf_connection_manager_add_object(InfConnectionManager* manager,
                                  GNetworkConnection* gnetwork_conn,
                                  InfNetObject* object,
                                  const gchar* identifier)
{
  InfConnectionManagerConnection* conn;
  gchar* identifier_copy;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(GNETWORK_IS_CONNECTION(gnetwork_conn));
  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(identifier != NULL);

  conn = g_object_get_qdata(G_OBJECT(gnetwork_conn), connection_quark);
  g_return_if_fail(conn != NULL);

  g_object_weak_ref(
    G_OBJECT(object),
    inf_connection_manager_connection_weak_notify_func,
    conn
  );

  identifier_copy = g_strdup(identifier);
  g_hash_table_insert(conn->objects, identifier_copy, object);
  g_hash_table_insert(conn->identifiers, object, identifier_copy);
}

/** inf_connection_manager_remove_object:
 *
 * @manager A #InfConnectionManager.
 * @gnetwork_conn A #GNetworkConnection that is managed by @manager.
 * @object A #InfNetObject that has been added to @gnetwork_conn by a call
 *         to inf_connection_manager_add_object().
 *
 * Removes #InfNetObject that has previously been added to a
 * #GNetworkConnection by a call to inf_connection_manager_add_object().
 * After this call, @object no longer receives network input from
 * @gnetwork_conn.
 */
void
inf_connection_manager_remove_object(InfConnectionManager* manager,
                                     GNetworkConnection* gnetwork_conn,
                                     InfNetObject* object)
{
  InfConnectionManagerConnection* conn;
  gchar* identifier;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(GNETWORK_IS_CONNECTION(gnetwork_conn));
  g_return_if_fail(INF_IS_NET_OBJECT(object));

  conn = g_object_get_qdata(G_OBJECT(gnetwork_conn), connection_quark);
  g_return_if_fail(conn != NULL);

  identifier = g_hash_table_lookup(conn->identifiers, object);
  g_return_if_fail(identifier != NULL);
 
  g_hash_table_remove(conn->identifiers, object);
  g_hash_table_remove(conn->objects, identifier);

  g_free(identifier);

  g_object_weak_unref(
    G_OBJECT(object),
    inf_connection_manager_connection_weak_notify_func,
    conn
  );
}

/** inf_connection_manager_send_to_object:
 *
 * @manager A #InfConnectionManager
 * @gnetwork_con A #GNetworkConnection managed by @manager
 * @object A #InfNetObject to which to send a message
 * @message The message to send
 *
 * This function will send a XML-based message to the other end of
 * @gnetwork_conn. If there is another #InfConnectionManager on the other
 * end it will forward the message to the #InfNetObject with the same
 * identifier (see inf_connection_manager_add_object()).
 **/
void
inf_connection_manager_send_to_object(InfConnectionManager* manager,
                                      GNetworkConnection* gnetwork_conn,
                                      InfNetObject* object,
                                      xmlNodePtr message)
{
  InfConnectionManagerConnection* conn;
  const gchar* identifier;

  xmlNodePtr header;
  xmlDocPtr doc;
  xmlBufferPtr buffer;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(GNETWORK_IS_CONNECTION(gnetwork_conn));
  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(message != NULL);

  conn = g_object_get_qdata(G_OBJECT(gnetwork_conn), connection_quark);
  g_return_if_fail(conn != NULL);

  identifier = g_hash_table_lookup(conn->identifiers, object);
  g_return_if_fail(identifier != NULL);

  header = xmlNewNode(NULL, (const xmlChar*)"message");
  xmlNewProp(header, (const xmlChar*)"to", (const xmlChar*)identifier);
  xmlAddChild(header, message);

  doc = xmlNewDoc(NULL);
  xmlDocSetRootElement(doc, header);

  buffer = xmlBufferCreate();
  xmlNodeDump(buffer, doc, header, 0, 0);

  xmlUnlinkNode(message);
  xmlFreeDoc(doc);

  gnetwork_connection_send(
    gnetwork_conn,
    xmlBufferContent(buffer),
    xmlBufferLength(buffer)
  );

  xmlBufferFree(buffer);
}
