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

#ifndef __INF_SESSION_PROXY_H__
#define __INF_SESSION_PROXY_H__

#include <libinfinity/common/inf-user-request.h>
#include <libinfinity/common/inf-user.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_SESSION_PROXY                 (inf_session_proxy_get_type())
#define INF_SESSION_PROXY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_SESSION_PROXY, InfSessionProxy))
#define INF_IS_SESSION_PROXY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_SESSION_PROXY))
#define INF_SESSION_PROXY_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_SESSION_PROXY, InfSessionProxyIface))

#define INF_TYPE_SESSION_PROXY_STATUS          (inf_session_proxy_status_get_type())

/**
 * InfSessionProxy:
 *
 * #InfSessionProxy is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfSessionProxy InfSessionProxy;
typedef struct _InfSessionProxyIface InfSessionProxyIface;

/**
 * InfSessionProxyIface:
 * @join_user: Virtual function to join a user into the proxy's session.
 *
 * Virtual functions for the #InfSessionProxy interface.
 */
struct _InfSessionProxyIface {
  /*< private >*/
  GTypeInterface parent;

  /* Signals */

  /*< public >*/
  InfUserRequest* (*join_user)(InfSessionProxy* proxy,
                               guint n_params,
                               const GParameter* params,
                               InfUserRequestFunc func,
                               gpointer user_data);
};

GType
inf_session_proxy_get_type(void) G_GNUC_CONST;

InfUserRequest*
inf_session_proxy_join_user(InfSessionProxy* proxy,
                            guint n_params,
                            const GParameter* params,
                            InfUserRequestFunc func,
                            gpointer user_data);

G_END_DECLS

#endif /* __INF_SESSION_PROXY_H__ */

/* vim:set et sw=2 ts=2: */
