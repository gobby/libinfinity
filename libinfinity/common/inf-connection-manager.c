/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-connection-manager
 * @short_description: Sharing connections by mulitple sessions
 * @include: libinfinity/common/inf-connection-manager.h
 * @see_also: #InfNetObject
 *
 * The connection manager handles all connections used in the infinote
 * framework. It allows sharing a connection for different (internal)
 * recipients, so two infinote sessions can use the same connection to
 * send and receive data from other collaborators.
 *
 * The key concept is that of so-called (connection manager) groups. A group
 * is identified by its name and its publisher id. Hosts can create and join
 * groups within the network, and send messages to others within the same
 * group. The publisher id is a string uniquely identifying a host in the
 * network. This is supposed to be some form of UUID since clients cannot join
 * two groups with both same name and same publisher id.
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
#include <libinfinity/inf-i18n.h>

#include <libxml/xmlsave.h>

#ifdef G_OS_WIN32
#include <rpc.h>
#else
#include <uuid/uuid.h>
#endif

#include <string.h>

/* Length of a UUID in normalized form: */
#define PUBLISHER_ID_LENGTH 36

typedef enum _InfConnectionManagerMsgType {
  INF_CONNECTION_MANAGER_MESSAGE,
  INF_CONNECTION_MANAGER_CONTROL
} InfConnectionManagerMsgType;

/* These are only used internally */
typedef enum _InfConnectionManagerError {
  INF_CONNECTION_MANAGER_ERROR_UNEXPECTED_MESSAGE,
  INF_CONNECTION_MANAGER_ERROR_INVALID_PUBLISHER_ID,
  INF_CONNECTION_MANAGER_ERROR_PUBLISHER_ID_KNOWN,
  INF_CONNECTION_MANAGER_ERROR_PUBLISHER_ID_IN_USE
} InfConnectionManagerError;

typedef struct _InfConnectionManagerKey InfConnectionManagerKey;
struct _InfConnectionManagerKey {
  gchar* group_name;
  /* publisher_id might be zero, if not yet known.
   * In that case, we sort by publisher_conn */
  gchar* publisher_id;
  InfXmlConnection* publisher_conn;
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
  xmlNodePtr send_item;
  guint inner_count;

  InfConnectionManagerQueue* parent_queue;
  xmlNodePtr parent_queue_item;

  /* Note that we actually refer to child queues, although we are storing
   * groups in this list. We always mean the queue within that group with
   * the same connection as this group (if such a queue does not exist, then
   * something went wrong). We just can't store pointers to those queues
   * directly since we need the group in
   * inf_connection_manager_group_real_send(). Perhaps we should just store a
   * group pointer in every queue instead. */
  GSList* children_groups;
};

typedef struct _InfConnectionManagerRegistration
  InfConnectionManagerRegistration;
struct _InfConnectionManagerRegistration {
  guint registration_count;
  gboolean publisher_id_known;
  gboolean publisher_id_sent;

  /* has only senseful content in case publisher_id_known is TRUE */
  gchar publisher_id[PUBLISHER_ID_LENGTH];
};

struct _InfConnectionManagerGroup {
  InfConnectionManagerKey key;

  InfConnectionManager* manager;
  guint ref_count;

  InfNetObject* object;

  GSList* methods;
  GSList* queues;
};

typedef struct _InfConnectionManagerPrivate InfConnectionManagerPrivate;
struct _InfConnectionManagerPrivate {
  /* uuid_unparse_lower insists on adding a trailing NUL byte */
  gchar own_publisher_id[PUBLISHER_ID_LENGTH + 1];
  GHashTable* registered_connections;
  GTree* groups;
};

typedef struct _InfConnectionManagerRegroupData
  InfConnectionManagerRegroupData;
struct _InfConnectionManagerRegroupData {
  InfXmlConnection* publisher_conn;
  GSList* groups;
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

  /* First by group name */
  res = strcmp(first_key->group_name, second_key->group_name);
  if(res != 0) return res;

  /* Groups with known publisher id come first */
  if(first_key->publisher_id != NULL)
  {
    if(second_key->publisher_id != NULL)
    {
      return memcmp(
        first_key->publisher_id,
        second_key->publisher_id,
        PUBLISHER_ID_LENGTH
      );
    }
    else
    {
      return -1;
    }
  }
  else if(second_key->publisher_id != NULL)
  {
    return 1;
  }
  else
  {
    /* Both publisher keys are zero */
    if(first_key->publisher_conn < second_key->publisher_conn)
      return -1;
    if(first_key->publisher_conn > second_key->publisher_conn)
      return 1;
    return 0;
  }
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

static guint
inf_connection_manager_group_queue_capacity(InfConnectionManagerGroup* group,
                                            InfConnectionManagerQueue* queue)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerRegistration* registration;
  guint limit;

