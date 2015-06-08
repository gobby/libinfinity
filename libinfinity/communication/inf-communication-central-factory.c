/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-communication-central-factory
 * @title: InfCommunicationCentralFactory
 * @short_description: Factory for #InfCommunicationCentralMethod methods
 * @include: libinfinity/communication/inf-communication-central-factory.h
 * @stability: Unstable
 *
 * #InfCommunicationCentralFactory implements #InfCommunicationFactory. It
 * supports the "central" method on all networks by instantiating
 * #InfCommunicationCentralMethod.
 **/

#include <libinfinity/communication/inf-communication-central-factory.h>
#include <libinfinity/communication/inf-communication-central-method.h>

#include <string.h>

static void inf_communication_central_factory_factory_iface_init(InfCommunicationFactoryInterface* iface);
G_DEFINE_TYPE_WITH_CODE(InfCommunicationCentralFactory, inf_communication_central_factory, G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE(INF_COMMUNICATION_TYPE_FACTORY, inf_communication_central_factory_factory_iface_init))

static gboolean
inf_communication_central_factory_supports_method(InfCommunicationFactory* fc,
                                                  const gchar* network,
                                                  const gchar* method_name)
{
  return strcmp(method_name, "central") == 0;
}

static InfCommunicationMethod*
inf_communication_central_factory_instantiate(InfCommunicationFactory* fc,
                                              const gchar* network,
                                              const gchar* method_name,
                                              InfCommunicationRegistry* reg,
                                              InfCommunicationGroup* group)
{
  g_assert(strcmp(method_name, "central") == 0);

  return g_object_new(
    INF_COMMUNICATION_TYPE_CENTRAL_METHOD,
    "registry", reg,
    "group", group,
    NULL
  );
}

static void
inf_communication_central_factory_init(
  InfCommunicationCentralFactory* factory)
{
}

static void
inf_communication_central_factory_class_init(
  InfCommunicationCentralFactoryClass* factory_class)
{
}

static void
inf_communication_central_factory_factory_iface_init(
  InfCommunicationFactoryInterface* iface)
{
  iface->supports_method = inf_communication_central_factory_supports_method;
  iface->instantiate = inf_communication_central_factory_instantiate;
}

/*
 * Public API
 */

/**
 * inf_communication_central_factory_get_default:
 *
 * Returns the default #InfCommunicationCentralFactory.
 *
 * Returns: (transfer none): A #InfCommunicationCentralFactory. It should not
 * be unrefed or freed.
 */

InfCommunicationFactory*
inf_communication_central_factory_get_default(void)
{
  /* TODO: Thread safety */
  static InfCommunicationFactory* default_central_factory;
  if(default_central_factory == NULL)
  {
    default_central_factory =
      g_object_new(INF_COMMUNICATION_TYPE_CENTRAL_FACTORY, NULL);
  }

  return default_central_factory;
}

/* vim:set et sw=2 ts=2: */
