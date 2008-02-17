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

/**
 * SECTION:infconnectionmanager
 * @short_description: Infinote connection manager
 * @include: libinfinity/common/inf-connection-manager.h
 * @see_also: #InfConnectionManager
 *
 * The connection manager handles all connections used in the infinote
 * framework. It allows sharing a connection for different (internal)
 * recipients, so two infinote sessions can use the same connection to
 * send and receive data from other collaborators.
 *
 * The key concept is that of so-called (connection manager) groups. A group
 * is identified by its name and its publisher. Hosts can create and join
 * groups within the network, and send messages to others within the same
 * group. The publisher of a group is the host that created the group and
 * is identified by a string representation of its (unique) address in that
 * network (this is IP address/Port number with TCP, or JID and resource in
 * the jabber network). The connection manager allows lookup by that address
 * within a group and a network.
 *
 * Messages can either be sent to a single group member or to the whole
 * group.
 *
 * How the actual message transmission is performed is defined by so-called
 * methods. When opening a group, the publisher can define the method used
 * for each network the group is present in. Such a method can be central
 * (all messages are sent via the publisher), decentral (messages are
 * directly sent to each other) or even different, such as jabber groupchat.
 * The publisher relays between different networks in case messages are sent
 * to the whole group.
 *
 * If the method allows, a group can continue to exist after the publisher
 * is gone. However, since the publisher cannot forward to members of the
 * group from other networks, only the members within the same network can
 * still see each other.
 **/

#include <libinfinity/common/inf-connection-manager.h>
#include <libinfinity/common/inf-xml-util.h>

#include <libxml/xmlsave.h>

#include <string.h>

typedef enum _InfConnectionManagerMsgType {
  INF_CONNECTION_MANAGER_MESSAGE,
  INF_CONNECTION_MANAGER_CONTROL
} InfConnectionManagerMsgType;

/* These are only used internally */
typedef enum _InfConnectionManagerError {
  INF_CONNECTION_MANAGER_ERROR_UNEXPECTED_MESSAGE
} InfConnectionManagerError;

typedef struct _InfConnectionManagerKey InfConnectionManagerKey;
struct _InfConnectionManagerKey {
  gchar* group_name;
  gchar* publisher_id;
};

typedef struct _InfConnectionManagerMethodInstance
  InfConnectionManagerMethodInstance;
struct _InfConnectionManagerMethodInstance {
  const InfConnectionManagerMethodDesc* desc;
  InfConnectionManagerMethod* method;
};

typedef struct _InfConnectionManagerQueue InfConnectionManagerQueue;
struct _InfConnectionManagerQueue {
  InfXmlConnection* connection;

  xmlNodePtr first_item;
  xmlNodePtr last_item;
  guint inner_count;
};

struct _InfConnectionManagerGroup {
  InfConnectionManagerKey key;

  InfConnectionManager* manager;
  guint ref_count;

  InfNetObject* object;
  InfXmlConnection* publisher_conn;

  GSList* methods;
  GSList* queues;
};

typedef struct _InfConnectionManagerPrivate InfConnectionManagerPrivate;
struct _InfConnectionManagerPrivate {
  GHashTable* registered_connections;
  GTree* groups;
};

#define INF_CONNECTION_MANAGER_PRIVATE(obj) ( \
  G_TYPE_INSTANCE_GET_PRIVATE( \
    (obj), \
    INF_TYPE_CONNECTION_MANAGER, \
    InfConnectionManagerPrivate \
  ) \
)

static const guint INNER_QUEUE_LIMIT = 5;
static GObjectClass* parent_class;
static GQuark inf_connection_manager_error_quark;

/*
 * Boxed semantics
 */
static InfConnectionManagerGroup*
inf_connection_manager_group_boxed_copy(InfConnectionManagerGroup* group)
{
  inf_connection_manager_group_ref(group);
  return group;
}

static void
inf_connection_manager_group_boxed_free(InfConnectionManagerGroup* group)
{
  inf_connection_manager_group_unref(group);
}

static int
inf_connection_manager_key_cmp(gconstpointer first,
                               gconstpointer second,
                               gpointer user_data)
{
  InfConnectionManagerKey* first_key;
  InfConnectionManagerKey* second_key;
  int res;

  first_key = (InfConnectionManagerKey*)first;
  second_key = (InfConnectionManagerKey*)second;

  res = strcmp(first_key->group_name, second_key->group_name);
  if(res != 0) return res;

  if(first_key->publisher_id == NULL)
  {
    if(second_key->publisher_id == NULL)
      return 0;
    else
      return -1;
  }
  else if(second_key->publisher_id == NULL)
    return 1;
  else
    return strcmp(first_key->publisher_id, second_key->publisher_id);
}

static void
inf_connection_manager_object_weak_unrefed(gpointer user_data,
                                           GObject* object)
{
  InfConnectionManagerGroup* group;
  group = (InfConnectionManagerGroup*)user_data;

  group->object = NULL;
  g_warning(
    "NetObject of group %s, publisher %s was finalized before the group was "
    "finalized",
    group->key.group_name,
    group->key.publisher_id ? group->key.publisher_id : "(self)"
  );
}

