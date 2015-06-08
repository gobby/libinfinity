/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/common/inf-simulated-connection.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/inf-define-enum.h>

static const GEnumValue inf_simulated_connection_mode_values[] = {
  {
    INF_SIMULATED_CONNECTION_IMMEDIATE,
    "INF_SIMULATED_CONNECTION_IMMEDIATE",
    "immediate"
  }, {
    INF_SIMULATED_CONNECTION_DELAYED,
    "INF_SIMULATED_CONNECTION_DELAYED",
    "delayed"
  }, {
    INF_SIMULATED_CONNECTION_IO_CONTROLLED,
    "INF_SIMULATED_CONNECTION_IO_CONTROLLED",
    "io-controlled"
  }, {
    0,
    NULL,
    NULL
  }
};

typedef struct _InfSimulatedConnectionPrivate InfSimulatedConnectionPrivate;
struct _InfSimulatedConnectionPrivate {
  InfIo* io;
  InfIoDispatch* io_handler;

  InfSimulatedConnection* target;
  InfSimulatedConnectionMode mode;

  xmlNodePtr queue;
  xmlNodePtr queue_last_item;
};

enum {
  PROP_0,

  PROP_IO,

  PROP_TARGET,
  PROP_MODE,

  /* From InfXmlConnection */
  PROP_STATUS,
  PROP_NETWORK,
  PROP_LOCAL_ID,
  PROP_REMOTE_ID,
  PROP_LOCAL_CERTIFICATE,
  PROP_REMOTE_CERTIFICATE
};

#define INF_SIMULATED_CONNECTION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_SIMULATED_CONNECTION, InfSimulatedConnectionPrivate))

static void inf_simulated_connection_xml_connection_iface_init(InfXmlConnectionInterface* iface);
INF_DEFINE_ENUM_TYPE(InfSimulatedConnectionMode, inf_simulated_connection_mode, inf_simulated_connection_mode_values)
G_DEFINE_TYPE_WITH_CODE(InfSimulatedConnection, inf_simulated_connection, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfSimulatedConnection)
  G_IMPLEMENT_INTERFACE(INF_TYPE_XML_CONNECTION, inf_simulated_connection_xml_connection_iface_init))

static void
inf_simulated_connection_clear_queue(InfSimulatedConnection* connection)
{
  InfSimulatedConnectionPrivate* priv;
  xmlNodePtr next;

  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);

  if(priv->io_handler != NULL)
  {
    g_assert(priv->io != NULL);

    inf_io_remove_dispatch(priv->io, priv->io_handler);
    priv->io_handler = NULL;
  }

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
inf_simulated_connection_init(InfSimulatedConnection* connection)
{
  InfSimulatedConnectionPrivate* priv;
  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);

  priv->io = NULL;

  priv->target = NULL;
  priv->mode = INF_SIMULATED_CONNECTION_IMMEDIATE;
}

