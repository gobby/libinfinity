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
 * SECTION:inf-communication-joined-group
 * @title: InfCommunicationJoinedGroup
 * @short_description: Communication group opened by a remote host
 * @include: libinfinity/communication/inf-communication-joined-group.h
 * @stability: Unstable
 * @see_also: #InfCommunicationGroup, #InfCommunicationManager
 *
 * #InfCommunicationJoinedGroup represents membership of the local host on a
 * #InfCommunicationGroup opened on a remote host.
 *
 * There is no API to add a member to a joined group. This is because new
 * members can only join via the group's publisher. It is the job of the
 * #InfCommunicationMethod to tell the joined group about the new member in
 * which case, the #InfCommunicationGroup::member-added signal will be
 * emitted.
 **/

#include <libinfinity/communication/inf-communication-joined-group.h>
#include <libinfinity/communication/inf-communication-group-private.h>

typedef struct _InfCommunicationJoinedGroupPrivate
  InfCommunicationJoinedGroupPrivate;
struct _InfCommunicationJoinedGroupPrivate {
  InfXmlConnection* publisher_conn;
  gchar* publisher_id;
  gchar* method;
};

enum {
  PROP_0,

  PROP_PUBLISHER,
  PROP_METHOD
};

#define INF_COMMUNICATION_JOINED_GROUP_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_COMMUNICATION_TYPE_JOINED_GROUP, InfCommunicationJoinedGroupPrivate))

static GObjectClass* parent_class;

/* Required by inf_communication_joined_group_publisher_notify_status_cb() */
static void
inf_communication_joined_group_set_publisher(InfCommunicationJoinedGroup* grp,
                                             InfXmlConnection* connection);

static void
inf_communication_joined_group_publisher_notify_status_cb(GObject* object,
                                                          GParamSpec* pspec,
                                                          gpointer user_data)
{
  InfXmlConnectionStatus status;
  g_object_get(G_OBJECT(object), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSING ||
     status == INF_XML_CONNECTION_CLOSED)
  {
    /* Don't remove from group, the method will do this by itself */
    inf_communication_joined_group_set_publisher(
      INF_COMMUNICATION_JOINED_GROUP(user_data),
      NULL
    );
  }
}

static void
inf_communication_joined_group_set_publisher(InfCommunicationJoinedGroup* grp,
                                             InfXmlConnection* connection)
{
  InfCommunicationJoinedGroupPrivate* priv;
  priv = INF_COMMUNICATION_JOINED_GROUP_PRIVATE(grp);

  if(priv->publisher_conn != NULL)
  {
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->publisher_conn),
      G_CALLBACK(inf_communication_joined_group_publisher_notify_status_cb),
      grp
    );

    g_object_unref(priv->publisher_conn);
  }

  priv->publisher_conn = connection;

  if(connection != NULL)
  {
    g_object_ref(connection);

    g_signal_connect(
      G_OBJECT(connection),
      "notify::status",
      G_CALLBACK(inf_communication_joined_group_publisher_notify_status_cb),
      grp
    );
  }

  g_object_notify(G_OBJECT(grp), "publisher");
}

static const gchar*
inf_communication_joined_group_get_method(InfCommunicationGroup* group,
                                          unsigned int index)
{
  InfCommunicationJoinedGroup* joined_group;
  InfCommunicationJoinedGroupPrivate* priv;

  joined_group = INF_COMMUNICATION_JOINED_GROUP(group);
  priv = INF_COMMUNICATION_JOINED_GROUP_PRIVATE(joined_group);

  if(index == 0)
    return priv->method;
  return NULL;
}

static gchar*
inf_communication_joined_group_get_publisher_id(InfCommunicationGroup* group,
                                                InfXmlConnection* for_conn)
{
  InfCommunicationJoinedGroup* joined_group;
  InfCommunicationJoinedGroupPrivate* priv;

  joined_group = INF_COMMUNICATION_JOINED_GROUP(group);
  priv = INF_COMMUNICATION_JOINED_GROUP_PRIVATE(joined_group);

  return g_strdup(priv->publisher_id);
}

/*
 * GObject overrides.
 */

static void
inf_communication_joined_group_init(GTypeInstance* instance,
                                    gpointer g_class)
{
  InfCommunicationJoinedGroup* group;
  InfCommunicationJoinedGroupPrivate* priv;

  group = INF_COMMUNICATION_JOINED_GROUP(instance);
  priv = INF_COMMUNICATION_JOINED_GROUP_PRIVATE(group);

  priv->publisher_conn = NULL;
  priv->publisher_id = NULL;
  priv->method = NULL;
}

static GObject*
inf_communication_joined_group_constructor(GType type,
                                           guint n_construct_properties,
                                           GObjectConstructParam* properties)
{
  GObject* object;
  InfCommunicationJoinedGroupPrivate* priv;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    properties
  );

  priv = INF_COMMUNICATION_JOINED_GROUP_PRIVATE(object);
  g_assert(priv->publisher_conn != NULL);
  g_assert(priv->method != NULL);

  g_object_get(
    G_OBJECT(priv->publisher_conn),
    "remote-id",
    &priv->publisher_id,
    NULL
  );

  /* method on publisher_conn's network must be supported, otherwise this
   * call will fail. */
  _inf_communication_group_add_member(
    INF_COMMUNICATION_GROUP(object),
    priv->publisher_conn
  );

  return object;
}

