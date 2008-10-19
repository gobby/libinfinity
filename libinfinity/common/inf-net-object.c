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
 * SECTION:inf-net-object
 * @title: InfNetObject
 * @short_description: Network message destinations
 * @see_also: #InfConnectionManager
 * @include: libinfinity/common/inf-net-object.h
 * @stability: Unstable
 *
 * A #InfNetObject is the destination of network messages sent through the
 * #InfConnectionManager. Each #InfConnectionManagerGroup is associated with
 * a #InfNetObject. Requests received by that group are reported to the
 * #InfNetObject by calling inf_net_object_received() on it. Messages sent
 * to a member of that group
 * (via inf_connection_manager_group_send_to_connection()) are also reported
 * by calling inf_net_object_sent().
 **/

#include <libinfinity/common/inf-net-object.h>

GType
inf_net_object_get_type(void)
{
  static GType net_object_type = 0;

  if(!net_object_type)
  {
    static const GTypeInfo net_object_info = {
      sizeof(InfNetObjectIface),     /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      NULL,                          /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      0,                             /* instance_size */
      0,                             /* n_preallocs */
      NULL,                          /* instance_init */
      NULL                           /* value_table */
    };

    net_object_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfNetObject",
      &net_object_info,
      0
    );

    g_type_interface_add_prerequisite(net_object_type, G_TYPE_OBJECT);
  }

  return net_object_type;
}

/**
 * inf_net_object_received:
 * @object: A #InfNetObject.
 * @conn: The #InfXmlConnection data was received from.
 * @node: The received data.
 * @error: Location to store error information, if any.
 *
 * This function is called when a #InfConnectionManager received data from
 * @connection belonging to a group whole netobject is @object. This function
 * should process the incoming data. If it could not process it, then it
 * should set @error.
 *
 * It should return %TRUE if the message is allowed to be forwarded to other
 * group members. Since recipients of forwarded messages don't see the
 * original sender (but just the forwarding host), forwarding arbitrary
 * messages could lead to a security problem in the worst case.
 *
 * For example, if, in central mode, a client sends an (invalid)
 * &lt;add-node&gt; request to the whole (#InfDirectory) group, and the server
 * forwarded this to all clients, those clients would try to create a new node
 * although the server rejected the request. In decentral mode, this is not a
 * problem since every client sees where the message comes from, and can
 * himself reject all messages not coming from the server.
 *
 * Return Value: %TRUE if the message is allowed to be forwarded,
 * %FALSE if not.
 **/
gboolean
inf_net_object_received(InfNetObject* object,
                        InfXmlConnection* conn,
                        xmlNodePtr node,
                        GError** error)
{
  InfNetObjectIface* iface;

  g_return_val_if_fail(INF_IS_NET_OBJECT(object), FALSE);
  /* temporarily commented-out: */
  /*g_return_val_if_fail(INF_IS_XML_CONNECTION(conn), FALSE);*/
  g_return_val_if_fail(node != NULL, FALSE);

  iface = INF_NET_OBJECT_GET_IFACE(object);

  if(iface->received != NULL)
    return (*iface->received)(object, conn, node, error);

  return FALSE;
}

/**
 * inf_net_object_enqueued:
 * @object: A #InfNetObject.
 * @conn: A #InfXmlConnection.
 * @node: The XML data.
 *
 * This function is called, when an XML message scheduled to be sent via
 * inf_connection_manager_group_send_to_connection() or
 * inf_connection_manager_group_send_to_group() cannot be cancelled anymore,
 * because it was already passed to @conn.
 **/
void
inf_net_object_enqueued(InfNetObject* object,
                        InfXmlConnection* conn,
                        xmlNodePtr node)
{
  InfNetObjectIface* iface;

  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(INF_IS_XML_CONNECTION(conn));
  g_return_if_fail(node != NULL);

  iface = INF_NET_OBJECT_GET_IFACE(object);

  if(iface->enqueued != NULL)
    (*iface->enqueued)(object, conn, node);
}

/**
 * inf_net_object_sent:
 * @object: A #InfNetObject.
 * @conn: A #InfXmlConnection.
 * @node: The sent data.
 *
 * This function is called when a XML message sent via
 * inf_connection_manager_group_send_to_connection() or
 * inf_connection_manager_group_send_to_group() has actually been sent out.
 **/
void
inf_net_object_sent(InfNetObject* object,
                    InfXmlConnection* conn,
                    xmlNodePtr node)
{
  InfNetObjectIface* iface;

  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(INF_IS_XML_CONNECTION(conn));
  g_return_if_fail(node != NULL);

  iface = INF_NET_OBJECT_GET_IFACE(object);

  if(iface->sent != NULL)
    (*iface->sent)(object, conn, node);
}

/* vim:set et sw=2 ts=2: */