static xmlNodePtr
inf_connection_manager_group_real_send(InfConnectionManagerGroup* group,
                                       InfConnectionManagerQueue* queue,
                                       xmlNodePtr xml,
                                       guint max)
{
  xmlNodePtr cur;
  xmlNodePtr container;
  InfXmlConnection* connection;
  InfConnectionManagerScope scope;
  InfConnectionManagerScope cur_scope;
  InfConnectionManagerMsgType type;
  InfConnectionManagerMsgType cur_type;

  const gchar* publisher_id;
  gchar* own_publisher_id;

  g_assert(group->object != NULL);
  connection = queue->connection;

  /* TODO: Cache in queue? */
  if(group->key.publisher_id != NULL)
  {
    own_publisher_id = NULL;
    publisher_id = group->key.publisher_id;
  }
  else
  {
    g_object_get(G_OBJECT(connection), "local-id", &own_publisher_id, NULL);
    publisher_id = own_publisher_id;
  }

  /* max==0 means all messages */
  if(max == 0) max = ~(guint)0;

  container = NULL;
  while(xml != NULL && max > 0)
  {
    cur = xml;
    xml = xml->next;
    -- max;
    xmlUnlinkNode(cur);

    cur_scope = GPOINTER_TO_UINT(cur->_private) & 0xff;
    cur_type = GPOINTER_TO_UINT(cur->_private) >> 8;

    if(container == NULL || scope != cur_scope || type != cur_type)
    {
      if(container != NULL)
        inf_xml_connection_send(connection, container);

      type = cur_type;
      scope = cur_scope;

      switch(cur_type)
      {
      case INF_CONNECTION_MANAGER_MESSAGE:
        container = xmlNewNode(NULL, (const xmlChar*)"group");
        break;
      case INF_CONNECTION_MANAGER_CONTROL:
        container = xmlNewNode(NULL, (const xmlChar*)"control");
        break;
      default:
        g_assert_not_reached();
        break;
      }

      inf_xml_util_set_attribute(container, "publisher", publisher_id);
      inf_xml_util_set_attribute(container, "name", group->key.group_name);

      switch(cur_scope)
      {
      case INF_CONNECTION_MANAGER_POINT_TO_POINT:
        break;
      case INF_CONNECTION_MANAGER_NETWORK:
        inf_xml_util_set_attribute(container, "scope", "net");
        break;
      case INF_CONNECTION_MANAGER_GROUP:
        inf_xml_util_set_attribute(container, "scope", "group");
        break;
      default:
        g_assert_not_reached();
        break;
      }
    }

    xmlAddChild(container, cur);
    inf_net_object_enqueued(group->object, connection, cur);
    ++ queue->inner_count;
  }

  if(container != NULL)
    inf_xml_connection_send(connection, container);

  g_free(own_publisher_id);
  return xml;
}

static InfConnectionManagerQueue*
inf_connection_manager_group_lookup_queue(InfConnectionManagerGroup* group,
                                          InfXmlConnection* connection)
{
  GSList* item;
  InfConnectionManagerQueue* queue;

  for(item = group->queues; item != NULL; item = g_slist_next(item))
  {
    queue = (InfConnectionManagerQueue*)item->data;
    if(queue->connection == connection)
      return queue;
  }

  return NULL;
}

static InfConnectionManagerMethodInstance*
inf_connection_manager_get_method_by_network(InfConnectionManagerGroup* group,
                                             const gchar* network)
{
  GSList* item;
  InfConnectionManagerMethodInstance* instance;

  for(item = group->methods; item != NULL; item = g_slist_next(item))
  {
    instance = (InfConnectionManagerMethodInstance*)item->data;
    if(strcmp(network, instance->desc->network) == 0)
      return instance;
  }

  return NULL;
}

/* Get by connection's network */
static InfConnectionManagerMethodInstance*
inf_connection_manager_get_method_by_connection(InfConnectionManagerGroup* g,
                                                InfXmlConnection* conn)
{
  gchar* network;
  InfConnectionManagerMethodInstance* instance;

  g_object_get(G_OBJECT(conn), "network", &network, NULL);
  instance = inf_connection_manager_get_method_by_network(g, network);
  g_free(network);

  return instance;
}

static void
inf_connection_manager_group_free(gpointer group_)
{
  InfConnectionManagerGroup* group;
  GSList* item;
  InfConnectionManagerMethodInstance* instance;

  group = (InfConnectionManagerGroup*)group_;
  g_assert(group->ref_count == 0);


  for(item = group->methods; item != NULL; item = g_slist_next(item))
  {
    instance = (InfConnectionManagerMethodInstance*)item->data;
    instance->desc->finalize(instance->method);
  }
  g_slist_free(group->methods);

  if(group->queues != NULL)
  {
    g_warning(
      "Group %s, publisher %s is being finalized, but has still registered "
      "connections. The method's finalize should have unregistered them.",
      group->key.group_name,
      group->key.publisher_id ? group->key.publisher_id : "(self)"
    );

    while(group->queues != NULL)
    {
      inf_connection_manager_unregister_connection(
        group,
        ((InfConnectionManagerQueue*)group->queues->data)->connection
      );
    }
  }

  if(group->object != NULL)
  {
    g_object_weak_unref(
      G_OBJECT(group->object),
      inf_connection_manager_object_weak_unrefed,
      group
    );
  }

  if(group->publisher_conn != NULL)
    g_object_unref(group->publisher_conn);

  g_free(group->key.publisher_id);
  g_free(group->key.group_name);

  g_slice_free(InfConnectionManagerGroup, group);
}

