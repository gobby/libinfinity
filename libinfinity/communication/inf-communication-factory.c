/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-communication-factory
 * @title: InfCommunicationFactory
 * @short_description: Creation of communication methods
 * @see_also: #InfCommunicationManager
 * @include: libinfinity/communication/inf-communication-factory.h
 * @stability: Unstable
 *
 * A #InfCommunicationFactory is used by the communication manager to create
 * #InfCommunicationMethod<!-- -->s. All a factory has to do is to tell
 * whether it supports a specific network and method name combination, and
 * create a corresponding #InfCommunicationMethod if it does.
 *
 * #InfCommunicationFactory<!-- -->s are added to a communication manager via
 * inf_communication_manager_add_factory(). The communication manager will
 * automatically use the factory if it needs to create a method it supports.
 **/

#include <libinfinity/communication/inf-communication-factory.h>

GType
inf_communication_factory_get_type(void)
{
  static GType communication_factory_type = 0;

  if(!communication_factory_type)
  {
    static const GTypeInfo communication_factory_info = {
      sizeof(InfCommunicationFactoryIface),    /* class_size */
      NULL,                                    /* base_init */
      NULL,                                    /* base_finalize */
      NULL,                                    /* class_init */
      NULL,                                    /* class_finalize */
      NULL,                                    /* class_data */
      0,                                       /* instance_size */
      0,                                       /* n_preallocs */
      NULL,                                    /* instance_init */
      NULL                                     /* value_table */
    };

    communication_factory_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfCommunicationFactory",
      &communication_factory_info,
      0
    );

    g_type_interface_add_prerequisite(
      communication_factory_type,
      G_TYPE_OBJECT
    );
  }

  return communication_factory_type;
}

/**
 * inf_communication_factory_supports_method:
 * @factory: A #InfCommunicationFactory.
 * @network: A network specifier, such as "tcp/ip" or "jabber".
 * @method_name: A method identifier, such as "central" or "groupchat".
 *
 * Returns whether @factory supports creating methods that implement
 * @method_name as communication method for connections on @network
 * (see #InfXmlConnection:network).
 *
 * Returns: Whether @factory supports the given network and method name.
 */
gboolean
inf_communication_factory_supports_method(InfCommunicationFactory* factory,
                                          const gchar* network,
                                          const gchar* method_name)
{
  InfCommunicationFactoryIface* iface;

  g_return_val_if_fail(INF_COMMUNICATION_IS_FACTORY(factory), FALSE);
  g_return_val_if_fail(network != NULL, FALSE);
  g_return_val_if_fail(method_name != NULL, FALSE);

  iface = INF_COMMUNICATION_FACTORY_GET_IFACE(factory);
  g_return_val_if_fail(iface->supports_method != NULL, FALSE);

  return iface->supports_method(factory, network, method_name);
}

/**
 * inf_communication_factory_instantiate:
 * @factory: A #InfCommunicationFactory.
 * @network: A network specifier, such as "tcp/ip" or "jabber".
 * @method_name: A method identifier, such as "central" or "groupchat".
 * @registry: A #InfCommunicationRegistry at which the created method can
 * register connections.
 * @group: The #InfCommunicationGroup for which to create the method.
 *
 * Creates a new #InfCommunicationMethod for @network and @method_name. The
 * factory needs to support that method, see
 * inf_communication_factory_supports_method().
 *
 * Returns: A new #InfCommunicationMethod.
 */
InfCommunicationMethod*
inf_communication_factory_instantiate(InfCommunicationFactory* factory,
                                      const gchar* network,
                                      const gchar* method_name,
                                      InfCommunicationRegistry* registry,
                                      InfCommunicationGroup* group)
{
  InfCommunicationFactoryIface* iface;

  g_return_val_if_fail(INF_COMMUNICATION_IS_FACTORY(factory), NULL);
  g_return_val_if_fail(network != NULL, NULL);
  g_return_val_if_fail(method_name != NULL, NULL);
  g_return_val_if_fail(INF_COMMUNICATION_IS_REGISTRY(registry), NULL);
  g_return_val_if_fail(INF_COMMUNICATION_IS_GROUP(group), NULL);

  iface = INF_COMMUNICATION_FACTORY_GET_IFACE(factory);
  g_return_val_if_fail(iface->instantiate != NULL, NULL);

  return iface->instantiate(factory, network, method_name, registry, group);
}

/* vim:set et sw=2 ts=2: */
