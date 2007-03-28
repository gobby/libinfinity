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

#include <libinfinity/common/inf-connection-manager.h>

#include <string.h>

typedef struct _InfConnectionManagerPrivate InfConnectionManagerPrivate;
struct _InfConnectionManagerPrivate {
  GSList* connections;
};

/* object-local stuff of connection */
typedef struct _InfConnectionManagerObject InfConnectionManagerObject;
struct _InfConnectionManagerObject {
  InfNetObject* net_object;
  gchar* identifier;
  guint ref_count;

  xmlNodePtr outer_queue;
  xmlNodePtr outer_queue_last_item;
  guint inner_queue_count;
};

typedef struct _InfConnectionManagerConnection InfConnectionManagerConnection;
struct _InfConnectionManagerConnection {
  /* identifier -> InfConnectionManagerObject */
  GHashTable* identifier_connobj_table;
  /* NetObject -> InfConnectionManagerObject */
  GHashTable* object_connobj_table;
};

#define INF_CONNECTION_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_CONNECTION_MANAGER, InfConnectionManagerPrivate))

static GObjectClass* parent_class;
static GQuark connection_quark;

/* Maximal number of XML nodes that are sent to a particular netobject A.
 * If more are to be sent, they are kept in an outer queue so that messages
 * from another netobject B can be sent through the same connection without
 * having to wait until all messages from A have been sent. */
static const guint INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT = 5;

static InfConnectionManagerObject*
inf_connection_manager_object_new(InfNetObject* net_object,
                                  const gchar* identifier)
{
  InfConnectionManagerObject* object;
  object = g_slice_new(InfConnectionManagerObject);

  object->net_object = net_object;
  g_object_ref(G_OBJECT(net_object));

  object->identifier = g_strdup(identifier);
  object->ref_count = 1;
  object->outer_queue = NULL;
  object->outer_queue_last_item = NULL;
  object->inner_queue_count = 0;

  return object;
}

static void
inf_connection_manager_object_free(InfConnectionManagerObject* object)
{
  xmlNodePtr next;

  for(; object->outer_queue != NULL; object->outer_queue = next)
  {
    next = object->outer_queue->next;
    xmlFreeNode(object->outer_queue);
  }

  g_free(object->identifier);

  g_object_unref(G_OBJECT(object));
  g_slice_free(InfConnectionManagerObject, object);
}

/* Sends a list of xml nodes through connection and returns the new list
 * head. Successfully sent nodes are freed. */
static xmlNodePtr
inf_connection_manager_object_real_send(InfConnectionManagerObject* connobj,
                                        InfXmlConnection* xml_conn,
                                        xmlNodePtr xml,
                                        guint max_messages)
{
  xmlNodePtr container;
  xmlNodePtr cur;

  container = xmlNewNode(NULL, (const xmlChar*)"message");

  xmlNewProp(
    container,
    (const xmlChar*)"to",
    (const xmlChar*)connobj->identifier
  );

  /* Increase max messages and count down to one instead of zero. This way,
   * max_messages == 0 means no limit. */
  if(max_messages != 0)
    ++ max_messages;

  while(xml != NULL && max_messages != 1)
  {
    cur = xml;
    xml = xml->next;

    xmlUnlinkNode(cur);
    xmlAddChild(container, cur);

    /* Object was enqueued in inner queue */
    inf_net_object_enqueued(connobj->net_object, xml_conn, cur);

    ++ connobj->inner_queue_count;
    if(max_messages > 1) -- max_messages;
  }

  inf_xml_connection_send(xml_conn, container);
  return xml;
}

static InfConnectionManagerObject*
inf_connection_manager_connection_grab(InfConnectionManagerConnection* conn,
                                       const xmlNodePtr message)
{
  xmlChar* identifier;
  InfConnectionManagerObject* object;

  object = NULL;
  if(strcmp((const char*)message->name, "message") == 0)
  {
    identifier = xmlGetProp(message, (const xmlChar*)"to");
    if(identifier != NULL)
    {
      object = g_hash_table_lookup(
        conn->identifier_connobj_table,
        (const gchar*)identifier
      );

      xmlFree(identifier);
    }
  }

  return object;
}

