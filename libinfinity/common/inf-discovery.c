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
 * SECTION:inf-discovery
 * @title: InfDiscovery
 * @short_description: Discovery of remote services
 * @include: libinfinity/common/inf-discovery.h
 * @see_also: #InfDiscoveryAvahi
 * @stability: Unstable
 *
 * #InfDiscovery provides a common interface for the discovery of services.
 * Discovered services are represented by #InfDiscoveryInfo<!-- --> objects
 * and can be used to query the name of the discovered service.
 *
 * To create a connection to the host providing a discovered service, use
 * inf_discovery_resolve().
 */

#include <libinfinity/common/inf-discovery.h>
#include <libinfinity/inf-marshal.h>

enum {
  DISCOVERED,
  UNDISCOVERED,

  LAST_SIGNAL
};

static guint discovery_signals[LAST_SIGNAL];

static void
inf_discovery_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    /**
     * InfDiscovery::discovered:
     * @discoverer: The #InfDiscovery object discovering something
     * @info: The #InfDiscoveryInfo describing the discovered service
     *
     * This signal is detailed. The detail is the name of the service that has
     * been discovered, so you can connect to
     * &quot;discovered::<emphasis>my-service-name</emphasis>&quot; if you are
     * only interested in a particular service.
     */
    discovery_signals[DISCOVERED] = g_signal_new(
      "discovered",
      INF_TYPE_DISCOVERY,
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      G_STRUCT_OFFSET(InfDiscoveryIface, discovered),
      NULL, NULL,
      inf_marshal_VOID__POINTER,
      G_TYPE_NONE,
      1,
      G_TYPE_POINTER /* InfDiscoveryInfo* */
    );

    /**
     * InfDiscovery::undiscovered:
     * @discoverer: The #InfDiscovery object undiscovering something
     * @info: The #InfDiscoveryInfo describing the undiscovered service
     * 
     * This signal is emitted if a previously discovered service is no longer
     * available.
     *
     * This signal is detailed. The detail is the name of the service that has
     * been undiscovered, so you can connect to
     * &quot;undiscovered::<emphasis>my-service-name</emphasis>&quot; if you
     * are only interested in a particular service.
     */
    discovery_signals[UNDISCOVERED] = g_signal_new(
      "undiscovered",
      INF_TYPE_DISCOVERY,
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      G_STRUCT_OFFSET(InfDiscoveryIface, undiscovered),
      NULL, NULL,
      inf_marshal_VOID__POINTER,
      G_TYPE_NONE,
      1,
      G_TYPE_POINTER
    );

    initialized = TRUE;
  }
}

GType
inf_discovery_get_type(void)
{
  static GType discovery_type = 0;

  if(!discovery_type)
  {
    static const GTypeInfo discovery_info = {
      sizeof(InfDiscoveryIface),     /* class_size */
      inf_discovery_base_init,       /* base_init */
      NULL,                          /* base_finalize */
      NULL,                          /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      0,                             /* instance_size */
      0,                             /* n_preallocs */
      NULL,                          /* instance_init */
      NULL                           /* value_table */
    };

    discovery_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfDiscovery",
      &discovery_info,
      0
    );

    g_type_interface_add_prerequisite(discovery_type, G_TYPE_OBJECT);
  }

  return discovery_type;
}

/**
 * inf_discovery_discover:
 * @discovery: A #InfDiscovery.
 * @type: The service type to discover.
 *
 * Starts the discovery of the given service type. Whenever a service of
 * this type is discovered, the "discovered" signal is emitted. If the
 * service disappears, the "undiscovered" signal is emitted. This can be
 * called more than once for the same type, but only the first call has
 * an effect.
 *
 * Note also that implementations of #InfDiscovery might restrict the
 * service types that can be discovered.
 **/
void
inf_discovery_discover(InfDiscovery* discovery,
                       const gchar* type)
{
  InfDiscoveryIface* iface;

  g_return_if_fail(INF_IS_DISCOVERY(discovery));
  g_return_if_fail(type != NULL);

  iface = INF_DISCOVERY_GET_IFACE(discovery);
  g_return_if_fail(iface->discover != NULL);

  iface->discover(discovery, type);
}

/**
 * inf_discovery_get_discovered:
 * @discovery: A #InfDiscovery.
 * @type: The service type of which to get discovered infos for.
 *
 * Returns a list of discovered #InfDiscoveryInfo for the given type.
 *
 * Return Value: A newly allocated list that needs to be freed with
 * g_slist_free().
 **/