static gboolean
inf_connection_manager_handle_message(InfConnectionManager* manager,
                                      InfXmlConnection* connection,
                                      xmlNodePtr xml,
                                      GError** error)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerGroup* group;
  InfConnectionManagerQueue* queue;
  InfConnectionManagerKey key;

  InfConnectionManagerMethodInstance* instance;
  InfConnectionManagerMsgType msg_type;
  InfConnectionManagerScope scope;

  xmlNodePtr child;
  xmlChar* group_name;
  xmlChar* publisher;
  xmlChar* scope_attr;
  gchar* own_id;
  gchar* other_id;

  gboolean can_forward;
  GSList* it;
  InfConnectionManagerMethodInstance* other_instance;
  GError* local_error;
  xmlBufferPtr buffer;
  xmlSaveCtxtPtr ctx;

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  if(strcmp((const char*)xml->name, "group") == 0)
    msg_type = INF_CONNECTION_MANAGER_MESSAGE;
  else if(strcmp((const char*)xml->name, "control") == 0)
    msg_type = INF_CONNECTION_MANAGER_CONTROL;
  else
  {
    g_set_error(
      error,
      inf_connection_manager_error_quark,
      INF_CONNECTION_MANAGER_ERROR_UNEXPECTED_MESSAGE,
      "Received unexpected message '%s'",
      (const gchar*)xml->name
    );
    
    return FALSE;
  }

  group_name = inf_xml_util_get_attribute_required(xml, "name", error);
  if(group_name == NULL) return FALSE;

  publisher = inf_xml_util_get_attribute_required(xml, "publisher", error);
  if(publisher == NULL) { xmlFree(group_name); return FALSE; }

  /* Create key to lookup group, publisher must be NULL if local host is
   * the publisher. */
  g_object_get(G_OBJECT(connection), "local-id", &own_id, NULL);
  if(strcmp((const char*)publisher, own_id) == 0)
    key.publisher_id = NULL;
  else
    key.publisher_id = (gchar*)publisher;
  key.group_name = (gchar*)group_name;
  g_free(own_id);

  /* Find scope */
  switch(msg_type)
  {
  case INF_CONNECTION_MANAGER_MESSAGE:
    scope_attr = xmlGetProp(xml, (const xmlChar*)"scope");
    if(scope_attr != NULL)
    {
      if(strcmp((const char*)scope_attr, "net") == 0)
        scope = INF_CONNECTION_MANAGER_NETWORK;
      else if(strcmp((const char*)scope_attr, "group") == 0)
        scope = INF_CONNECTION_MANAGER_GROUP;
      else
        scope = INF_CONNECTION_MANAGER_POINT_TO_POINT;
    }
    else
    {
      scope = INF_CONNECTION_MANAGER_POINT_TO_POINT;
    }

    break;
  case INF_CONNECTION_MANAGER_CONTROL:
    scope = INF_CONNECTION_MANAGER_POINT_TO_POINT;
    break;
  default:
    g_assert_not_reached();
    break;
  }

  /* Relookup group each time since callbacks could destroy or
   * replace group. */
  for(child = xml->children; child != NULL; child = child->next)
  {
    group = g_tree_lookup(priv->groups, &key);
    if(group != NULL)
    {
      /* Must have been registered to be processed: */
      queue = inf_connection_manager_group_lookup_queue(group, connection);
      if(queue != NULL)
      {
        instance = inf_connection_manager_get_method_by_connection(
          group,
          connection
        );

        g_assert(instance != NULL);
        g_assert(group->object != NULL);

        switch(msg_type)
        {
        case INF_CONNECTION_MANAGER_MESSAGE:
          local_error = NULL;
          can_forward = inf_net_object_received(
            group->object,
            connection,
            child,
            &local_error
          );

          instance->desc->receive_msg(
            instance->method,
            scope,
            can_forward,
            connection,
            child
          );

          if(can_forward && scope == INF_CONNECTION_MANAGER_GROUP)
          {
            for(it = group->methods; it != NULL; it = g_slist_next(it))
            {
              other_instance =
                (InfConnectionManagerMethodInstance*)it->data;

              if(other_instance != instance)
              {
                other_instance->desc->send_to_net(
                  other_instance->method,
                  NULL,
                  xmlCopyNode(child, 1)
                );
              }
            }
          }

          if(local_error != NULL)
          {
            buffer = xmlBufferCreate();

            /* TODO: Use the locale's encoding? */
            ctx = xmlSaveToBuffer(buffer, "UTF-8", XML_SAVE_FORMAT);
            xmlSaveTree(ctx, child);
            xmlSaveClose(ctx);

            g_object_get(G_OBJECT(connection), "remote-id", &other_id, NULL);

            g_warning(
              "Received bad XML request from %s: %s\n\nThe request could not "
              "be processed, thus the session is no longer guaranteed to be "
              "in a consistent state. Subsequent requests might therefore "
              "fail as well. The failed request was:\n\n%s\n\n",
              other_id,
              local_error->message,
              (const gchar*)xmlBufferContent(buffer)
            );

            g_free(other_id);
            xmlBufferFree(buffer);
            g_error_free(local_error);
          }

          break;
        case INF_CONNECTION_MANAGER_CONTROL:
          instance->desc->receive_ctrl(instance->method, connection, child);
          break;
        default:
          g_assert_not_reached();
          break;
        }
      }
    }
  }

  xmlFree(group_name);
  xmlFree(publisher);

  return TRUE;
}

static void
inf_connection_manager_connection_received_cb(InfXmlConnection* connection,
                                              xmlNodePtr xml,
                                              gpointer user_data)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;
  gchar* other_id;
  GError* error;

  manager = INF_CONNECTION_MANAGER(user_data);
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  error = NULL;
  
  inf_connection_manager_handle_message(manager, connection, xml, &error);

  if(error != NULL)
  {
    g_object_get(G_OBJECT(connection), "remote-id", &other_id, NULL);
    g_warning(
      "Received bad XML request from %s: %s",
      other_id,
      error->message
    );
    g_free(other_id);
    g_error_free(error);
  }
}

