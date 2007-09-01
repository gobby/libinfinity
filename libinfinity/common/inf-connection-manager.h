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

#ifndef __INF_CONNECTION_MANAGER_H__
#define __INF_CONNECTION_MANAGER_H__

#include <libinfinity/common/inf-net-object.h>
#include <libinfinity/common/inf-xml-connection.h>

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_CONNECTION_MANAGER                 (inf_connection_manager_get_type())
#define INF_CONNECTION_MANAGER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_CONNECTION_MANAGER, InfConnectionManager))
#define INF_CONNECTION_MANAGER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_CONNECTION_MANAGER, InfConnectionManagerClass))
#define INF_IS_CONNECTION_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_CONNECTION_MANAGER))
#define INF_IS_CONNECTION_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_CONNECTION_MANAGER))
#define INF_CONNECTION_MANAGER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_CONNECTION_MANAGER, InfConnectionManagerClass))

#define INF_TYPE_CONNECTION_MANAGER_GROUP           (inf_connection_manager_group_get_type())

typedef struct _InfConnectionManager InfConnectionManager;
typedef struct _InfConnectionManagerClass InfConnectionManagerClass;

typedef struct _InfConnectionManagerGroup InfConnectionManagerGroup;

struct _InfConnectionManagerClass {
  GObjectClass parent_class;
};

struct _InfConnectionManager {
  GObject parent;
};

GType
inf_connection_manager_group_get_type(void) G_GNUC_CONST;

GType
inf_connection_manager_get_type(void) G_GNUC_CONST;

InfConnectionManager*
inf_connection_manager_new(void);

InfConnectionManagerGroup*
inf_connection_manager_create_group(InfConnectionManager* manager,
                                    const gchar* group_name,
                                    InfNetObject* object);

InfConnectionManagerGroup*
inf_connection_manager_find_group_by_connection(InfConnectionManager* manager,
                                                const gchar* group_name,
                                                InfXmlConnection* connection);

void
inf_connection_manager_ref_group(InfConnectionManager* manager,
                                 InfConnectionManagerGroup* group);

void
inf_connection_manager_unref_group(InfConnectionManager* manager,
                                   InfConnectionManagerGroup* group);

void
inf_connection_manager_ref_connection(InfConnectionManager* manager,
                                      InfConnectionManagerGroup* group,
                                      InfXmlConnection* connection);

void
inf_connection_manager_unref_connection(InfConnectionManager* manager,
                                        InfConnectionManagerGroup* group,
                                        InfXmlConnection* connection);

InfNetObject*
inf_connection_manager_group_get_object(InfConnectionManagerGroup* group);

const gchar*
inf_connection_manager_group_get_name(InfConnectionManagerGroup* group);

gboolean
inf_connection_manager_has_connection(InfConnectionManager* manager,
                                      InfConnectionManagerGroup* group,
                                      InfXmlConnection* connection);

void
inf_connection_manager_send_to(InfConnectionManager* manager,
                               InfConnectionManagerGroup* group,
                               InfXmlConnection* connection,
                               xmlNodePtr xml);

void
inf_connection_manager_send_to_group(InfConnectionManager* manager,
                                     InfConnectionManagerGroup* group,
                                     InfXmlConnection* except,
                                     xmlNodePtr xml);

void
inf_connection_manager_send_multiple_to(InfConnectionManager* manager,
                                        InfConnectionManagerGroup* group,
                                        InfXmlConnection* connection,
                                        xmlNodePtr xml);

void
inf_connection_manager_cancel_outer(InfConnectionManager* manager,
                                    InfConnectionManagerGroup* group,
                                    InfXmlConnection* connection);

G_END_DECLS

#endif /* __INF_CONNECTION_MANAGER_H__ */

/* vim:set et sw=2 ts=2: */
