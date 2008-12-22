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
 * SECTION:inf-communication-group
 * @title: InfCommunicationGroup
 * @short_description: Communication groups 
 * @include: libinfinity/communication/inf-communication-group.h
 * @stability: Unstable
 *
 * #InfCommunicationGroup represents a group of different hosts. The group
 * supports sending messages between group members and to the whole group.
 *
 * A communication group supports multiple networks. Each connection belongs
 * to a network, identified by the #InfXmlConnection:network property. It is
 * assumed that hosts on different networks can't directly communicate with
 * each other. Examples for networks are "tcp/ip" or "jabber".
 *
 * All communication for a given network is performed by a
 * #InfCommunicationMethod. The method defines how data is sent is sent
 * to the group. For example, a method could choose to relay all data via
 * a central server, or to send all data directly between the hosts, or in
 * case of a jabber network, use jabber groupchat functionality.
 **/

#include <libinfinity/communication/inf-communication-group.h>

typedef struct _InfCommunicationGroupPrivate InfCommunicaitonGroupPrivate;
struct _InfCommunicationGroupPrivate {
  InfCommunicationManager* communication_manager;
  gchar* name;

  InfCommunicationObject* target;

  GHashTable* methods;
};

enum {
  PROP_0,

  /* construct only */
  PROP_COMMUNICATION_MANAGER,
  PROP_NAME,

  PROP_TARGET
};

enum {
  ADD_MEMBER,
  REMOVE_MEMBER,

  LAST_SIGNAL
};

#define INF_COMMUNICATION_GROUP_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_SESSION, InfCommunicationGroupPrivate))

static GObjectClass* parent_class;
static guint session_signals[LAST_SIGNAL];

/* TODO: Can we strong-ref the communicationmanager? */

static InfCommunicationMethod*
inf_communication_group_lookup_method_for_network(InfCommunicationGroup* grp,
                                                  const gchar* network)
{
  InfCommunicationGroupPrivate* priv;
  InfCommunicationMethod* method;

  priv = INF_COMMUNICATION_GROUP_PRIVATE(grp);
  method = g_hash_table_lookup(priv->methods, network);

  return method;
}

static InfCommunicationMethod*
inf_communication_group_lookup_method_for_connection(InfCommunicationGroup* g,
                                                     InfXmlConnection* conn)
{
  InfCommunicationMethod* method;
  gchar* network;

  g_object_get(G_OBJECT(conn), "network", &network, NULL);

  method = inf_communication_group_lookup_method_for_network(g, network);
  g_free(network);

  return method;
}

static void
inf_communication_group_target_unrefed(gpointer data,
                                       GObject* where_the_object_was)
{
  InfCommunicationGroup* group;
  InfCommunicationGroupPrivate* priv;

  group = INF_COMMUNICATION_GROUP(data);
  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);

  g_warning(
    "The target of communication group \"%s\" was released before the group "
    "itself was released.",
    priv->name
  );

  priv->target = NULL;
  g_object_notify(G_OBJECT(group), "target");
}

static void
inf_communication_group_manager_unrefed(gpointer data,
                                        GObject* where_the_object_was)
{
  InfCommunicationGroup* group;
  InfCommunicationGroupPrivate* priv;

  group = INF_COMMUNICATION_GROUP(data);
  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);

  g_warning(
    "The communication manager of group \"%s\" was released before the group "
    "itself was released.",
    priv->name
  );

  priv->communication_manager = NULL;
  g_object_notify(G_OBJECT(group), "communication-manager");
}

static void
inf_communication_group_set_manager(InfCommunicationGroup* group,
                                    InfCommunicationManager* manager)
{
  InfCommunicationGroupPrivate* priv;
  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);

  if(priv->communication_manager != NULL)
  {
    g_object_weak_unref(
      G_OBJECT(priv->communication_manager),
      inf_communication_group_manager_unrefed,
      group
    );
  }

  priv->communicaiton_manager = manager;

  if(manager != NULL)
  {
    g_object_weak_ref(
      G_OBJECT(manager),
      inf_communication_group_manager_unrefed,
      group
    );
  }

  g_object_notify(G_OBJECT(group), "communication-manager");
}

