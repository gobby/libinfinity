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

#ifndef __INFD_XMPP_SERVER_H__
#define __INFD_XMPP_SERVER_H__

#include <libinfinity/common/inf-tcp-connection.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_XMPP_SERVER                 (infd_xmpp_server_get_type())
#define INFD_XMPP_SERVER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_XMPP_SERVER, InfdXmppServer))
#define INFD_XMPP_SERVER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_XMPP_SERVER, InfdXmppServerClass))
#define INFD_IS_XMPP_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_XMPP_SERVER))
#define INFD_IS_XMPP_SERVER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_XMPP_SERVER))
#define INFD_XMPP_SERVER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_XMPP_SERVER, InfdXmppServerClass))

typedef struct _InfdXmppServer InfdXmppServer;
typedef struct _InfdXmppServerClass InfdXmppServerClass;

struct _InfdXmppServerClass {
  GObjectClass parent_class;

  /* Signals */
  void (*error)(InfdXmppServer* server,
                GError* error);
};

struct _InfdXmppServer {
  GObject parent;
};

GType
infd_xmpp_server_get_type(void) G_GNUC_CONST;

InfdXmppServer*
infd_xmpp_server_new(InfdTcpServer* tcp,
                     const gchar* jid,
                     gnutls_certificate_credentials_t cred,
                     Gsasl* sasl_context);

G_END_DECLS

#endif /* __INFD_XMPP_SERVER_H__ */
