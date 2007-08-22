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

#ifndef __INF_TCP_CONNECTION_H__
#define __INF_TCP_CONNECTION_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_TCP_CONNECTION                 (inf_tcp_connection_get_type())
#define INF_TCP_CONNECTION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_TCP_CONNECTION, InfTcpConnection))
#define INF_TCP_CONNECTION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_TCP_CONNECTION, InfTcpConnectionClass))
#define INF_IS_TCP_CONNECTION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_TCP_CONNECTION))
#define INF_IS_TCP_CONNECTION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_TCP_CONNECTION))
#define INF_TCP_CONNECTION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_TCP_CONNECTION, InfTcpConnectionClass))

#define INF_TYPE_TCP_CONNECTION_STATUS          (inf_tcp_connection_status_get_type())

typedef struct _InfTcpConnection InfTcpConnection;
typedef struct _InfTcpConnectionClass InfTcpConnectionClass;

struct _InfTcpConnectionClass {
  GObjectClass parent_class;

  /* Signals */
  void (*sent)(InfTcpConnection* connection,
               gconstpointer data,
               guint len);

  void (*received)(InfTcpConnection* connection,
                   gconstpointer data,
                   guint len);

  void (*error)(InfTcpConnection* connection,
                GError* error);
};

struct _InfTcpConnection {
  GObject parent;
};

typedef enum _InfTcpConnectionStatus {
  INF_TCP_CONNECTION_CONNECTING,
  INF_TCP_CONNECTION_CONNECTED,
  INF_TCP_CONNECTION_CLOSED
} InfTcpConnectionStatus;

GType
inf_tcp_connection_status_get_type(void) G_GNUC_CONST;

GType
inf_tcp_connection_get_type(void) G_GNUC_CONST;

gboolean
inf_tcp_connection_open(InfTcpConnection* connection,
                        GError** error);

void
inf_tcp_connection_close(InfTcpConnection* connection);

void
inf_tcp_connection_send(InfTcpConnection* connection,
                        gconstpointer data,
                        guint len);

G_END_DECLS

#endif /* __INF_TCP_CONNECTION_H__ */

/* vim:set et sw=2 ts=2: */
