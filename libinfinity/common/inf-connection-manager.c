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
#include <libinfinity/common/inf-xml-util.h>

#include <string.h>

typedef enum _InfConnectionManagerError {
  INF_CONNECTION_MANAGER_ERROR_GROUP_NOT_PRESENT,
  INF_CONNECTION_MANAGER_ERROR_NO_SUCH_GROUP
} InfConnectionManagerError;

typedef enum _InfConnectionManagerScope {
  INF_CONNECTION_MANAGER_SCOPE_NONE,
  /* point-to-point */
  INF_CONNECTION_MANAGER_SCOPE_POINT_TO_POINT,
  /* to all in group */
  INF_CONNECTION_MANAGER_SCOPE_GROUP
} InfConnectionManagerScope;

typedef struct _InfConnectionManagerQueue InfConnectionManagerQueue;
struct _InfConnectionManagerQueue {
  InfXmlConnection* connection;
  guint ref_count;

  xmlNodePtr outer_queue;
  xmlNodePtr outer_queue_last_item;
  guint inner_count;
};

struct _InfConnectionManagerGroup {
  InfConnectionManager* manager; /* parent manager */
  InfNetObject* net_object; /* weak-refed as long we have no connections */
  gchar* name;
  guint ref_count;

  GList* queues;
};

typedef struct _InfConnectionManagerPrivate InfConnectionManagerPrivate;
struct _InfConnectionManagerPrivate {
  GSList* connections;
  GSList* groups;

  /* TODO: Add a hash table that maps group name and connection to a group,
   * for quicker lookup */
};

#define INF_CONNECTION_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_CONNECTION_MANAGER, InfConnectionManagerPrivate))

static GObjectClass* parent_class;

/* Maximal number of XML nodes that are sent to a particular netobject A.
 * If more are to be sent, they are kept in an outer queue so that messages
 * from another netobject B can be sent through the same connection without
 * having to wait until all messages from A have been sent. */
static const guint INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT = 5;

static int
inf_connection_manager_cmp_queue_by_conn(gconstpointer queue_,
                                         gconstpointer conn)
{
  InfConnectionManagerQueue* queue;
  queue = (InfConnectionManagerQueue*)queue_;

  if(queue->connection < (InfXmlConnection*)conn)
    return -1;
  else if(queue->connection > (InfXmlConnection*)conn)
    return 1;
  else
    return 0;
}

static InfConnectionManagerGroup*
inf_connection_manager_group_boxed_copy(InfConnectionManagerGroup* group)
{
  inf_connection_manager_ref_group(group->manager, group);
  return group;
}

static void
inf_connection_manager_group_boxed_free(InfConnectionManagerGroup* group)
{
  inf_connection_manager_unref_group(group->manager, group);
}

static void
inf_connection_manager_object_unrefed(gpointer user_data,
                                      GObject* where_the_object_was)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerGroup* group;
  InfConnectionManagerQueue* queue;
  xmlNodePtr next;
  GList* item;
  
  group = (InfConnectionManagerGroup*)user_data;
  priv = INF_CONNECTION_MANAGER_PRIVATE(group->manager);

  g_warning(
    "NetObject of connection manager group '%s' finalized, but group is "
    "still referenced. Removing group.",
    group->name
  );
  
  /* TODO: Keep group alive, just issue warning? */
  
  /* Do not use regular free functions, those would try to access
   * NetObject for reference. */

  for(item = group->queues; item != NULL; item = g_list_next(item))
  {
    queue = (InfConnectionManagerQueue*)item->data;
    for(; queue->outer_queue != NULL; queue->outer_queue = next)
    {
      next = queue->outer_queue->next;
      xmlFreeNode(queue->outer_queue);
    }

    g_slice_free(InfConnectionManagerQueue, queue);
  }

  g_list_free(group->queues);
  g_free(group->name);

  priv->groups = g_slist_remove(priv->groups, group);
  g_slice_free(InfConnectionManagerGroup, group);
}

static InfConnectionManagerQueue*
inf_connection_manager_group_add_queue(InfConnectionManagerGroup* group,
                                       InfXmlConnection* connection)
{
  InfConnectionManagerQueue* queue;

  queue = g_slice_new(InfConnectionManagerQueue);
  queue->connection = connection;
  queue->ref_count = 1;
  queue->outer_queue = NULL;
  queue->outer_queue_last_item = NULL;
  queue->inner_count = 0;
  
  if(group->queues == NULL && group->net_object != NULL)
  {
    /* Turn into strong ref again */
    g_object_ref(G_OBJECT(group->net_object));

    g_object_weak_unref(
      G_OBJECT(group->net_object),
      inf_connection_manager_object_unrefed,
      group
    );
  }

  group->queues = g_list_prepend(group->queues, queue);
  return queue;
}

