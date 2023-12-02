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

#include <infinoted/plugins/http/infinoted-plugin-http-websocket-connection.h>

#include <infinoted/infinoted-plugin-manager.h>
#include <infinoted/infinoted-parameter.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <libsoup/soup-server.h>

typedef struct _InfinotedPluginHttp InfinotedPluginHttp;
struct _InfinotedPluginHttp {
  InfinotedPluginManager* manager;

  guint port;

  GAsyncQueue* init_done;
  GMainContext* context;
  GMainLoop* loop;
  GThread* thread;
  SoupServer* server;

  GSList* connections;
};

static void
infinoted_plugin_http_info_initialize(gpointer plugin_info)
{
  InfinotedPluginHttp* plugin;
  plugin = (InfinotedPluginHttp*)plugin_info;

  plugin->manager = NULL;
  plugin->port = 8080;

  plugin->context = NULL;
  plugin->loop = NULL;
  plugin->thread = NULL;
  plugin->server = NULL;

  plugin->connections = NULL;
}

static void
infinoted_plugin_http_websocket_connection_closed_cb(SoupWebsocketConnection* connection,
                                                     gpointer user_data);

static void
infinoted_plugin_http_remove_connection(InfinotedPluginHttp* plugin,
                                        InfinotedPluginHttpWebsocketConnection* conn)
{
  SoupWebsocketConnection* websocket;
  g_object_get(G_OBJECT(conn), "websocket", &websocket, NULL);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(websocket),
    G_CALLBACK(infinoted_plugin_http_websocket_connection_closed_cb),
    plugin
  );

  g_object_unref(websocket);

  plugin->connections = g_slist_remove(plugin->connections, conn);
}

static void
infinoted_plugin_http_websocket_connection_closed_cb(SoupWebsocketConnection* connection,
                                                     gpointer user_data)
{
  InfinotedPluginHttp* plugin;
  GSList* item;
  SoupWebsocketConnection* websocket;

  plugin = (InfinotedPluginHttp*)user_data;

  for (item = plugin->connections; item != NULL; item = item->next)
  {
    g_object_get(G_OBJECT(item->data), "websocket", &websocket, NULL);
    if (websocket == connection) {
      infinoted_plugin_http_remove_connection(
        plugin,
        INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION(item->data)
      );

      g_object_unref(websocket);
      break;
    }

    g_object_unref(websocket);
  }
}

static void
infinoted_plugin_http_websocket_func(SoupServer* server,
                                     SoupServerMessage* message,
                                     const char* path,
                                     SoupWebsocketConnection* connection,
                                     gpointer user_data)
{
  InfinotedPluginHttp* plugin;
  plugin = (InfinotedPluginHttp*)user_data;

  InfinotedPluginHttpWebsocketConnection* conn =
    infinoted_plugin_http_websocket_connection_new(
      connection, infinoted_plugin_manager_get_io(plugin->manager));

  printf("websocket created!\n");

  plugin->connections = g_slist_prepend(plugin->connections, conn);

  g_signal_connect(
    G_OBJECT(connection),
    "closed",
    G_CALLBACK(infinoted_plugin_http_websocket_connection_closed_cb),
    plugin
  );

  // TODO(armin): remove the connection once it gets closed. Can use
  // soup websocket connection state change signal for it.
}