static void
inf_connection_manager_connection_sent_cb(InfXmlConnection* xml_conn,
                                          const xmlNodePtr xml,
                                          gpointer user_data)
{
  InfConnectionManagerConnection* conn;
  InfConnectionManagerObject* object;
  xmlNodePtr child;

  conn = (InfConnectionManagerConnection*)user_data;
  object = inf_connection_manager_connection_grab(conn, xml);

  /* It may happen that a NetObject sends things but is removed until
   * the data is really sent out, so do not assert here. */
  if(object != NULL)
  {
    for(child = xml->children; child != NULL; child = child->next)
    {
      inf_net_object_sent(object->net_object, xml_conn, child);
      -- object->inner_queue_count;
    }

    if(object->inner_queue_count < INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT)
    {
      /* We actually sent some objects, so we have some space in the
       * inner queue again. */
      object->outer_queue = inf_connection_manager_object_real_send(
        object,
        xml_conn,
        object->outer_queue,
        INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT - object->inner_queue_count
      );

      if(object->outer_queue == NULL)
        object->outer_queue_last_item = NULL;
    }

    g_assert(
      object->inner_queue_count <= INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT
    );
  }
}

static void
inf_connection_manager_connection_received_cb(InfXmlConnection* xml_conn,
                                              const xmlNodePtr xml,
                                              gpointer user_data)
{
  InfConnectionManagerConnection* conn;
  InfConnectionManagerObject* object;
  xmlNodePtr child;

  conn = (InfConnectionManagerConnection*)user_data;
  object = inf_connection_manager_connection_grab(conn, xml);

  if(object != NULL)
  {
    for(child = xml->children; child != NULL; child = child->next)
    {
      inf_net_object_received(object->net_object, xml_conn, child);
    }
  }
}

static void
inf_connection_manager_connection_unassoc(InfXmlConnection* xml_conn)
{
  InfConnectionManagerConnection* conn;
  conn = g_object_steal_qdata(G_OBJECT(xml_conn), connection_quark);

  g_return_if_fail(conn != NULL);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(xml_conn),
    G_CALLBACK(inf_connection_manager_connection_received_cb),
    conn
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(xml_conn),
    G_CALLBACK(inf_connection_manager_connection_sent_cb),
    conn
  );

  g_hash_table_destroy(conn->identifier_connobj_table);
  g_hash_table_destroy(conn->object_connobj_table);

  g_slice_free(InfConnectionManagerConnection, conn);
}

static InfConnectionManagerConnection*
inf_connection_manager_connection_assoc(InfXmlConnection* xml_conn)
{
  InfConnectionManagerConnection* conn;

  conn = g_object_get_qdata(G_OBJECT(xml_conn), connection_quark);
  g_return_val_if_fail(conn == NULL, NULL);

  conn = g_slice_new(InfConnectionManagerConnection);

  g_object_set_qdata(
    G_OBJECT(xml_conn),
    connection_quark,
    conn
  );

  g_signal_connect(
    G_OBJECT(xml_conn),
    "received",
    G_CALLBACK(inf_connection_manager_connection_received_cb),
    conn
  );

  g_signal_connect(
    G_OBJECT(xml_conn),
    "sent",
    G_CALLBACK(inf_connection_manager_connection_sent_cb),
    conn
  );

  /* This maps from InfNetObject to InfConnectionManagerObject. */
  conn->object_connobj_table = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)inf_connection_manager_object_free
  );

  /* Maps identifier to InfConnectionManagerObject. The identifier given to
   * the hash table is exactly the same as the one stored in the
   * InfConnectionManagerObject, so we do not use it as a key_destroy_func,
   * because it is freed with that object. */
  conn->identifier_connobj_table = g_hash_table_new(g_str_hash, g_str_equal);

  return conn;
}

/* Required by inf_connection_manager_connection_notify_status_cb to
 * remove the gnetwork connection if it has been closed. */
static void
inf_connection_manager_free_connection(InfConnectionManager* manager,
                                       InfXmlConnection* xml_conn);

static void
inf_connection_manager_connection_notify_status_cb(InfXmlConnection* conn,
                                                   GParamSpec* pspec,
                                                   gpointer user_data)
{
  InfConnectionManager* manager;
  InfXmlConnectionStatus status;

  g_object_get(G_OBJECT(conn), "status", &status, NULL);

  /* Remove the connection from the list of connections if it has
   * been closed. */
  if(status == INF_XML_CONNECTION_CLOSED ||
     status == INF_XML_CONNECTION_CLOSING)
  {
    manager = INF_CONNECTION_MANAGER(user_data);
    inf_connection_manager_free_connection(manager, conn);
  }
}