static void
inf_connection_manager_group_remove_queue(InfConnectionManagerGroup* group,
                                          InfConnectionManagerQueue* queue)
{
  xmlNodePtr next;

  if(queue->ref_count > 0)
  {
    g_warning(
      "Removing a connection with ref_count %u from "
      "connection manager group '%s'",
      queue->ref_count,
      group->name
    );
  }

  /* Free scheduled data in outer queue, it will not be sent anymore.
   * Flush before calling this function if you want it to. */
  for(; queue->outer_queue != NULL; queue->outer_queue = next)
  {
    next = queue->outer_queue->next;
    xmlFreeNode(queue->outer_queue);
  }

  group->queues = g_list_remove(group->queues, queue);
  g_slice_free(InfConnectionManagerQueue, queue);

  if(group->queues == NULL && group->net_object != NULL)
  {
    /* Turn into weakref when group has no connections, so the netobject
     * can be freed if noone is interacting with it. */
    g_object_weak_ref(
      G_OBJECT(group->net_object),
      inf_connection_manager_object_unrefed,
      group
    );

    g_object_unref(G_OBJECT(group->net_object));
  }
}

static InfConnectionManagerGroup*
inf_connection_manager_group_new(InfConnectionManager* manager,
                                 const gchar* name,
                                 InfNetObject* net_object)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerGroup* group;

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  group = g_slice_new(InfConnectionManagerGroup);

  group->manager = manager;
  group->net_object = net_object;

  if(net_object != NULL)
  {
    g_object_weak_ref(
      G_OBJECT(net_object),
      inf_connection_manager_object_unrefed,
      group
    );
  }

  group->name = g_strdup(name);
  group->ref_count = 1;
  group->queues = NULL;

  priv->groups = g_slist_prepend(priv->groups, group);

  return group;
}

static void
inf_connection_manager_group_free(InfConnectionManagerGroup* group)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerQueue* queue;
  GList* item;
  xmlNodePtr next;

  if(group->ref_count > 0)
  {
    g_warning(
      "Freeing a connection manager group named '%s' with ref_count %u",
      group->name,
      group->ref_count
    );
  }

  if(group->queues == NULL)
  {
    if(group->net_object != NULL)
    {
      g_object_weak_unref(
        G_OBJECT(group->net_object),
        inf_connection_manager_object_unrefed,
        group
      );
    }
  }
  else
  {
    g_warning(
      "Connection manager group '%s' finally unrefed, but the group "
      "does still contain connections. Removing connections.",
      group->name
    );

    /* Don't just use inf_connection_manager_group_remove_queue() because
     * that could turn the reference to net_object to a weak reference. If
     * we held the only strong reference before, this would already lead to
     * destruction of the group. However, we are already destructing. */

    for(item = group->queues; item != NULL; item = g_list_next(item))
    {
      queue = (InfConnectionManagerQueue*)item->data;
      /* Free scheduled data in outer queue, it will not be sent anymore.
       * Flush before calling this function if you want it to. */
      for(; queue->outer_queue != NULL; queue->outer_queue = next)
      {
        next = queue->outer_queue->next;
        xmlFreeNode(queue->outer_queue);
      }
    }

    g_list_free(group->queues);
    g_slice_free(InfConnectionManagerQueue, queue);
    g_object_unref(G_OBJECT(group->net_object));
  }

  priv = INF_CONNECTION_MANAGER_PRIVATE(group->manager);
  priv->groups = g_slist_remove(priv->groups, group);

  g_free(group->name);
  g_slice_free(InfConnectionManagerGroup, group);
}

static InfConnectionManagerQueue*
inf_connection_manager_group_get_queue(InfConnectionManagerGroup* group,
                                       InfXmlConnection* connection)
{
  GList* link;

  link = g_list_find_custom(
    group->queues,
    connection,
    inf_connection_manager_cmp_queue_by_conn
  );

  if(link == NULL) return NULL;
  return (InfConnectionManagerQueue*)link->data;
}

