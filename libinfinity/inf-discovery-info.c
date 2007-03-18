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

#include <libinfinity/inf-discovery-info.h>

GType
inf_discovery_info_get_type(void)
{
  static GType discovery_info_type = 0;

  if(!discovery_info_type)
  {
    static const GTypeInfo discovery_info_info = {
      sizeof(InfDiscoveryInfoIface),    /* class_size */
      NULL,                             /* base_init */
      NULL,                             /* base_finalize */
      NULL,                             /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      0,                                /* instance_size */
      0,                                /* n_preallocs */
      NULL,                             /* instance_init */
      NULL                              /* value_table */
    };

    discovery_info_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfDiscoveryInfo",
      &discovery_info_info,
      0
    );

    g_type_interface_add_prerequisite(discovery_info_type, G_TYPE_OBJECT);
  }

  return discovery_info_type;
}

/** inf_discovery_info_get_service_name:
 *
 * @info: A #InfDiscoveryInfo.
 *
 * Returns the name of the discovered service.
 *
 * Return Value: The service name.
 **/
const gchar*
inf_discovery_info_get_service_name(InfDiscoveryInfo* info)
{
  InfDiscoveryInfoIface* iface;

  g_return_val_if_fail(INF_IS_DISCOVERY_INFO(info), NULL);

  iface = INF_DISCOVERY_INFO_GET_IFACE(info);
  g_return_val_if_fail(iface->get_service_name != NULL, NULL);

  return iface->get_service_name(info);
}

/** inf_discovery_info_get_service_type:
 *
 * @info: A #InfDiscoveryInfo.
 *
 * Returns the type of the discovered service.
 *
 * Return Value: The service type.
 **/
const gchar*
inf_discovery_info_get_service_type(InfDiscoveryInfo* info)
{
  InfDiscoveryInfoIface* iface;

  g_return_val_if_fail(INF_IS_DISCOVERY_INFO(info), NULL);

  iface = INF_DISCOVERY_INFO_GET_IFACE(info);
  g_return_val_if_fail(iface->get_service_type != NULL, NULL);

  return iface->get_service_type(info);
}

/** inf_discovery_info_get_host_name:
 *
 * @info: A #InfDiscoveryInfo.
 *
 * Returns the name of the host that offers the discovered service.
 *
 * Return Value: The name of the host offering the service.
 **/
const gchar*
inf_discovery_info_get_host_name(InfDiscoveryInfo* info)
{
  InfDiscoveryInfoIface* iface;

  g_return_val_if_fail(INF_IS_DISCOVERY_INFO(info), NULL);

  iface = INF_DISCOVERY_INFO_GET_IFACE(info);
  g_return_val_if_fail(iface->get_host_name != NULL, NULL);

  return iface->get_host_name(info);
}
