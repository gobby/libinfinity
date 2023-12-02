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

/**
 * SECTION:infinoted-plugin-http-websocket-connection
 * @title: InfinotedPluginHttpWebsocketConnection
 * @short_description: Implementation of the InfXmlConnection interface via websocket
 * @include: infinoted/plugins/http/infinoted-plugin-http-websocket-connection.h
 * @stability: Unstable
 *
 * This class implements the #InfXmlConnection interface through a WebSocket,
 * and translates XML messages to JSON so that web clients have something
 * familiar to work with.
 **/

#include <infinoted/plugins/http/infinoted-plugin-http-websocket-connection.h>

#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-xml-util.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

typedef struct _InfinotedPluginHttpWebsocketConnectionPrivate InfinotedPluginHttpWebsocketConnectionPrivate;
struct _InfinotedPluginHttpWebsocketConnectionPrivate {
  SoupWebsocketConnection* websocket;
  InfIo* io;
};

enum {
  PROP_0,

  PROP_WEBSOCKET,
  PROP_IO,

  /* From InfXmlConnection */
  PROP_STATUS,
  PROP_NETWORK,
  PROP_LOCAL_ID,
  PROP_REMOTE_ID,
  PROP_LOCAL_CERTIFICATE,
  PROP_REMOTE_CERTIFICATE
};

#define INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFINOTED_PLUGIN_HTTP_TYPE_WEBSOCKET_CONNECTION, InfinotedPluginHttpWebsocketConnectionPrivate))

static void infinoted_plugin_http_websocket_connection_xml_connection_iface_init(InfXmlConnectionInterface* iface);

G_DEFINE_TYPE_WITH_CODE(InfinotedPluginHttpWebsocketConnection, infinoted_plugin_http_websocket_connection, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfinotedPluginHttpWebsocketConnection)
  G_IMPLEMENT_INTERFACE(INF_TYPE_XML_CONNECTION, infinoted_plugin_http_websocket_connection_xml_connection_iface_init))

static void
infinoted_plugin_http_websocket_connection_set_websocket(InfinotedPluginHttpWebsocketConnection* conn,
                                                         SoupWebsocketConnection* websocket)
{
  //g_signal_connect(connection, "message", G_CALLBACK(infinoted_plugin_http_websocket_message_func), plugin);
#if 0
  InfinotedPluginHttpWebsocketConnectionPrivate* priv;
  InfTcpConnectionStatus tcp_status;

  priv = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(xmpp);

  g_object_freeze_notify(G_OBJECT(xmpp));

  if(priv->tcp != NULL)
  {
    g_object_get(G_OBJECT(priv->tcp), "status", &tcp_status, NULL);

    /* This will cause a status notify which will actually delete
     * GnuTLS session (if any) and the message queue. */
    if(tcp_status != INF_TCP_CONNECTION_CLOSED)
      inf_tcp_connection_close(priv->tcp);

    /* Make sure there is no SASL session running anymore... it's not a big
     * deal if there was but it should be aborted by the above call to
     * inf_tcp_connection_close(). */
    g_assert(priv->sasl_session == NULL);

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(infinoted_plugin_http_websocket_connection_sent_cb),
      xmpp
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(infinoted_plugin_http_websocket_connection_received_cb),
      xmpp
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(infinoted_plugin_http_websocket_connection_error_cb),
      xmpp
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->tcp),
      G_CALLBACK(infinoted_plugin_http_websocket_connection_notify_status_cb),
      xmpp
    );

    g_object_unref(G_OBJECT(priv->tcp));
  }

  priv->tcp = tcp;

  if(tcp != NULL)
  {
    g_object_ref(G_OBJECT(tcp));

    g_signal_connect(
      G_OBJECT(tcp),
      "sent",
      G_CALLBACK(infinoted_plugin_http_websocket_connection_sent_cb),
      xmpp
    );

    g_signal_connect(
      G_OBJECT(tcp),
      "received",
      G_CALLBACK(infinoted_plugin_http_websocket_connection_received_cb),
      xmpp
    );

    g_signal_connect(
      G_OBJECT(tcp),
      "error",
      G_CALLBACK(infinoted_plugin_http_websocket_connection_error_cb),
      xmpp
    );

    g_signal_connect(
      G_OBJECT(tcp),
      "notify::status",
      G_CALLBACK(infinoted_plugin_http_websocket_connection_notify_status_cb),
      xmpp
    );

    g_object_get(G_OBJECT(tcp), "status", &tcp_status, NULL);

    switch(tcp_status)
    {
    case INF_TCP_CONNECTION_CLOSED:
      g_assert(priv->status == INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_CLOSED);
      break;
    case INF_TCP_CONNECTION_CONNECTING:
      priv->status = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_CONNECTING;
      g_object_notify(G_OBJECT(xmpp), "status");
      break;
    case INF_TCP_CONNECTION_CONNECTED:
      /* Do not call initiate, this will be done in constructor little
       * time later. */
      priv->status = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_CONNECTED;
      g_object_notify(G_OBJECT(xmpp), "status");
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }

  g_object_thaw_notify(G_OBJECT(xmpp));
#endif
}