  priv = INF_CONNECTION_MANAGER_PRIVATE(group->manager);

  /* Determine maximum queued items */
  limit = INNER_QUEUE_LIMIT;
  /* We can't queue anything as long as we don't know the publisher ID
   * of the group since we need this to send things. */
  if(group->key.publisher_id == NULL)
    limit = 0;
  /* We can't queue anything as long as the connection is not yet
   * fully established (<=> we did not sent out our publisher ID yet, which
   * is the first thing we do when the connection is ready). */
  registration =
    g_hash_table_lookup(priv->registered_connections, queue->connection);
  g_assert(registration != NULL);
  if(!registration->publisher_id_sent)
    limit = 0;
  /* We don't want to queue anything as long as the parent_queue_item has
   * not yet been sent. */
  if(queue->parent_queue_item != NULL)
    limit = 0;

  g_assert(queue->inner_count <= limit);
  return limit - queue->inner_count;
}

static void
inf_connection_manager_announce_registration(InfConnectionManager* manager,
                                             InfXmlConnection* connection)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerRegistration* registration;
  xmlNodePtr id_announce;

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  registration =
    g_hash_table_lookup(priv->registered_connections, connection);
  g_assert(registration != NULL);
  g_assert(registration->publisher_id_sent == FALSE);

  /* Announce our publisher ID to the remote site so that locally created
   * groups can be joined by that connection. */
  id_announce = xmlNewNode(NULL, (const xmlChar*)"connection");
  inf_xml_util_set_attribute(id_announce, "id", priv->own_publisher_id);
  registration->publisher_id_sent = TRUE;

  inf_xml_connection_send(connection, id_announce);
}