GSList*
inf_discovery_get_discovered(InfDiscovery* discovery,
                             const gchar* type)
{
  InfDiscoveryIface* iface;

  g_return_val_if_fail(INF_IS_DISCOVERY(discovery), NULL);
  g_return_val_if_fail(type != NULL, NULL);

  iface = INF_DISCOVERY_GET_IFACE(discovery);
  g_return_val_if_fail(iface->get_discovered != NULL, NULL);

  return iface->get_discovered(discovery, type);
}

/**
 * inf_discovery_resolve:
 * @discovery: A #InfDiscovery.
 * @info: A #InfDiscoveryInfo discovered by @discovery.
 * @complete_func: A callback that will be called when the resolving process
 * has completed.
 * @error_func: A callback that will be called when an error has occured.
 * @user_data: Extra data to pass to @complete_func and @error_func.
 *
 * Attempts to resolve @info. Resolving a #InfDiscoveryInfo means creating
 * a #InfXmlConnection to the publisher. The connection might not be
 * open when @complete_func runs. This will call either @complete_func
 * or @error_func, but not both.
 **/
void
inf_discovery_resolve(InfDiscovery* discovery,
                      InfDiscoveryInfo* info,
                      InfDiscoveryResolvCompleteFunc complete_func,
                      InfDiscoveryResolvErrorFunc error_func,
                      gpointer user_data)
{
  InfDiscoveryIface* iface;

  g_return_if_fail(INF_IS_DISCOVERY(discovery));
  g_return_if_fail(info != NULL);

  iface = INF_DISCOVERY_GET_IFACE(discovery);
  g_return_if_fail(iface->resolve != NULL);

  iface->resolve(discovery, info, complete_func, error_func, user_data);
}

/**
 * inf_discovery_info_get_service_name:
 * @discovery: A #InfDiscovery.
 * @info: A #InfDiscoveryInfo discovered by @discovery.
 *
 * Returns the service name of the discovered @info.
 *
 * Return Value: A string owned by @discovery.
 **/
gchar*
inf_discovery_info_get_service_name(InfDiscovery* discovery,
                                    InfDiscoveryInfo* info)
{
  InfDiscoveryIface* iface;

  g_return_val_if_fail(INF_IS_DISCOVERY(discovery), NULL);
  g_return_val_if_fail(info != NULL, NULL);

  iface = INF_DISCOVERY_GET_IFACE(discovery);
  g_return_val_if_fail(iface->info_get_service_name != NULL, NULL);

  return iface->info_get_service_name(discovery, info);
}

/**
 * inf_discovery_info_get_service_type:
 * @discovery: A #InfDiscovery.
 * @info: A #InfDiscoveryInfo discovered by @discovery.
 *
 * Returns the service type of the discovered @info.
 *
 * Return Value: A string owned by @discovery.
 **/
const gchar*
inf_discovery_info_get_service_type(InfDiscovery* discovery,
                                    InfDiscoveryInfo* info)
{
  InfDiscoveryIface* iface;

  g_return_val_if_fail(INF_IS_DISCOVERY(discovery), NULL);
  g_return_val_if_fail(info != NULL, NULL);

  iface = INF_DISCOVERY_GET_IFACE(discovery);
  g_return_val_if_fail(iface->info_get_service_type != NULL, NULL);

  return iface->info_get_service_type(discovery, info);
}

/**
 * inf_discovery_discovered:
 * @discovery: A #InfDiscovery.
 * @info: The discovered #InfDiscoveryInfo.
 *
 * Emits the "discovered" signal on @discovery.
 **/
void
inf_discovery_discovered(InfDiscovery* discovery,
                         InfDiscoveryInfo* info)
{
  g_return_if_fail(INF_IS_DISCOVERY(discovery));
  g_return_if_fail(info != NULL);

  g_signal_emit(
    G_OBJECT(discovery),
    discovery_signals[DISCOVERED],
    g_quark_from_string(inf_discovery_info_get_service_type(discovery, info)),
    info
  );
}

/**
 * inf_discovery_undiscovered:
 * @discovery: A #InfDiscovery.
 * @info: The undiscovered @InfDiscoveryInfo.
 *
 * Emits the "undiscovered" signal on @discovery.
 **/
void
inf_discovery_undiscovered(InfDiscovery* discovery,
                           InfDiscoveryInfo* info)
{
  g_return_if_fail(INF_IS_DISCOVERY(discovery));
  g_return_if_fail(info != NULL);

  g_signal_emit(
    G_OBJECT(discovery),
    discovery_signals[UNDISCOVERED],
    g_quark_from_string(inf_discovery_info_get_service_type(discovery, info)),
    info
  );
}

/* vim:set et sw=2 ts=2: */
