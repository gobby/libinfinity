/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-communication-hosted-group
 * @title: InfCommunicationHostedGroup
 * @short_description: Communication group opened by the local host
 * @include: libinfinity/communication/inf-communication-hosted-group.h
 * @stability: Unstable
 * @see_also: #InfCommunicationGroup, #InfCommunicationManager
 *
 * #InfCommunicationHostedGroup is a #InfCommunicationHostedGroup opened on
 * the local host. It allows adding other hosts to the group via
 * inf_communication_hosted_group_add_member(), and to remove hosts via
 * inf_communication_hosted_group_remove_member().
 **/

#include <libinfinity/communication/inf-communication-hosted-group.h>
#include <libinfinity/communication/inf-communication-group-private.h>

typedef struct _InfCommunicationHostedGroupPrivate
  InfCommunicationHostedGroupPrivate;
struct _InfCommunicationHostedGroupPrivate {
  GPtrArray* methods;
};

#define INF_COMMUNICATION_HOSTED_GROUP_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_COMMUNICATION_TYPE_HOSTED_GROUP, InfCommunicationHostedGroupPrivate))

static GObjectClass* parent_class;

static const gchar*
inf_communication_hosted_group_get_method(InfCommunicationGroup* group,
                                          unsigned int index)
{
  InfCommunicationHostedGroup* hosted_group;
  InfCommunicationHostedGroupPrivate* priv;

  hosted_group = INF_COMMUNICATION_HOSTED_GROUP(group);
  priv = INF_COMMUNICATION_HOSTED_GROUP_PRIVATE(hosted_group);

  if(index < priv->methods->len)
    return g_ptr_array_index(priv->methods, index);

  /* fallback to central method */
  if(index == priv->methods->len)
    return "central";

  return NULL;
}

static gchar*
inf_communication_hosted_group_get_publisher_id(InfCommunicationGroup* group,
                                                InfXmlConnection* for_conn)
{
  gchar* local_id;
  g_object_get(G_OBJECT(for_conn), "local-id", &local_id, NULL);
  return local_id;
}

/*
 * GObject overrides.
 */

static void
inf_communication_hosted_group_init(GTypeInstance* instance,
                                    gpointer g_class)
{
  InfCommunicationHostedGroup* group;
  InfCommunicationHostedGroupPrivate* priv;

  group = INF_COMMUNICATION_HOSTED_GROUP(instance);
  priv = INF_COMMUNICATION_HOSTED_GROUP_PRIVATE(group);

  priv->methods = g_ptr_array_new();
}

static void
inf_communication_hosted_group_finalize(GObject* object)
{
  InfCommunicationHostedGroup* group;
  InfCommunicationHostedGroupPrivate* priv;
  guint i;

  group = INF_COMMUNICATION_HOSTED_GROUP(object);
  priv = INF_COMMUNICATION_HOSTED_GROUP_PRIVATE(group);

  for(i = 0; i < priv->methods->len; ++ i)
    g_free(g_ptr_array_index(priv->methods, i));
  g_ptr_array_free(priv->methods, TRUE);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

/*
 * GType registration.
 */

static void
inf_communication_hosted_group_class_init(gpointer g_class,
                                          gpointer class_data)
{
  GObjectClass* object_class;
  InfCommunicationGroupClass* group_class;

  object_class = G_OBJECT_CLASS(g_class);
  group_class = INF_COMMUNICATION_GROUP_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));

  g_type_class_add_private(
    g_class,
    sizeof(InfCommunicationHostedGroupPrivate)
  );

  object_class->finalize = inf_communication_hosted_group_finalize;

  group_class->get_method = inf_communication_hosted_group_get_method;
  group_class->get_publisher_id =
    inf_communication_hosted_group_get_publisher_id;
}

GType
inf_communication_hosted_group_get_type(void)
{
  static GType hosted_group_type = 0;

  if(!hosted_group_type)
  {
    static const GTypeInfo hosted_group_type_info = {
      sizeof(InfCommunicationHostedGroupClass),   /* class_size */
      NULL,                                       /* base_init */
      NULL,                                       /* base_finalize */
      inf_communication_hosted_group_class_init,  /* class_init */
      NULL,                                       /* class_finalize */
      NULL,                                       /* class_data */
      sizeof(InfCommunicationHostedGroup),        /* instance_size */
      0,                                          /* n_preallocs */
      inf_communication_hosted_group_init,        /* instance_init */
      NULL                                        /* value_table */
    };

    hosted_group_type = g_type_register_static(
      INF_COMMUNICATION_TYPE_GROUP,
      "InfCommunicationHostedGroup",
      &hosted_group_type_info,
      0
    );
  }

  return hosted_group_type;
}

/*
 * Public API.
 */

/**
 * inf_communication_hosted_group_add_method:
 * @group: A #InfCommunicationHostedGroup.
 * @method: The method name to add.
 *
 * Adds a method to the hosted group. When a connection from a given network
 * is added to the group the first time, a #InfCommunicationMethod is
 * instantiated to handle messaging for the group within this network.
 * The first method added will be tried first. If the communication manager
 * does support it (meaning inf_communication_manager_get_factory_for() for
 * the connection's network and the chosen method returns non-%NULL), then it
 * will be used, otherwise the next method will be tried, etc. If no method
 * is supported, or no methods are added to the group, then the "central"
 * method will be used as a fallback.
 */
void
inf_communication_hosted_group_add_method(InfCommunicationHostedGroup* group,
                                          const gchar* method)
{
  InfCommunicationHostedGroupPrivate* priv;

  g_return_if_fail(INF_COMMUNICATION_IS_HOSTED_GROUP(group));
  g_return_if_fail(method != NULL);

  priv = INF_COMMUNICATION_HOSTED_GROUP_PRIVATE(group);
  g_ptr_array_add(priv->methods, g_strdup(method));
}

/**
 * inf_communication_hosted_group_add_member:
 * @group: A #InfCommunicationGroup.
 * @connection: A #InfXmlConnection to add to group.
 *
 * Adds @connection as a member to @group. On the remote site, a
 * #InfCommunicationJoinedGroup with the same name and method used for
 * @connection (see inf_communication_group_get_method_for_connection())
 * needs to be created for successful communication.
 */
void
inf_communication_hosted_group_add_member(InfCommunicationHostedGroup* group,
                                          InfXmlConnection* connection)
{
  g_return_if_fail(INF_COMMUNICATION_IS_HOSTED_GROUP(group));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_return_if_fail(
    !inf_communication_group_is_member(
      INF_COMMUNICATION_GROUP(group),
      connection
    )
  );

  _inf_communication_group_add_member(
    INF_COMMUNICATION_GROUP(group),
    connection
  );
}

/**
 * inf_communication_hosted_group_remove_member:
 * @grp: A #InfCommunicationGroup.
 * @connection: The @InfXmlConnection to remove from the group.
 *
 * Removes @connection's membership from @group. On the remote site, the
 * corresponding #InfCommunicationJoinedGroup needs to be freed.
 */
void
inf_communication_hosted_group_remove_member(InfCommunicationHostedGroup* grp,
                                             InfXmlConnection* connection)
{
  g_return_if_fail(INF_COMMUNICATION_IS_HOSTED_GROUP(grp));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_return_if_fail(
    inf_communication_group_is_member(
      INF_COMMUNICATION_GROUP(grp),
      connection
    )
  );

  _inf_communication_group_remove_member(
    INF_COMMUNICATION_GROUP(grp),
    connection
  );
}

/* vim:set et sw=2 ts=2: */
