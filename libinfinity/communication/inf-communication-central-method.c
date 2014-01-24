/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-communication-central-method
 * @title: InfCommunicationCentralMethod
 * @short_description: Relying group messages via the publisher
 * @include: libinfinity/communication/inf-communication-central-method.h
 * @stability: Unstable
 *
 * #InfCommunicationCentralMethod implements #InfCommunicationMethod by
 * relaying all messages via the group's publisher. If the connection to the
 * publisher is lost, so is the connection to all other group members.
 **/

#include <libinfinity/communication/inf-communication-central-method.h>
#include <libinfinity/communication/inf-communication-hosted-group.h>
#include <libinfinity/communication/inf-communication-registry.h>
#include <libinfinity/inf-signals.h>

typedef struct _InfCommunicationCentralMethodPrivate
  InfCommunicationCentralMethodPrivate;
struct _InfCommunicationCentralMethodPrivate {
  InfCommunicationRegistry* registry;
  InfCommunicationGroup* group;
  gboolean is_publisher; /* Whether the local host is publisher of group */

  GSList* connections;
};

enum {
  PROP_0,

  /* construct only */
  PROP_REGISTRY,
  PROP_GROUP
};

#define INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_COMMUNICATION_TYPE_CENTRAL_METHOD, InfCommunicationCentralMethodPrivate))

static GObjectClass* parent_class;

static void
inf_communication_central_method_notify_status_cb(GObject* object,
                                                  GParamSpec* pspec,
                                                  gpointer user_data)
{
  InfCommunicationCentralMethod* method;
  InfCommunicationCentralMethodPrivate* priv;
  InfXmlConnectionStatus status;

  method = INF_COMMUNICATION_CENTRAL_METHOD(user_data);
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);
  g_object_get(object, "status", &status, NULL);

  switch(status)
  {
  case INF_XML_CONNECTION_CLOSED:
  case INF_XML_CONNECTION_CLOSING:
    g_object_ref(priv->group);

    inf_communication_method_remove_member(
      INF_COMMUNICATION_METHOD(method),
      INF_XML_CONNECTION(object)
    );

    g_object_unref(priv->group);
    break;
  case INF_XML_CONNECTION_OPENING:
    break;
  case INF_XML_CONNECTION_OPEN:
    inf_communication_registry_register(
      priv->registry,
      priv->group,
      INF_COMMUNICATION_METHOD(method),
      INF_XML_CONNECTION(object)
    );

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_communication_central_method_add_member(InfCommunicationMethod* method,
                                            InfXmlConnection* connection)
{
  InfCommunicationCentralMethodPrivate* priv;
  InfXmlConnectionStatus status;

  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);
  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  g_assert(status != INF_XML_CONNECTION_CLOSING && 
           status != INF_XML_CONNECTION_CLOSED);

  priv->connections = g_slist_prepend(priv->connections, connection);

  g_signal_connect(
    connection,
    "notify::status",
    G_CALLBACK(inf_communication_central_method_notify_status_cb),
    method
  );

  if(status == INF_XML_CONNECTION_OPEN)
  {
    inf_communication_registry_register(
      priv->registry,
      priv->group,
      method,
      connection
    );
  }
}

static void
inf_communication_central_method_remove_member(InfCommunicationMethod* method,
                                               InfXmlConnection* connection)
{
  InfCommunicationCentralMethodPrivate* priv;
  InfXmlConnectionStatus status;
  gboolean is_registered;

  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  is_registered = inf_communication_registry_is_registered(
    priv->registry,
    priv->group,
    connection
  );

  /* The connection might not be registered if it never was in
   * INF_XML_CONNECTION_OPEN status, but still is in
   * INF_XML_CONNECTION_OPENING, or changed from OPENING directly
   * to CLOSING or CLOSED. */
  if(is_registered)
  {
    inf_communication_registry_unregister(
      priv->registry,
      priv->group,
      connection
    );
  }

  inf_signal_handlers_disconnect_by_func(
    connection,
    G_CALLBACK(inf_communication_central_method_notify_status_cb),
    method
  );

  priv->connections = g_slist_remove(priv->connections, connection);
}

