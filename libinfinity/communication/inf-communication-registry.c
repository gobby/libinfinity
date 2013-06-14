/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-communication-registry
 * @title: InfCommunicationRegistry
 * @short_description: Sharing connections between multiple groups
 * @include: libinfinity/communication/inf-communication-registry.h
 * @stability: Unstable
 *
 * #InfCommunicationRegistry provides a way for #InfCommunicationMethod
 * implementations to share connections with other groups. Before using a
 * connection, call inf_communication_registry_register(). Then, messages can
 * be sent to the group via inf_communication_registry_send().
 *
 * The #InfCommunicationRegistry calls inf_communication_method_received()
 * on your method when it received a message for the group,
 * inf_communication_method_enqueued() when sending the message cannot be
 * cancelled anymore via inf_communication_registry_cancel_messages() and
 * inf_communication_method_sent() when the message has been sent.
 **/

#include <libinfinity/communication/inf-communication-registry.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/inf-signals.h>

#include <string.h>

/* TODO: Store connection->InfCommunicationRegistryConnection hashtable,
 * store network and remote_id there, only point to in key. */

typedef struct _InfCommunicationRegistryKey InfCommunicationRegistryKey;
struct _InfCommunicationRegistryKey {
  InfXmlConnection* connection;
  gchar* publisher_id;
  const gchar* group_name;
};

typedef struct _InfCommunicationRegistryEntry InfCommunicationRegistryEntry;
struct _InfCommunicationRegistryEntry {
  InfCommunicationRegistry* registry;
  InfCommunicationRegistryKey key;
  const gchar* publisher_string;

  InfCommunicationGroup* group;
  InfCommunicationMethod* method;

  /* Queue of messages to send */
  guint inner_count;
  xmlNodePtr queue_begin;
  xmlNodePtr queue_end;

  /* Activation status */
  gboolean registered;
  guint activation_count; /* # messages to be sent until activation */

  xmlNodePtr enqueued_list;
  xmlNodePtr sent_list;
};

typedef struct _InfCommunicationRegistryForeachMethodData
  InfCommunicationRegistryForeachMethodData;
struct _InfCommunicationRegistryForeachMethodData {
  InfCommunicationMethod* original_method;
  xmlNodePtr xml;
};

typedef struct _InfCommunicationRegistryPrivate
  InfCommunicationRegistryPrivate;
struct _InfCommunicationRegistryPrivate {
  GHashTable* connections;
  GHashTable* entries;
};

#define INF_COMMUNICATION_REGISTRY_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_COMMUNICATION_TYPE_REGISTRY, InfCommunicationRegistryPrivate))

static GObjectClass* parent_class;

/* Maximum number of messages enqueued at the same time */
static const guint INF_COMMUNICATION_REGISTRY_INNER_QUEUE_LIMIT = 5;

