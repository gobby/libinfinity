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

#ifndef __INF_DISCOVERY_INFO_H__
#define __INF_DISCOVERY_INFO_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_DISCOVERY_INFO                 (inf_discovery_info_get_type())
#define INF_DISCOVERY_INFO(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_DISCOVERY_INFO, InfDiscoveryInfo))
#define INF_IS_DISCOVERY_INFO(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_DISCOVERY_INFO))
#define INF_DISCOVERY_INFO_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_DISCOVERY_INFO, InfDiscoveryInfoIface))

typedef struct _InfDiscoveryInfo InfDiscoveryInfo;
typedef struct _InfDiscoveryInfoIface InfDiscoveryInfoIface;

struct _InfDiscoveryInfoIface {
  GTypeInterface parent;

  /* Virtual table */
  const gchar*(*get_service_name)(InfDiscoveryInfo* info);
  const gchar*(*get_service_type)(InfDiscoveryInfo* info);
  const gchar*(*get_host_name)(InfDiscoveryInfo* info);
};

GType
inf_discovery_info_get_type(void) G_GNUC_CONST;

const gchar*
inf_discovery_info_get_service_name(InfDiscoveryInfo* info);

const gchar*
inf_discovery_info_get_service_type(InfDiscoveryInfo* info);

const gchar*
inf_discovery_info_get_host_name(InfDiscoveryInfo* info);

G_END_DECLS

#endif /* __INF_DISCOVERY_INFO_H__ */

/* vim:set et sw=2 ts=2: */
