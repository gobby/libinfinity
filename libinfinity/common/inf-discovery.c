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
  GObjectClass* object_class;

  object_class = G_OBJECT_CLASS(g_class);

  if(!initialized)
  {
    discovery_signals[DISCOVERED] = g_signal_new(
      "discovered",
      G_OBJECT_CLASS_TYPE(object_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      G_STRUCT_OFFSET(InfDiscoveryIface, discovered),
      NULL, NULL,
      inf_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1,
      INF_TYPE_DISCOVERY_INFO
    );

    discovery_signals[UNDISCOVERED] = g_signal_new(
      "undiscovered",
      G_OBJECT_CLASS_TYPE(object_class),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      G_STRUCT_OFFSET(InfDiscoveryIface, undiscovered),
      NULL, NULL,
      inf_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1,
      INF_TYPE_DISCOVERY_INFO
    );
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

/** inf_discovery_publish:
 *
 * @discovery: A #InfDiscovery.
 * @type: The service type to publish, such as _http._tcp.
 * @name: The name of the service.
 *
 * Publishes a service through @discovery.
 **/
void
inf_discovery_publish(InfDiscovery* discovery,
                      const gchar* type,
                      const gchar* name)
{
  InfDiscoveryIface* iface;

  g_return_if_fail(INF_IS_DISCOVERY(discovery));
  g_return_if_fail(type != NULL);
  g_return_if_fail(name != NULL);

  iface = INF_DISCOVERY_GET_IFACE(discovery);
  g_return_if_fail(iface->publish != NULL);

  iface->publish(discovery, type, name);
}

/** inf_discovery_discover:
 *
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

/** inf_discovery_get_discovered:
 *
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

  iface->get_discovered(discovery, type);
}

/** inf_discovery_resolve:
 *
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
  g_return_if_fail(INF_IS_DISCOVERY_INFO(info));

  iface = INF_DISCOVERY_GET_IFACE(discovery);
  g_return_if_fail(iface->resolve != NULL);

  iface->resolve(discovery, info, complete_func, error_func, user_data);
}

/** inf_discovery_discovered:
 *
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
  g_return_if_fail(INF_IS_DISCOVERY_INFO(info));

  g_signal_emit(
    G_OBJECT(discovery),
    discovery_signals[DISCOVERED],
    g_quark_from_string(inf_discovery_info_get_service_type(info)),
    info
  );
}

/** inf_discovery_undiscovered:
 *
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
  g_return_if_fail(INF_IS_DISCOVERY_INFO(info));

  g_signal_emit(
    G_OBJECT(discovery),
    discovery_signals[UNDISCOVERED],
    g_quark_from_string(inf_discovery_info_get_service_type(info)),
    info
  );
}

/* vim:set et sw=2 ts=2: */