static void
inf_communication_registry_send_real(InfCommunicationRegistryEntry* entry,
                                     guint num_messages)
{
  xmlNodePtr container;
  xmlNodePtr child;
  xmlNodePtr xml;
  guint i;

  container = xmlNewNode(NULL, (const xmlChar*)"group");
  if(entry->publisher_string != NULL)
  {
    inf_xml_util_set_attribute(
      container,
      "publisher",
      entry->publisher_string
    );
  }

  inf_xml_util_set_attribute(container, "name", entry->key.group_name);

  for(i = 0; i < num_messages && ((xml = entry->queue_begin) != NULL); ++ i)
  {
    entry->queue_begin = entry->queue_begin->next;
    if(entry->queue_begin == NULL) entry->queue_end = NULL;
    ++ entry->inner_count;

    xmlUnlinkNode(xml);
    xmlAddChild(container, xml);
  }

  /* Keep order of enqueued() calls and inf_xml_connection_send() calls
   * intact even if this function is run recursively in one of the
   * functions mentioned above. */
  if(entry->enqueued_list != NULL)
  {
    entry->enqueued_list->next = container;
    entry->enqueued_list = container;
  }
  else
  {
    entry->enqueued_list = container;
    child = container;

    while(child != NULL)
    {
      /* TODO: The group could be unset at this point if called from
       * inf_communication_registry_entry_free() in turn called by
       * inf_communication_registry_group_unrefed(). This can be removed if
       * we keep the group alive in that case, refer to the comment below in
       * inf_communication_registry_entry_free(). */
      if(entry->group != NULL)
      {
        for(xml = child->children; xml != NULL; xml = xml->next)
        {
          inf_communication_method_enqueued(
            entry->method,
            entry->key.connection,
            xml
          );
        }
      }

      if(child == entry->enqueued_list)
        entry->enqueued_list = NULL;

      xml = child;
      child = child->next;

      /* There are two possible cases at this point:
       * 1) We reached the end of the list. In that case, entry->enqueued_list
       * has been reset to NULL. This also means that we have sent everything
       * successfully. We will terminate this function after this call, so
       * a recursive call can just begin from scratch.
       * 2) entry->enqueued_list is not NULL. In this case, a recursive call
       * will simply append to entry->enqueued_list, and we will enqueue and
       * send the messages within the next iteration(s).
       */
      inf_xml_connection_send(entry->key.connection, xml);
    }
  }
}

/* Required by inf_communication_registry_entry_free() */
static void
inf_communication_registry_group_unrefed(gpointer user_data,
                                         GObject* where_the_object_was);


static void
inf_communication_registry_entry_free(gpointer data)
{
  InfCommunicationRegistryEntry* entry;
  InfXmlConnectionStatus status;

  entry = (InfCommunicationRegistryEntry*)data;

  /* Send all messages directly as we are freed and can't keep them around
   * any longer. */
  /* TODO: Ref the group on unregistration, so that the group stays alive
   * until all scheduled messages have been sent. In this case, the entry
   * will in no cases be freed, ane we can assert() here. When we do this,
   * we need to take into account the following:
   * 1) Assert in inf_communication_registry_group_unrefed() that the entry
   * is registered, as the group cannot be unrefed as long as we hold a
   * reference.
   * 2) Unref the group here, as we do with the connection. Do this after the
   * weak unref.
   * 3) Allow connection manager to return existing groups on join or host,
   * as the groups can live longer than people expect.
   */
  g_object_get(G_OBJECT(entry->key.connection), "status", &status, NULL);
  if(status != INF_XML_CONNECTION_CLOSING &&
     status != INF_XML_CONNECTION_CLOSED)
  {
    if(entry->queue_begin != NULL)
      inf_communication_registry_send_real(entry, G_MAXUINT);
  }

  if(entry->group)
  {
    g_object_weak_unref(
      G_OBJECT(entry->group),
      inf_communication_registry_group_unrefed,
      entry
    );
  }

  if(!entry->registered)
    g_object_unref(entry->key.connection);

  g_free(entry->key.publisher_id);
  g_slice_free(InfCommunicationRegistryEntry, entry);
}

static guint
inf_communication_registry_key_hash(gconstpointer key_)
{
  const InfCommunicationRegistryKey* key;
  key = (const InfCommunicationRegistryKey*)key_;

  /* TODO: Is this a good hash function? */
  return g_direct_hash(key->connection)
       ^ g_str_hash(key->publisher_id)
       ^ g_str_hash(key->group_name);
}

static int
inf_communication_registry_key_cmp(gconstpointer first,
                                   gconstpointer second)
{
  const InfCommunicationRegistryKey* first_key;
  const InfCommunicationRegistryKey* second_key;
  int res;

  first_key = (const InfCommunicationRegistryKey*)first;
  second_key = (const InfCommunicationRegistryKey*)second;

  if(first_key->connection < second_key->connection)
    return -1;
  if(first_key->connection > second_key->connection)
    return 1;

  res = strcmp(first_key->group_name, second_key->group_name);
  if(res != 0) return res;

  return strcmp(first_key->publisher_id, second_key->publisher_id);
}

