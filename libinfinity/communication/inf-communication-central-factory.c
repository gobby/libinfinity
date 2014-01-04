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

static GObjectClass* parent_class;

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

/*
 * GType registration.
 */

static void
inf_communication_central_factory_class_init(gpointer g_class,
                                            gpointer class_data)
{
  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
}

static void
inf_communication_central_factory_factory_init(gpointer g_iface,
                                               gpointer iface_data)
{
  InfCommunicationFactoryIface* iface;
  iface = (InfCommunicationFactoryIface*)g_iface;

  iface->supports_method = inf_communication_central_factory_supports_method;
  iface->instantiate = inf_communication_central_factory_instantiate;
}

GType
inf_communication_central_factory_get_type(void)
{
  static GType central_factory_type = 0;

  if(!central_factory_type)
  {
    static const GTypeInfo central_factory_type_info = {
      sizeof(InfCommunicationCentralFactoryClass),   /* class_size */
      NULL,                                          /* base_init */
      NULL,                                          /* base_finalize */
      inf_communication_central_factory_class_init,  /* class_init */
      NULL,                                          /* class_finalize */
      NULL,                                          /* class_data */
      sizeof(InfCommunicationCentralFactory),        /* instance_size */
      0,                                             /* n_preallocs */
      NULL,                                          /* instance_init */
      NULL                                           /* value_table */
    };

    static const GInterfaceInfo factory_info = {
      inf_communication_central_factory_factory_init,
      NULL,
      NULL
    };

    central_factory_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfCommunicationCentralFactory",
      &central_factory_type_info,
      0
    );

    g_type_add_interface_static(
      central_factory_type,
      INF_COMMUNICATION_TYPE_FACTORY,
      &factory_info
    );
  }

  return central_factory_type;
}

/*
 * Public API
 */

/**
 * inf_communication_central_factory_get_default:
 *
 * Returns the default #InfCommunicationCentralFactory.
 *
 * Returns: A #InfCommunicationCentralFactory. It should not be unrefed or
 * freed.
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