/*
 * GObject overrides.
 */

static void
inf_communication_group_init(GTypeInstance* instance,
                             gpointer g_class)
{
  InfCommunicationGroup* group;
  InfCommunicationGroupPrivate* priv;

  group = INF_COMMUNICATION_GROUP(instance);
  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);

  priv->communication_manager = NULL;
  priv->name = NULL;

  priv->target = NULL;

  priv->methods =
    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
}

static void
inf_communication_group_dispose(GObject* object)
{
  InfCommunicationGroup* group;
  InfCommunicationGroupPrivate* priv;

  group = INF_COMMUNICATION_GROUP(object);
  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);

  if(priv->methods != NULL)
  {
    g_hash_table_unref(priv->methods);
    priv->methods = NULL;
  }

  inf_communication_group_set_manager(group, NULL);
  inf_communication_group_set_target(group, NULL);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_communication_group_finalize(GObject* object)
{
  InfCommunicationGroup* group;
  InfCommunicationGroupPrivate* priv;

  group = INF_COMMUNICATION_GROUP(object);
  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);

  g_free(priv->name);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_communication_group_set_property(GObject* object,
                                     guint prop_id,
                                     const GValue* value,
                                     GParamSpec* pspec)
{
  InfCommunicationGroup* group;
  InfCommunicationGroupPrivate* priv;

  group = INF_COMMUNICATION_GROUP(object);
  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);

  switch(prop_id)
  {
  case PROP_COMMUNICATION_MANAGER:
    g_assert(priv->communication_manager == NULL); /* construct only */

    inf_communication_group_set_manager(
      group,
      INF_COMMUNICATION_MANAGER(g_value_get_object(value))
    );

    break;
  case PROP_NAME:
    g_assert(priv->name == NULL); /* construct only */
    priv->name = g_value_dup_string(value);
    break;
  case PROP_TARGET:
    inf_communication_group_set_target(
      group,
      INF_COMMUNICATION_OBJECT(g_value_get_object(value))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_communication_group_get_property(GObject* object,
                                     guint prop_id,
                                     GValue* value,
                                     GParamSpec* pspec)
{
  InfCommunicationGroup* group;
  InfCommunicationGroupPrivate* priv;

  group = INF_COMMUNICATION_GROUP(object);
  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);

  switch(prop_id)
  {
  case PROP_COMMUNICATION_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->communication_manager));
    break;
  case PROP_NAME:
    g_value_set_string(value, priv->name);
    break;
  case PROP_TARGET:
    g_value_set_object(value, G_OBJECT(priv->target));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * GType registration.
 */

static void
inf_communication_group_class_init(gpointer g_class,
                                   gpointer class_data)
{
  GObjectClass* object_class;
  InfCommunicationGroupClass* group_class;

  object_class = G_OBJECT_CLASS(g_class);
  group_class = INF_COMMUNICATION_GROUP_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfCommunicationGroupPrivate));

  object_class->dispose = inf_communication_group_dispose;
  object_class->finalize = inf_communication_group_finalize;
  object_class->set_property = inf_communication_group_set_property;
  object_class->get_property = inf_communication_group_get_property;

  group_class->add_member = NULL;
  group_class->remove_member = NULL;

  g_object_class_install_property(
    object_class,
    PROP_COMMUNICATION_MANAGER,
    g_param_spec_object(
      "communication-manager",
      "Communication manager",
      "The communication manager used for sending requests",
      INF_COMMUNICATION_TYPE_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_NAME,
    g_param_spec_string(
      "name",
      "Name",
      "The name of the group",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_TARGET,
    g_param_spec_object(
      "target",
      "Target",
      "The communication object to call on received and sent data",
      INF_COMMUNICATION_TYPE_OBJECT,
      G_PARAM_READWRITE
    )
  );

  /**
   * InfCommunicationGroup::add-member:
   * @group: The #InfCommunicationGroup emitting the signal.
   * @connection: The newly joined connection.
   *
   * This signal is emitted when a connection has been added to the group.
   */
  session_signals[ADD_MEMBER] = g_signal_new(
    "add-member",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfCommunicationGroupClass, add_member),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XML_CONNECTION
  );

  /**
   * InfCommunicationGroup::remove-member:
   * @group: The #InfCommunicationGroup emitting the signal.
   * @connection: The connection that was removed
   *
   * This signal is emitted when a connection has been removed from the group.
   */
  session_signals[REMOVE_MEMBER] = g_signal_new(
    "add-member",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfCommunicationGroupClass, remove_member),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XML_CONNECTION
  );
}

