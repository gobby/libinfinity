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

#ifndef __INFD_TCP_SERVER_H__
#define __INFD_TCP_SERVER_H__

#include <libinfinity/common/inf-tcp-connection.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_TCP_SERVER                 (infd_tcp_server_get_type())
#define INFD_TCP_SERVER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_TCP_SERVER, InfdTcpServer))
#define INFD_TCP_SERVER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_TCP_SERVER, InfdTcpServerClass))
#define INFD_IS_TCP_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_TCP_SERVER))
#define INFD_IS_TCP_SERVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_TCP_SERVER))
#define INFD_TCP_SERVER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_TCP_SERVER, InfdTcpServerClass))

#define INFD_TYPE_TCP_SERVER_STATUS          (infd_tcp_server_status_get_type())

typedef struct _InfdTcpServer InfdTcpServer;
typedef struct _InfdTcpServerClass InfdTcpServerClass;

typedef enum _InfdTcpServerStatus {
  INFD_TCP_SERVER_CLOSED,
  INFD_TCP_SERVER_BOUND,
  INFD_TCP_SERVER_OPEN
} InfdTcpServerStatus;

struct _InfdTcpServerClass {
  GObjectClass parent_class;

  /* Signals */
  void (*new_connection)(InfdTcpServer* server,
                         InfTcpConnection* connection);

  void (*error)(InfdTcpServer* server,
                GError* error);
};

struct _InfdTcpServer {
  GObject parent;
};

GType
infd_tcp_server_status_get_type(void) G_GNUC_CONST;

GType
infd_tcp_server_get_type(void) G_GNUC_CONST;

gboolean
infd_tcp_server_bind(InfdTcpServer* server,
                     GError** error);

gboolean
infd_tcp_server_open(InfdTcpServer* server,
                     GError** error);

void
infd_tcp_server_close(InfdTcpServer* server);

void
infd_tcp_server_set_keepalive(InfdTcpServer* server,
                              const InfKeepalive* keepalive);

const InfKeepalive*
infd_tcp_server_get_keepalive(InfdTcpServer* server);

G_END_DECLS

#endif /* __INFD_TCP_SERVER_H__ */

/* vim:set et sw=2 ts=2: */
