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
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

/**
 * SECTION:inf-communication-object
 * @title: InfCommunicationObject
 * @short_description: Network message destinations
 * @see_also: #InfCommunicationManager
 * @include: libinfinity/communication/inf-communication-object.h
 * @stability: Unstable
 *
 * A #InfCommunicationObject is the destination of network messages sent
 * through the #InfCommunicationManager. Each #InfCommunicationGroup is
 * associated with #InfCommunicationObject. Requests received by that group
 * are reported to the #InfCommunicationObject by calling
 * inf_communication_object_received() on it. Messages sent to a member of
 * that group (via inf_communication_group_send_message()) are also reported
 * by calling inf_communication_object_sent().
 **/

#include <libinfinity/communication/inf-communication-object.h>

GType
inf_communication_scope_get_type(void)
{
  static GType scope_type = 0;

  if(!scope_type)
  {
    static const GEnumValue scope_values[] = {
      {
        INF_COMMUNICATION_SCOPE_PTP,
        "INF_COMMUNICATION_SCOPE_PTP",
        "ptp"
      }, {
        INF_COMMUNICATION_SCOPE_GROUP,
        "INF_COMMUNICATION_SCOPE_GROUP",
        "group",
      }, {
        0,
        NULL,
        NULL
      }
    };

    scope_type = g_enum_register_static(
      "InfCommunicationScope",
      scope_values
    );
  }

  return scope_type;
}


GType
inf_communication_object_get_type(void)
{
  static GType communication_object_type = 0;

  if(!communication_object_type)
  {
    static const GTypeInfo communication_object_info = {
      sizeof(InfCommunicationObjectIface),     /* class_size */
      NULL,                                    /* base_init */
      NULL,                                    /* base_finalize */
      NULL,                                    /* class_init */
      NULL,                                    /* class_finalize */
      NULL,                                    /* class_data */
      0,                                       /* instance_size */
      0,                                       /* n_preallocs */
      NULL,                                    /* instance_init */
      NULL                                     /* value_table */
    };

    communication_object_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfCommunicationObject",
      &communication_object_info,
      0
    );

    g_type_interface_add_prerequisite(
      communication_object_type,
      G_TYPE_OBJECT
    );
  }

  return communication_object_type;
}

/**
 * inf_communication_object_received:
 * @object: A #InfCommunicationObject.
 * @conn: The #InfXmlConnection data was received from.
 * @node: The received data.
 * @error: Location to store error information, if any.
 *
 * This function is called when a #InfCommunicationManager received data from
 * @connection belonging to a group whose communication object is @object.
 * This function should process the incoming data. If it could not process it,
 * then it should set @error.
 *
 * It should return %INF_COMMUNICATION_SCOPE_GROUP if the message is allowed
 * to be forwarded to other group members. Since recipients of forwarded
 * messages don't see the original sender (but just the forwarding host),
 * forwarding arbitrary messages could lead to a security problem in the worst
 * case.
 *
 * For example, if, in central mode, a client sends an (invalid)
 * &lt;add-node&gt; request to the whole (InfDirectory) group, and the server
 * forwarded this to all clients, those clients would try to create a new node
 * although the server rejected the request. In decentral mode, this is not a
 * problem since all clients see where the message comes from, and can
 * themselves reject all messages not coming from the server.
 *
 * Return Value: %INF_COMMUNICATION_SCOPE_GROUP if the message is allowed to
 * be forwarded, %INF_COMMUNICATION_SCOPE_PTP if not.
 **/
InfCommunicationScope
inf_communication_object_received(InfCommunicationObject* object,
                                  InfXmlConnection* conn,
                                  xmlNodePtr node,
                                  GError** error)
{
  InfCommunicationObjectIface* iface;

  g_return_val_if_fail(INF_COMMUNICATION_IS_OBJECT(object), FALSE);
  /* temporarily commented-out: */
  /*g_return_val_if_fail(INF_IS_XML_CONNECTION(conn), FALSE);*/
  g_return_val_if_fail(node != NULL, FALSE);

  iface = INF_COMMUNICATION_OBJECT_GET_IFACE(object);

  if(iface->received != NULL)
    return (*iface->received)(object, conn, node, error);

  return FALSE;
}

/**
 * inf_communication_object_enqueued:
 * @object: A #InfCommunicationObject.
 * @conn: A #InfXmlConnection.
 * @node: The XML data.
 *
 * This function is called, when an XML message scheduled to be sent via
 * inf_communication_group_send_message() or
 * inf_communication_group_send_group_message() cannot be cancelled anymore,
 * because it was already passed to @conn.
 **/
void
inf_communication_object_enqueued(InfCommunicationObject* object,
                                  InfXmlConnection* conn,
                                  xmlNodePtr node)
{
  InfCommunicationObjectIface* iface;

  g_return_if_fail(INF_COMMUNICATION_IS_OBJECT(object));
  g_return_if_fail(INF_IS_XML_CONNECTION(conn));
  g_return_if_fail(node != NULL);

  iface = INF_COMMUNICATION_OBJECT_GET_IFACE(object);

  if(iface->enqueued != NULL)
    (*iface->enqueued)(object, conn, node);
}

/**
 * inf_communication_object_sent:
 * @object: A #InfCommunicationObject.
 * @conn: A #InfXmlConnection.
 * @node: The sent data.
 *
 * This function is called when a XML message sent via
 * inf_communication_group_send_message() or
 * inf_communication_group_send_group_message() has actually been sent out.
 **/
void
inf_communication_object_sent(InfCommunicationObject* object,
                              InfXmlConnection* conn,
                              xmlNodePtr node)
{
  InfCommunicationObjectIface* iface;

  g_return_if_fail(INF_COMMUNICATION_IS_OBJECT(object));
  g_return_if_fail(INF_IS_XML_CONNECTION(conn));
  g_return_if_fail(node != NULL);

  iface = INF_COMMUNICATION_OBJECT_GET_IFACE(object);

  if(iface->sent != NULL)
    (*iface->sent)(object, conn, node);
}

/* vim:set et sw=2 ts=2: */