static gboolean
inf_communication_registry_key_equal(gconstpointer first,
                                     gconstpointer second)
{
  return inf_communication_registry_key_cmp(first, second) == 0;
}

static void
inf_communication_registry_foreach_method_func(InfCommunicationMethod* method,
                                               gpointer user_data)
{
  InfCommunicationRegistryForeachMethodData* data;
  data = (InfCommunicationRegistryForeachMethodData*)user_data;

  /* TODO: Make sure that any callbacks in the send functions do not alter
   * the hash table which holds the methods in InfCommunicationGroup. */
  if(method != data->original_method)
    inf_communication_method_send_all(method, xmlCopyNode(data->xml, 1));
}

static void
inf_communication_registry_received_cb(InfXmlConnection* connection,
                                       xmlNodePtr xml,
                                       gpointer user_data)
{
  InfCommunicationRegistry* registry;
  InfCommunicationRegistryPrivate* priv;
  InfCommunicationRegistryKey key;
  InfCommunicationRegistryEntry* entry;
  xmlChar* group_name;
  xmlChar* publisher;
  xmlNodePtr child;
  InfCommunicationScope scope;
  InfCommunicationRegistryForeachMethodData data;

  registry = INF_COMMUNICATION_REGISTRY(user_data);
  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);

  group_name = xmlGetProp(xml, (const xmlChar*)"name");
  if(group_name == NULL) return;

  publisher = xmlGetProp(xml, (const xmlChar*)"publisher");

  if(publisher == NULL)
  {
    g_object_get(G_OBJECT(connection), "remote-id", &key.publisher_id, NULL);
  }
  else if(strcmp((const char*)publisher, "me") == 0)
  {
    g_object_get(G_OBJECT(connection), "remote-id", &key.publisher_id, NULL);
    xmlFree(publisher);
    publisher = NULL;
  }
  else if(strcmp((const char*)publisher, "you") == 0)
  {
    g_object_get(G_OBJECT(connection), "local-id", &key.publisher_id, NULL);
    xmlFree(publisher);
    publisher = NULL;
  }
  else
  {
    key.publisher_id = (gchar*)publisher;
  }

  key.connection = connection;
  key.group_name = (const gchar*)group_name;

  /* Relookup for each child to make sure the entry stays alive */
  for(child = xml->children; child != NULL; child = child->next)
  {
    entry = g_hash_table_lookup(priv->entries, &key);
    if(entry != NULL && entry->registered == TRUE)
    {
      scope = inf_communication_method_received(
        entry->method,
        entry->key.connection,
        child
      );

      /* If this was a group message then we relay it to any
       * other networks group is in. */
      if(scope == INF_COMMUNICATION_SCOPE_GROUP)
      {
        data.original_method = entry->method;
        data.xml = child;

        _inf_communication_group_foreach_method(
	  entry->group,
	  inf_communication_registry_foreach_method_func,
	  &data
	);
      }
    }
  }

  if(publisher != NULL)
    xmlFree(publisher);
  else
    g_free(key.publisher_id);
  xmlFree(group_name);
}

