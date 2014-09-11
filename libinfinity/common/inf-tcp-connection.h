/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TCP_CONNECTION_H__
#define __INF_TCP_CONNECTION_H__

#include <libinfinity/common/inf-name-resolver.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-keepalive.h>
#include <libinfinity/common/inf-io.h>

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

/**
 * InfTcpConnectionClass:
 * @sent: Default signal handler for the #InfTcpConnection::sent signal.
 * @received: Default signal handler for the #InfTcpConnection::received
 * signal.
 * @error: Default signal handler for the #InfTcpConnection::error signal.
 *
 * This structure contains the default signal handlers of #InfTcpConnection.
 */
struct _InfTcpConnectionClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/

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

/**
 * InfTcpConnection:
 *
 * #InfTcpConnection is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfTcpConnection {
  /*< private >*/
  GObject parent;
};

/**
 * InfTcpConnectionStatus:
 * @INF_TCP_CONNECTION_CONNECTING: A new connection is currently being
 * established.
 * @INF_TCP_CONNECTION_CONNECTED: The connection is ready to send and
 * receive data.
 * @INF_TCP_CONNECTION_CLOSED: The connection is closed. Before data can be
 * transmitted, it needs to be opened with inf_tcp_connection_open().
 *
 * #InfTcpConnectionStatus specifies the connection status of a
 * #InfTcpConnection.
 */
typedef enum _InfTcpConnectionStatus {
  INF_TCP_CONNECTION_CONNECTING,
  INF_TCP_CONNECTION_CONNECTED,
  INF_TCP_CONNECTION_CLOSED
} InfTcpConnectionStatus;

GType
inf_tcp_connection_status_get_type(void) G_GNUC_CONST;

GType
inf_tcp_connection_get_type(void) G_GNUC_CONST;

InfTcpConnection*
inf_tcp_connection_new(InfIo* io,
                       const InfIpAddress* remote_addr,
                       guint remote_port);

InfTcpConnection*
inf_tcp_connection_new_and_open(InfIo* io,
                                const InfIpAddress* remote_addr,
                                guint remote_port,
                                GError** error);

InfTcpConnection*
inf_tcp_connection_new_resolve(InfIo* io,
                               InfNameResolver* resolver);

gboolean
inf_tcp_connection_open(InfTcpConnection* connection,
                        GError** error);

void
inf_tcp_connection_close(InfTcpConnection* connection);

void
inf_tcp_connection_send(InfTcpConnection* connection,
                        gconstpointer data,
                        guint len);

InfIpAddress*
inf_tcp_connection_get_remote_address(InfTcpConnection* connection);

guint
inf_tcp_connection_get_remote_port(InfTcpConnection* connection);

gboolean
inf_tcp_connection_set_keepalive(InfTcpConnection* connection,
                                 const InfKeepalive* keepalive,
                                 GError** error);

const InfKeepalive*
inf_tcp_connection_get_keepalive(InfTcpConnection* connection);

G_END_DECLS

#endif /* __INF_TCP_CONNECTION_H__ */

/* vim:set et sw=2 ts=2: */