static void
inf_connection_manager_free_connection(InfConnectionManager* manager,
                                       InfXmlConnection* xml_conn)
{
  InfConnectionManagerPrivate* priv;
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  inf_connection_manager_connection_unassoc(xml_conn);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(xml_conn),
    G_CALLBACK(inf_connection_manager_connection_notify_status_cb),
    manager
  );

  priv->connections = g_slist_remove(priv->connections, xml_conn);
  g_object_unref(G_OBJECT(xml_conn));
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

  manager = INF_CONNECTION_MANAGER(object);
  priv = INF_CONNECTION_MANAGER_PRIVATE(object);

  while(priv->connections != NULL)
    inf_connection_manager_free_connection(manager, priv->connections->data);

  G_OBJECT_CLASS(parent_class)->dispose(object);
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
 * Return Value: A new #InfConnectionManager.
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
 * @manager: A #InfConnectionManager.
 * @connection: A #InfConnection that is not yet added to the manager.
 *
 * This function adds a new connection to the connection manager. It holds
 * a reference on the connection until the connection is closed or the
 * connection manager is finalized. Incoming data is forwarded to any
 * associated #InfNetObject objects associated with this connection
 * (see inf_connection_manager_add_object()).
 **/
void
inf_connection_manager_add_connection(InfConnectionManager* manager,
                                      InfXmlConnection* connection)
{
  InfConnectionManagerPrivate* priv;
  GSList* item;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  item = g_slist_find(priv->connections, connection);

  g_return_if_fail(item == NULL);

  g_object_ref(G_OBJECT(connection));
  inf_connection_manager_connection_assoc(connection);
  priv->connections = g_slist_prepend(priv->connections, connection);

  g_signal_connect_after(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(inf_connection_manager_connection_notify_status_cb),
    manager
  );
}

/** inf_connection_manager_has_connection:
 *
 * @manager: A #InfConnectionManager.
 * @connection: A #InfConnection.
 *
 * Returns TRUE if @connection was added to @manager and false otherwise.
 *
 * Return Value: Whether @connection is managed by @manager.
 **/
gboolean
inf_connection_manager_has_connection(InfConnectionManager* manager,
                                      InfXmlConnection* connection)
{
  InfConnectionManagerPrivate* priv;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), FALSE);

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  if(g_slist_find(priv->connections, connection) == NULL)
    return FALSE;
  else
    return TRUE;
}

#if 0
/** inf_connection_manager_get_by_address:
 *
 * @manager: A #InfConnectionManager.
 * @address: The IP address to which to fetch a connection
 * @port: The port to which to fetch a connection
 *
 * This function looks for a connection to the given host and port in the
 * currently open connections the manager manages. If none has been found,
 * a new connection is created. The returned connection might not yet be
 * fully established but yet being opened.
 *
 * Return Value: A #GNetworkTcpConnection to the given address.
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
 * @manager: A #InfConnectionManager
 * @hostname: The name of the host to which to fetch a connection
 * @port: The port to which to fetch a connection
 *
 * This function looks for a connection to the given host and port in the
 * currently open connections the manager manages. If none has been found,
 * a new connection is created. The returned connection might not yet be
 * fully established but yet being opened.
 *
 * Return Value: A #GNetworkTcpConnection to the given host.
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
  g_object_unref(G_OBJECT(tcp));

  return tcp;
}
#endif

/** inf_connection_manager_add_object:
 *
 * @manager: A #InfConnectionManager
 * @inf_conn: A #InfConnection that is managed by @manager
 * @object: An object implementing #InfNetObject
 * @identifier: A unique identifier for @object
 *
 * Adds a #InfNetObject to the given #InfConnection. This allows that
 * messages may be sent to the remote site where a #InfNetObject with the
 * same identifier should be registered. Vice-versa, incoming messages
 * addressed to this #InfNetObject are delivered to @object.
 *
 * If the object is already registered, and the identifier does not match
 * the one with which it was previously registered, the function produces
 * an error. Otherwise, a reference count on that object is increased, so
 * that you have to call inf_connection_manager_remove_object() one more
 * time to actually remove the object from the connection manager.
 **/