static void
inf_communication_registry_sent_cb(InfXmlConnection* connection,
                                   xmlNodePtr xml,
                                   gpointer user_data)
{
  InfCommunicationRegistry* registry;
  InfCommunicationRegistryPrivate* priv;
  InfCommunicationRegistryEntry* entry;
  InfCommunicationRegistryKey key;
  xmlChar* publisher;
  xmlChar* group_name;
  xmlNodePtr child;
  xmlNodePtr cur;

  registry = INF_COMMUNICATION_REGISTRY(user_data);
  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);

  group_name = xmlGetProp(xml, (const xmlChar*)"name");
  g_assert(group_name != NULL);

  publisher = xmlGetProp(xml, (const xmlChar*)"publisher");
  if(publisher == NULL)
  {
    g_object_get(G_OBJECT(connection), "local-id", &key.publisher_id, NULL);
  }
  else if(strcmp((const char*)publisher, "me") == 0)
  {
    g_object_get(G_OBJECT(connection), "local-id", &key.publisher_id, NULL);
    xmlFree(publisher);
    publisher = NULL;
  }
  else if(strcmp((const char*)publisher, "you") == 0)
  {
    g_object_get(G_OBJECT(connection), "remote-id", &key.publisher_id, NULL);
    xmlFree(publisher);
    publisher = NULL;
  }
  else
  {
    key.publisher_id = (gchar*)publisher;
  }

  key.connection = connection;
  key.group_name = (const gchar*)group_name;

  entry = g_hash_table_lookup(priv->entries, &key);
  if(entry != NULL)
  {
    if(entry->sent_list != NULL)
    {
      entry->sent_list->next = xmlCopyNode(xml, 1);
      entry->sent_list = entry->sent_list->next;
    }
    else
    {
      entry->sent_list = xml;
      child = xml;

      while(child != NULL)
      {
        for(cur = child->children; cur != NULL; cur = cur->next)
        {
          g_assert(entry->inner_count > 0);

          /* Still registered */
          if(entry->activation_count > 0)
          {
            -- entry->activation_count;
          }
          else
          {
            /* Must be registered if activation count is 0 */
            g_assert(entry->registered == TRUE);

            inf_communication_method_sent(
              entry->method,
              entry->key.connection,
              cur
            );

            /* If the callback did unregister us, then the activation count
             * was set (counting the message for which the callback was
             * called, since inner_count has not yet been decreased). We do
             * correct this here. */
            if(entry->activation_count > 0)
              -- entry->activation_count;
          }

          -- entry->inner_count;
        }

        cur = child;
        child = child->next;

        if(cur == entry->sent_list) entry->sent_list = NULL;
        if(cur != xml) xmlFreeNode(cur);
      }
    }

    /* Messages have been sent, meaning the number of queued messages has
     * decreased, so we can send more messages now. */
    /* Send next bunch of messages if inner_count reached zero, meaning no
     * more messages have been enqueued, for better packing. */
    if(entry->inner_count == 0 && entry->queue_end != NULL)
    {
      inf_communication_registry_send_real(
        entry,
        INF_COMMUNICATION_REGISTRY_INNER_QUEUE_LIMIT - entry->inner_count
      );
    }

    /* Free the entry in case all scheduled messages have been sent after
     * unregistration. */
    if(entry->registered == FALSE && entry->activation_count == 0)
      g_hash_table_remove(priv->entries, &key);
  }

  if(publisher == NULL)
    g_free(key.publisher_id);
  else
    xmlFree(publisher);
  xmlFree(group_name);
}

static void
inf_communication_registry_notify_status_cb(GObject* object,
                                            GParamSpec* pspec,
                                            gpointer user_data)
{
  InfCommunicationRegistry* registry;
  InfCommunicationRegistryPrivate* priv;
  InfXmlConnectionStatus status;
  InfXmlConnection* connection;
  GHashTableIter iter;
  gpointer value;
  InfCommunicationRegistryEntry* entry;
  InfCommunicationGroup* group;
  gboolean registered;

  registry = INF_COMMUNICATION_REGISTRY(user_data);
  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);

  connection = INF_XML_CONNECTION(object);
  g_object_get(object, "status", &status, NULL);

  /* Free all entries that have been unregistered if the connection was
   * closed. */
  if(status == INF_XML_CONNECTION_CLOSING ||
     status == INF_XML_CONNECTION_CLOSED)
  {
    g_hash_table_iter_init(&iter, priv->entries);
    while(g_hash_table_iter_next(&iter, NULL, &value))
    {
      entry = (InfCommunicationRegistryEntry*)value;
      if(entry->key.connection == connection)
      {
        group = g_object_ref(entry->group);
        registered = entry->registered;

        if(entry->registered == FALSE)
          g_hash_table_iter_remove(&iter);

        g_object_unref(group);
      }
    }
  }
}

