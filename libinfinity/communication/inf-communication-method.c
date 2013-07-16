/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-communication-method
 * @title: InfCommunicationMethod
 * @short_description: Network communication method
 * @see_also: #InfCommunicationManager
 * @include: libinfinity/communication/inf-communication-method.h
 * @stability: Unstable
 *
 * A #InfCommunicationMethod specifies how messages are transferred between
 * group members on the same network. So one method handles all connections
 * on a specific network for the group.
 **/

#include <libinfinity/communication/inf-communication-method.h>

#include <libinfinity/inf-marshal.h>

enum {
  ADD_MEMBER,
  REMOVE_MEMBER,

  LAST_SIGNAL
};

static guint method_signals[LAST_SIGNAL];

static void
inf_communication_method_base_init(gpointer class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    /**
     * InfCommunicationMethod::add-member:
     * @method: The #InfCommunicationMethod emitting the signal.
     * @connection: The #InfXmlConnection that was added.
     *
     * This signal is emitted whenever a new connection has been added to the
     * group on the network this method handles.
     */
    method_signals[ADD_MEMBER] = g_signal_new(
      "add-member",
      INF_COMMUNICATION_TYPE_METHOD,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfCommunicationMethodIface, add_member),
      NULL, NULL,
      inf_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1,
      INF_TYPE_XML_CONNECTION
    );

    /**
     * InfCommunicationMethod::remove-member:
     * @method: The #InfCommunicationMethod emitting the signal.
     * @connection: The #InfXmlConnection that was removed.
     *
     * This signal is emitted whenever a connection has been removed from the
     * group on the network this method handles.
     */
    method_signals[REMOVE_MEMBER] = g_signal_new(
      "remove-member",
      INF_COMMUNICATION_TYPE_METHOD,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfCommunicationMethodIface, remove_member),
      NULL, NULL,
      inf_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1,
      INF_TYPE_XML_CONNECTION
    );

    initialized = TRUE;
  }
}

GType
inf_communication_method_get_type(void)
{
  static GType communication_method_type = 0;

  if(!communication_method_type)
  {
    static const GTypeInfo communication_method_info = {
      sizeof(InfCommunicationMethodIface),     /* class_size */
      inf_communication_method_base_init,      /* base_init */
      NULL,                                    /* base_finalize */
      NULL,                                    /* class_init */
      NULL,                                    /* class_finalize */
      NULL,                                    /* class_data */
      0,                                       /* instance_size */
      0,                                       /* n_preallocs */
      NULL,                                    /* instance_init */
      NULL                                     /* value_table */
    };

    communication_method_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfCommunicationMethod",
      &communication_method_info,
      0
    );

    g_type_interface_add_prerequisite(
      communication_method_type,
      G_TYPE_OBJECT
    );
  }

  return communication_method_type;
}

/**
 * inf_communication_method_add_member:
 * @method: A #InfCommunicationMethod.
 * @connection: The #InfXmlConnection to add.
 *
 * Adds a new connection to the group. The network of @connection must match
 * the network the method is handling, and @connection must not already be
 * a member of the group (see inf_communication_method_is_member()).
 */
void
inf_communication_method_add_member(InfCommunicationMethod* method,
                                    InfXmlConnection* connection)
{
  g_return_if_fail(INF_COMMUNICATION_IS_METHOD(method));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(!inf_communication_method_is_member(method, connection));

  g_signal_emit(
    G_OBJECT(method),
    method_signals[ADD_MEMBER],
    0,
    connection
  );
}

/**
 * inf_communication_method_remove_member:
 * @method: A #InfCommunicationMethod.
 * @connection: The #InfXmlConnection to remove.
 *
 * Removes a connection from the group. @connection needs to be a member of
 * the group (see inf_communication_method_is_member()).
 */
void
inf_communication_method_remove_member(InfCommunicationMethod* method,
                                       InfXmlConnection* connection)
{
  g_return_if_fail(INF_COMMUNICATION_IS_METHOD(method));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(inf_communication_method_is_member(method, connection));

  g_signal_emit(
    G_OBJECT(method),
    method_signals[REMOVE_MEMBER],
    0,
    connection
  );
}

/**
 * inf_communication_method_is_member:
 * @method: A #InfCommunicationMethod.
 * @connection: A #InfXmlConnection.
 *
 * Returns whether @connection was added to the group via
 * inf_communication_method_add_member().
 *
 * Returns: Whether @connection is a member of the group.
 */
gboolean
inf_communication_method_is_member(InfCommunicationMethod* method,
                                   InfXmlConnection* connection)
{
  InfCommunicationMethodIface* iface;

  g_return_val_if_fail(INF_COMMUNICATION_IS_METHOD(method), FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), FALSE);

  iface = INF_COMMUNICATION_METHOD_GET_IFACE(method);
  g_return_val_if_fail(iface->is_member != NULL, FALSE);

  return iface->is_member(method, connection);
}