void
inf_connection_manager_add_object(InfConnectionManager* manager,
                                  InfXmlConnection* xml_conn,
                                  InfNetObject* object,
                                  const gchar* identifier)
{
  InfConnectionManagerConnection* conn;
  InfConnectionManagerObject* connobj;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(INF_IS_XML_CONNECTION(xml_conn));
  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(identifier != NULL);

  conn = g_object_get_qdata(G_OBJECT(xml_conn), connection_quark);
  g_return_if_fail(conn != NULL);

  connobj = g_hash_table_lookup(
    conn->identifier_connobj_table,
    identifier
  );

  if(connobj != NULL)
  {
    g_assert(
      g_hash_table_lookup(conn->object_connobj_table, object) == connobj
    );

    ++ connobj->ref_count;
  }
  else
  {
    g_assert(g_hash_table_lookup(conn->object_connobj_table, object) == NULL);

    connobj = inf_connection_manager_object_new(object, identifier);

    g_hash_table_insert(
      conn->object_connobj_table,
      object,
      connobj
    );

    g_hash_table_insert(
      conn->identifier_connobj_table,
      connobj->identifier,
      connobj
    );
  }
}

/** inf_connection_manager_remove_object:
 *
 * @manager: A #InfConnectionManager.
 * @inf_conn: A #InfConnection that is managed by @manager.
 * @object: A #InfNetObject that has been added to @inf_conn by a call
 *          to inf_connection_manager_add_object().
 *
 * Removes #InfNetObject that has previously been added to a
 * #InfConnection by a call to inf_connection_manager_add_object().
 * After this call, @object no longer receives network input from
 * @inf_conn.
 *
 * This function causes all remaining messages in the outer queue to be
 * flushed, regardless of how many messages are already in the inner queue.
 * If the messages in the outer queue do not need to reach the remote site
 * anymore, you should cancel them before calling this function using
 * inf_connection_manager_cancel_outer().
 **/
void
inf_connection_manager_remove_object(InfConnectionManager* manager,
                                     InfXmlConnection* xml_conn,
                                     InfNetObject* object)
{
  InfConnectionManagerConnection* conn;
  InfConnectionManagerObject* connobj;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(INF_IS_XML_CONNECTION(xml_conn));
  g_return_if_fail(INF_IS_NET_OBJECT(object));

  conn = g_object_get_qdata(G_OBJECT(xml_conn), connection_quark);
  g_return_if_fail(conn != NULL);

  connobj = g_hash_table_lookup(conn->object_connobj_table, object);
  g_return_if_fail(connobj != NULL);

  -- connobj->ref_count;
  if(connobj->ref_count == 0)
  {
    if(connobj->outer_queue != NULL)
    {
      /* TODO: Do not do this if the queue is big, but we need to find a
       * way to specify which messages still have to be flushed. */
      /* Flush outer queue completely */
      connobj->outer_queue = inf_connection_manager_object_real_send(
        connobj,
        xml_conn,
        connobj->outer_queue,
        0
      );
    }
 
    g_hash_table_remove(conn->identifier_connobj_table, connobj->identifier);
    g_hash_table_remove(conn->object_connobj_table, object);
  }
}

/** inf_connection_manager_send:
 *
 * @manager: A #InfConnectionManager
 * @inf_conn: A #InfConnection managed by @manager
 * @object: A #InfNetObject to which to send a message
 * @message: The message to send
 *
 * This function will send a XML-based message to the other end of
 * @inf_conn. If there is another #InfConnectionManager on the other
 * end it will forward the message to the #InfNetObject with the same
 * identifier (see inf_connection_manager_add_object()).
 *
 * The function takes ownership of @message and unlinks it from its current
 * context.
 **/
void
inf_connection_manager_send(InfConnectionManager* manager,
                            InfXmlConnection* xml_conn,
                            InfNetObject* object,
                            xmlNodePtr message)
{
  InfConnectionManagerConnection* conn;
  InfConnectionManagerObject* connobj;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(INF_IS_XML_CONNECTION(xml_conn));
  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(message != NULL);

  conn = g_object_get_qdata(G_OBJECT(xml_conn), connection_quark);
  g_return_if_fail(conn != NULL);

  connobj = g_hash_table_lookup(conn->object_connobj_table, object);
  g_return_if_fail(connobj != NULL);

  if(connobj->inner_queue_count < INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT)
  {
    message = inf_connection_manager_object_real_send(
      connobj,
      xml_conn,
      message,
      1
    );
  }
  else
  {
    /* Message was not sent because inner queue is full, so enqueue it to
     * outer queue and wait until other messages have been sent. */
    if(message != NULL)
    {
      if(connobj->outer_queue_last_item == NULL)
        connobj->outer_queue = message;
      else
        connobj->outer_queue_last_item->next = message;

      message->prev = connobj->outer_queue_last_item;
      message->next = NULL;
      connobj->outer_queue_last_item = message;
    }
  }
}