static void
inf_connection_manager_group_real_send(InfConnectionManagerGroup* group,
                                       InfConnectionManagerQueue* queue,
                                       guint max)
{
  xmlNodePtr cur;
  xmlNodePtr cont;
  xmlNodePtr container_list;
  xmlNodePtr new_container;

  InfXmlConnection* connection;

  InfConnectionManagerScope scope;
  InfConnectionManagerScope cur_scope;
  InfConnectionManagerMsgType type;
  InfConnectionManagerMsgType cur_type;

  const gchar* publisher_id;

  GSList* released_groups;
  GSList* item;
  InfConnectionManagerGroup* child_group;
  InfConnectionManagerQueue* child_queue;
  guint capacity;

  g_assert(group->object != NULL);
  connection = queue->connection;

  g_assert(group->key.publisher_id != NULL);
  publisher_id = group->key.publisher_id;

  /* max==0 means all messages in queue */
  if(max == 0) max = ~(guint)0;

  released_groups = NULL;

  /* Append all messages to be sent to the queue->send_item list. This is
   * used to ensure order in recursive calls to this function. */
  container_list = NULL;

  /* Collect messages that we want to send, and put them into the container
   * list. Don't issue any callbacks here. */
  while(queue->first_item != NULL && max > 0)
  {
    cur = queue->first_item;
    queue->first_item = queue->first_item->next;
    if(queue->first_item == NULL)
      queue->last_item = NULL;
    ++ queue->inner_count;

    -- max;
    xmlUnlinkNode(cur);

    cur_scope = GPOINTER_TO_UINT(cur->_private) & 0xff;
    cur_type = GPOINTER_TO_UINT(cur->_private) >> 8;

    if(queue->send_item == NULL || scope != cur_scope || type != cur_type)
    {
      type = cur_type;
      scope = cur_scope;

      switch(cur_type)
      {
      case INF_CONNECTION_MANAGER_MESSAGE:
        new_container = xmlNewNode(NULL, (const xmlChar*)"group");
        break;
      case INF_CONNECTION_MANAGER_CONTROL:
        new_container = xmlNewNode(NULL, (const xmlChar*)"control");
        break;
      default:
        g_assert_not_reached();
        break;
      }

      if(queue->send_item)
        queue->send_item->next = new_container;
      else
        container_list = new_container;
      queue->send_item = new_container;

      inf_xml_util_set_attribute(queue->send_item, "publisher", publisher_id);

      inf_xml_util_set_attribute(
        queue->send_item,
        "name",
        group->key.group_name
      );

      switch(cur_scope)
      {
      case INF_CONNECTION_MANAGER_POINT_TO_POINT:
        break;
      case INF_CONNECTION_MANAGER_NETWORK:
        inf_xml_util_set_attribute(queue->send_item, "scope", "net");
        break;
      case INF_CONNECTION_MANAGER_GROUP:
        inf_xml_util_set_attribute(queue->send_item, "scope", "group");
        break;
      default:
        g_assert_not_reached();
        break;
      }
    }

    /* We need to reset the _private field before we allow the node to be
     * destroyed, otherwise libxml++ (if linked in) thinks it points to the
     * C++ wrapper and tries to delete it. */
    cur->_private = NULL;

    xmlAddChild(queue->send_item, cur);
  }

  /* Now we have all messages that we are about to send. There are two cases
   * here: Either this is the first call to send_real. In that case,
   * queue->send_item was NULL before the call, and we set container_list
   * to point to the messages to send. However, if this is a recursive call,
   * then queue->send_item is set. In that case, we appended the messages
   * we would like to send to queue->send_item and return immediately now
   * because container_list is not set. The call before ours will take care
   * of sending everything in the correct order. */
  if(container_list != NULL)
  {
    g_assert(queue->send_item != NULL);
    for(cont = container_list; cont != NULL; cont = cont->next)
    {
      for(cur = cont->children; cur != NULL; cur = cur->next)
      {
        /* If we have children groups, then the queue for the same connection
         * in these children groups wait for us to send something before they
         * start sending themselves. If they wait for the packet we are
         * currently sending, then remember the queue to unfreeze later. Don't
         * unfreeze now since we did not yet have sent the message (and we
         * can't check after sending since inf_xml_connection_send takes
         * ownership of cur eventually). */
        for(item = queue->children_groups; item != NULL; item = item->next)
        {
          child_group = (InfConnectionManagerGroup*)item->data;
          child_queue = inf_connection_manager_group_lookup_queue(
            child_group,
            queue->connection
          );

          g_assert(child_queue != NULL);
          g_assert(child_queue->parent_queue == queue);
          g_assert(child_queue->parent_queue_item != NULL);

          if(child_queue->parent_queue_item == cur)
            released_groups = g_slist_prepend(released_groups, child_group);
        }

        inf_net_object_enqueued(group->object, connection, cur);
      }

      inf_xml_connection_send(connection, cont);
    }

    queue->send_item = NULL;
  }

  /* Unfreeze queues that were waiting for a parent message to be sent */
  /* TODO: Should we ref the groups above, to make sure the callbacks in
   * between do not finally unref them? */
  for(item = released_groups; item != NULL; item = g_slist_next(item))
  {
    child_group = (InfConnectionManagerGroup*)item->data;
    child_queue = inf_connection_manager_group_lookup_queue(
      child_group,
      queue->connection
    );
    g_assert(child_queue != NULL);
    g_assert(child_queue->parent_queue == queue);

    queue->children_groups = g_slist_remove(
      queue->children_groups,
      child_group
    );

    child_queue->parent_queue = NULL;
    child_queue->parent_queue_item = NULL;

    capacity =
      inf_connection_manager_group_queue_capacity(child_group, child_queue);

    if(capacity > 0)
    {
      inf_connection_manager_group_real_send(
        child_group,
        child_queue,
        capacity
      );
    }
  }

  g_slist_free(released_groups);
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
    g_slice_free(InfConnectionManagerMethodInstance, instance);
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

  g_free(group->key.publisher_id);
  g_free(group->key.group_name);

  g_slice_free(InfConnectionManagerGroup, group);
}

static gboolean
inf_connection_manager_handle_connection_regroup_func(gpointer key,
                                                      gpointer value,
                                                      gpointer data)
{
  InfConnectionManagerKey* tree_key;
  InfConnectionManagerRegroupData* regroup_data;

  tree_key = (InfConnectionManagerKey*)key;
  regroup_data = (InfConnectionManagerRegroupData*)data;

  if(tree_key->publisher_id == NULL &&
     tree_key->publisher_conn == regroup_data->publisher_conn)
  {
    regroup_data->groups = g_slist_prepend(regroup_data->groups, value);
  }

  return FALSE;
}