static InfConnectionManagerGroup*
inf_connection_manager_get_group_from_xml(InfConnectionManager* manager,
                                          InfXmlConnection* connection,
                                          xmlNodePtr xml,
                                          GError** error)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerGroup* group;
  xmlChar* group_name;

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  group_name = xmlGetProp(xml, (const xmlChar*)"name");
  if(group_name == NULL)
  {
    g_set_error(
      error,
      g_quark_from_static_string("INF_CONNECTION_MANAGER_ERROR"),
      INF_CONNECTION_MANAGER_ERROR_GROUP_NOT_PRESENT,
      "'name' attribute of group tag is missing"
    );

    return NULL;
  }
  else
  {
    group = inf_connection_manager_find_group_by_connection(
      manager,
      (const gchar*)group_name,
      connection
    );

    if(group == NULL)
    {
      g_set_error(
        error,
        g_quark_from_static_string("INF_CONNECTION_MANAGER_ERROR"),
        INF_CONNECTION_MANAGER_ERROR_NO_SUCH_GROUP,
        "No object for group '%s' registered",
        (const gchar*)group_name
      );

      xmlFree(group_name);
      return NULL;
    }
    else
    {
      xmlFree(group_name);
      return group;
    }
  }
}

/* Sends a list of xml nodes through connection and returns the new list
 * head. Successfully sent nodes are freed. */
/* TODO: Don't take connection because we have queue->connection anyway */
static xmlNodePtr
inf_connection_manager_group_real_send(InfConnectionManagerGroup* group,
                                       InfConnectionManagerQueue* queue,
                                       InfXmlConnection* connection,
                                       xmlNodePtr xml,
                                       guint max_messages)
{
  InfConnectionManagerScope scope;
  xmlNodePtr container;
  xmlNodePtr cur;

  scope = INF_CONNECTION_MANAGER_SCOPE_NONE;
  container = NULL;

  g_assert(group->net_object != NULL);

  /* Increase max messages and count down to one instead of zero. This way,
   * max_messages == 0 means no limit. */
  if(max_messages != 0)
    ++ max_messages;

  /* TODO: Don't pack too many messages into the same container, otherwise
   * the recipient has to receive the whole container before processing
   * the first request in it.
   *
   * An alternative woud be to change the InfXmlConnection interface to be
   * SAX-like so it can begin to process the first message without the 
   * container being closed. Hm. This probably doesn't work with XMPP. */

  while(xml != NULL && max_messages != 1)
  {
    cur = xml;
    xml = xml->next;

    /* This node has another type then the previous node(s): This means we
     * need to open a new <group> tag with another type. The current container
     * can already be sent. */
    if(GPOINTER_TO_UINT(cur->_private) != scope)
    {
      if(container != NULL)
        inf_xml_connection_send(connection, container);

      scope = GPOINTER_TO_UINT(cur->_private);

      container = xmlNewNode(NULL, (const xmlChar*)"group");
      inf_xml_util_set_attribute(container, "name", group->name);

      switch(scope)
      {
      case INF_CONNECTION_MANAGER_SCOPE_POINT_TO_POINT:
        inf_xml_util_set_attribute(container, "scope", "p2p");
        break;
      case INF_CONNECTION_MANAGER_SCOPE_GROUP:
        inf_xml_util_set_attribute(container, "scope", "group");
        break;
      case INF_CONNECTION_MANAGER_SCOPE_NONE:
      default:
        g_assert_not_reached();
        break;
      }
    }

    xmlUnlinkNode(cur);
    xmlAddChild(container, cur);

    /* Object was enqueued in inner queue */
    inf_net_object_enqueued(group->net_object, connection, cur);

    ++ queue->inner_count;
    if(max_messages > 1) -- max_messages;
  }

  /* Send final */
  inf_xml_connection_send(connection, container);
  return xml;
}