static void
inf_connection_manager_connection_sent_cb(InfXmlConnection* connection,
                                          xmlNodePtr xml,
                                          gpointer user_data)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerGroup* group;
  InfConnectionManagerQueue* queue;
  InfConnectionManagerKey key;
  InfConnectionManagerMsgType msg_type;

  xmlNodePtr child;
  xmlChar* group_name;
  xmlChar* publisher;
  gchar* own_id;
  guint messages_sent;

  manager = INF_CONNECTION_MANAGER(user_data);
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  if(strcmp((const char*)xml->name, "group") == 0)
    msg_type = INF_CONNECTION_MANAGER_MESSAGE;
  else if(strcmp((const char*)xml->name, "control") == 0)
    msg_type = INF_CONNECTION_MANAGER_CONTROL;
  else
    /* We should not have sent such nonsense */
    g_assert_not_reached();

  group_name = xmlGetProp(xml, (const xmlChar*)"name");
  publisher = xmlGetProp(xml, (const xmlChar*)"publisher");
  /* We should not have sent such nonsense: */
  g_assert(group_name != NULL && publisher != NULL);

  g_object_get(G_OBJECT(connection), "local-id", &own_id, NULL);
  if(strcmp((const char*)publisher, own_id) == 0)
    key.publisher_id = NULL;
  else
    key.publisher_id = (gchar*)publisher;
  key.group_name = (gchar*)group_name;
  g_free(own_id);

  messages_sent = 0;
  group = g_tree_lookup(priv->groups, &key);
  xmlFree(group_name);
  xmlFree(publisher);

  /* Group might have been removed in the meanwhile, so do not assert here */
  if(group != NULL)
  {
    queue = inf_connection_manager_group_lookup_queue(group, connection);
    if(queue != NULL)
    {
      for(child = xml->children; child != NULL; child = child->next)
      {
        g_assert(group->object);
        inf_net_object_sent(group->object, connection, child);
        /* TODO: Check that group still exists, and that it is the same */
        ++ messages_sent;
      }

      queue->inner_count -= messages_sent;
      queue->first_item = inf_connection_manager_group_real_send(
        group,
        queue,
        queue->first_item,
        INNER_QUEUE_LIMIT - queue->inner_count
      );
      if(queue->first_item == NULL)
        queue->last_item = NULL;
    }
  }
}

static void
inf_connection_manager_init(GTypeInstance* instance,
                            gpointer g_class)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;

  manager = INF_CONNECTION_MANAGER(instance);
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  priv->registered_connections = g_hash_table_new(NULL, NULL);

  priv->groups = g_tree_new_full(
    inf_connection_manager_key_cmp,
    NULL,
    NULL,
    inf_connection_manager_group_free
  );
}

static void
inf_connection_manager_dispose(GObject* object)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;

  manager = INF_CONNECTION_MANAGER(object);
  priv = INF_CONNECTION_MANAGER_PRIVATE(object);

  g_tree_destroy(priv->groups);
  g_hash_table_unref(priv->registered_connections);

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

  inf_connection_manager_error_quark =
    g_quark_from_static_string("INF_CONNECTION_MANAGER_ERROR");

  object_class->dispose = inf_connection_manager_dispose;
}

GType
inf_connection_manager_group_get_type(void)
{
  static GType connection_manager_group_type = 0;

  if(!connection_manager_group_type)
  {
    connection_manager_group_type = g_boxed_type_register_static(
      "InfConnectionManagerGroup",
      (GBoxedCopyFunc)inf_connection_manager_group_boxed_copy,
      (GBoxedFreeFunc)inf_connection_manager_group_boxed_free
    );
  }
  
  return connection_manager_group_type;
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

/**
 * inf_connection_manager_new:
 *
 * Creates a new #InfConnectionManager object.
 *
 * Returns: A #InfConnectionManager.
 **/
InfConnectionManager*
inf_connection_manager_new(void)
{
  GObject* object;
  object = g_object_new(INF_TYPE_CONNECTION_MANAGER, NULL);
  return INF_CONNECTION_MANAGER(object);
}

/**
 * inf_connection_manager_open_group:
 * @manager: A #InfConnectionManager.
 * @group_name: A name for the group to open.
 * @net_object: A #InfNetObject to receive messages, or %NULL.
 * @methods: %NULL-terminated array ofcommunication methods, one for each
 * network.
 *
 * Opens a new group with the local host as publisher. @group_name
 * specifies the name for the group. You cannot open two groups with the same
 * name, but it is possible to join groups from other publishers with the
 * same name (see inf_connection_manager_join_group()).
 *
 * All messages received are reported to @net_object. If @net_object is
 * %NULL, you can later set it using inf_connection_manager_group_set_object().
 * You must do this before the first message arrives (i.e. before returning
 * to the main loop) since receiving a message without @net_object is
 * considered an error.
 *
 * @methods specifies the methods to be used for each network to support.
 *
 * Returns: A new #InfConnectionManagerGroup.
 **/
InfConnectionManagerGroup*
inf_connection_manager_open_group(InfConnectionManager* manager,
                                  const gchar* group_name,
                                  InfNetObject* net_object,
                                  InfConnectionManagerMethodDesc** methods)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerGroup* group;
  InfConnectionManagerMethodDesc** desc;
  InfConnectionManagerMethodInstance* instance;
  
  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);
  g_return_val_if_fail(
    net_object == NULL || INF_IS_NET_OBJECT(net_object),
    NULL
  );
  g_return_val_if_fail(methods != NULL && *methods != NULL, NULL);
  
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  group = g_slice_new(InfConnectionManagerGroup);

  group->key.group_name = g_strdup(group_name);
  group->key.publisher_id = NULL;

  group->manager = manager;
  group->ref_count = 1;
  group->publisher_conn = NULL;

  group->object = NULL;
  if(net_object != NULL)
    inf_connection_manager_group_set_object(group, net_object);

  g_tree_insert(priv->groups, &group->key, group);

  group->queues = NULL;
  group->methods = NULL;
  for(desc = methods; *desc != NULL; ++ desc)
  {
    instance = g_slice_new(InfConnectionManagerMethodInstance);
    instance->desc = *desc;
    instance->method = instance->desc->open(instance->desc, group);
    group->methods = g_slist_prepend(group->methods, instance);
  }

  return group;
}

