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

#ifndef __INFD_SERVER_H__
#define __INFD_SERVER_H__

#include <libinfinity/server/infd-directory.h>

#include <libgnetwork/gnetwork-tcp-server.h>
#include <libgnetwork/gnetwork-server.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_SERVER                 (infd_server_get_type())
#define INFD_SERVER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_SERVER, InfdServer))
#define INFD_SERVER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_SERVER, InfdServerClass))
#define INFD_IS_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_SERVER))
#define INFD_IS_SERVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_SERVER))
#define INFD_SERVER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_SERVER, InfdServerClass))

typedef struct _InfdServer InfdServer;
typedef struct _InfdServerClass InfdServerClass;

struct _InfdServerClass {
  GObjectClass parent_class;
};

struct _InfdServer {
  GObject parent;
};

GType
infd_server_get_type(void) G_GNUC_CONST;

InfdServer*
infd_server_new(InfdDirectory* directory,
                GNetworkServer* server);

GNetworkServer*
infd_server_get_server(InfdServer* server);

void
infd_server_set_server(InfdServer* server,
                       GNetworkServer* gnetwork_server);

GNetworkTcpServer*
infd_server_open(InfdServer* server,
                 const gchar* interface,
                 guint port);

G_END_DECLS

#endif /* __INFD_SERVER_H__ */