static void
inf_connection_manager_connection_sent_cb(InfXmlConnection* connection,
                                          const xmlNodePtr xml,
                                          gpointer user_data)
{
  InfConnectionManager* manager;
  InfConnectionManagerGroup* group;
  InfConnectionManagerQueue* queue;
  xmlNodePtr child;

  manager = INF_CONNECTION_MANAGER(user_data);

  if(strcmp((const char*)xml->name, "group") == 0)
  {
    group = inf_connection_manager_get_group_from_xml(
      manager,
      connection,
      xml,
      NULL
    );

    /* It may happen that a NetObject sends things but is removed until
     * the data is really sent out, so do not assert here. */
    if(group != NULL)
    {
      g_assert(group->net_object != NULL);
      queue = inf_connection_manager_group_get_queue(group, connection);

      /* Again, the connection may have been removed from the group after
       * sending. */
      if(queue != NULL)
      {
        g_object_ref(G_OBJECT(group->net_object));

        for(child = xml->children; child != NULL; child = child->next)
        {
          inf_net_object_sent(group->net_object, connection, child);

          g_assert(queue->inner_count > 0);
          -- queue->inner_count;
        }

        if(queue->outer_queue != NULL &&
           queue->inner_count < INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT)
        {
          /* We actually sent some requests, so we have some space in the
           * inner queue again. */
          queue->outer_queue = inf_connection_manager_group_real_send(
            group,
            queue,
            connection,
            queue->outer_queue,
            INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT - queue->inner_count
          );

          if(queue->outer_queue == NULL)
            queue->outer_queue_last_item = NULL;
        }

        g_object_unref(G_OBJECT(group->net_object));
      }
    }
  }
  else
  {
    /* We should not have sent anything else */
    g_assert_not_reached();
  }
}

static void
inf_connection_manager_connection_received_cb(InfXmlConnection* connection,
                                              const xmlNodePtr xml,
                                              gpointer user_data)
{
  InfConnectionManager* manager;
  InfConnectionManagerGroup* group;
  InfConnectionManagerQueue* queue;
  InfNetObject* object;
  xmlNodePtr child;
  GError* error;

  /* TODO: A virtual function to obtain a human-visible remote address
   * for InfXmlConnection (IP, JID, etc.) */

  manager = INF_CONNECTION_MANAGER(user_data);

  if(strcmp((const char*)xml->name, "group") == 0)
  {
    error = NULL;
    group = inf_connection_manager_get_group_from_xml(
      manager,
      connection,
      xml,
      &error
    );

    if(group == NULL)
    {
      /*g_warning(
        "Failed to get group to forward received XML: %s",
        error->message
      );*/
    }
    else
    {
      object = group->net_object;
      g_assert(object != NULL);

      queue = inf_connection_manager_group_get_queue(group, connection);

      if(queue == NULL)
      {
        /* Got something from a connection that is not in the group it sent
         * something to. */
        g_warning(
          "Received XML for connection manager group named '%s' of which the "
          "connection is not a member",
          group->name
        );
      }
      else
      {
        g_object_ref(object);

        for(child = xml->children; child != NULL; child = child->next)
        {
          inf_net_object_received(group->net_object, connection, child);
        }

        /* TODO: Forward if it is scope="group" */

        g_object_unref(object);
      }
    }
  }
  else
  {
    g_warning(
      "Received unexpected XML message '%s'",
      (const gchar*)xml->name
    );
  }
}

/* Required by inf_connection_manager_connection_notify_status_cb to
 * the gnetwork connection if it has been closed. */
static void
inf_connection_manager_unregister_connection(InfConnectionManager* manager,
                                             InfXmlConnection* connection);

static void
inf_connection_manager_connection_notify_status_cb(InfXmlConnection* conn,
                                                   GParamSpec* pspec,
                                                   gpointer user_data)
{
  InfConnectionManager* manager;
  InfXmlConnectionStatus status;

  g_object_get(G_OBJECT(conn), "status", &status, NULL);

  /* Remove the connection from the list of connections if it has
   * been closed. Keep it alive if it is already CLOSING to allow it to
   * properly close the connection. */
  if(status == INF_XML_CONNECTION_CLOSED)
  {
    manager = INF_CONNECTION_MANAGER(user_data);
    inf_connection_manager_unregister_connection(manager, conn);
  }
}

static void
inf_connection_manager_unregister_connection_func(gpointer value,
                                                  gpointer user_data)
{
  InfConnectionManagerGroup* group;
  GList* link;

  group = value;

  link = g_list_find_custom(
    group->queues,
    (InfXmlConnection*)user_data,
    inf_connection_manager_cmp_queue_by_conn
  );

  /* Perhaps connection was not in this group */
  if(link != NULL)
    inf_connection_manager_group_remove_queue(group, link->data);
}

static void
inf_connection_manager_unregister_connection(InfConnectionManager* manager,
                                             InfXmlConnection* connection)
{
  InfConnectionManagerPrivate* priv;

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  /* Remove connection from groups */
  g_slist_foreach(
    priv->groups,
    inf_connection_manager_unregister_connection_func,
    connection
  );

  priv->connections = g_slist_remove(priv->connections, connection);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(connection),
    G_CALLBACK(inf_connection_manager_connection_notify_status_cb),
    manager
  );

  g_object_unref(G_OBJECT(connection));
}