/**
 * inf_connection_manager_join_group:
 * @manager: A #InfConnectionManager.
 * @group_name: The name of the group to join.
 * @publisher_conn: A #InfXmlConnection to the publisher.
 * @object: A #InfNetObject, or %NULL.
 * @meth: The communication method to use.
 *
 * Joins a group that was published on a different host. @publisher_conn
 * must be an open connection to the publisher. @group_name specifies the 
 * name of the group to join. It is not possible to join a group twice. You
 * can, however, join a group with the same name but another publisher.
 *
 * All messages received are reported to @net_object. If @net_object is
 * %NULL, you can later set it using inf_connection_manager_group_set_object().
 * You must do this before the first message arrives (i.e. before returning
 * to the main loop) since receiving a message without @net_object is
 * considered an error.
 *
 * The "network" field of @method must match the network of @publisher_conn.
 *
 * Returns: A new #InfConnectionManagerGroup.
 **/
InfConnectionManagerGroup*
inf_connection_manager_join_group(InfConnectionManager* manager,
                                  const gchar* group_name,
                                  InfXmlConnection* publisher_conn,
                                  InfNetObject* object,
                                  const InfConnectionManagerMethodDesc* meth)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerGroup* group;
  InfConnectionManagerMethodInstance* instance;
  
  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(publisher_conn), NULL);
  g_return_val_if_fail(object == NULL || INF_IS_NET_OBJECT(object), NULL);
  g_return_val_if_fail(meth != NULL, NULL);

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  group = g_slice_new(InfConnectionManagerGroup);

  group->key.group_name = g_strdup(group_name);
  g_object_get(
    G_OBJECT(publisher_conn),
    "remote-id",
    &group->key.publisher_id,
    NULL
  );

  group->manager = manager;
  group->ref_count = 1;
  group->publisher_conn = publisher_conn;
  g_object_ref(publisher_conn);

  group->object = NULL;
  if(object != NULL) inf_connection_manager_group_set_object(group, object);

  g_tree_insert(priv->groups, &group->key, group);
  group->queues = NULL;

  instance = g_slice_new(InfConnectionManagerMethodInstance);
  instance->desc = meth;

  group->methods = NULL;
  instance->method =
    instance->desc->join(instance->desc, group, publisher_conn);
  group->methods = g_slist_prepend(NULL, instance);

  return group;
}

/**
 * inf_connection_manager_lookup_group:
 * @manager: A #InfConnectionManager.
 * @group_name: The name of the group to lookup.
 * @publisher: A connection to the publisher of the group, or %NULL.
 *
 * If @publisher is non-%NULL, then this function tries to find a group
 * with the given name and @publisher that has been joined before (i.e.
 * that has previously been created with inf_connection_manager_join_group()).
 *  If @publisher is %NULL, then it tries to find a group of which the local
 * host is publisher (i.e. that has previously been opened with
 * inf_connection_manager_open_group()).
 *
 * Returns: A #InfConnectionManagerGroup, or %NULL.
 **/
InfConnectionManagerGroup*
inf_connection_manager_lookup_group(InfConnectionManager* manager,
                                    const gchar* group_name,
                                    InfXmlConnection* publisher)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerKey key;
  gpointer result;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);
  g_return_val_if_fail(
    publisher == NULL || INF_IS_XML_CONNECTION(publisher),
    NULL
  );

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  key.group_name = (gchar*)group_name;

  if(publisher != NULL)
    g_object_get(G_OBJECT(publisher), "remote-id", &key.publisher_id, NULL);
  else
    key.publisher_id = NULL;

  result = g_tree_lookup(priv->groups, &key);
  g_free(key.publisher_id);

  return (InfConnectionManagerGroup*)result;
}

/**
 * inf_connection_manager_lookup_group_by_id:
 * @manager: A #InfConnectionManager.
 * @group_name: The name of the group to lookup.
 * @publisher_id: The ID of the connection to the publisher.
 *
 * If @publisher_id is non-%NULL, then this function tries to find a joined
 * group (i.e. one that was created with a previous call to 
 * inf_connection_manager_join_group()) with the given name and whose publisher
 * connection has the given ID. In contrast to
 * inf_connection_manager_lookup_group() this still works when the publisher
 * connection is no longer available.
 *
 * If @publisher is %NULL, then it tries to find a group of which the local
 * host is publisher (i.e. that has previously been opened with
 * inf_connection_manager_open_group()).
 *
 * Returns: A #InfConnectionManagerGroup, or %NULL.
 **/
InfConnectionManagerGroup*
inf_connection_manager_lookup_group_by_id(InfConnectionManager* manager,
                                          const gchar* group_name,
                                          const gchar* publisher_id)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerKey key;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  key.group_name = (gchar*)group_name;
  key.publisher_id = (gchar*)publisher_id;

  return (InfConnectionManagerGroup*)g_tree_lookup(priv->groups, &key);
}

/**
 * inf_connection_manager_group_ref:
 * @group: A #InfConnectionManagerGroup.
 *
 * Increases the reference count of @group by one.
 **/
void
inf_connection_manager_group_ref(InfConnectionManagerGroup* group)
{
  ++ group->ref_count;
}

/**
 * inf_connection_manager_group_unref:
 * @group: A #InfConnectionManagerGroup.
 *
 * Decreases the reference count of @group by one. If the reference count
 * reaches zero, the group is destroyed. This means that the local host
 * is no longer part of that group. If the local host was publisher, then
 * the group name can be reused (although it should not, since the old
 * group can still continue to exist without publisher if the method
 * allows).
 */
void
inf_connection_manager_group_unref(InfConnectionManagerGroup* group)
{
  InfConnectionManagerPrivate* priv;
  priv = INF_CONNECTION_MANAGER_PRIVATE(group->manager);

  -- group->ref_count;
  if(group->ref_count == 0)
    g_tree_remove(priv->groups, &group->key);
}

