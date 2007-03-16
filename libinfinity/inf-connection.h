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

#ifndef __INF_CONNECTION_H__
#define __INF_CONNECTION_H__

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_CONNECTION                 (inf_connection_get_type())
#define INF_CONNECTION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_CONNECTION, InfConnection))
#define INF_IS_CONNECTION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_CONNECTION))
#define INF_CONNECTION_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_CONNECTION, InfConnectionIface))

#define INF_TYPE_CONNECTION_STATUS          (inf_connection_status_get_type())

typedef struct _InfConnection InfConnection;
typedef struct _InfConnectionIface InfConnectionIface;

typedef enum _InfConnectionStatus {
  INF_CONNECTION_CLOSED,
  INF_CONNECTION_CLOSING,
  INF_CONNECTION_OPEN,
  INF_CONNECTION_OPENING
} InfConnectionStatus;

struct _InfConnectionIface {
  GTypeInterface parent;

  /* Virtual table */
  void (*close)(InfConnection* connection);
  void (*send)(InfConnection* connection,
               xmlNodePtr xml);

  /* Signals */
  void (*sent)(InfConnection* connection,
               const xmlNodePtr xml);
  void (*received)(InfConnection* connection,
                   const xmlNodePtr xml);
};

GType
inf_connection_status_get_type(void) G_GNUC_CONST;

GType
inf_connection_get_type(void) G_GNUC_CONST;

void
inf_connection_close(InfConnection* connection);

void
inf_connection_send(InfConnection* connection,
                    xmlNodePtr xml);

void
inf_connection_sent(InfConnection* connection,
                    const xmlNodePtr xml);

void
inf_connection_received(InfConnection* connection,
                        const xmlNodePtr xml);

G_END_DECLS

#endif /* __INF_CONNECTION_H__ */