static gpointer
infinoted_plugin_http_thread_func(gpointer plugin_info)
{
  InfinotedPluginHttp* plugin;
  GError* error;

  plugin = (InfinotedPluginHttp*)plugin_info;

  g_main_context_push_thread_default(plugin->context);
  plugin->loop = g_main_loop_new(plugin->context, FALSE);

  plugin->server = g_object_new(SOUP_TYPE_SERVER, NULL);

  soup_server_add_websocket_handler(
    plugin->server,
    "/",
    NULL,
    NULL,
    infinoted_plugin_http_websocket_func,
    plugin,
    NULL
  );

  error = NULL;
  if (!soup_server_listen_all(plugin->server, plugin->port, 0, &error))
  {
    g_object_unref(plugin->server);
    plugin->server = NULL;

    g_main_loop_unref(plugin->loop);
    plugin->loop = NULL;

    g_async_queue_push(plugin->init_done, error);
    return NULL;
  }

  g_async_queue_push(plugin->init_done, NULL);
  g_main_loop_run(plugin->loop);

  while (plugin->connections != NULL)
  {
    infinoted_plugin_http_remove_connection(
      plugin,
      INFINOTED_PLUGIN_HTTP_WEBSOCKET_CONNECTION(plugin->connections->data)
    );
  }

  if (plugin->server != NULL)
  {
    soup_server_disconnect(plugin->server);
    g_object_unref(plugin->server);
    plugin->server = NULL;
  }

  g_main_loop_unref(plugin->loop);
  plugin->loop = NULL;

  return NULL;
}

static gboolean
infinoted_plugin_http_deinitialize_thread_func(gpointer user_data)
{
  InfinotedPluginHttp* plugin;
  plugin = (InfinotedPluginHttp*)user_data;

  /* If none of the callbacks has been called yet, then wait for them to be
   * called. */
  g_main_loop_quit(plugin->loop);
  return FALSE;
}

static gboolean
infinoted_plugin_http_initialize(InfinotedPluginManager* manager,
                                 gpointer plugin_info,
                                 GError** error)
{
  InfinotedPluginHttp* plugin;
  GError* init_error;

  plugin = (InfinotedPluginHttp*)plugin_info;

  // libsoup runs in a Glib main loop, but infinoted does not
  // by default, so we run libsoup in a separate thread with a Glib main loop.

  // TODO(armin): do we need to perform the same trick with making libsoup resident here?

  plugin->manager = manager;
  plugin->init_done = g_async_queue_new_full((GDestroyNotify)g_error_free);
  plugin->context = g_main_context_new();

  /* We run the DBus activity in its own thread, so that we can iterate
   * a glib main loop there. */
  plugin->thread = g_thread_try_new(
    "InfinotedPluginDbus",
    infinoted_plugin_http_thread_func,
    plugin_info,
    error
  );

  if (plugin->thread == NULL)
  {
    return FALSE;
  }

  init_error = g_async_queue_pop(plugin->init_done);
  if (init_error != NULL) {
    g_propagate_error(error, init_error);
    return FALSE;
  }

  return TRUE;
}

static void
infinoted_plugin_http_deinitialize(gpointer plugin_info)
{
  InfinotedPluginHttp* plugin;
  GSource* source;

  plugin = (InfinotedPluginHttp*)plugin_info;

  if(plugin->thread != NULL)
  {
    /* Tell the thread to quit */
    if(plugin->context != NULL)
    {
      source = g_idle_source_new();

      g_source_set_callback(
        source,
        infinoted_plugin_http_deinitialize_thread_func,
        plugin,
        NULL
      );

      g_source_attach(source, plugin->context);
    }

    g_thread_join(plugin->thread);
    plugin->thread = NULL;
  }

  if (plugin->context != NULL)
  {
    g_main_context_unref(plugin->context);
    plugin->context = NULL;
  }
}

static const InfinotedParameterInfo INFINOTED_PLUGIN_HTTP_OPTIONS[] = {
  {
    "port",
    INFINOTED_PARAMETER_INT,
    0,
    offsetof(InfinotedPluginHttp, port),
    infinoted_parameter_convert_port,
    0,
    N_("The port to serve the HTTP service on."),
    NULL
  }, {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "http",
  N_("This plugin provides a HTTP (WebSocket) interface to the server and "
     "allows integration of web clients."),
  INFINOTED_PLUGIN_HTTP_OPTIONS,
  sizeof(InfinotedPluginHttp),
  0,
  0,
  NULL,
  infinoted_plugin_http_info_initialize,
  infinoted_plugin_http_initialize,
  infinoted_plugin_http_deinitialize,
  NULL,
  NULL,
  NULL,
  NULL
};

/* vim:set et sw=2 ts=2: */