static gboolean
inf_communication_central_method_is_member(InfCommunicationMethod* method,
                                           InfXmlConnection* connection)
{
  InfCommunicationCentralMethodPrivate* priv;
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  if(g_slist_find(priv->connections, connection) == NULL)
    return FALSE;
  return TRUE;
}

static void
inf_communication_central_method_send_single(InfCommunicationMethod* method,
                                             InfXmlConnection* connection,
                                             xmlNodePtr xml)
{
  InfCommunicationCentralMethodPrivate* priv;
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  inf_communication_registry_send(
    priv->registry,
    priv->group,
    connection,
    xml
  );
}

static void
inf_communication_central_method_send_all(InfCommunicationMethod* method,
                                          xmlNodePtr xml)
{
  InfCommunicationCentralMethodPrivate* priv;
  InfCommunicationRegistry* registry;
  InfCommunicationGroup* group;
  GSList* connections;
  GSList* item;
  InfXmlConnection* connection;
  gboolean is_registered;

  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  /* Each of the inf_communication_registry_send() calls can do a callback
   * which might possibly screw up our connection list completely. So be safe
   * here by copying all relevant information on the stack. */
  g_object_ref(method);
  registry = g_object_ref(priv->registry);
  group = g_object_ref(priv->group);

  connections = g_slist_copy(priv->connections);
  for(item = connections; item != NULL; item = item->next)
    g_object_ref(item->data);

  while(connections)
  {
    connection = INF_XML_CONNECTION(connections->data);

    /* A callback from a prior iteration might have unregistered the
     * connection. */
    is_registered = inf_communication_registry_is_registered(
      registry,
      group,
      connection
    );

    if(is_registered)
    {
      if(connections->next != NULL)
      {
        /* Keep ownership of XML if there might be more connections we should
         * send it to. */
        inf_communication_registry_send(
          registry,
          group,
          connection,
          xmlCopyNode(xml, 1)
        );
      }
      else
      {
        /* Pass ownership of XML if this is definitely the last connection
         * in the list. */
        inf_communication_registry_send(registry, group, connection, xml);
        xml = NULL;
      }
    }

    g_object_unref(connection);
    connections = g_slist_delete_link(connections, connections);
  }

  g_object_unref(method);
  g_object_unref(registry);
  g_object_unref(group);

  if(xml != NULL)
    xmlFreeNode(xml);
}

static void
inf_communication_central_method_cancel_messages(InfCommunicationMethod* meth,
                                                 InfXmlConnection* connection)
{
  InfCommunicationCentralMethodPrivate* priv;
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(meth);

  inf_communication_registry_cancel_messages(
    priv->registry,
    priv->group,
    connection
  );
}

static InfCommunicationScope
inf_communication_central_method_received(InfCommunicationMethod* method,
                                          InfXmlConnection* connection,
                                          xmlNodePtr xml)
{
  InfCommunicationCentralMethodPrivate* priv;
  InfCommunicationObject* target;
  InfCommunicationGroup* group;
  InfCommunicationScope scope;
  gchar* remote_id;
  gchar* publisher_id;
  GSList* item;

  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);
  target = inf_communication_group_get_target(priv->group);
  group = priv->group;

  if(target != NULL)
  {
    /* Make sure we stay alive, so we can check our connection list and
     * publisher flag after the callback. This means we also need to keep
     * group and target alive, to ensure the correct deinitialization order */
    g_object_ref(target);
    g_object_ref(group);
    g_object_ref(method);

    scope = inf_communication_object_received(
      target,
      connection,
      xml
    );

    if(priv->is_publisher && scope == INF_COMMUNICATION_SCOPE_GROUP)
    {
      for(item = priv->connections; item != NULL; item = item->next)
      {
        if(item->data != connection)
        {
          inf_communication_registry_send(
            priv->registry,
            priv->group,
            INF_XML_CONNECTION(item->data),
            xmlCopyNode(xml, 1)
          );
        }
      }
    }

    g_object_unref(method);
    g_object_unref(group);
    g_object_unref(target);

    return scope;
  }
  else
  {
    /* Without target we don't know what to do with the message, so better
     * don't relay. */
    return INF_COMMUNICATION_SCOPE_PTP;
  }
}