static void
inf_communication_registry_add_connection(InfCommunicationRegistry* registry,
                                          InfXmlConnection* connection)
{
  InfCommunicationRegistryPrivate* priv;
  gpointer reg;

  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);
  reg = g_hash_table_lookup(priv->connections, connection);
  g_hash_table_steal(priv->connections, connection);

  if(reg == NULL)
  {
    g_hash_table_insert(
      priv->connections,
      connection,
      GUINT_TO_POINTER(1)
    );

    g_object_ref(connection);

    g_signal_connect_after(
      G_OBJECT(connection),
      "received",
      G_CALLBACK(inf_communication_registry_received_cb),
      registry
    );

    g_signal_connect_after(
      G_OBJECT(connection),
      "sent",
      G_CALLBACK(inf_communication_registry_sent_cb),
      registry
    );

    g_signal_connect(
      G_OBJECT(connection),
      "notify::status",
      G_CALLBACK(inf_communication_registry_notify_status_cb),
      registry
    );
  }
  else
  {
    g_hash_table_insert(
      priv->connections,
      connection,
      GUINT_TO_POINTER(1+GPOINTER_TO_UINT(reg))
    );
  }
}

static void
inf_communication_registry_remove_connection(InfCommunicationRegistry* rgstry,
                                             InfXmlConnection* connection)
{
  InfCommunicationRegistryPrivate* priv;
  gpointer reg;
  guint count;

  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(rgstry);
  reg = g_hash_table_lookup(priv->connections, connection);
  g_assert(reg != NULL);

  g_hash_table_steal(priv->connections, connection);

  count = GPOINTER_TO_UINT(reg);
  if(--count > 0)
  {
    g_hash_table_insert(
      priv->connections,
      connection,
      GUINT_TO_POINTER(count)
    );
  }
  else
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(inf_communication_registry_received_cb),
      rgstry
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(inf_communication_registry_sent_cb),
      rgstry
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(inf_communication_registry_notify_status_cb),
      rgstry
    );

    g_object_unref(connection);
  }
}

static void
inf_communication_registry_group_unrefed(gpointer user_data,
                                         GObject* where_the_object_was)
{
  InfCommunicationRegistryEntry* entry;
  InfCommunicationRegistry* registry;
  InfCommunicationRegistryPrivate* priv;
  GHashTableIter iter;
  gpointer value;

  InfXmlConnection* connection;
  gboolean registered;

  entry = (InfCommunicationRegistryEntry*)user_data;
  registry = entry->registry;
  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);

  /* This is completely valid if the connection was unregistered, only
   * sending final scheduled messages. */
  if(entry->registered == TRUE)
    g_warning("An unrefed group still had registered connections");

  /* The group has already been finalized, so we can't remove the group by
   * key since group_name in the entry's key pointed to group's memory. */
  g_hash_table_iter_init(&iter, priv->entries);
  while(g_hash_table_iter_next(&iter, NULL, &value))
  {
    if(value == entry)
    {
      connection = entry->key.connection;
      registered = entry->registered;

      /* So inf_communication_registry_entry_free() does not try to weak unref
       * the non-existing group: */
      entry->group = NULL;

      /* TODO: This relies on entry->key.group_name being still valid.
       * valgrind suggests it is. However, I don't feel confident with this.
       * I this can be properly fixed when we keep the group alive for
       * unregistered connections, refer to the comment in
       * inf_communication_registry_entry_free(). */
      g_hash_table_iter_remove(&iter);

      if(registered == TRUE)
        inf_communication_registry_remove_connection(registry, connection);

      break;
    }
  }
}

/*
 * GObject overrides.
 */

static void
inf_communication_registry_init(GTypeInstance* instance,
                                gpointer g_class)
{
  InfCommunicationRegistry* registry;
  InfCommunicationRegistryPrivate* priv;

  registry = INF_COMMUNICATION_REGISTRY(instance);
  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);

  priv->connections = g_hash_table_new(NULL, NULL);

  priv->entries = g_hash_table_new_full(
    inf_communication_registry_key_hash,
    inf_communication_registry_key_equal,
    NULL,
    inf_communication_registry_entry_free
  );
}

