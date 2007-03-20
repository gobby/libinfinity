/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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

#include <libinfinity/inf-net-object.h>

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

void
inf_net_object_received(InfNetObject* object,
                        InfXmlConnection* conn,
                        const xmlNodePtr node)
{
  InfNetObjectIface* iface;

  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(INF_IS_XML_CONNECTION(conn));
  g_return_if_fail(node != NULL);

  iface = INF_NET_OBJECT_GET_IFACE(object);

  g_return_if_fail(iface->received != NULL);

  (*iface->received)(object, conn, node);
}

void
inf_net_object_enqueued(InfNetObject* object,
                        InfXmlConnection* conn,
                        const xmlNodePtr node)
{
  InfNetObjectIface* iface;

  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(INF_IS_XML_CONNECTION(conn));
  g_return_if_fail(node != NULL);

  iface = INF_NET_OBJECT_GET_IFACE(object);
  g_return_if_fail(iface->enqueued != NULL);

  (*iface->enqueued)(object, conn, node);
}

void
inf_net_object_sent(InfNetObject* object,
                    InfXmlConnection* conn,
                    const xmlNodePtr node)
{
  InfNetObjectIface* iface;

  g_return_if_fail(INF_IS_NET_OBJECT(object));
  g_return_if_fail(INF_IS_XML_CONNECTION(object));
  g_return_if_fail(node != NULL);

  iface = INF_NET_OBJECT_GET_IFACE(object);

  g_return_if_fail(iface->sent != NULL);

  (*iface->sent)(object, conn, node);
}