/*
 * GObject overrides
 */

static void
infinoted_plugin_http_websocket_connection_init(InfinotedPluginHttpWebsocketConnection* connection)
{
  InfinotedPluginHttpWebsocketConnectionPrivate* priv;
  priv = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(connection);

  priv->websocket = NULL;
  priv->io = NULL;
}

static void
infinoted_plugin_http_websocket_connection_dispose(GObject* object)
{
  InfinotedPluginHttpWebsocketConnection* conn;
  InfinotedPluginHttpWebsocketConnectionPrivate* priv;

  conn = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION(object);
  priv = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(conn);

  infinoted_plugin_http_websocket_connection_set_websocket(conn, NULL);

  if (priv->io != NULL) {
    g_object_unref(priv->io);
    priv->io = NULL;

    g_object_notify(G_OBJECT(conn), "io");
  }

  G_OBJECT_CLASS(infinoted_plugin_http_websocket_connection_parent_class)->dispose(object);
}

static void
infinoted_plugin_http_websocket_connection_finalize(GObject* object)
{
  InfinotedPluginHttpWebsocketConnection* conn;
  InfinotedPluginHttpWebsocketConnectionPrivate* priv;

  conn = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION(object);
  priv = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(conn);

  G_OBJECT_CLASS(infinoted_plugin_http_websocket_connection_parent_class)->finalize(object);
}

