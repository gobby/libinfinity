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

#include <libinfinity/inf-net-object.h>
#include <libinfinity/inf-xml-connection.h>

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_CONNECTION_MANAGER                 (inf_connection_manager_get_type())
#define INF_CONNECTION_MANAGER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_CONNECTION_MANAGER, InfConnectionManager))
#define INF_CONNECTION_MANAGER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_CONNECTION_MANAGER, InfConnectionManagerClass))
#define INF_IS_CONNECTION_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_CONNECTION_MANAGER))
#define INF_IS_CONNECTION_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_CONNECTION_MANAGER))
#define INF_CONNECTION_MANAGER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_CONNECTION_MANAGER, InfConnectionManagerClass))

typedef struct _InfConnectionManager InfConnectionManager;
typedef struct _InfConnectionManagerClass InfConnectionManagerClass;

struct _InfConnectionManagerClass {
  GObjectClass parent_class;
};

struct _InfConnectionManager {
  GObject parent;
};

GType
inf_connection_manager_get_type(void) G_GNUC_CONST;

InfConnectionManager*
inf_connection_manager_new(void);

void
inf_connection_manager_add_connection(InfConnectionManager* manager,
                                      InfXmlConnection* connection);

gboolean
inf_connection_manager_has_connection(InfConnectionManager* manager,
                                      InfXmlConnection* connection);

#if 0
GNetworkTcpXmlConnection*
inf_connection_manager_get_by_address(InfConnectionManager* manager,
                                      const GNetworkIpAddress* address,
                                      guint port);

GNetworkTcpXmlConnection*
inf_connection_manager_get_by_hostname(InfConnectionManager* manager,
                                       const gchar* hostname,
                                       guint port);
#endif

void
inf_connection_manager_add_object(InfConnectionManager* manager,
                                  InfXmlConnection* inf_conn,
                                  InfNetObject* object,
                                  const gchar* identifier);

void
inf_connection_manager_remove_object(InfConnectionManager* manager,
                                     InfXmlConnection* inf_conn,
                                     InfNetObject* object);

void
inf_connection_manager_send(InfConnectionManager* manager,
                            InfXmlConnection* inf_conn,
                            InfNetObject* object,
                            xmlNodePtr message);

void
inf_connection_manager_send_multiple(InfConnectionManager* manager,
                                     InfXmlConnection* inf_conn,
                                     InfNetObject* object,
                                     xmlNodePtr messages);

void
inf_connection_manager_cancel_outer(InfConnectionManager* manager,
                                    InfXmlConnection* inf_conn,
                                    InfNetObject* object);

G_END_DECLS

#endif /* __INF_CONNECTION_MANAGER_H__ */