static gboolean
inf_connection_manager_handle_connection(InfConnectionManager* manager,
                                         InfXmlConnection* connection,
                                         xmlNodePtr xml,
                                         GError** error)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerRegistration* registration;
  xmlChar* publisher_id;
  gchar* conn_id;
  InfConnectionManagerRegroupData data;
  InfConnectionManagerGroup* group;
  InfConnectionManagerGroup* other_group;
  InfConnectionManagerQueue* queue;
  GSList* item;
  GSList* queue_item;
  guint capacity;

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  registration = g_hash_table_lookup(
    priv->registered_connections,
    connection
  );

  /* This can only be called if the connection was registered */
  g_assert(registration != NULL);
  
  if(registration->publisher_id_known == TRUE)
  {
    g_object_get(G_OBJECT(connection), "remote-id", &conn_id, NULL);

    g_set_error(
      error,
      inf_connection_manager_error_quark,
      INF_CONNECTION_MANAGER_ERROR_PUBLISHER_ID_KNOWN,
      _("Publisher ID of connection '%s' is already known"),
      conn_id
    );

    g_free(conn_id);
    return FALSE;
  }

  /* Set publisher ID for yet unknown connection */
  publisher_id = inf_xml_util_get_attribute_required(xml, "id", error);
  if(publisher_id == NULL) return FALSE;

  if(strlen((const char*)publisher_id) != PUBLISHER_ID_LENGTH)
  {
    g_set_error(
      error,
      inf_connection_manager_error_quark,
      INF_CONNECTION_MANAGER_ERROR_INVALID_PUBLISHER_ID,
      _("Publisher ID '%s' has incorrect length"),
      (const gchar*)publisher_id
    );

    xmlFree(publisher_id);
    return FALSE;
  }

  registration->publisher_id_known = TRUE;
  memcpy(registration->publisher_id, publisher_id, PUBLISHER_ID_LENGTH);
  xmlFree(publisher_id);

  /* Reinsert groups that do not have a publisher id, but this publisher
   * connection. */
  data.publisher_conn = connection;
  data.groups = NULL;

  g_tree_foreach(
    priv->groups,
    inf_connection_manager_handle_connection_regroup_func,
    &data
  );
  
  for(item = data.groups; item != NULL; item = g_slist_next(item))
  {
    group = (InfConnectionManagerGroup*)item->data;
    g_tree_steal(priv->groups, &group->key);

    /* We do not let this point to registration->publisher_id since the
     * registered connection could be closed before we are done with the
     * group. Also, we need it NULL terminated to pass to xmlSetProp. */
    group->key.publisher_id = g_malloc(PUBLISHER_ID_LENGTH + 1);
    memcpy(
      group->key.publisher_id,
      registration->publisher_id,
      PUBLISHER_ID_LENGTH
    );
    group->key.publisher_id[PUBLISHER_ID_LENGTH] = '\0';

    /* safety, we should never access this anymore anyway: */
    group->key.publisher_conn = NULL;
    
    other_group = g_tree_lookup(priv->groups, &group->key);
    if(other_group != NULL)
    {
      g_set_error(
        error,
        inf_connection_manager_error_quark,
        INF_CONNECTION_MANAGER_ERROR_PUBLISHER_ID_IN_USE,
        _("Publisher ID '%s' is already in use"),
        group->key.publisher_id
      );

      /* Panique, this seems like another publisher has the same UUID
       * as the one that currently sent us its UUID, and additionally has
       * opened a group with the same name. */

      /* TODO: Emit some signal that the group no longer exists */
      /*inf_connection_manager_group_free(group);*/
      return FALSE;
    }
    else
    {
      g_tree_insert(priv->groups, &group->key, group);

      for(queue_item = group->queues;
          queue_item != NULL;
          queue_item = g_slist_next(queue_item))
      {
        queue = (InfConnectionManagerQueue*)queue_item->data;
        capacity = inf_connection_manager_group_queue_capacity(group, queue);

        if(capacity > 0)
        {
          inf_connection_manager_group_real_send(group, queue, capacity);
        }
      }
    }
  }

  g_slist_free(data.groups);
  return TRUE;
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
      _("Received unexpected message '%s'"),
      (const gchar*)xml->name
    );
    
    return FALSE;
  }

  group_name = inf_xml_util_get_attribute_required(xml, "name", error);
  if(group_name == NULL) return FALSE;

  publisher = inf_xml_util_get_attribute_required(xml, "publisher", error);
  if(publisher == NULL) { xmlFree(group_name); return FALSE; }

  /* Create key to lookup group */
  key.group_name = (gchar*)group_name;
  key.publisher_id = (gchar*)publisher;

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

      xmlFree(scope_attr);
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
      /* Group ref since net_object_received could do a final unref */
      inf_connection_manager_group_ref(group);

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
              _("Request from %s caused error: %s\n\nThe request could not "
                "be processed, thus the session is no longer guaranteed to be "
                "in a consistent state. Subsequent requests might therefore "
                "fail as well. The failed request was:\n\n%s\n\n"),
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

      inf_connection_manager_group_unref(group);
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
  
  if(strcmp((const char*)xml->name, "connection") == 0)
  {
    inf_connection_manager_handle_connection(
      manager,
      connection,
      xml,
      &error
    );
  }
  else
  {
    inf_connection_manager_handle_message(manager, connection, xml, &error);
  }

  if(error != NULL)
  {
    g_object_get(G_OBJECT(connection), "remote-id", &other_id, NULL);
    g_warning(
      _("Received bad XML request from %s: %s"),
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
  guint messages_sent;
  guint capacity;

  manager = INF_CONNECTION_MANAGER(user_data);
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

  if(strcmp((const char*)xml->name, "group") == 0)
    msg_type = INF_CONNECTION_MANAGER_MESSAGE;
  else if(strcmp((const char*)xml->name, "control") == 0)
    msg_type = INF_CONNECTION_MANAGER_CONTROL;
  else if(strcmp((const char*)xml->name, "connection") == 0)
    return;
  else
    /* We should not have sent such nonsense */
    g_assert_not_reached();

  group_name = xmlGetProp(xml, (const xmlChar*)"name");
  publisher = xmlGetProp(xml, (const xmlChar*)"publisher");
  /* We should not have sent such nonsense: */
  g_assert(group_name != NULL && publisher != NULL);

  key.group_name = (gchar*)group_name;
  key.publisher_id = (gchar*)publisher;

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
        /* TODO: Check that group still exists, and that it is the same,
         * maybe we should simply ref it. */
        ++ messages_sent;
      }

      queue->inner_count -= messages_sent;
      capacity = inf_connection_manager_group_queue_capacity(group, queue);

      inf_connection_manager_group_real_send(group, queue, capacity);
    }
  }
}