static void
inf_communication_registry_dispose(GObject* object)
{
  InfCommunicationRegistry* registry;
  InfCommunicationRegistryPrivate* priv;
  GHashTableIter iter;
  gpointer key;

  registry = INF_COMMUNICATION_REGISTRY(object);
  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);

  if(g_hash_table_size(priv->connections))
  {
    g_warning(
      "There are still registered connections on communication "
      "registry dispose"
    );

    /* Release all connections. We can't rely on a key FreeFunc since
     * the signal handlers cannot be disconnected easily this way as we
     * don't have access to the registry in the FreeFunc. */
    g_hash_table_iter_init(&iter, priv->connections);
    while(g_hash_table_iter_next(&iter, &key, NULL))
    {
      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(key),
        G_CALLBACK(inf_communication_registry_received_cb),
        registry
      );

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(key),
        G_CALLBACK(inf_communication_registry_sent_cb),
        registry
      );

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(key),
        G_CALLBACK(inf_communication_registry_notify_status_cb),
        registry
      );

      g_object_unref(key);
    }
  }

  g_hash_table_unref(priv->connections);
  g_hash_table_unref(priv->entries);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

/*
 * GType registration.
 */

static void
inf_communication_registry_class_init(gpointer g_class,
                                     gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfCommunicationRegistryPrivate));

  object_class->dispose = inf_communication_registry_dispose;
}

GType
inf_communication_registry_get_type(void)
{
  static GType registry_type = 0;

  if(!registry_type)
  {
    static const GTypeInfo registry_type_info = {
      sizeof(InfCommunicationRegistryClass),  /* class_size */
      NULL,                                   /* base_init */
      NULL,                                   /* base_finalize */
      inf_communication_registry_class_init,  /* class_init */
      NULL,                                   /* class_finalize */
      NULL,                                   /* class_data */
      sizeof(InfCommunicationRegistry),       /* instance_size */
      0,                                      /* n_preallocs */
      inf_communication_registry_init,        /* instance_init */
      NULL                                    /* value_table */
    };

    registry_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfCommunicationRegistry",
      &registry_type_info,
      0
    );
  }

  return registry_type;
}

/*
 * Public API.
 */

/**
 * inf_communication_registry_register:
 * @registry: A #InfCommunicationRegistry.
 * @group: The group for which to register a connection.
 * @method: The #InfCommunicationMethod used.
 * @connection: The connection to register.
 *
 * Registers connection with @group. This allows sending messages to
 * @connection via inf_communication_registry_send(). For received messages,
 * inf_communication_method_received() is called on @method.
 *
 * @connection must have status %INF_XML_CONNECTION_OPEN.
 */
