/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFD_SESSION_PROXY_H__
#define __INFD_SESSION_PROXY_H__

#include <libinfinity/common/inf-session.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_SESSION_PROXY                 (infd_session_proxy_get_type())
#define INFD_SESSION_PROXY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_SESSION_PROXY, InfdSessionProxy))
#define INFD_SESSION_PROXY_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_SESSION_PROXY, InfdSessionProxyClass))
#define INFD_IS_SESSION_PROXY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_SESSION_PROXY))
#define INFD_IS_SESSION_PROXY_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_SESSION_PROXY))
#define INFD_SESSION_PROXY_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_SESSION_PROXY, InfdSessionProxyClass))

typedef struct _InfdSessionProxy InfdSessionProxy;
typedef struct _InfdSessionProxyClass InfdSessionProxyClass;

struct _InfdSessionProxyClass {
  GObjectClass parent_class;

  /* Signals */
  void (*add_subscription)(InfdSessionProxy* proxy,
                           InfXmlConnection* connection,
                           guint seq_id);

  void (*remove_subscription)(InfdSessionProxy* proxy,
                              InfXmlConnection* connection);
};

struct _InfdSessionProxy {
  GObject parent;
};

GType
infd_session_proxy_get_type(void) G_GNUC_CONST;

InfSession*
infd_session_proxy_get_session(InfdSessionProxy* proxy);

InfUser*
infd_session_proxy_add_user(InfdSessionProxy* proxy,
                            const GParameter* params,
                            guint n_params,
                            GError** error);

void
infd_session_proxy_subscribe_to(InfdSessionProxy* proxy,
                                InfXmlConnection* connection,
                                guint seq_id,
                                gboolean synchronize);

gboolean
infd_session_proxy_has_subscriptions(InfdSessionProxy* proxy);

gboolean
infd_session_proxy_is_subscribed(InfdSessionProxy* proxy,
                                 InfXmlConnection* connection);

gboolean
infd_session_proxy_is_idle(InfdSessionProxy* proxy);

G_END_DECLS

#endif /* __INFD_SESSION_PROXY_H__ */

/* vim:set et sw=2 ts=2: */