/** inf_connection_manager_send_multiple:
 *
 * @manager: A #InfConnectionManager
 * @inf_conn: A #InfConnection managed by @manager
 * @object: A #InfNetObject to which to send a message
 * @messages: The messages to send, linked with the next field.
 *
 * This function will send multiple XML-based messages to the other end of
 * @inf_conn. If there is another #InfConnectionManager on the other
 * end it will forward the messages to the #InfNetObject with the same
 * identifier (see inf_connection_manager_add_object()).
 *
 * The function takes ownership of the list of messages and unlinks them
 * from their current context.
 **/
void
inf_connection_manager_send_multiple(InfConnectionManager* manager,
                                     InfXmlConnection* xml_conn,
                                     InfNetObject* object,
                                     xmlNodePtr messages)
{
  InfConnectionManagerConnection* conn;
  InfConnectionManagerObject* connobj;
  xmlNodePtr cur;
  xmlNodePtr prev;
  xmlNodePtr next;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(INF_IS_XML_CONNECTION(xml_conn));
  g_return_if_fail(INF_IS_NET_OBJECT(object));

  conn = g_object_get_qdata(G_OBJECT(xml_conn), connection_quark);
  g_return_if_fail(conn != NULL);

  connobj = g_hash_table_lookup(conn->object_connobj_table, object);
  g_return_if_fail(connobj != NULL);

  if(connobj->inner_queue_count < INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT)
  {
    messages = inf_connection_manager_object_real_send(
      connobj,
      xml_conn,
      messages,
      INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT - connobj->inner_queue_count
    );
  }

  /* Not all messages could be sent, so enqueue in outer queue. */
  if(messages != NULL)
  {
    /* Detach nodes from their current context, but keep them linked */
    prev = NULL;
    for(cur = messages; cur != NULL; cur = next)
    {
      next = cur->next;
      xmlUnlinkNode(cur);

      /* Do not set cur->next because it would be unlinked in the
       * next iteration, anyway. */
      cur->prev = prev;
      if(prev != NULL) prev->next = cur;

      prev = cur;
    }

    /* prev now contains the last node in the list */

    if(connobj->outer_queue_last_item == NULL)
      connobj->outer_queue = messages;
    else
      connobj->outer_queue_last_item->next = messages;

    messages->prev = connobj->outer_queue_last_item;
    prev->next = NULL;
    connobj->outer_queue_last_item = prev;
  }
}

/** inf_connection_manager_cancel_outer:
 *
 * @manager: A #InfConnectionManager.
 * @inf_conn: A #InfConnection managed by @manager
 * @object: A #InfNetObject.
 *
 * Cancels all messages that were registered to be sent by
 * inf_connection_manager_send() or inf_connection_manager_send_multiple()
 * and that have not yet been enqueued. Sending already enqueued messages
 * cannot be cancelled.
 **/
void
inf_connection_manager_cancel_outer(InfConnectionManager* manager,
                                    InfXmlConnection* xml_conn,
                                    InfNetObject* object)
{
  InfConnectionManagerConnection* conn;
  InfConnectionManagerObject* connobj;
  xmlNodePtr next;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(INF_IS_XML_CONNECTION(xml_conn));
  g_return_if_fail(INF_IS_NET_OBJECT(object));

  conn = g_object_get_qdata(G_OBJECT(xml_conn), connection_quark);
  g_return_if_fail(conn != NULL);

  connobj = g_hash_table_lookup(conn->object_connobj_table, object);
  g_return_if_fail(connobj != NULL);

  /* Clear outer queue */
  for(; connobj->outer_queue != NULL; connobj->outer_queue = next)
  {
    next = connobj->outer_queue->next;
    xmlFreeNode(connobj->outer_queue);
  }

  connobj->outer_queue_last_item = NULL;
}
