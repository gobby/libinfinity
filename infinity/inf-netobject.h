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

#ifndef __INF_NETOBJECT_H__
#define __INF_NETOBJECT_H__

#include <glib-object.h>

#include <libgnetwork/gnetwork-connection.h>

#include <libxml/tree.h>

G_BEGIN_DECLS

#define INF_NET_OBJECT_TYPE                 (inf_net_object_get_type())
#define INF_NET_OBJECT(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_NET_OBJECT_TYPE, InfNetObject))
#define INF_IS_NET_OBJECT(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_NET_OBJECT_TYPE))
#define INF_NET_OBJECT_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_NET_OBJECT_TYPE, InfNetObjectIface))

typedef struct _InfNetObject InfNetObject;
typedef struct _InfNetObjectIface InfNetObjectIface;

struct _InfNetObjectIface {
	GTypeInterface parent;

	/* Virtual Table */
	void (*received)(InfNetObject* object,
                         GNetworkConnection* conn,
                         const xmlNodePtr node);

	void (*sent)(InfNetObject* object,
                     GNetworkConnection* conn,
                     const xmlNodePtr node);
};

GType
inf_net_object_get_type(void) G_GNUC_CONST;

void
inf_net_object_received(InfNetObject* object,
                        GNetworkConnection* conn,
                        const xmlNodePtr node);

void
inf_net_object_sent(InfNetObject* object,
                    GNetworkConnection* conn,
                    const xmlNodePtr node);

G_END_DECLS

#endif /* __INF_NETOBJCET_H__ */
