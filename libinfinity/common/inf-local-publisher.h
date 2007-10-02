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

#ifndef __INF_LOCAL_PUBLISHER_H__
#define __INF_LOCAL_PUBLISHER_H__

#include <libinfinity/common/inf-xml-connection.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_LOCAL_PUBLISHER                 (inf_local_publisher_get_type())
#define INF_LOCAL_PUBLISHER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_LOCAL_PUBLISHER, InfLocalPublisher))
#define INF_IS_LOCAL_PUBLISHER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_LOCAL_PUBLISHER))
#define INF_LOCAL_PUBLISHER_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_LOCAL_PUBLISHER, InfLocalPublisherIface))

typedef struct _InfLocalPublisher InfLocalPublisher;
typedef struct _InfLocalPublisherIface InfLocalPublisherIface;

typedef struct _InfLocalPublisherItem InfLocalPublisherItem;

struct _InfLocalPublisherIface {
  GTypeInterface parent;

  /* Virtual table */
  InfLocalPublisherItem* (*publish)(InfLocalPublisher* publisher,
                                    const gchar* type,
                                    const gchar* name,
                                    guint port);

  void (*unpublish)(InfLocalPublisher* publisher,
                    InfLocalPublisherItem* item);
};

GType
inf_local_publisher_get_type(void) G_GNUC_CONST;

InfLocalPublisherItem*
inf_local_publisher_publish(InfLocalPublisher* publisher,
                            const gchar* type,
                            const gchar* name,
                            guint port);

void
inf_local_publisher_unpublish(InfLocalPublisher* publisher,
                              InfLocalPublisherItem* item);

G_END_DECLS

#endif /* __INF_LOCAL_PUBLISHER_H__ */

/* vim:set et sw=2 ts=2: */
