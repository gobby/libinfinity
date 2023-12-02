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

#include <infinoted/infinoted-plugin-manager.h>
#include <infinoted/infinoted-parameter.h>

#include <libinfinity/inf-i18n.h>

#include <libsoup/soup-server.h>

typedef struct _InfinotedPluginHttp InfinotedPluginHttp;
struct _InfinotedPluginHttp {
  InfinotedPluginManager* manager;

  guint port;

  GMainContext* context;
  GMainLoop* loop;
  GThread* thread;
  SoupServer* server;
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
}

static gpointer
infinoted_plugin_http_thread_func(gpointer plugin_info)
{
  InfinotedPluginHttp* plugin;
  plugin = (InfinotedPluginHttp*)plugin_info;

  g_main_context_push_thread_default(plugin->context);
  plugin->loop = g_main_loop_new(plugin->context, FALSE);

  plugin->server = g_object_new(SOUP_TYPE_SERVER, NULL);

  // TODO(armin): terminate if it failed to listen by sending
  // the error (or NULL) back to the initializing thread.
  if (soup_server_listen_all(plugin->server, plugin->port, 0, NULL)) {
    g_main_loop_run(plugin->loop);
  }

  if (plugin->server != NULL) {
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
  plugin = (InfinotedPluginHttp*)plugin_info;

  // TODO(armin): libsoup runs in a Glib main loop, but infinoted does not
  // by default, so we run libsoup in a separate thread with a Glib main loop.

  plugin->manager = manager;

  plugin->context = g_main_context_new();

  /* We run the DBus activity in its own thread, so that we can iterate
   * a glib main loop there. */
  plugin->thread = g_thread_try_new(
    "InfinotedPluginDbus",
    infinoted_plugin_http_thread_func,
    plugin_info,
    error
  );

  if (plugin->thread == NULL) {
    return FALSE;
  }

  // TODO(armin): here wait for the server to have initialized

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

  if (plugin->context != NULL) {
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
