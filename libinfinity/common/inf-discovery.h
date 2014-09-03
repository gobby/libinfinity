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

#ifndef __INF_DISCOVERY_H__
#define __INF_DISCOVERY_H__

#include <libinfinity/common/inf-xml-connection.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_DISCOVERY                 (inf_discovery_get_type())
#define INF_DISCOVERY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_DISCOVERY, InfDiscovery))
#define INF_IS_DISCOVERY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_DISCOVERY))
#define INF_DISCOVERY_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_DISCOVERY, InfDiscoveryInterface))

/**
 * InfDiscovery:
 *
 * #InfDiscovery is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfDiscovery InfDiscovery;
typedef struct _InfDiscoveryInterface InfDiscoveryInterface;

/**
 * InfDiscoveryInfo:
 *
 * #InfDiscoveryInfo is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfDiscoveryInfo InfDiscoveryInfo;

/**
 * InfDiscoveryResolvCompleteFunc:
 * @info: The resolved #InfDiscoveryInfo.
 * @connection: The resulting #InfXmlConnection.
 * @user_data: The user_data passed to inf_discovery_resolve().
 *
 * This callback is called when a call to inf_discovery_resolve() finished
 * successfully.
 */
typedef void(*InfDiscoveryResolvCompleteFunc)(InfDiscoveryInfo* info,
                                              InfXmlConnection* connection,
                                              gpointer user_data);

/**
 * InfDiscoveryResolvErrorFunc:
 * @info: The resolved #InfDiscoveryInfo.
 * @error: Reason for the failure.
 * @user_data: The user_data passed to inf_discovery_resolve().
 *
 * This callback is called when a call to inf_discovery_resolve() failed.
 */
typedef void(*InfDiscoveryResolvErrorFunc)(InfDiscoveryInfo* info,
                                           const GError* error,
                                           gpointer user_data);

/**
 * InfDiscoveryInterface:
 * @discover: Virtual function to start discovery of services of the given
 * type. If the discovery was already started ealier, then this function does
 * nothing.
 * @get_discovered: Virtual function to retrieve a list of
 * #InfDiscoveryInfo<!-- -->s that represent discovered services.
 * It needs to be freed by the caller via g_slist_free().
 * @resolve: Virtual function that attempts to resolve the given discovery
 * info. It guarantees to either call @complete_func or @error_func when the
 * process has finished.
 * @info_get_service_name: Returns the service name of the given
 * #InfDiscoveryInfo as a new string, to be freed by the caller with g_free().
 * @info_get_service_type: Returns the type of the discovered service of the
 * given #InfDiscoveryInfo as a static string.
 * @discovered: Default signal handler for the #InfDiscovery::discovered
 * signal.
 * @undiscovered: Default signal handler for the #InfDiscovery::undiscovered
 * signal.
 *
 * The virtual methods and default signal handlers of #InfDiscovery.
 * Implementing these allows discovering infinote servers.
 */
struct _InfDiscoveryInterface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/
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

  void (*undiscovered)(InfDiscovery* discovery,
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