static void
inf_communication_central_method_enqueued(InfCommunicationMethod* method,
                                          InfXmlConnection* connection,
                                          xmlNodePtr xml)
{
  InfCommunicationCentralMethodPrivate* priv;
  InfCommunicationObject* target;

  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);
  target = inf_communication_group_get_target(priv->group);

  if(target != NULL)
    inf_communication_object_enqueued(target, connection, xml);
}

static void
inf_communication_central_method_sent(InfCommunicationMethod* method,
                                      InfXmlConnection* connection,
                                      xmlNodePtr xml)
{
  InfCommunicationCentralMethodPrivate* priv;
  InfCommunicationObject* target;

  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);
  target = inf_communication_group_get_target(priv->group);

  if(target != NULL)
    inf_communication_object_sent(target, connection, xml);
}

/* Weakref handling. Both group and registry should never be unrefed before
 * the method is unrefed, but this can help debugging in case one of
 * them is. */

static void
inf_communication_central_method_registry_unrefed(gpointer data,
                                                  GObject* object)
{
  InfCommunicationCentralMethod* method;
  InfCommunicationCentralMethodPrivate* priv;

  method = INF_COMMUNICATION_CENTRAL_METHOD(data);
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  g_warning("A method's registry was unrefed before the method was unrefed");
  priv->registry = NULL;
}

static void
inf_communication_central_method_group_unrefed(gpointer data,
                                               GObject* where_the_object_was)
{
  InfCommunicationCentralMethod* method;
  InfCommunicationCentralMethodPrivate* priv;

  method = INF_COMMUNICATION_CENTRAL_METHOD(data);
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  g_warning("A method's group was unrefed before the method was unrefed");
  priv->group = NULL;
}

static void
inf_communication_central_method_set_registry(InfCommunicationCentralMethod* m,
                                              InfCommunicationRegistry* reg)
{
  InfCommunicationCentralMethodPrivate* priv;
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(m);

  if(priv->registry != NULL)
  {
    g_object_weak_unref(
      G_OBJECT(priv->registry),
      inf_communication_central_method_registry_unrefed,
      m
    );
  }

  priv->registry = reg;

  if(reg != NULL)
  {
    g_object_weak_ref(
      G_OBJECT(reg),
      inf_communication_central_method_registry_unrefed,
      m
    );
  }

  g_object_notify(G_OBJECT(m), "registry");
}

static void
inf_communication_central_method_set_group(InfCommunicationCentralMethod* m,
                                           InfCommunicationGroup* group)
{
  InfCommunicationCentralMethodPrivate* priv;
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(m);

  if(priv->group != NULL)
  {
    g_object_weak_unref(
      G_OBJECT(priv->group),
      inf_communication_central_method_group_unrefed,
      m
    );

    priv->is_publisher = FALSE;
  }

  priv->group = group;

  if(group != NULL)
  {
    g_object_weak_ref(
      G_OBJECT(group),
      inf_communication_central_method_group_unrefed,
      m
    );

    if(INF_COMMUNICATION_IS_HOSTED_GROUP(group))
      priv->is_publisher = TRUE;
    else
      priv->is_publisher = FALSE;
  }

  g_object_notify(G_OBJECT(m), "group");
}

/*
 * GObject overrides.
 */

static void
inf_communication_central_method_init(GTypeInstance* instance,
                                      gpointer g_class)
{
  InfCommunicationCentralMethod* method;
  InfCommunicationCentralMethodPrivate* priv;

  method = INF_COMMUNICATION_CENTRAL_METHOD(instance);
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  priv->group = NULL;
  priv->registry = NULL;
  priv->is_publisher = FALSE;
  priv->connections = NULL;
}