static void
inf_simulated_connection_dispose(GObject* object)
{
  InfSimulatedConnection* connection;
  InfSimulatedConnectionPrivate* priv;

  connection = INF_SIMULATED_CONNECTION(object);
  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);

  inf_simulated_connection_unset_target(connection);
  g_assert(priv->io_handler == NULL);

  if(priv->io != NULL)
  {
    g_object_unref(priv->io);
    priv->io = NULL;
  }

  G_OBJECT_CLASS(inf_simulated_connection_parent_class)->dispose(object);
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
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct only */
    priv->io = INF_IO(g_value_get_object(value));
    if(priv->io) g_object_ref(priv->io);
    break;
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
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
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
  case PROP_LOCAL_CERTIFICATE:
    g_value_set_pointer(value, NULL);
    break;
  case PROP_REMOTE_CERTIFICATE:
    g_value_set_boxed(value, NULL);
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
inf_simulated_connection_dispatch_func(gpointer user_data)
{
  InfSimulatedConnection* connection;
  InfSimulatedConnectionPrivate* priv;

  connection = INF_SIMULATED_CONNECTION(user_data);
  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);

  priv->io_handler = NULL;
  inf_simulated_connection_flush(connection);
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
  case INF_SIMULATED_CONNECTION_IO_CONTROLLED:
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

    if(priv->mode == INF_SIMULATED_CONNECTION_IO_CONTROLLED)
    {
      if(priv->io_handler == NULL)
      {
        g_assert(priv->io != NULL);

        priv->io_handler = inf_io_add_dispatch(
          priv->io,
          inf_simulated_connection_dispatch_func,
          connection,
          NULL
        );
      }
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
inf_simulated_connection_class_init(
  InfSimulatedConnectionClass* connection_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(connection_class);

  object_class->dispose = inf_simulated_connection_dispose;
  object_class->set_property = inf_simulated_connection_set_property;
  object_class->get_property = inf_simulated_connection_get_property;

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "The main loop to be used for IO_CONTROLLED mode",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

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

  g_object_class_override_property(
    object_class,
    PROP_LOCAL_CERTIFICATE,
    "local-certificate"
  );

  g_object_class_override_property(
    object_class,
    PROP_REMOTE_CERTIFICATE,
    "remote-certificate"
  );
}

static void
inf_simulated_connection_xml_connection_iface_init(
  InfXmlConnectionInterface* iface)
{
  iface->close = inf_simulated_connection_xml_connection_close;
  iface->send = inf_simulated_connection_xml_connection_send;
}

/*
 * Public API
 */

/**
 * inf_simulated_connection_new: (constructor)
 *
 * Creates a new #InfSimulatedConnection. A connection created this way cannot
 * be switched to %INF_SIMULATED_CONNECTION_IO_CONTROLLED mode. Use
 * inf_simulated_connection_new_with_io() instead if you intend to do that.
 *
 * Returns: (transfer full): A new #InfSimulatedConnection.
 **/
InfSimulatedConnection*
inf_simulated_connection_new(void)
{
  GObject* object;
  object = g_object_new(INF_TYPE_SIMULATED_CONNECTION, NULL);
  return INF_SIMULATED_CONNECTION(object);
}

/**
 * inf_simulated_connection_new_with_io: (constructor)
 * @io: The main loop to be used for %INF_SIMULATED_CONNECTION_IO_CONTROLLED
 * mode.
 *
 * Creates a new #InfSimulatedConnection with the given #InfIo. This
 * connection can be used with %INF_SIMULATED_CONNECTION_IO_CONTROLLED mode.
 * If you don't intend to use that mode then using
 * inf_simulated_connection_new() is also good enough since the #InfIo object
 * is not required in that case.
 *
 * Returns: (transfer full): A new #InfSimulatedConnection.
 */
InfSimulatedConnection*
inf_simulated_connection_new_with_io(InfIo* io)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_IO(io), NULL);

  object = g_object_new(INF_TYPE_SIMULATED_CONNECTION, "io", io, NULL);
  return INF_SIMULATED_CONNECTION(object);
}

/**
 * inf_simulated_connection_connect:
 * @connection: A #InfSimulatedConnection.
 * @to: The target connection.
 *
 * Connects two simulated connections, so that data sent through one of them
 * is received by the other one and vice versa. So one call to this function
 * connects both #InfSimulatedConnection<!-- -->s to each other. There is no
 * need to call this function once for each connection.
 *
 * If one or both of the connections already have another target, then the
 * simulated connection between those is closed first.
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
 * In %INF_SIMULATED_CONNECTION_IO_CONTROLLED mode, messages are queued and
 * received by the target as soon as a dispatch handler (see
 * inf_io_add_dispatch()) installed on the main loop is called.
 *
 * When changing the mode from %INF_SIMULATED_CONNECTION_DELAYED or
 * %INF_SIMULATED_CONNECTION_IO_CONTROLLED to
 * %INF_SIMULATED_CONNECTION_IMMEDIATE, then the queue is flushed, too.
 */
void
inf_simulated_connection_set_mode(InfSimulatedConnection* connection,
                                  InfSimulatedConnectionMode mode)
{
  InfSimulatedConnectionPrivate* priv;
  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);

  g_return_if_fail(priv->io != NULL ||
                   mode != INF_SIMULATED_CONNECTION_IO_CONTROLLED);

  if(priv->mode != mode)
  {
    if(mode == INF_SIMULATED_CONNECTION_IMMEDIATE)
      inf_simulated_connection_flush(connection);

    priv->mode = mode;
    g_object_notify(G_OBJECT(connection), "mode");
  }
}

/**
 * inf_simulated_connection_flush:
 * @connection: A #InfSimulatedConnection.
 *
 * When @connection's mode is %INF_SIMULATED_CONNECTION_DELAYED or
 * %INF_SIMULATED_CONNECTION_IO_CONTROLLED, then calling this function makes
 * the target connection receive all the queued messages.
 */
void
inf_simulated_connection_flush(InfSimulatedConnection* connection)
{
  InfSimulatedConnectionPrivate* priv;
  xmlNodePtr next;

  priv = INF_SIMULATED_CONNECTION_PRIVATE(connection);
  g_return_if_fail(priv->target != NULL);

  if(priv->mode == INF_SIMULATED_CONNECTION_IO_CONTROLLED)
  {
    g_assert(priv->io != NULL);

    if(priv->io_handler != NULL)
    {
      inf_io_remove_dispatch(priv->io, priv->io_handler);
      priv->io_handler = NULL;
    }
  }

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
