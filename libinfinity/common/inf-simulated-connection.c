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

#include <libinfinity/common/inf-simulated-connection.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/inf-marshal.h>

/**
 * SECTION:inf-simulated-connection
 * @title: InfSimulatedConnection
 * @short_description: Simulated network connection
 * @include: libinfinity/common/inf-simulated-connection.h
 * @stability: Unstable
 *
 * #InfSimulatedConnection simulates a connection and can be used everywhere
 * where a #InfXmlConnection is expected. Use
 * inf_simulated_connection_connect() to connect two such connections so that
 * data sent through one is received by the other.
 */

typedef struct _InfSimulatedConnectionPrivate InfSimulatedConnectionPrivate;
struct _InfSimulatedConnectionPrivate {
  InfSimulatedConnection* target;
  InfSimulatedConnectionMode mode;

  xmlNodePtr queue;
  xmlNodePtr queue_last_item;
};

enum {
  PROP_0,

  PROP_TARGET,
  PROP_MODE,

  /* From InfXmlConnection */
  PROP_STATUS,
  PROP_NETWORK,
  PROP_LOCAL_ID,
  PROP_REMOTE_ID
};

#define INF_SIMULATED_CONNECTION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_SIMULATED_CONNECTION, InfSimulatedConnectionPrivate))

static GObjectClass* parent_class;

static void
inf_simulated_connection_clear_queue(InfSimulatedConnection* connection)
{
  InfSimulatedConnectionPrivate* priv;
  xmlNodePtr next;

  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);

  while(priv->queue != NULL)
  {
    next = priv->queue->next;
    xmlFreeNode(priv->queue);
    priv->queue = next;
  }

  priv->queue_last_item = NULL;
}

static void
inf_simulated_connection_unset_target(InfSimulatedConnection* connection)
{
  InfSimulatedConnectionPrivate* priv;
  InfSimulatedConnection* target;
  InfSimulatedConnectionPrivate* target_priv;

  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);
  target = priv->target;

  if(target != NULL)
  {
    target_priv = INF_SIMULATED_CONNECTION_PRIVATE(priv->target);
    g_assert(target_priv->target == connection);

    priv->target = NULL;
    target_priv->target = NULL;

    inf_simulated_connection_clear_queue(connection);
    inf_simulated_connection_clear_queue(target);

    g_object_notify(G_OBJECT(connection), "target");
    g_object_notify(G_OBJECT(connection), "status");
    g_object_notify(G_OBJECT(target), "target");
    g_object_notify(G_OBJECT(target), "status");
  }
}

static void
inf_simulated_connection_set_target(InfSimulatedConnection* connection,
                                    InfSimulatedConnection* target)
{
  InfSimulatedConnectionPrivate* priv;

  inf_simulated_connection_unset_target(connection);
  if(target != NULL) inf_simulated_connection_unset_target(target);

  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);
  priv->target = target;

  if(target != NULL)
  {
    priv = INF_SIMULATED_CONNECTION_PRIVATE(target);
    priv->target = connection;
  }

  g_object_notify(G_OBJECT(connection), "target");
  g_object_notify(G_OBJECT(connection), "status");

  if(target != NULL)
  {
    g_object_notify(G_OBJECT(target), "target");
    g_object_notify(G_OBJECT(target), "status");
  }
}

/*
 * GObject overrides
 */

static void
inf_simulated_connection_init(GTypeInstance* instance,
                              gpointer g_class)
{
  InfSimulatedConnection* io;
  InfSimulatedConnectionPrivate* priv;

  io = INF_SIMULATED_CONNECTION(instance);
  priv = INF_SIMULATED_CONNECTION_PRIVATE(io);

  priv->target = NULL;
  priv->mode = INF_SIMULATED_CONNECTION_IMMEDIATE;
}

