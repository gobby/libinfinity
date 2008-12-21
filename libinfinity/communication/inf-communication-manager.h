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

#ifndef __INF_COMMUNICATION_MANAGER_H__
#define __INF_COMMUNICATION_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_COMMUNICTATION_TYPE_MANAGER                (inf_communication_manager_get_type())
#define INF_COMMUNICATION_MANAGER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_COMMUNICATION_TYPE_MANAGER, InfCommunicationManager))
#define INF_COMMUNICATION_MANAGER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_COMMUNICATION_TYPE_MANAGER, InfCommunicationManagerClass))
#define INF_COMMUNICATION_IS_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_COMMUNICATION_TYPE_MANAGER))
#define INF_COMMUNICATION_IS_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_COMMUNICATION_TYPE_MANAGER))
#define INF_COMMUNICATION_MANAGER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_COMMUNICATION_TYPE_MANAGER, InfCommunicationManagerClass))

typedef struct _InfCommunicationManager InfCommunicationGroup;
typedef struct _InfCommunicationManagerClass InfCommunicationGroupClass;

/**
 * InfCommunicationManagerClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfCommunicationManagerClass {
  /*< private >*/
  GObjectClass parent;
};

/**
 * InfCommunicationManager:
 *
 * #InfCommunicationManager is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfCommunicationManager {
  /*< private >*/
  GObject parent_instance;
};

GType
inf_communication_manager_get_type(void) G_GNUC_CONST;

InfCommunicationHostedGroup*
inf_communication_manager_open_group(InfCommunicationManager* manager,
                                     const gchar* group_name,
                                     /* methods[0] primary, others fallback */
                                     const gchar* const* methods);

InfCommunicationJoinedGroup*
inf_communication_manager_join_group(InfCommunicationManager* manager,
                                     const gchar* group_name,
                                     InfXmlConnection* publisher_conn,
                                     const gchar* method);

void
inf_communication_manager_add_factory(InfCommunicationManager* manager,
                                      InfCommunicationFactory* factory);

G_END_DECLS

#endif /* __INF_COMMUNICATION_MANAGER_H__ */

/* vim:set et sw=2 ts=2: */