/**
 * inf_connection_manager_group_get_method_for_network:
 * @g: A #InfConnectionManagerGroup.
 * @network: A network identifier, such as "local" or "jabber".
 *
 * Returns the communication method @grp uses to communicate within @network,
 * or %NULL if @network is not supported.
 *
 * Returns: A #InfConnectionManagerMethodDesc, or %NULL.
 **/
const InfConnectionManagerMethodDesc*
inf_connection_manager_group_get_method_for_network(InfConnectionManagerGroup* g,
                                                    const gchar* network)
{
  InfConnectionManagerMethodInstance* instance;

  g_return_val_if_fail(g != NULL, NULL);
  g_return_val_if_fail(network != NULL, NULL);

  instance = inf_connection_manager_get_method_by_network(g, network);
  if(instance == NULL) return NULL;
  return instance->desc;
}

/**
 * inf_connection_manager_group_set_object:
 * @group: A #InfConnectionManagerGroup.
 * @object: A #InfNetObject.
 *
 * Sets the #InfNetObject that receives incoming messages. This can only be
 * used if there is not already a #InfNetObject set. This means, you must
 * have passed %NULL for @net_object in inf_connection_manager_open_group()
 * or inf_connection_manager_join_group().
 **/
void
inf_connection_manager_group_set_object(InfConnectionManagerGroup* group,
                                        InfNetObject* object)
{
  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(group->object == NULL);

  group->object = object;

  g_object_weak_ref(
    G_OBJECT(object),
    inf_connection_manager_object_weak_unrefed,
    group
  );
}

/**
 * inf_connection_manager_group_get_publisher:
 * @group: A #InfConnectionManagerGroup.
 *
 * Returns a connection to the publisher of @group, or %NULL if the
 * publisher is no longer available or the local host is the publisher.
 *
 * Return Value: A #InfXmlConnection, or %NULL.
 **/
InfXmlConnection*
inf_connection_manager_group_get_publisher(InfConnectionManagerGroup* group)
{
  g_return_val_if_fail(group != NULL, NULL);
  return group->publisher_conn;
}

/**
 * inf_connection_manager_group_get_publisher_id:
 * @grp: A #InfConnectionManagerGroup.
 *
 * Returns the connection ID of the publisher of @group, or %NULL if the local
 * host is the publisher. This still returns a sensible value when the
 * publisher is no longer available.
 *
 * Return Value: A publisher's ID, or %NULL.
 **/
const gchar*
inf_connection_manager_group_get_publisher_id(InfConnectionManagerGroup* grp)
{
  g_return_val_if_fail(grp != NULL, NULL);
  return grp->key.publisher_id;
}

/**
 * inf_connection_manager_has_connection:
 * @group: A #InfConnectionManagerGroup.
 * @conn: A #InfXmlConnection.
 *
 * Returns whether @connection is a member of @group (see 
 * inf_connection_manager_group_add_connection() and
 * inf_connection_manager_group_remove_connection()).
 **/
gboolean
inf_connection_manager_group_has_connection(InfConnectionManagerGroup* group,
                                            InfXmlConnection* conn)
{
  InfConnectionManagerMethodInstance* instance;

  g_return_val_if_fail(group != NULL, FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(conn), FALSE);

  instance = inf_connection_manager_get_method_by_connection(group, conn);
  return instance->desc->has_connection(instance->method, conn);
}

/**
 * inf_connection_manager_group_get_name:
 * @group: A #InfConnectionManagerGroup.
 *
 * Returns the name of @group.
 *
 * Returns: The name of the group.
 **/
const gchar*
inf_connection_manager_group_get_name(InfConnectionManagerGroup* group)
{
  g_return_val_if_fail(group != NULL, NULL);
  return group->key.group_name;
}

/**
 * inf_connection_manager_group_add_connection:
 * @group: A #InfConnectionManagerGroup.
 * @conn: A #InfXmlConnection.
 *
 * This must be called whenever a remote host joins this group.
 *
 * Adds @conn to @group. Note that only publishers can add connections,
 * so the local host must be a publisher of @group to use this. This means
 * the group must have been created with inf_connection_manager_open_group().
 * Note that it is therefore impossible to join a group that has lost its
 * publisher.
 *
 * Returns: %TRUE if the connection was added, or %FALSE if no method for
 * the connection's network was found.
 **/
gboolean
inf_connection_manager_group_add_connection(InfConnectionManagerGroup* group,
                                            InfXmlConnection* conn)
{
  InfConnectionManagerMethodInstance* instance;

  g_return_val_if_fail(group != NULL, FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(conn), FALSE);
  g_return_val_if_fail(group->key.publisher_id == NULL, FALSE);

  instance = inf_connection_manager_get_method_by_connection(group, conn);
  if(instance == NULL) return FALSE;

  instance->desc->add_connection(instance->method, conn);
  return TRUE;
}

/**
 * inf_connection_manager_group_remove_connection:
 * @grp: A #InfConnectionManagerGroup.
 * @conn: A #InfXmlConnection.
 *
 * Removes @conn from @group. Normally, only publishers can remove
 * connections from groups. If you want to leave the group, then use
 * inf_connection_manager_group_unref() instead.
 *
 * This method is allowed to be called without being publisher when the
 * publisher itself is no longer available. This is for example used when
 * a user unsubscribes from a session that has not a publisher anymore.
 * Note that, normally, this isn't even necessary since the unsubscribing host
 * removes itself from the group. However, an evil host could still send
 * the unsubscribe request but stay within the group and still receive all
 * the messages. To prevent this, the others do explicitely remove that host
 * from the group.
 **/