static void
inf_connection_manager_register_connection(InfConnectionManager* manager,
                                           InfXmlConnection* connection)
{
  InfConnectionManagerPrivate* priv;

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  /* Only add if not already present */
  if(g_slist_find(priv->connections, connection) == NULL)
  {
    g_object_ref(G_OBJECT(connection));
    priv->connections = g_slist_prepend(priv->connections, connection);

    /* Connect after so that this callback is called after other signal
     * handlers of the same signal that might want to remove connection
     * references. */
    g_signal_connect_after(
      G_OBJECT(connection),
      "notify::status",
      G_CALLBACK(inf_connection_manager_connection_notify_status_cb),
      manager
    );

    g_signal_connect(
      G_OBJECT(connection),
      "received",
      G_CALLBACK(inf_connection_manager_connection_received_cb),
      manager
    );

    g_signal_connect(
      G_OBJECT(connection),
      "sent",
      G_CALLBACK(inf_connection_manager_connection_sent_cb),
      manager
    );
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

  priv->connections = NULL;
  priv->groups = NULL;
}

static void
inf_connection_manager_dispose(GObject* object)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;
  GSList* item;

  manager = INF_CONNECTION_MANAGER(object);
  priv = INF_CONNECTION_MANAGER_PRIVATE(object);

  while(priv->groups != NULL)
    inf_connection_manager_group_free(priv->groups->data);

  for(item = priv->connections; item != NULL; item = g_slist_next(item))
    g_object_unref(G_OBJECT(item->data));

  g_slist_free(priv->connections);
  priv->connections = NULL;

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

/** inf_connection_manager_create_group:
 *
 * @manager: A #InfConnectionManager.
 * @group_name: A name for the new group.
 * @object: A #InfNetObject, or %NULL.
 *
 * Creates a new group with name @group_name. A connection cannot be in two
 * groups with the same name. If a connection within a group receives
 * data, the XML message is forwarded to @object.
 *
 * If @object is %NULL, the group is created without an object. In such a
 * state, you cannot send or receive data from the group. Use
 * inf_connection_manager_group_set_object() before such an attempt.
 *
 * Return Value: The new #InfConnectionManagerGroup.
 **/
InfConnectionManagerGroup*
inf_connection_manager_create_group(InfConnectionManager* manager,
                                    const gchar* group_name,
                                    InfNetObject* object)
{
  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);
  g_return_val_if_fail(object == NULL || INF_IS_NET_OBJECT(object), NULL);

  return inf_connection_manager_group_new(manager, group_name, object);
}

/** inf_connection_manager_find_group_by_connection:
 *
 * @manager: A #InfConnectionManager.
 * @group_name: A group name.
 * @connection: A #InfXmlConnection.
 *
 * Returns a group with the given name of which @connection is a member.
 *
 * Return Value: A #InfConnectionManagerGroup, or %NULL.
 **/
InfConnectionManagerGroup*
inf_connection_manager_find_group_by_connection(InfConnectionManager* manager,
                                                const gchar* group_name,
                                                InfXmlConnection* connection)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerGroup* group;
  GSList* item;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), NULL);

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  for(item = priv->groups; item != NULL; item = g_slist_next(item))
  {
    group = (InfConnectionManagerGroup*)item->data;
    if(strcmp(group->name, group_name) == 0)
      if(inf_connection_manager_group_get_queue(group, connection) != NULL)
        return group;
  }

  return NULL;
}

/** inf_connection_manager_ref_group:
 *
 * @manager: A #InfConnectionManager.
 * @group: A #InfConnectionManagerGroup created with
 * inf_connection_manager_create_group().
 *
 * Increases the reference count of @group. It needs to be unrefed as many
 * times as it has been refed (plus 1, since it has a already a reference
 * after creation) to actually remove the group.
 **/
void
inf_connection_manager_ref_group(InfConnectionManager* manager,
                                 InfConnectionManagerGroup* group)
{
  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(group != NULL);

  ++ group->ref_count;
}

/** inf_connection_manager_unref_group:
 *
 * @manager: A #InfConnectionManager
 * @group: A #InfConnectionManagerGroup created with
 * inf_connection_manager_create_group().
 *
 * Decreases the reference count of @group. If the reference count reaches
 * zero, the group is removed. In that case, there should not be any
 * connections anymore in the group, otherwise those are removed even if
 * they are still referenced.
 **/