GType
inf_communication_group_get_type(void)
{
  static GType group_type = 0;

  if(!group_type)
  {
    static const GTypeInfo group_type_info = {
      sizeof(InfCommunicationGroupClass),  /* class_size */
      NULL,                                /* base_init */
      NULL,                                /* base_finalize */
      inf_communication_group_class_init,  /* class_init */
      NULL,                                /* class_finalize */
      NULL,                                /* class_data */
      sizeof(InfCommunicationGroup),       /* instance_size */
      0,                                   /* n_preallocs */
      inf_communication_group_init,        /* instance_init */
      NULL                                 /* value_table */
    };

    group_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfCommunicationGroup",
      &group_type_info,
      G_TYPE_FLAG_ABSTRACT
    );
  }

  return group_type;
}

/*
 * Public API.
 */

/**
 * inf_communication_group_get_name:
 * @group: A #InfCommunicationGroup.
 *
 * Returns the name of the group.
 *
 * Returns: The name of the group. The returned string is owned by the group,
 * you don't need to free it.
 */
const gchar*
inf_communication_group_get_name(InfCommunicationGroup* group)
{
  g_return_val_if_fail(INF_COMMUNICATION_IS_GROUP(group), NULL);
  return INF_COMMUNICATION_GROUP_PRIVATE(group)->name;
}

/**
 * inf_communication_group_get_target:
 * @group: A #InfCommunicationGroup.
 *
 * Returns the group's target. The target of a group is the
 * #InfCommunicationObject to which received and sent messages are reported.
 *
 * Returns: A #InfCommunicationGroup, or %NULL.
 */
InfCommunicationObject*
inf_communication_group_get_target(InfCommunicationGroup* group)
{
  g_return_val_if_fail(INF_COMMUNICATION_IS_GROUP(group), NULL);
  return INF_COMMUNICATION_GROUP_PRIVATE(group)->target;
}

/**
 * inf_communication_group_set_target:
 * @group: A #InfCommunicationGroup.
 * @target: A #InfCommunicationObject, or %NULL.
 *
 * Sets the group's target. The target of a group is the
 * #InfCommunicationObject to which received and sent messages are reported.
 * If @target is %NULL, then the target will be unset.
 *
 * You can safely call this function with an object that holds a reference on
 * the group since the #InfCommunicationGroup only holds a weak reference to
 * its @target. This means that you need to keep a reference on @target
 * yourself.
 */
void
inf_communication_group_set_target(InfCommunicationGroup* group,
                                   InfCommunicationObject* object)
{
  InfCommunicationGroupPrivate* priv;

  g_return_if_fail(INF_COMMUNICATION_IS_GROUP(group));
  g_return_if_fail(object == NULL || INF_COMMUNICATION_IS_OBJECT(object));

  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);

  if(priv->target != object)
  {
    if(priv->target != NULL)
    {
      g_object_weak_unref(
        G_OBJECT(priv->target),
        inf_communication_group_target_unrefed,
        group
      );
    }

    priv->target = object;

    if(object != NULL)
    {
      g_object_weak_ref(
        G_OBJECT(object),
        inf_communication_group_target_unrefed,
        group
      );
    }

    g_object_notify(G_OBJECT(group), "target");
  }
}

/**
 * inf_communication_group_is_member:
 * @group: A #InfCommunicationGroup.
 * @connection: A #InfXmlConnection.
 *
 * Returns whether @connection is a member of @group.
 *
 * Returns: %TRUE if @connection is a member of @group, %FALSE otherwise.
 */
