/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

/**
 * InfcSessionProxyClass:
 * @translate_error: Virtual function to transform an error domain and code
 * into a #GError object. If the domain and code are known, this allows to
 * show a localized error message to the user, independent from the language
 * the server uses in the error message that went over the wire.
 *
 * This structure contains virtual functions of the #InfcSessionProxy class.
 */
struct _InfcSessionProxyClass {
  /*< private >*/
  GObjectClass parent_class;

  /* Virtual Functions */

  /*< public >*/
  GError* (*translate_error)(InfcSessionProxy* session,
                             GQuark domain,
                             guint code);
};

/**
 * InfcSessionProxy:
 *
 * #InfcSessionProxy is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfcSessionProxy {
  /*< private >*/
  GObject parent;
};

GType
infc_session_proxy_get_type(void) G_GNUC_CONST;

void
infc_session_proxy_set_connection(InfcSessionProxy* proxy,
                                  InfCommunicationJoinedGroup* group,
                                  InfXmlConnection* connection,
                                  guint seq_id);

InfXmlConnection*
infc_session_proxy_get_connection(InfcSessionProxy* proxy);

InfCommunicationJoinedGroup*
infc_session_proxy_get_subscription_group(InfcSessionProxy* proxy);

G_END_DECLS

#endif /* __INFC_SESSION_PROXY_H__ */

/* vim:set et sw=2 ts=2: */
