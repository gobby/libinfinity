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

#include <infinoted/infinoted-plugin-manager.h>
#include <infinoted/infinoted-parameter.h>
#include <infinoted/infinoted-util.h>

#include <libinfinity/inf-signals.h>
#include <libinfinity/inf-i18n.h>

#include <libxml/xmlsave.h>

#include <string.h>
#include <errno.h>

typedef struct _InfinotedPluginTrafficLogging InfinotedPluginTrafficLogging;
struct _InfinotedPluginTrafficLogging {
  InfinotedPluginManager* manager;
  gchar* path;
};

typedef struct _InfinotedPluginTrafficLoggingConnectionInfo
  InfinotedPluginTrafficLoggingConnectionInfo;
struct _InfinotedPluginTrafficLoggingConnectionInfo {
  InfinotedPluginTrafficLogging* plugin;
  InfXmlConnection* connection;
  gchar* filename;
  FILE* file;
};

static void
infinoted_plugin_traffic_logging_write(
  InfinotedPluginTrafficLoggingConnectionInfo* info,
  const gchar* fmt,
  ...)
{
  time_t cur_time;
  struct tm* cur_tm;
  char time_msg[128];
  va_list arglist;

  g_assert(info->file != NULL);

  cur_time = time(NULL);
  cur_tm = localtime(&cur_time);
  strftime(time_msg, 128, "[%c] ", cur_tm);
  fputs(time_msg, info->file);

  va_start(arglist, fmt);
  vfprintf(info->file, fmt, arglist);
  va_end(arglist);
  fputc('\n', info->file);

  fflush(info->file);
}

static void
infinoted_plugin_traffic_logging_received_cb(InfXmlConnection* conn,
                                             xmlNodePtr xml,
                                             gpointer user_data)
{
  InfinotedPluginTrafficLoggingConnectionInfo* info;
  xmlBufferPtr buffer;
  xmlSaveCtxtPtr ctx;

  info = (InfinotedPluginTrafficLoggingConnectionInfo*)user_data;

  buffer = xmlBufferCreate();
  ctx = xmlSaveToBuffer(buffer, "UTF-8", 0);
  xmlSaveTree(ctx, xml);
  xmlSaveClose(ctx);

  infinoted_plugin_traffic_logging_write(
    info,
    "<<< %s",
    (const gchar*)xmlBufferContent(buffer)
  );

  xmlBufferFree(buffer);
}

static void
infinoted_plugin_traffic_logging_sent_cb(InfXmlConnection* conn,
                                         xmlNodePtr xml,
                                         gpointer user_data)
{
  InfinotedPluginTrafficLoggingConnectionInfo* info;
  xmlBufferPtr buffer;
  xmlSaveCtxtPtr ctx;

  info = (InfinotedPluginTrafficLoggingConnectionInfo*)user_data;

  buffer = xmlBufferCreate();
  ctx = xmlSaveToBuffer(buffer, "UTF-8", 0);
  xmlSaveTree(ctx, xml);
  xmlSaveClose(ctx);

  infinoted_plugin_traffic_logging_write(
    info,
    ">>> %s",
    (const gchar*)xmlBufferContent(buffer)
  );

  xmlBufferFree(buffer);
}

static void
infinoted_plugin_traffic_logging_error_cb(InfXmlConnection* conn,
                                          const GError* error,
                                          gpointer user_data)
{
  InfinotedPluginTrafficLoggingConnectionInfo* info;
  gchar* text;

  info = (InfinotedPluginTrafficLoggingConnectionInfo*)user_data;

  text = g_strdup_printf(_("Connection error: %s"), error->message);
  infinoted_plugin_traffic_logging_write(info, "!!! %s", text);
  g_free(text);
}

static void
infinoted_plugin_traffic_logging_info_initialize(gpointer plugin_info)
{
  InfinotedPluginTrafficLogging* plugin;
  plugin = (InfinotedPluginTrafficLogging*)plugin_info;

  plugin->manager = NULL;
  plugin->path = NULL;
}

static gboolean
infinoted_plugin_traffic_logging_initialize(InfinotedPluginManager* manager,
                                            gpointer plugin_info,
                                            GError** error)
{
  InfinotedPluginTrafficLogging* plugin;
  plugin = (InfinotedPluginTrafficLogging*)plugin_info;

  plugin->manager = manager;

  return TRUE;
}

static void
infinoted_plugin_traffic_logging_deinitialize(gpointer plugin_info)
{
  InfinotedPluginTrafficLogging* plugin;
  plugin = (InfinotedPluginTrafficLogging*)plugin_info;

  g_free(plugin->path);
}