static gboolean
inf_connection_manager_connection_notify_cb_foreach_func(gpointer key,
                                                         gpointer value,
                                                         gpointer data)
{
  InfConnectionManagerGroup* group;
  InfXmlConnection* connection;
  GSList* item;
  InfConnectionManagerQueue* queue;
  guint capacity;

  group = (InfConnectionManagerGroup*)value;
  connection = (InfXmlConnection*)data;

  for(item = group->queues; item != NULL; item = g_slist_next(item))
  {
    queue = (InfConnectionManagerQueue*)item->data;
    if(queue->connection == connection)
    {
      capacity = inf_connection_manager_group_queue_capacity(group, queue);

      if(capacity > 0)
      {
        inf_connection_manager_group_real_send(group, queue, capacity);

        if(queue->first_item == NULL)
          queue->last_item = NULL;
      }
    }
  }

  return FALSE;
}

static void
inf_connection_manager_connection_notify_status_cb(GObject* object,
                                                   GParamSpec* pspec,
                                                   gpointer user_data)
{
  InfXmlConnection* connection;
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;
  InfXmlConnectionStatus status;

  connection = INF_XML_CONNECTION(object);
  manager = INF_CONNECTION_MANAGER(user_data);
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  switch(status)
  {
  case INF_XML_CONNECTION_OPEN:
    inf_connection_manager_announce_registration(manager, connection);

    /* Begin to send queued stuff, now that our own publisher
     * ID has been sent. */
    g_tree_foreach(
      priv->groups,
      inf_connection_manager_connection_notify_cb_foreach_func,
      connection
    );

    break;
  case INF_XML_CONNECTION_CLOSING:
  case INF_XML_CONNECTION_CLOSED:
    /* TODO: Free all queues using that connection? */
    break;
  case INF_XML_CONNECTION_OPENING:
    /* Nothing special */
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_connection_manager_init(GTypeInstance* instance,
                            gpointer g_class)
{
  InfConnectionManager* manager;
  InfConnectionManagerPrivate* priv;
#ifdef G_OS_WIN32
  UUID uuid;
  unsigned char* temp_uuid_str;
#else
  uuid_t uuid;
#endif

  manager = INF_CONNECTION_MANAGER(instance);
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);

#ifdef G_OS_WIN32
  temp_uuid_str = NULL;
  /* MinGW doesn't seem to have this, so we use UuidCreate instead */
  /* UuidCreateSequential(&uuid); */
  UuidCreate(&uuid);
  UuidToString(&uuid, &temp_uuid_str);

  /* TODO: Fall back to create a provider id using rand() or something? */
  g_assert(temp_uuid_str != NULL);
  if(temp_uuid_str != NULL)
  {
    memcpy(priv->own_publisher_id, temp_uuid_str, PUBLISHER_ID_LENGTH);
    priv->own_publisher_id[PUBLISHER_ID_LENGTH] = '\0';
  }
#else
  uuid_generate(uuid);
  uuid_unparse_lower(uuid, priv->own_publisher_id);
#endif

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
  g_assert(g_hash_table_size(priv->registered_connections) == 0);
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
                                  const InfConnectionManagerMethodDesc* const*
                                          methods)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerGroup* group;
  const InfConnectionManagerMethodDesc* const* desc;
  InfConnectionManagerMethodInstance* instance;
  
  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);
  g_return_val_if_fail(
    net_object == NULL || INF_IS_NET_OBJECT(net_object),
    NULL
  );
  g_return_val_if_fail(methods != NULL && *methods != NULL, NULL);
  g_return_val_if_fail(
    inf_connection_manager_lookup_group(manager, group_name, NULL) == NULL,
    NULL
  );
  
  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  group = g_slice_new(InfConnectionManagerGroup);

  group->key.group_name = g_strdup(group_name);
  group->key.publisher_id =
    g_strndup(priv->own_publisher_id, PUBLISHER_ID_LENGTH);
  group->key.publisher_conn = NULL; /* safety */

  group->manager = manager;
  group->ref_count = 1;

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
  InfConnectionManagerRegistration* registration;
  
  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(publisher_conn), NULL);
  g_return_val_if_fail(object == NULL || INF_IS_NET_OBJECT(object), NULL);
  g_return_val_if_fail(meth != NULL, NULL);
  g_return_val_if_fail(
    inf_connection_manager_lookup_group(
      manager,
      group_name,
      publisher_conn
    ) == NULL,
    NULL
  );

  priv = INF_CONNECTION_MANAGER_PRIVATE(manager);
  group = g_slice_new(InfConnectionManagerGroup);

  registration = g_hash_table_lookup(
    priv->registered_connections,
    publisher_conn
  );

  group->key.group_name = g_strdup(group_name);
  if(registration == NULL || !registration->publisher_id_known)
  {
    group->key.publisher_id = NULL;
    /* TODO: This relies on join() to register the publisher_conn. We should
     * perhaps ref it until the key does not need it any longer. */
    group->key.publisher_conn = publisher_conn;
  }
  else
  {
    group->key.publisher_id =
      g_strndup(registration->publisher_id, PUBLISHER_ID_LENGTH);
    group->key.publisher_conn = NULL; /* safety */
  }

  group->manager = manager;
  group->ref_count = 1;

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
  InfConnectionManagerRegistration* registration;
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
  {
    registration = g_hash_table_lookup(
      priv->registered_connections,
      publisher
    );

    if(registration == NULL || !registration->publisher_id_known)
    {
      key.publisher_id = NULL;
      key.publisher_conn = publisher;
    }
    else
    {
      key.publisher_id = registration->publisher_id;
      key.publisher_conn = NULL;
    }
  }
  else
  {
    key.publisher_id = priv->own_publisher_id;
    key.publisher_conn = NULL;
  }

  result = g_tree_lookup(priv->groups, &key);
  return (InfConnectionManagerGroup*)result;
}