/**
 * inf_communication_method_send_single:
 * @method: A #InfCommunicationMethod.
 * @connection: A #InfXmlConnection that is a group member.
 * @xml: The message to send.
 *
 * Sends an XML message to @connection. This function takes ownership of @xml.
 */
void
inf_communication_method_send_single(InfCommunicationMethod* method,
                                     InfXmlConnection* connection,
                                     xmlNodePtr xml)
{
  InfCommunicationMethodIface* iface;

  g_return_if_fail(INF_COMMUNICATION_IS_METHOD(method));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(inf_communication_method_is_member(method, connection));
  g_return_if_fail(xml != NULL);

  iface = INF_COMMUNICATION_METHOD_GET_IFACE(method);
  g_return_if_fail(iface->send_single != NULL);

  iface->send_single(method, connection, xml);
}

/**
 * inf_communication_method_send_all:
 * @method: A #InfCommunicationMethod.
 * @xml: The message to send.
 *
 * Sends an XML message to all group members on this network. This function
 * takes ownership of @xml.
 */
void
inf_communication_method_send_all(InfCommunicationMethod* method,
                                  xmlNodePtr xml)
{
  InfCommunicationMethodIface* iface;

  g_return_if_fail(INF_COMMUNICATION_IS_METHOD(method));
  g_return_if_fail(xml != NULL);

  iface = INF_COMMUNICATION_METHOD_GET_IFACE(method);
  g_return_if_fail(iface->send_all != NULL);

  iface->send_all(method, xml);
}

/**
 * inf_communication_method_cancel_messages:
 * @method: A #InfCommunicationMethod.
 * @connection: A #InfXmlConnection that is a group member.
 *
 * This function stops all messages to be sent to @connection that have not
 * yet been sent.
 */
void
inf_communication_method_cancel_messages(InfCommunicationMethod* method,
                                         InfXmlConnection* connection)
{
  InfCommunicationMethodIface* iface;

  g_return_if_fail(INF_COMMUNICATION_IS_METHOD(method));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  iface = INF_COMMUNICATION_METHOD_GET_IFACE(method);
  g_return_if_fail(iface->cancel_messages != NULL);

  iface->cancel_messages(method, connection);
}

/**
 * inf_communication_method_received:
 * @method: A #InfCommunicationMethod.
 * @connection: A #InfXmlConnection that is a group member.
 * @xml: The received message.
 *
 * This function is called by the #InfCommunicationRegistry if data has been
 * received on registered connections
 * (see inf_communication_registry_register()).
 *
 * This function returns the scope of the message. If the scope is
 * %INF_COMMUNICATION_SCOPE_GROUP then the registry relays the message to
 * other connections on different networks (if any).
 *
 * Returns: The scope of the message.
 */
InfCommunicationScope
inf_communication_method_received(InfCommunicationMethod* method,
                                  InfXmlConnection* connection,
                                  xmlNodePtr xml)
{
  InfCommunicationMethodIface* iface;

  g_return_val_if_fail(INF_COMMUNICATION_IS_METHOD(method), 0);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), 0);
  g_return_val_if_fail(inf_communication_method_is_member(method, connection), 0);
  g_return_val_if_fail(xml != NULL, 0);

  iface = INF_COMMUNICATION_METHOD_GET_IFACE(method);
  g_return_val_if_fail(iface->received != NULL, 0);

  return iface->received(method, connection, xml);
}

/**
 * inf_communication_method_enqueued:
 * @method: A #InfCommunicationMethod.
 * @connection: A #InfXmlConnection.
 * @xml: The enqueued message.
 *
 * This function is called by the #InfCommunicationRegistry if data has been
 * enqueued on registered connections
 * (see inf_communication_registry_register()).
 */
void
inf_communication_method_enqueued(InfCommunicationMethod* method,
                                  InfXmlConnection* connection,
                                  xmlNodePtr xml)
{
  InfCommunicationMethodIface* iface;

  g_return_if_fail(INF_COMMUNICATION_IS_METHOD(method));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(inf_communication_method_is_member(method, connection));
  g_return_if_fail(xml != NULL);

  iface = INF_COMMUNICATION_METHOD_GET_IFACE(method);
  g_return_if_fail(iface->enqueued != NULL);

  iface->enqueued(method, connection, xml);
}

/**
 * inf_communication_method_sent:
 * @method: A #InfCommunicationMethod.
 * @connection: A #InfXmlConnection.
 * @xml: The sent message.
 *
 * This function is called by the #InfCommunicationRegistry if data has been
 * sent on registered connections (see inf_communication_registry_register()).
 */
void
inf_communication_method_sent(InfCommunicationMethod* method,
                              InfXmlConnection* connection,
                              xmlNodePtr xml)
{
  InfCommunicationMethodIface* iface;

  g_return_if_fail(INF_COMMUNICATION_IS_METHOD(method));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(inf_communication_method_is_member(method, connection));
  g_return_if_fail(xml != NULL);

  iface = INF_COMMUNICATION_METHOD_GET_IFACE(method);
  g_return_if_fail(iface->sent != NULL);

  iface->sent(method, connection, xml);
}

/* vim:set et sw=2 ts=2: */