static void
infinoted_plugin_http_websocket_connection_set_property(GObject* object,
                                                        guint prop_id,
                                                        const GValue* value,
                                                        GParamSpec* pspec)
{
  InfinotedPluginHttpWebsocketConnection* conn;
  InfinotedPluginHttpWebsocketConnectionPrivate* priv;

  conn = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION(object);
  priv = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(conn);

  switch(prop_id)
  {
  case PROP_WEBSOCKET:
    infinoted_plugin_http_websocket_connection_set_websocket(
      conn,
      SOUP_WEBSOCKET_CONNECTION(g_value_get_object(value))
    );

    break;
  case PROP_IO:
    if (priv->io != NULL) {
      g_object_unref(priv->io);
      priv->io = NULL;
    }

    priv->io = INF_IO(g_value_dup_object(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infinoted_plugin_http_websocket_connection_get_property(GObject* object,
                                                        guint prop_id,
                                                        GValue* value,
                                                        GParamSpec* pspec)
{
  InfinotedPluginHttpWebsocketConnection* conn;
  InfinotedPluginHttpWebsocketConnectionPrivate* priv;

  conn = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION(object);
  priv = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(conn);

  switch(prop_id)
  {
  case PROP_WEBSOCKET:
    g_value_set_object(value, G_OBJECT(priv->websocket));
    break;
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_STATUS:
    // TODO(armin):
    g_value_set_enum(value, INF_XML_CONNECTION_CLOSED);
    break;
  case PROP_NETWORK:
    g_value_set_static_string(value, "websocket");
    break;
  case PROP_LOCAL_ID:
    // TODO(armin): From WebsocketConnection:uri
    g_value_set_static_string(value, "TODO");
    break;
  case PROP_REMOTE_ID:
    // TODO(armin): From WebsocketConnection:origin
    g_value_set_static_string(value, "TODO");
    break;
  case PROP_LOCAL_CERTIFICATE:
    // TODO
    g_value_set_pointer(value, NULL);
    break;
  case PROP_REMOTE_CERTIFICATE:
    // TODO
    g_value_set_boxed(value, NULL);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * InfXmlConnection interface implementation
 */

static gboolean
infinoted_plugin_http_websocket_connection_xml_connection_open(InfXmlConnection* connection,
                                        GError** error)
{
  InfinotedPluginHttpWebsocketConnectionPrivate* priv;
  priv = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(connection);

  // TODO
  return FALSE;
}

static void
infinoted_plugin_http_websocket_connection_xml_connection_close(InfXmlConnection* connection)
{
  InfinotedPluginHttpWebsocketConnectionPrivate* priv;
  priv = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(connection);

  // TODO
}

static void
infinoted_plugin_http_websocket_connection_xml_connection_send(InfXmlConnection* connection,
                                                               xmlNodePtr xml)
{
  InfinotedPluginHttpWebsocketConnectionPrivate* priv;
  priv = INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION_PRIVATE(connection);

  // TODO

/*
<a x="bla">
  <b>foo</b>ble
</a>

{name:"a"
 x:"bla"
 children: [
   {name:"b",
    children: [
      "foo"
    ],
   },
   "ble"
 ]}
*/

}

/*
 * GObject type registration
 */

static void
infinoted_plugin_http_websocket_connection_class_init(InfinotedPluginHttpWebsocketConnectionClass* connection_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(connection_class);

  object_class->dispose = infinoted_plugin_http_websocket_connection_dispose;
  object_class->finalize = infinoted_plugin_http_websocket_connection_finalize;
  object_class->set_property = infinoted_plugin_http_websocket_connection_set_property;
  object_class->get_property = infinoted_plugin_http_websocket_connection_get_property;

  g_object_class_install_property(
    object_class,
    PROP_WEBSOCKET,
    g_param_spec_object(
      "websocket",
      "WebSocket connection",
      "Underlaying WebSocket connection",
      SOUP_TYPE_WEBSOCKET_CONNECTION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "I/O object running the main libinfinity thread",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_override_property(object_class, PROP_STATUS, "status");
  g_object_class_override_property(object_class, PROP_NETWORK, "network");
  g_object_class_override_property(object_class, PROP_LOCAL_ID, "local-id");
  g_object_class_override_property(object_class, PROP_REMOTE_ID, "remote-id");

  g_object_class_override_property(
    object_class,
    PROP_LOCAL_CERTIFICATE,
    "local-certificate"
  );

  g_object_class_override_property(
    object_class,
    PROP_REMOTE_CERTIFICATE,
    "remote-certificate"
  );
}

static void
infinoted_plugin_http_websocket_connection_xml_connection_iface_init(
  InfXmlConnectionInterface* iface)
{
  iface->open = infinoted_plugin_http_websocket_connection_xml_connection_open;
  iface->close = infinoted_plugin_http_websocket_connection_xml_connection_close;
  iface->send = infinoted_plugin_http_websocket_connection_xml_connection_send;
}

/*
 * Public API
 */

/**
 * infinoted_plugin_http_websocket_connection_new: (constructor)
 * @websocket: The underlaying WebSocket connection to use.
 * @io: The I/O object running the libinfinity main thread.
 *
 * Creates a new #InfinotedPluginHttpWebsocketConnection with @websocket as
 * communication channel.
 *
 * Returns: (transfer full): A new #InfinotedPluginHttpWebsocketConnection.
 **/
InfinotedPluginHttpWebsocketConnection*
infinoted_plugin_http_websocket_connection_new(SoupWebsocketConnection* websocket,
                                               InfIo* io)
{
  GObject* object;

  g_return_val_if_fail(SOUP_IS_WEBSOCKET_CONNECTION(websocket), NULL);
  g_return_val_if_fail(INF_IS_IO(io), NULL);

  object = g_object_new(
    INFINOTED_PLUGIN_HTTP_TYPE_WEBSOCKET_CONNECTION,
    "websocket", websocket,
    "io", io,
    NULL
  );

  return INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION(object);
}

/* vim:set et sw=2 ts=2: */