gboolean
inf_communication_group_is_member(InfCommunicationGroup* group,
                                  InfXmlConnection* connection)
{
  InfCommunicationGroupPrivate* priv;
  InfCommunicationMethod* method;

  g_return_val_if_fail(INF_COMMUNICATION_IS_GROUP(group), FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), FALSE);

  method = inf_communication_group_lookup_method_for_connection(
    group,
    connection
  );

  if(method != NULL)
    return inf_communication_method_is_member(method, connection);
  else
    return FALSE;
}

/**
 * inf_communication_group_send_message:
 * @group: A #InfCommunicationGroup.
 * @connection: The #InfXmlConnection to which to send the message.
 * @xml: The message to send.
 *
 * Sends a message @connection which must be a member of @group. @connection
 * needs to be a member of this group. This function takes ownership of @xml.
 */
void
inf_communication_group_send_message(InfCommunicationGroup* group,
                                     InfXmlConnection* connection,
                                     xmlNodePtr xml)
{
  InfCommunicationMethod* method;

  g_return_if_fail(INF_COMMUNICATION_IS_GROUP(group));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  method = inf_communication_group_lookup_method_for_connection(
    group,
    connection
  );

  g_return_if_fail(method != NULL);

  inf_communication_method_send_single(method, connection, xml);
}

/**
 * inf_communication_group_send_group_message:
 * @group: A #InfCommunicationGroup.
 * @except: A #InfXmlConnection not to send the message to, or %NULL.
 * @xml: The message to send.
 *
 * Sends a message to all members of @group, except @except. This function
 * takes ownership of @xml.
 */
void
inf_communication_group_send_group_message(InfCommunicationGroup* group,
                                           InfXmlConnection* except,
                                           xmlNodePtr xml)
{
  InfCommunicationGroupPrivate* priv;
  GHashTableIter iter;
  gpointer value;
  InfCommunicationMethod* method;
  gboolean has_next;

  g_return_if_fail(INF_COMMUNICATION_IS_GROUP(group));
  g_return_if_fail(except == NULL || INF_IS_XML_CONNECTION(except));
  g_return_if_fail(xml != NULL);

  priv = INF_COMMUNICATION_GROUP_PRIVATE(group);
  g_hash_table_iter_init(priv->methods, &iter);

  has_next = g_hash_table_iter_next(&iter, NULL, &value);

  if(!has_next)
  {
    xmlFreeNode(xml);
  }
  else
  {
    do
    {
      method = INF_COMMUNICATION_METHOD(value);
      has_next = g_hash_table_iter_next(&iter, NULL, &value);

      inf_communication_method_send_all(
        method,
        except,
        has_next ? xmlCopyNode(xml, 1) : xml
      );
    } while(has_next);
  }
}

/**
 * inf_communication_group_cancel_messages:
 * @group: A #InfCommunicationGroup.
 * @connection: The #InfXmlConnection for which to cancel messages.
 *
 * Stops all messages scheduled to be sent to @connection from being sent.
 * Messages for which inf_communication_object_enqueued() has already been
 * called cannot be cancelled anymore.
 */
void
inf_communication_group_cancel_messages(InfCommunicationGroup* group,
                                        InfXmlConnection* connection)
{
  InfCommunicationMethod* method;

  g_return_if_fail(INF_COMMUNICATION_IS_GROUP(group));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  method = inf_communication_group_lookup_method_for_connection(
    group,
    connection
  );

  g_return_if_fail(method != NULL);
  inf_communication_method_cancel_messages(method, connection);
}

/**
 * inf_communication_group_get_method_for_network:
 * @group: A #InfCommunicationGroup.
 * @network: A network specifier, such as "tcp/ip" or "jabber".
 *
 * Returns the method name of the method used for communication on @network
 * within @group.
 *
 * Returns: A method name. The string is owned by the group, you don't need
 * to free it.
 */

/**
 * inf_communication_group_get_method_for_connection:
 * @group: A #InfCommunicationGroup.
 * @conn: The #InfXmlConnection for which to retrieve the method.
 *
 * Returns the method name of the method used for communication on @conn's
 * network within @group.
 *
 * Returns: A method name. The string is owned by the group, you don't need
 * to free it.
 */

/* vim:set et sw=2 ts=2: */