void
inf_communication_registry_register(InfCommunicationRegistry* registry,
                                    InfCommunicationGroup* group,
                                    InfCommunicationMethod* method,
                                    InfXmlConnection* connection)
{
  InfCommunicationRegistryPrivate* priv;
  InfCommunicationRegistryKey key;
  InfCommunicationRegistryEntry* entry;
  InfXmlConnectionStatus status;
  gchar* local_id;
  gchar* remote_id;

  g_return_if_fail(INF_COMMUNICATION_IS_REGISTRY(registry));
  g_return_if_fail(INF_COMMUNICATION_IS_GROUP(group));
  g_return_if_fail(INF_COMMUNICATION_IS_METHOD(method));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_object_get(G_OBJECT(connection), "status", &status, NULL);
  g_return_if_fail(status == INF_XML_CONNECTION_OPEN);

  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);
  key.connection = connection;
  key.publisher_id =
    inf_communication_group_get_publisher_id(group, connection);
  key.group_name = inf_communication_group_get_name(group);

  inf_communication_registry_add_connection(registry, connection);

  entry = g_hash_table_lookup(priv->entries, &key);
  if(entry != NULL)
  {
    /* Reactivation */
    g_assert(entry->registered == FALSE);
    entry->registered = TRUE;
  }
  else
  {
    entry = g_slice_new(InfCommunicationRegistryEntry);
    entry->registry = registry;
    entry->key = key;

    g_object_get(
      G_OBJECT(connection),
      "remote-id", &remote_id,
      "local-id", &local_id,
      NULL
    );

    if(strcmp(remote_id, key.publisher_id) == 0)
      entry->publisher_string = "you";
    else if(strcmp(local_id, key.publisher_id) == 0)
      entry->publisher_string = NULL; /* "me" */
    else
      entry->publisher_string = entry->key.publisher_id;

    g_free(remote_id);
    g_free(local_id);

    entry->group = group;
    entry->method = method;

    entry->inner_count = 0;
    entry->queue_begin = NULL;
    entry->queue_end = NULL;

    entry->registered = TRUE;
    entry->activation_count = 0;

    entry->enqueued_list = NULL;
    entry->sent_list = NULL;

    g_object_weak_ref(
      G_OBJECT(group),
      inf_communication_registry_group_unrefed,
      entry
    );

    g_hash_table_insert(priv->entries, &entry->key, entry);
  }
}

/**
 * inf_communication_registry_unregister:
 * @registry: A #InfCommunicationRegistry.
 * @group: The group for which to unregister a connection.
 * @connection: The connection to unregister.
 *
 * Unregisters @connection from @group. Incoming messages are no longer
 * reported to group's method, and inf_communication_registry_send() can
 * no longer be called for @connection.
 */
void
inf_communication_registry_unregister(InfCommunicationRegistry* registry,
                                      InfCommunicationGroup* group,
                                      InfXmlConnection* connection)
{
  InfCommunicationRegistryPrivate* priv;
  InfCommunicationRegistryKey key;
  InfCommunicationRegistryEntry* entry;
  InfXmlConnectionStatus status;
  xmlNodePtr xml;

  g_return_if_fail(INF_COMMUNICATION_IS_REGISTRY(registry));
  g_return_if_fail(INF_COMMUNICATION_IS_GROUP(group));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);

  key.connection = connection;
  key.publisher_id =
    inf_communication_group_get_publisher_id(group, connection);
  key.group_name = inf_communication_group_get_name(group);

  entry = g_hash_table_lookup(priv->entries, &key);
  g_assert(entry != NULL && entry->registered == TRUE);

  if( (entry->queue_end != NULL || entry->inner_count > 0) &&
     status != INF_XML_CONNECTION_CLOSING &&
     status != INF_XML_CONNECTION_CLOSED)
  {
    /* The entry has still messages to send, so don't remove it right now
     * but wait until all scheduled messages have been sent. */
    entry->registered = FALSE;
    entry->activation_count = entry->inner_count;
    for(xml = entry->queue_begin; xml != NULL; xml = xml->next)
      ++ entry->activation_count;
    g_assert(entry->activation_count > 0);

    /* Keep an additional reference on the connection as the connection will
     * be unregistered below. */
    g_object_ref(connection);
  }
  else
  {
    /* No scheduled messages, remove entry */
    g_hash_table_remove(priv->entries, &key);
  }

  g_free(key.publisher_id);
  inf_communication_registry_remove_connection(registry, connection);
}

/**
 * inf_communication_registry_is_registered:
 * @registry: A #InfCommunicationRegistry.
 * @group: The group for which to check whether @connection is registered.
 * @connection: The connection to check for registration.
 *
 * Returns whether @connection has been registered for @group with
 * inf_communication_registry_register().
 *
 * Returns: %TRUE if @connection has been registered, or %FALSE otherwise.
 */