void
inf_connection_manager_unref_group(InfConnectionManager* manager,
                                   InfConnectionManagerGroup* group)
{
  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(group != NULL);

  -- group->ref_count;
  if(group->ref_count == 0)
    inf_connection_manager_group_free(group);
}

/** inf_connection_manager_ref_connection:
 *
 * @manager: A #InfConnectionManager
 * @group: A #InfConnectionManagerGroup created with
 * inf_connection_manager_create_group().
 * @connection: A #InfXmlConnection.
 *
 * Adds @connection to @group. If a connection is member of a group, the
 * #InfNetObject of the group
 * (as given in inf_connection_manager_create_group()) gets notified when
 * XML messages are sent or received for that group.
 *
 * If @connection is already in @group, its reference count is increased. To
 * remove the connection again, you have to call
 * inf_connection_manager_unref_connection() one more time.
 *
 * A connection cannot be in two groups with the same name.
 **/
void
inf_connection_manager_ref_connection(InfConnectionManager* manager,
                                      InfConnectionManagerGroup* group,
                                      InfXmlConnection* connection)
{
  InfConnectionManagerQueue* queue;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  queue = inf_connection_manager_group_get_queue(group, connection);
  if(queue != NULL)
  {
    ++ queue->ref_count;
  }
  else
  {
    g_return_if_fail(
      inf_connection_manager_find_group_by_connection(
        manager,
        group->name,
        connection
      ) == NULL
    );

    inf_connection_manager_register_connection(manager, connection);
    inf_connection_manager_group_add_queue(group, connection);
  }
}

/** inf_connection_manager_unref_connection:
 *
 * @manager: A #InfConnectionManager
 * @group: A #InfConnectionManagerGroup created with
 * inf_connection_manager_create_group().
 * @connection: A #InfXmlConnection that is member of @group.
 *
 * Decreases the reference count of the membership of @connection in @group.
 * If the reference count reaches zero, @connection is removed from @group.
 **/
void
inf_connection_manager_unref_connection(InfConnectionManager* manager,
                                        InfConnectionManagerGroup* group,
                                        InfXmlConnection* connection)
{
  InfConnectionManagerQueue* queue;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  queue = inf_connection_manager_group_get_queue(group, connection);
  g_return_if_fail(queue != NULL);

  -- queue->ref_count;
  if(queue->ref_count == 0)
  {
    /* Flush pending data. */
    if(queue->outer_queue != NULL)
    {
      /*TODO: Keep the queue alive until everything has been sent */
      queue->outer_queue = inf_connection_manager_group_real_send(
        group,
        queue,
        connection,
        queue->outer_queue,
        0
      );

      queue->outer_queue_last_item = NULL;
    }

    inf_connection_manager_group_remove_queue(group, queue);
  }
}

/** inf_connection_manager_group_get_object:
 *
 * @group: A #InfConnectionManagerGroup.
 *
 * Returns the #InfNetObject of @group as given to
 * inf_connection_manager_create_group().
 *
 * Return Value: The #InfNetObject of @group.
 **/
InfNetObject*
inf_connection_manager_group_get_object(InfConnectionManagerGroup* group)
{
  g_return_val_if_fail(group != NULL, NULL);
  return group->net_object;
}

/** inf_connection_manager_group_get_name:
 *
 * @group: A #InfConnectionManagerGroup.
 *
 * Returns the name of @group as given to
 * inf_connection_manager_create_group().
 *
 * Return Value: The name of @group.
 **/
const gchar*
inf_connection_manager_group_get_name(InfConnectionManagerGroup* group)
{
  g_return_val_if_fail(group != NULL, NULL);
  return group->name;
}

/** inf_connection_manager_group_set_object:
 *
 * @group: A #InfConnectionManagerGroup.
 * @object: A #InfNetObject, or %NULL.
 *
 * Changes the #InfNetObject of @group. @object may be %NULL, in which case
 * no send/receive operation can be issued.
 **/
void
inf_connection_manager_group_set_object(InfConnectionManagerGroup* group,
                                        InfNetObject* object)
{
  g_return_if_fail(group != NULL);
  g_return_if_fail(object == NULL || INF_IS_NET_OBJECT(object));

  if(group->net_object != NULL)
  {
    if(group->queues == NULL)
    {
      g_object_weak_unref(
        G_OBJECT(group->net_object),
        inf_connection_manager_object_unrefed,
        group
      );
    }
    else
    {
      g_object_unref(G_OBJECT(group->net_object));
    }
  }

  group->net_object = object;

  if(object != NULL)
  {
    if(group->queues == NULL)
    {
      g_object_weak_ref(
        G_OBJECT(object),
        inf_connection_manager_object_unrefed,
        group
      );
    }
    else
    {
      g_object_ref(G_OBJECT(object));
    }
  }
}

