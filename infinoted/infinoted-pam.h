/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_PAM_H__
#define __INFINOTED_PAM_H__

#include <infinoted/infinoted-options.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/server/infd-xmpp-server.h>

#include <gsasl.h>

#include <glib.h>

G_BEGIN_DECLS

gboolean
infinoted_pam_authenticate(const char* service,
                           const char* username,
                           const char* password);

GError*
infinoted_pam_user_authenticated_cb(InfdXmppServer* xmpp_server,
                                    InfXmppConnection* xmpp_connection,
                                    Gsasl_session* sasl_session,
                                    gpointer user_data);

G_END_DECLS

#endif /* __INFINOTED_PAM_H__ */

/* vim:set et sw=2 ts=2: */