/**
 * inf_connection_manager_lookup_group_by_id:
 * @manager: A #InfConnectionManager.
 * @group_name: The name of the group to lookup.
 * @publisher_id: The ID of the connection to the publisher.
 *
 * If @publisher_id is non-%NULL, then this function tries to find a group
 * with the given name and publisher ID. In contrast to
 * inf_connection_manager_lookup_group() this still works when the publisher
 * connection is no longer available.
 *
 * If @publisher_id is %NULL, then it tries to find a group of which the local
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
  {
    /* It can happen that the group is not in the tree, because a group with
     * the same key exists already. This check should no longer be necessary
     * when we directly remove such groups in
     * inf_connection_manager_handle_connection(), see also the todo comment
     * there. */
    if(g_tree_lookup(priv->groups, &group->key) == group)
      g_tree_remove(priv->groups, &group->key);
    else
      inf_connection_manager_group_free(group);
  }
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

#if 0
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
#endif

/**
 * inf_connection_manager_group_has_connection:
 * @group: A #InfConnectionManagerGroup.
 * @conn: A #InfXmlConnection.
 *
 * Returns whether @connection is a member of @group (see 
 * inf_connection_manager_group_add_connection() and
 * inf_connection_manager_group_remove_connection()).
 *
 * Returns: Whether @connection is in @group.
 **/