/** inf_connection_manager_has_connection:
 *
 * @manager: A #InfConnectionManager.
 * @group: A #InfConnectionManagerGroup created with
 * inf_connection_manager_create_group().
 * @connection:A #InfXmlConnection.
 *
 * Returns whether @connection is a member of @group.
 *
 * Return Value: Whether @connection belongs to @group.
 **/
gboolean
inf_connection_manager_has_connection(InfConnectionManager* manager,
                                      InfConnectionManagerGroup* group,
                                      InfXmlConnection* connection)
{
  InfConnectionManagerQueue* queue;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), FALSE);
  g_return_val_if_fail(group != NULL, FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), FALSE);

  queue = inf_connection_manager_group_get_queue(group, connection);

  if(queue == NULL)
    return FALSE;
  else
    return TRUE;
}

/** inf_connection_manager_send_to:
 *
 * @manager: A #InfConnectionManager.
 * @group: A #InfConnectionManagerGroup created with
 * inf_connection_manager_create_group().
 * @connection: A #InfXmlConnection that has previously been added via
 * inf_connection_manager_add_connection().
 * @xml: A XML message to be sent.
 *
 * Sends an XML message via @connection to @group. @connection must be a
 * member of @group. This function takes ownership of @xml and unlinks it
 * from its current context.
 *
 * It is not guaranteed that @xml is passed instantly to @connection because
 * there might be other messages waiting to be sent via @connection.
 * inf_net_object_enqueued() is called on @group's #InfNetObject as soon as
 * @xml has been passed to @connection. This also means that the sending of
 * the message cannot be cancelled anymore with
 * inf_connection_manager_cancel_outer().
 **/