void
inf_connection_manager_group_remove_connection(InfConnectionManagerGroup* grp,
                                               InfXmlConnection* conn)
{
  InfConnectionManagerMethodInstance* instance;

  g_return_if_fail(grp != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(conn));
  g_return_if_fail(grp->key.publisher_id == NULL);

  instance = inf_connection_manager_get_method_by_connection(grp, conn);
  /* We could not have added the connection otherwise: */
  g_assert(instance != NULL);

  instance->desc->remove_connection(instance->method, conn);
}

/**
 * inf_connection_manager_group_lookup_connection:
 * @grp: A @InfConnectionManagerGroup.
 * @network: The network in which to find the connection.
 * @id: A connection ID.
 *
 * Returns the connection whose remote id matches @id, or %NULL if there
 * is no such connection within @grp, or @network is not supported by @grp.
 *
 * Return Value: A #InfXmlConnection, or %NULL.
 **/
InfXmlConnection*
inf_connection_manager_group_lookup_connection(InfConnectionManagerGroup* grp,
                                               const gchar* network,
                                               const gchar* id)
{
  InfConnectionManagerMethodInstance* instance;
  
  g_return_val_if_fail(grp != NULL, FALSE);
  g_return_val_if_fail(network != NULL, FALSE);
  g_return_val_if_fail(id != NULL, NULL);

  instance = inf_connection_manager_get_method_by_network(grp, network);
  return instance->desc->lookup_connection(instance->method, id);
}

/**
 * inf_connection_manager_group_send_to_connection:
 * @g: A #InfConnectionManagerGroup.
 * @connection: A #InfXmlConnection.
 * @xml: The message to send.
 *
 * Sends an XML message to @connection which must be a member of @g. This
 * function takes ownership of @xml. The message is not sent immediately, but
 * it is first enqueued in the so-called outer queue. Messages in the
 * outer queue can be cancelled from being send by
 * inf_connection_manager_group_clear_queue(). When all previous messages
 * for that group have been sent to @connection, then the message is
 * enqueued in the inner queue. This makes sure that a huge amount of messages
 * for a single group (such as a session synchronization) does not block
 * traffic from other sessions.
 *
 * When that happens, then inf_net_object_enqueued() is called on the
 * group's #InfNetObject. At this point, the sending can no longer be
 * cancelled. Finally, when the message was actually sent,
 * inf_net_object_sent() is called.
 *
 * This function takes ownership of @xml.
 **/
void
inf_connection_manager_group_send_to_connection(InfConnectionManagerGroup* g,
                                                InfXmlConnection* connection,
                                                xmlNodePtr xml)
{
  g_return_if_fail(g != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  inf_connection_manager_send_msg(
    g,
    connection,
    INF_CONNECTION_MANAGER_POINT_TO_POINT,
    xml
  );
}

/**
 * inf_connection_manager_group_send_to_group:
 * @group: A #InfConnectionManagerGroup.
 * @except: A connection not to send the message to, or %NULL.
 * @xml: The message to send.
 *
 * Sends a message to all connections of group, except @except if non-%NULL.
 * The same procedure as described in
 * inf_connection_manager_group_send_to_connection() takes place for each
 * recipient.
 *
 * This function takes ownership of @xml.
 **/
void
inf_connection_manager_group_send_to_group(InfConnectionManagerGroup* group,
                                           InfXmlConnection* except,
                                           xmlNodePtr xml)
{
  GSList* item;
  InfConnectionManagerMethodInstance* instance;

  g_return_if_fail(group != NULL);
  g_return_if_fail(except == NULL || INF_IS_XML_CONNECTION(except));
  g_return_if_fail(xml != NULL);

  for(item = group->methods; item != NULL; item = g_slist_next(item))
  {
    instance = (InfConnectionManagerMethodInstance*)item->data;
    if(g_slist_next(item) == NULL)
      instance->desc->send_to_net(instance->method, except, xml);
    else
      instance->desc->send_to_net(instance->method, except, xmlCopyNode(xml, 1));
  }
}

/**
 * inf_connection_manager_group_clear_queue:
 * @group: A #InfConnectionManagerGroup.
 * @connection: A #InfXmlConnection.
 *
 * Clears all messages for which inf_net_object_enqueued() has not yet been
 * called on @group's #InfNetObject. The sending of these messages is
 * cancelled.
 **/
void
inf_connection_manager_group_clear_queue(InfConnectionManagerGroup* group,
                                         InfXmlConnection* connection)
{
  InfConnectionManagerQueue* queue;

  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  queue = inf_connection_manager_group_lookup_queue(group, connection);
  g_return_if_fail(queue != NULL);

  if(queue->first_item != NULL)
  {
    xmlFreeNodeList(queue->first_item);
    queue->first_item = NULL;
    queue->last_item = NULL;
  }
}

/**
 * inf_connection_manager_register_connection:
 * @group: A #InfConnectionManagerGroup.
 * @connection: A #InfXmlConnection.
 *
 * Registers @connection with @group. When a connection is registered, the
 * connection manager forwards incoming messages to the method to process,
 * and allows sending messages via inf_connection_manager_send_msg() and
 * inf_connection_manager_send_ctrl().
 *
 * This function should only be used by method implementations. 
 **/
void
inf_connection_manager_register_connection(InfConnectionManagerGroup* group,
                                           InfXmlConnection* connection)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerQueue* queue;
  gpointer regcount;

  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_return_if_fail(
    inf_connection_manager_group_lookup_queue(group, connection) == NULL
  );

  priv = INF_CONNECTION_MANAGER_PRIVATE(group->manager);

  queue = g_slice_new(InfConnectionManagerQueue);
  queue->connection = connection;
  queue->first_item = NULL;
  queue->last_item = NULL;
  queue->inner_count = 0;
  group->queues = g_slist_prepend(group->queues, queue);

  regcount = g_hash_table_lookup(priv->registered_connections, connection);
  if(regcount == NULL || GPOINTER_TO_UINT(regcount) == 0)
  {
    regcount = GUINT_TO_POINTER(0);

    g_signal_connect(
      G_OBJECT(connection),
      "received",
      G_CALLBACK(inf_connection_manager_connection_received_cb),
      group->manager
    );

    g_signal_connect(
      G_OBJECT(connection),
      "sent",
      G_CALLBACK(inf_connection_manager_connection_sent_cb),
      group->manager
    );
  }

  g_hash_table_insert(
    priv->registered_connections,
    connection,
    GUINT_TO_POINTER(GPOINTER_TO_UINT(regcount) + 1)
  );
}

