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

#ifndef __INF_DISCOVERY_H__
#define __INF_DISCOVERY_H__

#include <libinfinity/common/inf-xml-connection.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_DISCOVERY                 (inf_discovery_get_type())
#define INF_DISCOVERY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_DISCOVERY, InfDiscovery))
#define INF_IS_DISCOVERY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_DISCOVERY))
#define INF_DISCOVERY_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_DISCOVERY, InfDiscoveryIface))

typedef struct _InfDiscovery InfDiscovery;
typedef struct _InfDiscoveryIface InfDiscoveryIface;

/* TODO: Rename to InfDiscoveryItem */
typedef struct _InfDiscoveryInfo InfDiscoveryInfo;

typedef void(*InfDiscoveryResolvCompleteFunc)(InfDiscoveryInfo* info,
                                              InfXmlConnection* connection,
                                              gpointer user_data);

typedef void(*InfDiscoveryResolvErrorFunc)(InfDiscoveryInfo* info,
                                           const GError* error,
                                           gpointer user_data);

struct _InfDiscoveryIface {
  GTypeInterface parent;

  void (*discover)(InfDiscovery* discovery,
                   const gchar* type);

  GSList* (*get_discovered)(InfDiscovery* discovery,
                            const gchar* type);

  void (*resolve)(InfDiscovery* discovery,
                  InfDiscoveryInfo* info,
                  InfDiscoveryResolvCompleteFunc complete_func,
                  InfDiscoveryResolvErrorFunc error_func,
                  gpointer user_data);

  gchar*(*info_get_service_name)(InfDiscovery* discovery,
                                 InfDiscoveryInfo* info);

  const gchar*(*info_get_service_type)(InfDiscovery* discovery,
                                       InfDiscoveryInfo* info);

  /* Signals */
  void (*discovered)(InfDiscovery* discovery,
                     InfDiscoveryInfo* info);

  void (*undiscovered)(InfDiscovery* discovry,
                       InfDiscoveryInfo* info);
};

GType
inf_discovery_get_type(void) G_GNUC_CONST;

void
inf_discovery_discover(InfDiscovery* discovery,
                       const gchar* type);

GSList*
inf_discovery_get_discovered(InfDiscovery* discovery,
                             const gchar* type);

void
inf_discovery_resolve(InfDiscovery* discovery,
                      InfDiscoveryInfo* info,
                      InfDiscoveryResolvCompleteFunc complete_func,
                      InfDiscoveryResolvErrorFunc error_func,
                      gpointer user_data);

gchar*
inf_discovery_info_get_service_name(InfDiscovery* discovery,
                                    InfDiscoveryInfo* info);

const gchar*
inf_discovery_info_get_service_type(InfDiscovery* discovery,
                                    InfDiscoveryInfo* info);

void
inf_discovery_discovered(InfDiscovery* discovery,
                         InfDiscoveryInfo* info);

void
inf_discovery_undiscovered(InfDiscovery* discovery,
                           InfDiscoveryInfo* info);

G_END_DECLS

#endif /* __INF_DISCOVERY_H__ */

/* vim:set et sw=2 ts=2: */