void
inf_connection_manager_send_to(InfConnectionManager* manager,
                               InfConnectionManagerGroup* group,
                               InfXmlConnection* connection,
                               xmlNodePtr xml)
{
  InfConnectionManagerQueue* queue;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(group != NULL);
  g_return_if_fail(group->net_object != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  queue = inf_connection_manager_group_get_queue(group, connection);
  g_return_if_fail(queue != NULL);

  xml->_private =
    GUINT_TO_POINTER(INF_CONNECTION_MANAGER_SCOPE_POINT_TO_POINT);

  if(queue->inner_count < INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT)
  {
    inf_connection_manager_group_real_send(group, queue, connection, xml, 1);
  }
  else
  {
    /* Inner queue is full, wait for some messages to be sent */
    if(queue->outer_queue_last_item == NULL)
      queue->outer_queue = xml;
    else
      queue->outer_queue_last_item->next = xml;

    xml->prev = queue->outer_queue_last_item;
    xml->next = NULL;
    queue->outer_queue_last_item = xml;
  }
}

/** inf_connection_manager_send_to_group:
 *
 * @manager: A #InfConnectionManager.
 * @group: A #InfConnectionManagerGroup created with
 * inf_connection_manager_create_group().
 * @except: A #InfXmlConnection that has previously been added via
 * inf_connection_manager_add_connection(), or %NULL.
 * @xml: A XML message to be sent.
 *
 * Sends an XML message to all connections that have been added to @group
 * except @except (if non-%NULL). This function takes ownership of @xml and
 * unlinks it from its current context.
 *
 * See inf_connection_manager_send_to() for why @xml might not instantly be
 * passed to @connection via inf_xml_connection_send().
 **/
void
inf_connection_manager_send_to_group(InfConnectionManager* manager,
                                     InfConnectionManagerGroup* group,
                                     InfXmlConnection* except,
                                     xmlNodePtr xml)
{
  InfConnectionManagerQueue* queue;
  xmlNodePtr copy_xml;
  GList* item;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(group != NULL);
  g_return_if_fail(group->net_object != NULL);
  g_return_if_fail(except == NULL || INF_IS_XML_CONNECTION(except));
  g_return_if_fail(xml != NULL);

  xml->_private = GUINT_TO_POINTER(INF_CONNECTION_MANAGER_SCOPE_GROUP);

  /* TODO: In most situations, we only have to copy xml n-1 times because we
   * take ownership anyway. */

  for(item = group->queues; item != NULL; item = g_list_next(item))
  {
    queue = (InfConnectionManagerQueue*)item->data;
    if(queue->connection != except)
    {
      copy_xml = xmlCopyNode(xml, 1);
      if(queue->inner_count < INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT)
      {
        inf_connection_manager_group_real_send(
          group,
          queue,
          queue->connection,
          copy_xml,
          1
        );
      }
      else
      {
        /* Inner queue is full, wait for some messages to be sent */
        if(queue->outer_queue_last_item == NULL)
          queue->outer_queue = copy_xml;
        else
          queue->outer_queue_last_item->next = copy_xml;

        copy_xml->prev = queue->outer_queue_last_item;
        copy_xml->next = NULL;
        queue->outer_queue_last_item = copy_xml;
      }
    }
  }

  xmlFreeNode(xml);
}

/** inf_connection_manager_send_multiple_to:
 *
 * @manager: A #InfConnectionManager.
 * @group: A #InfConnectionManagerGroup created with
 * inf_connection_manager_create_group().
 * @connection: A #InfXmlConnection that has previously been added via
 * inf_connection_manager_add_connection().
 * @xml: A linked list of XML messages to be sent.
 *
 * Sends multiple XML messages via @connection to @group. This function takes
 * ownership of all the nodes contained in the list @xml and unlinks them
 * from their current context.
 *
 * See inf_connection_manager_send_to() for why @xml might not instantly be
 * passed to @connection via inf_xml_connection_send().
 **/
void
inf_connection_manager_send_multiple_to(InfConnectionManager* manager,
                                        InfConnectionManagerGroup* group,
                                        InfXmlConnection* connection,
                                        xmlNodePtr xml)
{
  InfConnectionManagerQueue* queue;
  xmlNodePtr prev;
  xmlNodePtr cur;
  xmlNodePtr next;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(group != NULL);
  g_return_if_fail(group->net_object != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  queue = inf_connection_manager_group_get_queue(group, connection);
  g_return_if_fail(queue != NULL);

  xml->_private =
    GUINT_TO_POINTER(INF_CONNECTION_MANAGER_SCOPE_POINT_TO_POINT);

  if(queue->inner_count < INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT)
  {
    xml = inf_connection_manager_group_real_send(
      group,
      queue,
      connection,
      xml,
      INF_CONNECTION_MANAGER_INNER_QUEUE_LIMIT - queue->inner_count
    );
  }

  /* Requeue remaining entries */
  if(xml != NULL)
  {
    /* Detach nodes from their current context, but keep them linked */
    prev = NULL;
    for(cur = xml; cur != NULL; cur = next)
    {
      next = cur->next;
      xmlUnlinkNode(cur);

      /* Do not set cur->next because it would be unlinked in the
       * next iteration anyway. */
      cur->prev = prev;
      if(prev != NULL) prev->next = cur;

      prev = cur;
    }

    /* prev now contains the last node in the list */

    if(queue->outer_queue_last_item == NULL)
      queue->outer_queue = xml;
    else
      queue->outer_queue_last_item->next = xml;

    xml->prev = queue->outer_queue_last_item;

    prev->next = NULL;
    queue->outer_queue_last_item = prev;
  }
}

/** inf_connection_manager_cancel_outer:
 *
 * @manager: A #InfConnectionManager.
 * @group: A #InfConnectionManagerGroup created with
 * inf_connection_manager_create_group().
 * @connection: A #InfXmlConnection that has previously been added via
 * inf_connection_manager_add_connection().
 *
 * Discards all messages that are waiting to be sent via @connection to
 * @group. These are all messages for which inf_net_object_enqueued() has not
 * yet been called.
 **/
void
inf_connection_manager_cancel_outer(InfConnectionManager* manager,
                                    InfConnectionManagerGroup* group,
                                    InfXmlConnection* connection)
{
  InfConnectionManagerQueue* queue;
  xmlNodePtr next;

  g_return_if_fail(INF_IS_CONNECTION_MANAGER(manager));
  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  queue = inf_connection_manager_group_get_queue(group, connection);
  g_return_if_fail(queue != NULL);

  for(; queue->outer_queue != NULL; queue->outer_queue = next)
  {
    next = queue->outer_queue->next;
    xmlFreeNode(queue->outer_queue);
  }

  queue->outer_queue_last_item = NULL;
}

/* vim:set et sw=2 ts=2: */
