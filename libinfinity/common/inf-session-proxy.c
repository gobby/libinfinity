/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-session_proxy
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

static void
inf_session_proxy_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    g_object_interface_install_property(
      g_class,
      g_param_spec_object(
        "session",
        "Session",
        "The underlying session object",
        INF_TYPE_SESSION,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
      )
    );

    initialized = TRUE;
  }
}

GType
inf_session_proxy_get_type(void)
{
  static GType session_proxy_type = 0;

  if(!session_proxy_type)
  {
    static const GTypeInfo session_proxy_info = {
      sizeof(InfSessionProxyIface),     /* class_size */
      inf_session_proxy_base_init,      /* base_init */
      NULL,                             /* base_finalize */
      NULL,                             /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      0,                                /* instance_size */
      0,                                /* n_preallocs */
      NULL,                             /* instance_init */
      NULL                              /* value_table */
    };

    session_proxy_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfSessionProxy",
      &session_proxy_info,
      0
    );

    g_type_interface_add_prerequisite(session_proxy_type, G_TYPE_OBJECT);
  }

  return session_proxy_type;
}

/**
 * inf_session_proxy_join_user:
 * @proxy: A #InfSessionProxy.
 * @n_params: Number of parameters.
 * @params: Construction properties for the InfUser (or derived) object.
 *
 * Requests a user join for a user with the given properties (which must not
 * include #InfUser:id or #InfUser:flags since these are chosen by the session
 * proxy). The #InfUser:status property is optional and defaults to
 * %INF_USER_ACTIVE if not given. It must not be %INF_USER_UNAVAILABLE.
 *
 * Returns: A #InfUserRequest object that may be used to get notified
 * when the request finishes.
 */
InfUserRequest*
inf_session_proxy_join_user(InfSessionProxy* proxy,
                            guint n_params,
                            const GParameter* params)
{
  InfSessionProxyIface* iface;

  g_return_val_if_fail(INF_IS_SESSION_PROXY(proxy), NULL);
  g_return_val_if_fail(n_params == 0 || params != NULL, NULL);

  iface = INF_SESSION_PROXY_GET_IFACE(proxy);
  g_return_val_if_fail(iface->join_user != NULL, NULL);

  return iface->join_user(proxy, n_params, params);
}

/* vim:set et sw=2 ts=2: */