static void
infinoted_plugin_traffic_logging_connection_added(
  InfXmlConnection* connection,
  gpointer plugin_info,
  gpointer connection_info)
{
  InfinotedPluginTrafficLogging* plugin;
  InfinotedPluginTrafficLoggingConnectionInfo* info;
  gchar* remote_id;
  gchar* basename;
  gchar* c;
  gchar* text;
  GError* error;

  plugin = (InfinotedPluginTrafficLogging*)plugin_info;
  info = (InfinotedPluginTrafficLoggingConnectionInfo*)connection_info;

  info->plugin = plugin;
  info->connection = connection;
  info->filename = NULL;
  info->file = NULL;

  g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);

  basename = g_strdup(remote_id);
  for(c = basename; *c != '\0'; ++c)
    if(*c == '[' || *c == ']')
      *c = '_';
  info->filename = g_build_filename(plugin->path, basename, NULL);
  g_free(basename);

  error = NULL;
  if(infinoted_util_create_dirname(info->filename, &error) == FALSE)
  {
    basename = g_path_get_dirname(info->filename);

    infinoted_log_warning(
      infinoted_plugin_manager_get_log(plugin->manager),
      _("Failed to create directory \"%s\": %s\nTraffic logging "
        "for connection \"%s\" is disabled."),
      basename,
      error->message,
      remote_id
    );

    g_error_free(error);
    g_free(basename);
  }
  else
  {
    info->file = fopen(info->filename, "a");
    if(info->file == NULL)
    {
      infinoted_log_warning(
        infinoted_plugin_manager_get_log(plugin->manager),
        _("Failed to open file \"%s\": %s\nTraffic logging "
          "for connection \"%s\" is disabled."),
        info->filename,
        strerror(errno),
        remote_id
      );
    }
    else
    {
      text = g_strdup_printf(_("%s connected"), remote_id);
      infinoted_plugin_traffic_logging_write(info, "!!! %s", text);
      g_free(text);

      g_signal_connect(
        G_OBJECT(connection),
        "received",
        G_CALLBACK(infinoted_plugin_traffic_logging_received_cb),
        info
      );

      g_signal_connect(
        G_OBJECT(connection),
        "sent",
        G_CALLBACK(infinoted_plugin_traffic_logging_sent_cb),
        info
      );

      g_signal_connect(
        G_OBJECT(connection),
        "error",
        G_CALLBACK(infinoted_plugin_traffic_logging_error_cb),
        info
      );
    }
  }

  g_free(remote_id);
}

static void
infinoted_plugin_traffic_logging_connection_removed(
  InfXmlConnection* connection,
  gpointer plugin_info,
  gpointer connection_info)
{
  InfinotedPluginTrafficLogging* plugin;
  InfinotedPluginTrafficLoggingConnectionInfo* info;
  gchar* remote_id;

  plugin = (InfinotedPluginTrafficLogging*)plugin_info;
  info = (InfinotedPluginTrafficLoggingConnectionInfo*)connection_info;

  if(info->file != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(infinoted_plugin_traffic_logging_received_cb),
      info
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(infinoted_plugin_traffic_logging_sent_cb),
      info
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(infinoted_plugin_traffic_logging_error_cb),
      info
    );

    infinoted_plugin_traffic_logging_write(info, "!!! %s", _("Log closed"));

    if(fclose(info->file) == -1)
    {
      infinoted_log_warning(
        infinoted_plugin_manager_get_log(plugin->manager),
        "Failed to close file \"%s\": %s",
        info->filename,
        strerror(errno)
      );
    }
  }

  g_free(info->filename);
}

static const InfinotedParameterInfo
INFINOTED_PLUGIN_TRAFFIC_LOGGING_OPTIONS[] = {
  {
    "path",
    INFINOTED_PARAMETER_STRING,
    INFINOTED_PARAMETER_REQUIRED,
    offsetof(InfinotedPluginTrafficLogging, path),
    infinoted_parameter_convert_filename,
    0,
    N_("The directory into which to write the log files."),
    N_("DIRECTORY")
  }, {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "traffic-logging",
  N_("This plugin writes a dump of all network traffic between the server "
     "and the client into a file. Note that the traffic written to the log "
     "files is unencrypted. It is meant to be used as a diagnostic tool for "
     "debugging, and it should never be used in a production environment as "
     "it is a severe privacy issue."),
  INFINOTED_PLUGIN_TRAFFIC_LOGGING_OPTIONS,
  sizeof(InfinotedPluginTrafficLogging),
  sizeof(InfinotedPluginTrafficLoggingConnectionInfo),
  0,
  NULL,
  infinoted_plugin_traffic_logging_info_initialize,
  infinoted_plugin_traffic_logging_initialize,
  infinoted_plugin_traffic_logging_deinitialize,
  infinoted_plugin_traffic_logging_connection_added,
  infinoted_plugin_traffic_logging_connection_removed,
  NULL,
  NULL
};

/* vim:set et sw=2 ts=2: */