/**
 * inf_connection_manager_unregister_connection:
 * @group: A #InfConnectionManagerGroup.
 * @connection: A #InfXmlConnection.
 *
 * Unregisters @connection from @group. Messages can no longer be sent to
 * this connection and incoming messages are not forwarded to the method.
 *
 * This function should only be used by method implementations.
 **/
void
inf_connection_manager_unregister_connection(InfConnectionManagerGroup* group,
                                             InfXmlConnection* connection)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerQueue* queue;
  InfXmlConnectionStatus status;
  gpointer regcount;

  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  queue = inf_connection_manager_group_lookup_queue(group, connection);
  g_return_if_fail(queue != NULL);

  priv = INF_CONNECTION_MANAGER_PRIVATE(group->manager);

  group->queues = g_slist_remove(group->queues, queue);

  /* Flush everything */
  /* TODO: Keep queue alive until all has been sent */
  g_object_get(G_OBJECT(connection), "status", &status, NULL);
  if(status == INF_XML_CONNECTION_OPEN && queue->first_item != NULL)
  {
    inf_connection_manager_group_real_send(
      group,
      queue,
      queue->first_item,
      0
    );
  }

  regcount = g_hash_table_lookup(priv->registered_connections, connection);
  g_assert(regcount != NULL && GPOINTER_TO_UINT(regcount) > 0);

  regcount = GUINT_TO_POINTER(GPOINTER_TO_UINT(regcount) - 1);
  if(GPOINTER_TO_UINT(regcount) == 0)
  {
    g_hash_table_remove(priv->registered_connections, connection);
    
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(inf_connection_manager_connection_received_cb),
      group->manager
    );
    
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(inf_connection_manager_connection_sent_cb),
      group->manager
    );
  }
  else
  {
    g_hash_table_insert(priv->registered_connections, connection, regcount);
  }

  g_slice_free(InfConnectionManagerQueue, queue);
}

/**
 * inf_connection_manager_send_msg:
 * @group: A #InfConnectionManagerGroup.
 * @connection: A registered #InfXmlConnection.
 * @scope: A #InfConnectionManagerScope.
 * @xml: The message to send.
 *
 * Sends a message to @connection which must be registered with @group.
 * @scope is the scope of the message that might tell the recipient to
 * forward the message. The concrete implementation depends on the the
 * method, though. Forwarding must be confirmed by the message handler
 * (see inf_net_object_received()), so that it is not possible to send
 * an arbitrary message to the recipient of the forwarded message, pretending
 * the message comes from the forwarder.
 *
 * This function takes ownership of @xml and should only be used by method
 * implementations.
 **/
void
inf_connection_manager_send_msg(InfConnectionManagerGroup* group,
                                InfXmlConnection* connection,
                                InfConnectionManagerScope scope,
                                xmlNodePtr xml)
{
  InfConnectionManagerQueue* queue;

  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  queue = inf_connection_manager_group_lookup_queue(group, connection);
  g_return_if_fail(queue != NULL);

  xml->_private = GUINT_TO_POINTER(
    (INF_CONNECTION_MANAGER_MESSAGE << 8) | scope
  );

  /* Just to be sure: */
  xmlUnlinkNode(xml);

  if(queue->inner_count < INNER_QUEUE_LIMIT)
  {
    inf_connection_manager_group_real_send(group, queue, xml, 1);
  }
  else
  {
    if(queue->last_item != NULL)
      queue->last_item->next = xml;
    else
      queue->first_item = xml;
    queue->last_item = xml;
  }
}

/**
 * inf_connection_manager_send_ctrl:
 * @group: A #InfConnectionManagerGroup.
 * @connection: A #InfXmlConnection.
 * @xml: The message to send.
 *
 * Sends a control message to @connection. A control message is a message that
 * is used by the method implementation internally. This can be used in
 * decentral methods to broadcast joining connections, for example. The scope
 * of a control message is always %INF_CONNECTION_MANAGER_POINT_TO_POINT.
 *
 * This function takes ownership of @xml and should only be used by method
 * implementations.
 **/
void
inf_connection_manager_send_ctrl(InfConnectionManagerGroup* group,
                                 InfXmlConnection* connection,
                                 xmlNodePtr xml)
{
  InfConnectionManagerQueue* queue;

  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  queue = inf_connection_manager_group_lookup_queue(group, connection);
  g_return_if_fail(queue != NULL);

  xml->_private = GUINT_TO_POINTER(
    (INF_CONNECTION_MANAGER_CONTROL << 8) |
    INF_CONNECTION_MANAGER_POINT_TO_POINT
  );

  /* Just to be sure: */
  xmlUnlinkNode(xml);

  if(queue->inner_count < INNER_QUEUE_LIMIT)
  {
    inf_connection_manager_group_real_send(group, queue, xml, 1);
  }
  else
  {
    if(queue->last_item != NULL)
      queue->last_item->next = xml;
    else
      queue->first_item = xml;
    queue->last_item = xml;
  }
}

/* vim:set et sw=2 ts=2: */