static void
inf_simulated_connection_dispose(GObject* object)
{
  InfSimulatedConnection* connection;
  connection = INF_SIMULATED_CONNECTION(object);

  inf_simulated_connection_unset_target(connection);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_simulated_connection_set_property(GObject* object,
                                      guint prop_id,
                                      const GValue* value,
                                      GParamSpec* pspec)
{
  InfSimulatedConnection* sim;
  InfSimulatedConnectionPrivate* priv;

  sim = INF_SIMULATED_CONNECTION(object);
  priv = INF_SIMULATED_CONNECTION_PRIVATE(sim);

  switch(prop_id)
  {
  case PROP_TARGET:
    inf_simulated_connection_set_target(
      sim,
      INF_SIMULATED_CONNECTION(g_value_get_object(value))
    );

    break;
  case PROP_MODE:
    inf_simulated_connection_set_mode(sim, g_value_get_enum(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_simulated_connection_get_property(GObject* object,
                                      guint prop_id,
                                      GValue* value,
                                      GParamSpec* pspec)
{
  InfSimulatedConnection* sim;
  InfSimulatedConnectionPrivate* priv;
  gchar* id;

  sim = INF_SIMULATED_CONNECTION(object);
  priv = INF_SIMULATED_CONNECTION_PRIVATE(sim);

  switch(prop_id)
  {
  case PROP_TARGET:
    g_value_set_object(value, G_OBJECT(priv->target));
    break;
  case PROP_MODE:
    g_value_set_enum(value, priv->mode);
    break;
  case PROP_STATUS:
    if(priv->target != NULL)
      g_value_set_enum(value, INF_XML_CONNECTION_OPEN);
    else
      g_value_set_enum(value, INF_XML_CONNECTION_CLOSED);

    break;
  case PROP_NETWORK:
    g_value_set_static_string(value, "simulated");
    break;
  case PROP_LOCAL_ID:
    id = g_strdup_printf("simulated-%p", (void*)sim);
    g_value_take_string(value, id);
    break;
  case PROP_REMOTE_ID:
    g_assert(priv->target != NULL);
    id = g_strdup_printf("simulated-%p", (void*)priv->target);
    g_value_take_string(value, id);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * InfXmlConnection interface implementation
 */

static void
inf_simulated_connection_xml_connection_close(InfXmlConnection* connection)
{
  InfSimulatedConnectionPrivate* priv;
  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);

  g_assert(priv->target != NULL);
  inf_simulated_connection_unset_target(INF_SIMULATED_CONNECTION(connection));
}

static void
inf_simulated_connection_xml_connection_send(InfXmlConnection* connection,
                                             xmlNodePtr xml)
{
  InfSimulatedConnectionPrivate* priv;
  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);

  g_assert(priv->target != NULL);

  switch(priv->mode)
  {
  case INF_SIMULATED_CONNECTION_IMMEDIATE:
    inf_xml_connection_sent(connection, xml);
    inf_xml_connection_received(INF_XML_CONNECTION(priv->target), xml);
    xmlFreeNode(xml);
    break;
  case INF_SIMULATED_CONNECTION_DELAYED:
    xmlUnlinkNode(xml);
    if(priv->queue == NULL)
    {
      priv->queue = xml;
      priv->queue_last_item = xml;
    }
    else
    {
      priv->queue_last_item->next = xml;
      priv->queue_last_item = xml;
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

/*
 * GObject type registration
 */

static void
inf_simulated_connection_class_init(gpointer g_class,
                                    gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfSimulatedConnectionPrivate));

  object_class->dispose = inf_simulated_connection_dispose;
  object_class->set_property = inf_simulated_connection_set_property;
  object_class->get_property = inf_simulated_connection_get_property;

  g_object_class_install_property(
    object_class,
    PROP_TARGET,
    g_param_spec_object(
      "target",
      "Target connection",
      "The simulated connection receiving data sent through this connection",
      INF_TYPE_SIMULATED_CONNECTION,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_MODE,
    g_param_spec_enum(
      "mode",
      "Mode",
      "The mode of the simulated connection",
      INF_TYPE_SIMULATED_CONNECTION_MODE,
      INF_SIMULATED_CONNECTION_IMMEDIATE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_override_property(object_class, PROP_STATUS, "status");
  g_object_class_override_property(object_class, PROP_NETWORK, "network");
  g_object_class_override_property(object_class, PROP_LOCAL_ID, "local-id");
  g_object_class_override_property(object_class, PROP_REMOTE_ID, "remote-id");
}

static void
inf_simulated_connection_xml_connection_init(gpointer g_iface,
                                             gpointer iface_data)
{
  InfXmlConnectionIface* iface;
  iface = (InfXmlConnectionIface*)g_iface;

  iface->close = inf_simulated_connection_xml_connection_close;
  iface->send = inf_simulated_connection_xml_connection_send;
}

GType
inf_simulated_connection_mode_get_type(void)
{
  static GType mode_type = 0;

  if(!mode_type)
  {
    static const GEnumValue mode_type_values[] = {
      {
        INF_SIMULATED_CONNECTION_IMMEDIATE,
        "INF_SIMULATED_CONNECTION_IMMEDIATE",
        "immediate"
      }, {
        INF_SIMULATED_CONNECTION_DELAYED,
        "INF_SIMULATED_CONNECTION_DELAYED",
        "delayed"
      }, {
        0,
        NULL,
        NULL
      }
    };

    mode_type = g_enum_register_static(
      "InfSimulatedConnectionMode",
      mode_type_values
    );
  }

  return mode_type;
}

GType
inf_simulated_connection_get_type(void)
{
  static GType simulated_connection_type = 0;

  if(!simulated_connection_type)
  {
    static const GTypeInfo simulated_connection_type_info = {
      sizeof(InfSimulatedConnectionClass),   /* class_size */
      NULL,                                  /* base_init */
      NULL,                                  /* base_finalize */
      inf_simulated_connection_class_init,   /* class_init */
      NULL,                                  /* class_finalize */
      NULL,                                  /* class_data */
      sizeof(InfSimulatedConnection),        /* instance_size */
      0,                                     /* n_preallocs */
      inf_simulated_connection_init,         /* instance_init */
      NULL                                   /* value_table */
    };

    static const GInterfaceInfo xml_connection_info = {
      inf_simulated_connection_xml_connection_init,
      NULL,
      NULL
    };

    simulated_connection_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfSimulatedConnection",
      &simulated_connection_type_info,
      0
    );

    g_type_add_interface_static(
      simulated_connection_type,
      INF_TYPE_XML_CONNECTION,
      &xml_connection_info
    );
  }

  return simulated_connection_type;
}

/*
 * Public API
 */

/**
 * inf_simulated_connection_new:
 *
 * Creates a new #InfSimulatedConnection.
 *
 * Return Value: A new #InfSimulatedConnection.
 **/
InfSimulatedConnection*
inf_simulated_connection_new(void)
{
  GObject* object;
  object = g_object_new(INF_TYPE_SIMULATED_CONNECTION, NULL);
  return INF_SIMULATED_CONNECTION(object);
}

/**
 * inf_simulated_connection_connect:
 * @connection: A #InfSimulatedConnection.
 * @to: The target connection.
 *
 * Connects two simulated connections, so that data sent through one of them
 * is received by the other one. If one or both of the connections already
 * have another target, then the simulated connection between those is closed
 * first.
 */
void
inf_simulated_connection_connect(InfSimulatedConnection* connection,
                                 InfSimulatedConnection* to)
{
  inf_simulated_connection_set_target(connection, to);
}

/**
 * inf_simulated_connection_set_mode:
 * @connection: A #InfSimulatedConnection.
 * @mode: The new mode to set.
 *
 * Sets the mode of the simulated connection.
 *
 * In %INF_SIMULATED_CONNECTION_IMMEDIATE mode, messages sent through the
 * connection are received by the target during the call to
 * inf_xml_connection_send().
 *
 * In %INF_SIMULATED_CONNECTION_DELAYED mode, messages sent are queued and
 * received by the target when inf_simulated_connection_flush() is called.
 *
 * When changing the mode from %INF_SIMULATED_CONNECTION_DELAYED to
 * %INF_SIMULATED_CONNECTION_IMMEDIATE, then the queue is flushed, too.
 */
void
inf_simulated_connection_set_mode(InfSimulatedConnection* connection,
                                  InfSimulatedConnectionMode mode)
{
  InfSimulatedConnectionPrivate* priv;
  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);

  if(priv->mode != mode)
  {
    if(priv->mode == INF_SIMULATED_CONNECTION_DELAYED)
      inf_simulated_connection_flush(connection);

    priv->mode = mode;
    g_object_notify(G_OBJECT(connection), "mode");
  }
}

/**
 * inf_simulated_connection_flush:
 * @connection: A #InfSimulatedConnection.
 *
 * When @connection's mode is %INF_SIMULATED_CONNECTION_DELAYED, then calling
 * this function makes the target connection receive all the queued messages.
 */
void
inf_simulated_connection_flush(InfSimulatedConnection* connection)
{
  InfSimulatedConnectionPrivate* priv;
  xmlNodePtr next;

  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);
  g_assert(priv->target != NULL);

  while(priv->queue != NULL)
  {
    inf_xml_connection_sent(
      INF_XML_CONNECTION(connection),
      priv->queue
    );

    inf_xml_connection_received(
      INF_XML_CONNECTION(priv->target),
      priv->queue
    );

    next = priv->queue->next;
    xmlFreeNode(priv->queue);
    priv->queue = next;
  }

  priv->queue_last_item = NULL;
}

/* vim:set et sw=2 ts=2: */