gboolean
inf_communication_registry_is_registered(InfCommunicationRegistry* registry,
                                         InfCommunicationGroup* group,
                                         InfXmlConnection* connection)
{
  InfCommunicationRegistryPrivate* priv;
  InfCommunicationRegistryKey key;
  InfCommunicationRegistryEntry* entry;

  g_return_val_if_fail(INF_COMMUNICATION_IS_REGISTRY(registry), FALSE);
  g_return_val_if_fail(INF_COMMUNICATION_IS_GROUP(group), FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), FALSE);

  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);

  key.connection = connection;
  key.publisher_id =
    inf_communication_group_get_publisher_id(group, connection);
  key.group_name = inf_communication_group_get_name(group);

  entry = g_hash_table_lookup(priv->entries, &key);
  g_free(key.publisher_id);

  return entry != NULL && entry->registered == TRUE;
}

/**
 * inf_communication_registry_send:
 * @registry: A #InfCommunicationRegistry.
 * @group: The group for which to send the message #InfCommunicationGroup.
 * @connection: A registered #InfXmlConnection.
 * @xml: The message to send.
 *
 * Sends an XML message to @connection. @connection must have been registered
 * with inf_communication_registry_register() before. If the message has been
 * sent, inf_communication_method_sent() is called on the method the
 * connection was registered with. inf_communication_method_enqueued() is
 * called when sending the message can no longer be cancelled via
 * inf_communication_registry_cancel_messages().
 *
 * This function takes ownership of @xml.
 */
void
inf_communication_registry_send(InfCommunicationRegistry* registry,
                                InfCommunicationGroup* group,
                                InfXmlConnection* connection,
                                xmlNodePtr xml)
{
  InfCommunicationRegistryPrivate* priv;
  InfCommunicationRegistryKey key;
  InfCommunicationRegistryEntry* entry;

  g_return_if_fail(INF_COMMUNICATION_IS_REGISTRY(registry));
  g_return_if_fail(INF_COMMUNICATION_IS_GROUP(group));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);
  key.connection = connection;
  key.publisher_id =
    inf_communication_group_get_publisher_id(group, connection);
  key.group_name = inf_communication_group_get_name(group);

  entry = g_hash_table_lookup(priv->entries, &key);
  g_assert(entry != NULL && entry->registered == TRUE);

  xmlUnlinkNode(xml);
  if(entry->queue_end == NULL)
  {
    entry->queue_begin = xml;
    entry->queue_end = xml;
  }
  else
  {
    entry->queue_end->next = xml;
    entry->queue_end = xml;
  }

  /* If there is something in the inner queue, don't send directly but wait
   * until the message has been sent, for better packing. */
  if(entry->inner_count == 0)
  {
    inf_communication_registry_send_real(
      entry,
      INF_COMMUNICATION_REGISTRY_INNER_QUEUE_LIMIT - entry->inner_count
    );
  }

  g_free(key.publisher_id);
}

/**
 * inf_communication_registry_cancel_messages:
 * @registry: A #InfCommunicationRegistry.
 * @group: The group for which to cancel messages.
 * @connection: A registered #InfXmlConnection.
 *
 * Stops all messages scheduled to be sent to @connection in @group from being
 * sent.
 */
void
inf_communication_registry_cancel_messages(InfCommunicationRegistry* registry,
                                           InfCommunicationGroup* group,
                                           InfXmlConnection* connection)
{
  InfCommunicationRegistryPrivate* priv;
  InfCommunicationRegistryKey key;
  InfCommunicationRegistryEntry* entry;

  g_return_if_fail(INF_COMMUNICATION_IS_REGISTRY(registry));
  g_return_if_fail(INF_COMMUNICATION_IS_GROUP(group));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);
  key.connection = connection;
  key.publisher_id =
    inf_communication_group_get_publisher_id(group, connection);
  key.group_name = inf_communication_group_get_name(group);

  entry = g_hash_table_lookup(priv->entries, &key);
  g_assert(entry != NULL && entry->registered == TRUE);

  /* TODO: Don't cancel messages prior activation? */
  xmlFreeNodeList(entry->queue_begin);
  entry->queue_begin = NULL;
  entry->queue_end = NULL;

  g_free(key.publisher_id);
}

/* vim:set et sw=2 ts=2: */