gboolean
inf_connection_manager_group_has_connection(InfConnectionManagerGroup* group,
                                            InfXmlConnection* conn)
{
  InfConnectionManagerQueue* queue;

  g_return_val_if_fail(group != NULL, FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(conn), FALSE);

  queue = inf_connection_manager_group_lookup_queue(group, conn);
  if(queue != NULL) return TRUE;
  return FALSE;
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
 * @prnt: A #InfXmlConnectionManagerGroup, or %NULL.
 *
 * This must be called whenever a remote host joins this group.
 *
 * Adds @conn to @group. Note that only publishers can add connections,
 * so the local host must be a publisher of @group to use this. This means
 * the group must have been created with inf_connection_manager_open_group().
 * Note that it is therefore impossible to join a group that has lost its
 * publisher.
 *
 * If @parent is non-%NULL, then messages sent to @conn via @group are queued
 * until all queued messages to @conn via @parent have been sent. Always
 * use this when transmitting the new group name in @parent to @conn, to make
 * sure the client has joined the group before the first messages in @group
 * arrives.
 *
 * Returns: %TRUE if the connection was added, or %FALSE if no method for
 * the connection's network was found.
 **/
gboolean
inf_connection_manager_group_add_connection(InfConnectionManagerGroup* group,
                                            InfXmlConnection* conn,
                                            InfConnectionManagerGroup* prnt)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerMethodInstance* instance;

  g_return_val_if_fail(group != NULL, FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(conn), FALSE);

  priv = INF_CONNECTION_MANAGER_PRIVATE(group->manager);
  g_return_val_if_fail(
    memcmp(
      group->key.publisher_id,
      priv->own_publisher_id,
      PUBLISHER_ID_LENGTH
    ) == 0,
    FALSE
  );

  g_return_val_if_fail(
    prnt == NULL ||
    inf_connection_manager_group_lookup_queue(prnt, conn),
    FALSE
  );

  instance = inf_connection_manager_get_method_by_connection(group, conn);
  if(instance == NULL) return FALSE;

  instance->desc->add_connection(instance->method, conn);
  inf_connection_manager_register_connection(group, conn, prnt);
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
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerMethodInstance* instance;

  g_return_if_fail(grp != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(conn));

  priv = INF_CONNECTION_MANAGER_PRIVATE(grp->manager);
  
  /* TODO: Allow the call when the publisher conn is no longer available
   * (<=> there is no connection with grp->key.publisher_id in registered
   * connections). */
  g_return_if_fail(
    memcmp(
      grp->key.publisher_id,
      priv->own_publisher_id,
      PUBLISHER_ID_LENGTH
    ) == 0
  );

  instance = inf_connection_manager_get_method_by_connection(grp, conn);
  /* We could not have added the connection otherwise: */
  g_assert(instance != NULL);

  instance->desc->remove_connection(instance->method, conn);
  inf_connection_manager_unregister_connection(grp, conn);
}

#if 0
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
#endif

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
  xmlNode* cur;

  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  queue = inf_connection_manager_group_lookup_queue(group, connection);
  g_return_if_fail(queue != NULL);

  if(queue->first_item != NULL)
  {
    /* We need to reset the _private field to NULL before destruction to be
     * compatible with libxml++, and perhaps other libxml2 wrappers. */
    for(cur = queue->first_item; cur != NULL; cur = cur->next)
      cur->_private = NULL;

    xmlFreeNodeList(queue->first_item);
    queue->first_item = NULL;
    queue->last_item = NULL;
  }
}

