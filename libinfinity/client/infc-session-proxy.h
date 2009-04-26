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

#ifndef __INFC_SESSION_PROXY_H__
#define __INFC_SESSION_PROXY_H__

#include <libinfinity/client/infc-user-request.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/communication/inf-communication-joined-group.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_SESSION_PROXY                 (infc_session_proxy_get_type())
#define INFC_SESSION_PROXY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_SESSION_PROXY, InfcSessionProxy))
#define INFC_SESSION_PROXY_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_SESSION_PROXY, InfcSessionProxyClass))
#define INFC_IS_SESSION_PROXY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_SESSION_PROXY))
#define INFC_IS_SESSION_PROXY_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_SESSION_PROXY))
#define INFC_SESSION_PROXY_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_SESSION_PROXY, InfcSessionProxyClass))

typedef struct _InfcSessionProxy InfcSessionProxy;
typedef struct _InfcSessionProxyClass InfcSessionProxyClass;

struct _InfcSessionProxyClass {
  GObjectClass parent_class;

  GError* (*translate_error)(InfcSessionProxy* session,
                             GQuark domain,
                             guint code);
};

struct _InfcSessionProxy {
  GObject parent;
};

GType
infc_session_proxy_get_type(void) G_GNUC_CONST;

void
infc_session_proxy_set_connection(InfcSessionProxy* proxy,
                                  InfCommunicationJoinedGroup* group,
                                  InfXmlConnection* connection);

InfcUserRequest*
infc_session_proxy_join_user(InfcSessionProxy* proxy,
                             const GParameter* params,
                             guint n_params,
                             GError** error);

InfSession*
infc_session_proxy_get_session(InfcSessionProxy* proxy);

InfXmlConnection*
infc_session_proxy_get_connection(InfcSessionProxy* proxy);

InfCommunicationJoinedGroup*
infc_session_proxy_get_subscription_group(InfcSessionProxy* proxy);

G_END_DECLS

#endif /* __INFC_SESSION_PROXY_H__ */

/* vim:set et sw=2 ts=2: */
