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

#ifndef __INF_NET_OBJECT_H__
#define __INF_NET_OBJECT_H__

#include <libinfinity/common/inf-xml-connection.h>

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_NET_OBJECT                 (inf_net_object_get_type())
#define INF_NET_OBJECT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_NET_OBJECT, InfNetObject))
#define INF_IS_NET_OBJECT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_NET_OBJECT))
#define INF_NET_OBJECT_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_NET_OBJECT, InfNetObjectIface))

/**
 * InfNetObject:
 *
 * #InfNetObject is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfNetObject InfNetObject;
typedef struct _InfNetObjectIface InfNetObjectIface;

/**
 * InfNetObjectIface:
 * @received: Called when a message for the group related to this
 * #InfNetObject was received.
 * @enqueued: Called when a message to be sent to another group member has
 * been enqueued, which means sending it can no longer be cancelled via
 * inf_connection_manager_group_clear_queue().
 * @sent: Called when a message has been sent to another group member of the
 * group related no this #InfNetObject.
 *
 * The virtual methods of #InfNetObject. These are called by the
 * #InfConnectionManager when appropriate.
 */
struct _InfNetObjectIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/
  gboolean (*received)(InfNetObject* object,
                       InfXmlConnection* conn,
                       xmlNodePtr node,
                       GError** error);

  void (*enqueued)(InfNetObject* object,
                   InfXmlConnection* conn,
                   xmlNodePtr node);

  void (*sent)(InfNetObject* object,
               InfXmlConnection* conn,
               xmlNodePtr node);
};

GType
inf_net_object_get_type(void) G_GNUC_CONST;

gboolean
inf_net_object_received(InfNetObject* object,
                        InfXmlConnection* conn,
                        xmlNodePtr node,
                        GError** error);

void
inf_net_object_enqueued(InfNetObject* object,
                        InfXmlConnection* conn,
                        xmlNodePtr node);

void
inf_net_object_sent(InfNetObject* object,
                    InfXmlConnection* conn,
                    xmlNodePtr node);

G_END_DECLS

#endif /* __INF_NET_OBJECT_H__ */

/* vim:set et sw=2 ts=2: */
