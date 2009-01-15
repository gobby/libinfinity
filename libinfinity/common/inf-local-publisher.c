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
 * SECTION:inf-local-publisher
 * @short_description: Publish services on the local network
 * @include: libinfinity/common/inf-local-publisher.h
 * @see_also: #InfDiscoveryAvahi
 *
 * #InfLocalPublisher provides a common interface to publish services on the
 * local network.
 */

#include <libinfinity/common/inf-local-publisher.h>

static void
inf_local_publisher_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;
  if(!initialized)
  {
    initialized = TRUE;
  }
}

GType
inf_local_publisher_get_type(void)
{
  static GType local_publisher_type = 0;

  if(!local_publisher_type)
  {
    static const GTypeInfo local_publisher_info = {
      sizeof(InfLocalPublisherIface),     /* class_size */
      inf_local_publisher_base_init,      /* base_init */
      NULL,                               /* base_finalize */
      NULL,                               /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      0,                                  /* instance_size */
      0,                                  /* n_preallocs */
      NULL,                               /* instance_init */
      NULL                                /* value_table */
    };

    local_publisher_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfLocalPublisher",
      &local_publisher_info,
      0
    );

    g_type_interface_add_prerequisite(local_publisher_type, G_TYPE_OBJECT);
  }

  return local_publisher_type;
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
 * Return Value: A #InfLocalPublisherItem that can be used to unpublish
 * the service again.
 **/
InfLocalPublisherItem*
inf_local_publisher_publish(InfLocalPublisher* publisher,
                            const gchar* type,
                            const gchar* name,
                            guint port)
{
  InfLocalPublisherIface* iface;

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
  InfLocalPublisherIface* iface;

  g_return_if_fail(INF_IS_LOCAL_PUBLISHER(publisher));
  g_return_if_fail(item != NULL);

  iface = INF_LOCAL_PUBLISHER_GET_IFACE(publisher);
  g_return_if_fail(iface->unpublish != NULL);

  iface->unpublish(publisher, item);
}

/* vim:set et sw=2 ts=2: */
