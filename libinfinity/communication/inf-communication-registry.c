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

typedef struct _InfCommunicationRegistryEntry InfCommunicationRegistryEntry;
struct _InfCommunicationRegistryEntry {
  InfCommunicationRegistryKey key;
};

typedef struct _InfCommunicationRegistryConnection
  InfCommunicationRegistryConnection;
struct _InfCommunicationRegistryConnection {
  InfXmlConnection* connection;
  guint ref_count;
};

typedef struct _InfCommunicationRegistryPrivate
  InfCommunicationRegistryPrivate;
struct _InfCommunicationRegistryPrivate {
  GHashTable* connections;
  GHashTable* entries;
};

static void
inf_communication_registry_add_connection(InfCommunicationRegistry* registry,
                                          InfXmlConnection* connection)
{
  InfCommunicationRegistryPrivate* priv;
  gpointer reg;

  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(registry);
  reg = g_hash_table_steal(priv->connections, connection);

  if(reg == NULL)
  {
    g_hash_table_insert(
      priv->connections,
      connection,
      GUINT_TO_POINTER(1)
    );

    g_object_ref(connection);

    g_signal_connect(
      G_OBJECT(connection),
      "received",
      G_CALLBACK(connection),
      registry
    );

    g_signal_connect(
      G_OBJECT(connection),
      "sent",
      G_CALLBACK(inf_communication_registry_sent_cb),
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
  reg = g_hash_table_steal(priv->connections, connection);
  g_assert(reg != NULL);

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
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(inf_communication_registry_received_cb),
      rgstry
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(inf_communication_registry_sent_cb),
      rgstry
    );

    g_object_unref(connection);
  }
}



#define INF_COMMUNICATION_REGISTRY_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_COMMUNICATION_TYPE_GROUP, InfCommunicationRegistryPrivate))

static GObjectClass* parent_class;

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
    NULL
    inf_communication_registry_entry_free
  );
}

static void
inf_communication_registry_dispose(GObject* object)
{
  InfCommunicationRegistry* group;
  InfCommunicationRegistryPrivate* priv;
  GHashTableIter iter;
  gpointer value;

  group = INF_COMMUNICATION_REGISTRY(object);
  priv = INF_COMMUNICATION_REGISTRY_PRIVATE(group);

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
    while(g_hash_table_iter_next(&iter, NULL, &value))
    {
      g_signal_handlers_disconnect_by_func(
        G_OBJECT(value),
        G_CALLBACK(inf_communication_registry_received_cb),
        registry
      );

      g_signal_handlers_disconnect_by_func(
        G_OBJECT(value),
        G_CALLBACK(inf_communication_registry_sent_cb),
        registry
      );

      g_object_unref(value);
    }
  }

  g_hash_table_unref(priv->connections)
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
 */

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

/**
 * inf_communication_registry_cancel_messages:
 * @registry: A #InfCommunicationRegistry.
 * @group: The group for which to cancel messages.
 * @connection: A registered #InfXmlConnection.
 *
 * Stops all messages scheduled to be sent to @connection in @group from being
 * sent.
 */

