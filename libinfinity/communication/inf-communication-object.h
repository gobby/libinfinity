/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_COMMUNICATION_OBJECT_H__
#define __INF_COMMUNICATION_OBJECT_H__

#include <libinfinity/common/inf-xml-connection.h>

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_COMMUNICATION_TYPE_OBJECT                 (inf_communication_object_get_type())
#define INF_COMMUNICATION_OBJECT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_COMMUNICATION_TYPE_OBJECT, InfCommunicationObject))
#define INF_COMMUNICATION_IS_OBJECT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_COMMUNICATION_TYPE_OBJECT))
#define INF_COMMUNICATION_OBJECT_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_COMMUNICATION_TYPE_OBJECT, InfCommunicationObjectIface))

#define INF_COMMUNICATION_TYPE_SCOPE                  (inf_communication_scope_get_type())

/**
 * InfCommunicationScope:
 * @INF_COMMUNICATION_SCOPE_PTP: The message is sent from one group member to
 * another.
 * @INF_COMMUNICATION_SCOPE_GROUP: The message is sent to all group members.
 *
 * #InfCommunicationScope specifies to which hosts a message belongs.
 */
typedef enum _InfCommunicationScope {
  INF_COMMUNICATION_SCOPE_PTP,
  INF_COMMUNICATION_SCOPE_GROUP
} InfCommunicationScope;

/**
 * InfCommunicationObject:
 *
 * #InfCommunicationObject is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfCommunicationObject InfCommunicationObject;
typedef struct _InfCommunicationObjectIface InfCommunicationObjectIface;

/**
 * InfCommunicationObjectIface:
 * @received: Called when a message for the group related to this
 * #InfCommunicationObject was received.
 * @enqueued: Called when a message to be sent to another group member has
 * been enqueued, which means sending it can no longer be cancelled via
 * inf_connection_manager_group_clear_queue().
 * @sent: Called when a message has been sent to another group member of the
 * group related no this #InfCommunicationObject.
 *
 * The virtual methods of #InfCommunicationObject. These are called by the
 * #InfConnectionManager when appropriate.
 */
struct _InfCommunicationObjectIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/
  InfCommunicationScope (*received)(InfCommunicationObject* object,
                                    InfXmlConnection* conn,
                                    xmlNodePtr node,
                                    GError** error);

  void (*enqueued)(InfCommunicationObject* object,
                   InfXmlConnection* conn,
                   xmlNodePtr node);

  void (*sent)(InfCommunicationObject* object,
               InfXmlConnection* conn,
               xmlNodePtr node);
};

GType
inf_communication_scope_get_type(void) G_GNUC_CONST;

GType
inf_communication_object_get_type(void) G_GNUC_CONST;

InfCommunicationScope
inf_communication_object_received(InfCommunicationObject* object,
                                  InfXmlConnection* conn,
                                  xmlNodePtr node,
                                  GError** error);

void
inf_communication_object_enqueued(InfCommunicationObject* object,
                                  InfXmlConnection* conn,
                                  xmlNodePtr node);

void
inf_communication_object_sent(InfCommunicationObject* object,
                              InfXmlConnection* conn,
                              xmlNodePtr node);

G_END_DECLS

#endif /* __INF_COMMUNICATION_OBJECT_H__ */

/* vim:set et sw=2 ts=2: */