static void
inf_communication_central_method_dispose(GObject* object)
{
  InfCommunicationCentralMethod* method;
  InfCommunicationCentralMethodPrivate* priv;

  method = INF_COMMUNICATION_CENTRAL_METHOD(object);
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  while(priv->connections != NULL)
  {
    inf_communication_method_remove_member(
      INF_COMMUNICATION_METHOD(method),
      INF_XML_CONNECTION(priv->connections->data)
    );
  }

  inf_communication_central_method_set_group(method, NULL);
  inf_communication_central_method_set_registry(method, NULL);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_communication_central_method_set_property(GObject* object,
                                              guint prop_id,
                                              const GValue* value,
                                              GParamSpec* pspec)
{
  InfCommunicationCentralMethod* method;
  InfCommunicationCentralMethodPrivate* priv;

  method = INF_COMMUNICATION_CENTRAL_METHOD(object);
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  switch(prop_id)
  {
  case PROP_REGISTRY:
    g_assert(priv->registry == NULL); /* construct only */

    inf_communication_central_method_set_registry(
      method,
      INF_COMMUNICATION_REGISTRY(g_value_get_object(value))
    );

    break;
  case PROP_GROUP:
    g_assert(priv->group == NULL); /* construct only */

    inf_communication_central_method_set_group(
      method,
      INF_COMMUNICATION_GROUP(g_value_get_object(value))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_communication_central_method_get_property(GObject* object,
                                              guint prop_id,
                                              GValue* value,
                                              GParamSpec* pspec)
{
  InfCommunicationCentralMethod* method;
  InfCommunicationCentralMethodPrivate* priv;

  method = INF_COMMUNICATION_CENTRAL_METHOD(object);
  priv = INF_COMMUNICATION_CENTRAL_METHOD_PRIVATE(method);

  switch(prop_id)
  {
  case PROP_REGISTRY:
    g_value_set_object(value, priv->registry);
    break;
  case PROP_GROUP:
    g_value_set_object(value, priv->group);
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
inf_communication_central_method_class_init(gpointer g_class,
                                            gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(
    g_class,
    sizeof(InfCommunicationCentralMethodPrivate)
  );

  object_class->dispose = inf_communication_central_method_dispose;
  object_class->set_property = inf_communication_central_method_set_property;
  object_class->get_property = inf_communication_central_method_get_property;

  g_object_class_install_property(
    object_class,
    PROP_REGISTRY,
    g_param_spec_object(
      "registry",
      "Registry",
      "The communication registry to register connections with",
      INF_COMMUNICATION_TYPE_REGISTRY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_GROUP,
    g_param_spec_object(
      "group",
      "Group",
      "The communication group for which to handle messages",
      INF_COMMUNICATION_TYPE_GROUP,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
inf_communication_central_method_method_init(gpointer g_iface,
                                             gpointer iface_data)
{
  InfCommunicationMethodIface* iface;
  iface = (InfCommunicationMethodIface*)g_iface;

  iface->add_member = inf_communication_central_method_add_member;
  iface->remove_member = inf_communication_central_method_remove_member;
  iface->is_member = inf_communication_central_method_is_member;
  iface->send_single = inf_communication_central_method_send_single;
  iface->send_all = inf_communication_central_method_send_all;
  iface->cancel_messages = inf_communication_central_method_cancel_messages;
  iface->received = inf_communication_central_method_received;
  iface->enqueued = inf_communication_central_method_enqueued;
  iface->sent = inf_communication_central_method_sent;
}

GType
inf_communication_central_method_get_type(void)
{
  static GType central_method_type = 0;

  if(!central_method_type)
  {
    static const GTypeInfo central_method_type_info = {
      sizeof(InfCommunicationCentralMethodClass),   /* class_size */
      NULL,                                         /* base_init */
      NULL,                                         /* base_finalize */
      inf_communication_central_method_class_init,  /* class_init */
      NULL,                                         /* class_finalize */
      NULL,                                         /* class_data */
      sizeof(InfCommunicationCentralMethod),        /* instance_size */
      0,                                            /* n_preallocs */
      inf_communication_central_method_init,        /* instance_init */
      NULL                                          /* value_table */
    };

    static const GInterfaceInfo method_info = {
      inf_communication_central_method_method_init,
      NULL,
      NULL
    };

    central_method_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfCommunicationCentralMethod",
      &central_method_type_info,
      0
    );

    g_type_add_interface_static(
      central_method_type,
      INF_COMMUNICATION_TYPE_METHOD,
      &method_info
    );
  }

  return central_method_type;
}

/* vim:set et sw=2 ts=2: */