static void
inf_communication_joined_group_dispose(GObject* object)
{
  InfCommunicationJoinedGroup* group;
  InfCommunicationJoinedGroupPrivate* priv;
  
  group = INF_COMMUNICATION_JOINED_GROUP(object);
  priv = INF_COMMUNICATION_JOINED_GROUP_PRIVATE(group);

  inf_communication_joined_group_set_publisher(group, NULL);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_communication_joined_group_finalize(GObject* object)
{
  InfCommunicationJoinedGroup* group;
  InfCommunicationJoinedGroupPrivate* priv;

  group = INF_COMMUNICATION_JOINED_GROUP(object);
  priv = INF_COMMUNICATION_JOINED_GROUP_PRIVATE(group);

  g_free(priv->publisher_id);
  g_free(priv->method);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_communication_joined_group_set_property(GObject* object,
                                            guint prop_id,
                                            const GValue* value,
                                            GParamSpec* pspec)
{
  InfCommunicationJoinedGroup* group;
  InfCommunicationJoinedGroupPrivate* priv;

  group = INF_COMMUNICATION_JOINED_GROUP(object);
  priv = INF_COMMUNICATION_JOINED_GROUP_PRIVATE(group);

  switch(prop_id)
  {
  case PROP_PUBLISHER:
    g_assert(priv->publisher_conn == NULL); /* construct only */

    inf_communication_joined_group_set_publisher(
      group,
      INF_XML_CONNECTION(g_value_get_object(value))
    );

    break;
  case PROP_METHOD:
    g_assert(priv->method == NULL); /* construct only */
    priv->method = g_value_dup_string(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_communication_joined_group_get_property(GObject* object,
                                            guint prop_id,
                                            GValue* value,
                                            GParamSpec* pspec)
{
  InfCommunicationJoinedGroup* group;
  InfCommunicationJoinedGroupPrivate* priv;

  group = INF_COMMUNICATION_JOINED_GROUP(object);
  priv = INF_COMMUNICATION_JOINED_GROUP_PRIVATE(group);

  switch(prop_id)
  {
  case PROP_PUBLISHER:
    g_value_set_object(value, priv->publisher_conn);
    break;
  case PROP_METHOD:
    g_value_set_string(value, priv->method);
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
inf_communication_joined_group_class_init(gpointer g_class,
                                          gpointer class_data)
{
  GObjectClass* object_class;
  InfCommunicationGroupClass* group_class;

  object_class = G_OBJECT_CLASS(g_class);
  group_class = INF_COMMUNICATION_GROUP_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));

  g_type_class_add_private(
    g_class,
    sizeof(InfCommunicationJoinedGroupPrivate)
  );

  object_class->constructor = inf_communication_joined_group_constructor;
  object_class->dispose = inf_communication_joined_group_dispose;
  object_class->finalize = inf_communication_joined_group_finalize;
  object_class->set_property = inf_communication_joined_group_set_property;
  object_class->get_property = inf_communication_joined_group_get_property;

  group_class->get_method = inf_communication_joined_group_get_method;
  group_class->get_publisher_id =
    inf_communication_joined_group_get_publisher_id;

  g_object_class_install_property(
    object_class,
    PROP_PUBLISHER,
    g_param_spec_object(
      "publisher",
      "Publisher",
      "A connection to the group's publisher",
      INF_TYPE_XML_CONNECTION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_METHOD,
    g_param_spec_string(
      "method",
      "Method",
      "The communication method to use for this group",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

GType
inf_communication_joined_group_get_type(void)
{
  static GType joined_group_type = 0;

  if(!joined_group_type)
  {
    static const GTypeInfo joined_group_type_info = {
      sizeof(InfCommunicationJoinedGroupClass),   /* class_size */
      NULL,                                       /* base_init */
      NULL,                                       /* base_finalize */
      inf_communication_joined_group_class_init,  /* class_init */
      NULL,                                       /* class_finalize */
      NULL,                                       /* class_data */
      sizeof(InfCommunicationJoinedGroup),        /* instance_size */
      0,                                          /* n_preallocs */
      inf_communication_joined_group_init,        /* instance_init */
      NULL                                        /* value_table */
    };

    joined_group_type = g_type_register_static(
      INF_COMMUNICATION_TYPE_GROUP,
      "InfCommunicationJoinedGroup",
      &joined_group_type_info,
      0
    );
  }

  return joined_group_type;
}

/*
 * Public API.
 */

/**
 * inf_communication_joined_group_remove_member:
 * @grp: A #InfCommunicationJoinedGroup.
 * @connection: A connection to a member of @grp.
 *
 * Removes @connection as @grp's member. On the remote site, the
 * corresponding #InfCommunicationGroup needs to be freed (which may be a
 * #InfCommunicationJoinedGroup or a #InfCommunicationHostedGroup).
 */
void
inf_communication_joined_group_remove_member(InfCommunicationJoinedGroup* grp,
                                             InfXmlConnection* connection)
{
  g_return_if_fail(INF_COMMUNICATION_IS_JOINED_GROUP(grp));
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

/**
 * inf_communication_joined_group_get_publisher:
 * @g: A #InfCommunicationJoinedGroup.
 *
 * Returns a #InfXmlConnection to the group's publisher, or %NULL if the
 * publisher is no longer a group member.
 *
 * Returns: A #InfXmlConnection, or %NULL.
 */
InfXmlConnection*
inf_communication_joined_group_get_publisher(InfCommunicationJoinedGroup* g)
{
  g_return_val_if_fail(INF_COMMUNICATION_IS_JOINED_GROUP(g), NULL);
  return INF_COMMUNICATION_JOINED_GROUP_PRIVATE(g)->publisher_conn;
}