/**
 * inf_connection_manager_register_connection:
 * @group: A #InfConnectionManagerGroup.
 * @connection: A #InfXmlConnection.
 * @parent: A #InfConnectionManagerGroup, or %NULL.
 *
 * Registers @connection with @group. When a connection is registered, the
 * connection manager forwards incoming messages to the method to process,
 * and allows sending messages via inf_connection_manager_send_msg() and
 * inf_connection_manager_send_ctrl().
 *
 * If @parent is non-%NULL, then messages sent to @connection via @group are
 * queued until all queued messages to @connection via @parent (at the point
 * of this call) have been sent. Always use this when transmitting the new
 * group name in @parent to @connection, to make sure the client has joined
 * the group before the first messages in @group arrives.
 *
 * This function should only be used by method implementations. 
 **/
void
inf_connection_manager_register_connection(InfConnectionManagerGroup* group,
                                           InfXmlConnection* connection,
                                           InfConnectionManagerGroup* parent)
{
  InfConnectionManagerPrivate* priv;
  InfConnectionManagerQueue* queue;
  InfConnectionManagerRegistration* registration;
  InfXmlConnectionStatus status;
  InfConnectionManagerQueue* parent_queue;

  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_return_if_fail(
    inf_connection_manager_group_lookup_queue(group, connection) == NULL
  );

  priv = INF_CONNECTION_MANAGER_PRIVATE(group->manager);

  if(parent != NULL)
  {
    parent_queue =
      inf_connection_manager_group_lookup_queue(parent, connection);
    g_return_if_fail(parent_queue != NULL);
  }
  else
  {
    parent_queue = NULL;
  }

  queue = g_slice_new(InfConnectionManagerQueue);
  queue->connection = connection;
  queue->first_item = NULL;
  queue->last_item = NULL;
  queue->send_item = NULL;
  queue->inner_count = 0;
  if(parent_queue && parent_queue->last_item)
  {
    queue->parent_queue = parent_queue;
    queue->parent_queue_item = parent_queue->last_item;
  }
  else
  {
    queue->parent_queue = NULL;
    queue->parent_queue_item = NULL;
  }
  queue->children_groups = NULL;
  group->queues = g_slist_prepend(group->queues, queue);
  
  registration =
    g_hash_table_lookup(priv->registered_connections, connection);
  if(registration == NULL)
  {
    g_object_ref(connection);

    registration = g_slice_new(InfConnectionManagerRegistration);
    registration->registration_count = 1;
    /*registration->connection = connection;*/
    registration->publisher_id_known = FALSE;
    registration->publisher_id_sent = FALSE;

    g_hash_table_insert(
      priv->registered_connections,
      connection,
      registration
    );

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

    g_signal_connect(
      G_OBJECT(connection),
      "notify::status",
      G_CALLBACK(inf_connection_manager_connection_notify_status_cb),
      group->manager
    );

    g_object_get(G_OBJECT(connection), "status", &status, NULL);
    if(status == INF_XML_CONNECTION_OPEN)
    {
      inf_connection_manager_announce_registration(
        group->manager,
        connection
      );
    }
  }
  else
  {
    ++ registration->registration_count;
  }
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
  InfConnectionManagerRegistration* registration;
  InfXmlConnectionStatus status;

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
    inf_connection_manager_group_real_send(group, queue, 0);
  }

  /* After having sent everything, all children should have been released */
  g_assert(queue->children_groups == NULL);
  if(queue->parent_queue != NULL)
  {
    queue->parent_queue->children_groups =
      g_slist_remove(queue->parent_queue->children_groups, group);
  }

  registration =
    g_hash_table_lookup(priv->registered_connections, connection);
  g_assert(registration != NULL);

  -- registration->registration_count;
  if(registration->registration_count == 0)
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

    g_object_unref(connection);
    g_slice_free(InfConnectionManagerRegistration, registration);
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
  guint capacity;

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
  
  capacity = inf_connection_manager_group_queue_capacity(group, queue);

  if(queue->last_item != NULL)
    queue->last_item->next = xml;
  else
    queue->first_item = xml;
  queue->last_item = xml;

  if(capacity > 0)
  {
    g_assert(queue->first_item == xml);
    inf_connection_manager_group_real_send(group, queue, 1);
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
  guint capacity;

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

  capacity = inf_connection_manager_group_queue_capacity(group, queue);

  if(queue->last_item != NULL)
    queue->last_item->next = xml;
  else
    queue->first_item = xml;
  queue->last_item = xml;

  if(capacity > 0)
  {
    g_assert(queue->first_item = xml);
    inf_connection_manager_group_real_send(group, queue, 1);
  }
}

/* vim:set et sw=2 ts=2: */
