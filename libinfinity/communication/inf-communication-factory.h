/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_COMMUNICATION_FACTORY_H__
#define __INF_COMMUNICATION_FACTORY_H__

#include <libinfinity/communication/inf-communication-method.h>
#include <libinfinity/communication/inf-communication-registry.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_COMMUNICATION_TYPE_FACTORY                 (inf_communication_factory_get_type())
#define INF_COMMUNICATION_FACTORY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_COMMUNICATION_TYPE_FACTORY, InfCommunicationFactory))
#define INF_COMMUNICATION_IS_FACTORY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_COMMUNICATION_TYPE_FACTORY))
#define INF_COMMUNICATION_FACTORY_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_COMMUNICATION_TYPE_FACTORY, InfCommunicationFactoryIface))

/**
 * InfCommunicationFactory:
 *
 * #InfCommunicationFactory is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfCommunicationFactory InfCommunicationFactory;
typedef struct _InfCommunicationFactoryIface InfCommunicationFactoryIface;

/**
 * InfCommunicationFactoryIface:
 * @supports_method: Returns whether the given method_name is supported for
 * the network in question by the factory.
 * @instantiate: Instantiates a communication method for the given method
 * name, handling communication for the passed group.
 *
 * The virtual methods of #InfCommunicationFactory. These handle instantiating
 * a #InfCommunicationMethod for a #InfCommunicationGroup.
 */
struct _InfCommunicationFactoryIface {
  /*< private >*/
  GTypeInterface parent;

  gboolean (*supports_method)(InfCommunicationFactory* factory,
                              const gchar* network,
                              const gchar* method_name);

  InfCommunicationMethod* (*instantiate)(InfCommunicationFactory* factory,
                                         const gchar* network,
                                         const gchar* method_name,
                                         InfCommunicationRegistry* registry,
                                         InfCommunicationGroup* group);
};

GType
inf_communication_factory_get_type(void) G_GNUC_CONST;

gboolean
inf_communication_factory_supports_method(InfCommunicationFactory* factory,
                                          const gchar* network,
                                          const gchar* method_name);

InfCommunicationMethod*
inf_communication_factory_instantiate(InfCommunicationFactory* factory,
                                      const gchar* network,
                                      const gchar* method_name,
                                      InfCommunicationRegistry* registry,
                                      InfCommunicationGroup* group);

G_END_DECLS

#endif /* __INF_COMMUNICATION_FACTORY_H__ */

/* vim:set et sw=2 ts=2: */
