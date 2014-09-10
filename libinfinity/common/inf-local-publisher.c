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
 * SECTION:inf-local-publisher
 * @short_description: Publish services on the local network
 * @include: libinfinity/common/inf-local-publisher.h
 * @see_also: #InfDiscoveryAvahi
 *
 * #InfLocalPublisher provides a common interface to publish services on the
 * local network.
 */

#include <libinfinity/common/inf-local-publisher.h>

G_DEFINE_INTERFACE(InfLocalPublisher, inf_local_publisher, G_TYPE_OBJECT)

static void
inf_local_publisher_default_init(InfLocalPublisherInterface* iface)
{
}

/**
 * inf_local_publisher_publish:
 * @publisher: A #InfLocalPublisher.
 * @type: The service type to publish, such as _http._tcp.
 * @name: The name of the service.
 * @port: The port the service is listening on.
 *
 * Publishes a service through @publisher so that it can be discovered with
 * an appropriate #InfDiscovery.
 *
 * Return Value: (transfer none): A #InfLocalPublisherItem that can be used
 * to unpublish the service again.
 **/
InfLocalPublisherItem*
inf_local_publisher_publish(InfLocalPublisher* publisher,
                            const gchar* type,
                            const gchar* name,
                            guint port)
{
  InfLocalPublisherInterface* iface;

  g_return_val_if_fail(INF_IS_LOCAL_PUBLISHER(publisher), NULL);
  g_return_val_if_fail(type != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail(port != 0, NULL);

  iface = INF_LOCAL_PUBLISHER_GET_IFACE(publisher);
  g_return_val_if_fail(iface->publish != NULL, NULL);

  return iface->publish(publisher, type, name, port);
}

/**
 * inf_local_publisher_unpublish:
 * @publisher: A #InfLocalPublisher.
 * @item: A published item obtained from inf_local_publisher_publish().
 *
 * Unpublishes @item so that it can no longer be found in the network.
 **/
void
inf_local_publisher_unpublish(InfLocalPublisher* publisher,
                              InfLocalPublisherItem* item)
{
  InfLocalPublisherInterface* iface;

  g_return_if_fail(INF_IS_LOCAL_PUBLISHER(publisher));
  g_return_if_fail(item != NULL);

  iface = INF_LOCAL_PUBLISHER_GET_IFACE(publisher);
  g_return_if_fail(iface->unpublish != NULL);

  iface->unpublish(publisher, item);
}

/* vim:set et sw=2 ts=2: */
