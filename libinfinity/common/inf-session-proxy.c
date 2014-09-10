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

/**
 * SECTION:inf-session-proxy
 * @title: InfSessionProxy
 * @short_description: Joining users into a session
 * @include: libinfinity/common/inf-session_proxy.h
 * @see_also: #InfSession, #InfBrowser, #InfcSessionProxy, #InfdSessionProxy
 * @stability: Unstable
 *
 * A #InfSessionProxy is a network-architecture-aware layer on top of a
 * #InfSession. A #InfSession has no idea about what kind of network it is in,
 * all it has is a possibility to send messages to one user or to all users.
 *
 * A #InfSessionProxy implements the part of the infinote protocol which
 * depends on whether the session is at an infinote server or an infinote
 * client. This interface provides a method to join a user into a session so
 * that it does not need to be known to the caller whether the session at
 * hand is on a server or a client.
 */

#include <libinfinity/common/inf-session-proxy.h>
#include <libinfinity/common/inf-session.h>

G_DEFINE_INTERFACE(InfSessionProxy, inf_session_proxy, G_TYPE_OBJECT)

static void
inf_session_proxy_default_init(InfSessionProxyInterface* iface)
{
  g_object_interface_install_property(
    iface,
    g_param_spec_object(
      "session",
      "Session",
      "The underlying session object",
      INF_TYPE_SESSION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

/**
 * inf_session_proxy_join_user:
 * @proxy: A #InfSessionProxy.
 * @n_params: Number of parameters.
 * @params: (array length=n_params): Construction properties for the
 * #InfUser (or derived) object.
 * @func: (scope async): Function to be called on completion of the user
 * join, or %NULL.
 * @user_data: Additional data to be passed to @func.
 *
 * Requests a user join for a user with the given properties (which must not
 * include #InfUser:id or #InfUser:flags since these are chosen by the session
 * proxy). The #InfUser:status property is optional and defaults to
 * %INF_USER_ACTIVE if not given. It must not be %INF_USER_UNAVAILABLE.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none): A #InfRequest object that may be used to get
 * notified when the request finishes, or %NULL.
 */
InfRequest*
inf_session_proxy_join_user(InfSessionProxy* proxy,
                            guint n_params,
                            const GParameter* params,
                            InfRequestFunc func,
                            gpointer user_data)
{
  InfSessionProxyInterface* iface;

  g_return_val_if_fail(INF_IS_SESSION_PROXY(proxy), NULL);
  g_return_val_if_fail(n_params == 0 || params != NULL, NULL);

  iface = INF_SESSION_PROXY_GET_IFACE(proxy);
  g_return_val_if_fail(iface->join_user != NULL, NULL);

  return iface->join_user(proxy, n_params, params, func, user_data);
}

/* vim:set et sw=2 ts=2: */
