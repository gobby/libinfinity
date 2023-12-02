/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2023 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_H__
#define __INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_H__

#include <libinfinity/common/inf-io.h>

#include <libsoup/soup-websocket-connection.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFINOTED_PLUGIN_HTTP_TYPE_WEBSOCKET_CONNECTION                 (infinoted_plugin_http_websocket_connection_get_type())
#define INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFINOTED_PLUGIN_HTTP_TYPE_WEBSOCKET_CONNECTION, InfinotedPluginHttpWebsocketConnection))
#define INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFINOTED_PLUGIN_HTTP_TYPE_WEBSOCKET_CONNECTION, InfinotedPluginHttpWebsocketConnectionClass))
#define INFINOTED_PLUGIN_HTTP_IS_WEBSOCKET_CONNECTION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFINOTED_PLUGIN_HTTP_TYPE_WEBSOCKET_CONNECTION))
#define INFINOTED_PLUGIN_HTTP_IS_WEBSOCKET_CONNECTION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFINOTED_PLUGIN_HTTP_TYPE_WEBSOCKET_CONNECTION))
#define INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFINOTED_PLUGIN_HTTP_TYPE_WEBSOCKET_CONNECTION, InfinotedPluginHttpWebsocketConnectionClass))

typedef struct _InfinotedPluginHttpWebsocketConnection InfinotedPluginHttpWebsocketConnection;
typedef struct _InfinotedPluginHttpWebsocketConnectionClass InfinotedPluginHttpWebsocketConnectionClass;

/**
 * InfinotedPluginHttpWebsocketConnectionClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfinotedPluginHttpWebsocketConnectionClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfinotedPluginHttpWebsocketConnection:
 *
 * #InfinotedPluginHttpWebsocketConnection is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfinotedPluginHttpWebsocketConnection {
  /*< private >*/
  GObject parent;
};

GType
infinoted_plugin_http_websocket_connection_get_type(void) G_GNUC_CONST;

InfinotedPluginHttpWebsocketConnection*
infinoted_plugin_http_websocket_connection_new(SoupWebsocketConnection* connection,
                                               InfIo* io);

G_END_DECLS

#endif /* __INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_H__ */

/* vim:set et sw=2 ts=2: */
