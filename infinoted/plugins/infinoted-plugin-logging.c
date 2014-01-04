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

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/adopted/inf-adopted-session-record.h>

#include <libinfinity/inf-i18n.h>

#include <libxml/xmlsave.h>

#include <string.h>

typedef struct _InfinotedPluginLogging InfinotedPluginLogging;
struct _InfinotedPluginLogging {
  InfinotedPluginManager* manager;

  gboolean log_connections;
  gboolean log_connection_errors;
  gboolean log_session_errors;
  gboolean log_session_request_extra;

  /* TODO: Make this a hash table, and use the thread ID as a key */
  gchar* extra_message;
  InfSessionProxy* current_session;
  InfAdoptedRequest* current_request;
  InfAdoptedUser* current_user;
};

typedef struct _InfinotedPluginLoggingSessionInfo
  InfinotedPluginLoggingSessionInfo;
struct _InfinotedPluginLoggingSessionInfo {
  InfinotedPluginLogging* plugin;
  InfSessionProxy* proxy;
  InfBrowserIter iter;
};

static gchar*
infinoted_plugin_logging_get_document_name(
  InfinotedPluginLoggingSessionInfo* info)
{
  InfSession* session;
  InfAdoptedSessionRecord* record;
  gchar* record_basename;
  gchar* record_filename;
  gchar* path;
  gchar* document_name;

  g_object_get(G_OBJECT(info->proxy), "session", &session, NULL);

  record = INF_ADOPTED_SESSION_RECORD(
    g_object_get_data(
      G_OBJECT(session),
      "infinoted-record"
    )
  );

  g_object_unref(session);

  record_basename = NULL;
  if(record != NULL)
  {
    g_object_get(G_OBJECT(record), "filename", &record_filename, NULL);
    record_basename = g_path_get_basename(record_filename);
    g_free(record_filename);
  }

  path = inf_browser_get_path(
    INF_BROWSER(
      infinoted_plugin_manager_get_directory(info->plugin->manager)
    ),
    &info->iter
  );

  if(record_basename == NULL)
  {
    document_name = path;
  }
  else
  {
    document_name = g_strdup_printf("%s (%s)", path, record_basename);

    g_free(record_basename);
    g_free(path);
  }

  return document_name;
}

static void
infinoted_plugin_logging_log_message_cb(InfinotedLog* log,
                                        guint priority,
                                        guint depth,
                                        const gchar* text,
                                        gpointer user_data)
{
  InfinotedPluginLogging* plugin;
  InfinotedPluginLoggingSessionInfo* info;
  gchar* request_str;
  const gchar* user_name;
  InfXmlConnection* user_connection;
  gchar* user_connection_str;
  gchar* document_name;

  plugin = (InfinotedPluginLogging*)user_data;

  if(depth == 0)
  {
    if(plugin->extra_message != NULL)
      infinoted_log_log(log, priority, "%s", plugin->extra_message);

    if(plugin->current_session != NULL &&
       plugin->current_request != NULL &&
       plugin->current_user != NULL)
    {
      info = infinoted_plugin_manager_get_session_info(
        plugin->manager,
        plugin,
        plugin->current_session
      );

      g_assert(info != NULL);

      request_str = inf_adopted_state_vector_to_string(
        inf_adopted_request_get_vector(plugin->current_request)
      );

      user_name = inf_user_get_name(INF_USER(plugin->current_user));
      user_connection =
        inf_user_get_connection(INF_USER(plugin->current_user));

      if(user_connection != NULL)
      {
        g_object_get(
          G_OBJECT(user_connection),
          "remote-id", &user_connection_str,
          NULL
        );
      }
      else
      {
        user_connection_str = g_strdup("local");
      }

      document_name = infinoted_plugin_logging_get_document_name(info);

      infinoted_log_log(
        log,
        priority,
        _("when executing request \"%s\" from user %s (%s) in document %s"),
        request_str,
        user_name,
        user_connection_str,
        document_name
      );

      g_free(document_name);
      g_free(user_connection_str);
      g_free(request_str);
    }
  }
}

static void
infinoted_plugin_logging_execute_request_before_cb(InfAdoptedAlgorithm* algo,
                                                   InfAdoptedUser* user,
                                                   InfAdoptedRequest* request,
                                                   gboolean apply,
                                                   gpointer user_data)
{
  InfinotedPluginLoggingSessionInfo* info;
  info = (InfinotedPluginLoggingSessionInfo*)user_data;

  g_assert(info->plugin->current_session == NULL);
  g_assert(info->plugin->current_user == NULL);
  g_assert(info->plugin->current_request == NULL);

  /* Don't need to ref these */
  info->plugin->current_session = info->proxy;
  info->plugin->current_user = user;
  info->plugin->current_request = request;
}

static void
infinoted_plugin_logging_execute_request_after_cb(InfAdoptedAlgorithm* algo,
                                                  InfAdoptedUser* user,
                                                  InfAdoptedRequest* request,
                                                  gboolean apply,
                                                  gpointer user_data)
{
  InfinotedPluginLoggingSessionInfo* info;
  info = (InfinotedPluginLoggingSessionInfo*)user_data;

  g_assert(info->plugin->current_session != NULL);
  g_assert(info->plugin->current_user != NULL);
  g_assert(info->plugin->current_request != NULL);

  /* Don't need to ref these */
  info->plugin->current_session = NULL;
  info->plugin->current_user = NULL;
  info->plugin->current_request = NULL;
}

static void
infinoted_plugin_logging_connection_error_cb(InfXmlConnection* connection,
                                             const GError* error,
                                             gpointer user_data)
{
  InfinotedPluginLogging* plugin;
  gchar* remote_id;

  plugin = (InfinotedPluginLogging*)user_data;
  g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);

  infinoted_log_error(
    infinoted_plugin_manager_get_log(plugin->manager),
    _("Error from connection %s: %s"),
    remote_id,
    error->message
  );

  g_free(remote_id);
}

static void
infinoted_pluggin_logging_session_error_cb(InfSession* session,
                                           InfXmlConnection* connection,
                                           xmlNodePtr xml,
                                           const GError* error,
                                           gpointer user_data)
{
  InfinotedPluginLoggingSessionInfo* info;
  InfAdoptedSessionRecord* record;
  gchar* connection_str;
  gchar* document_name;
  xmlBufferPtr buffer;
  xmlSaveCtxtPtr ctx;
  gchar* error_msg;
  gchar* extra_msg;

  info = (InfinotedPluginLoggingSessionInfo*)user_data;

  g_object_get(G_OBJECT(connection), "remote-id", &connection_str, NULL);

  document_name = infinoted_plugin_logging_get_document_name(info);

  buffer = xmlBufferCreate();
  ctx = xmlSaveToBuffer(buffer, "UTF-8", 0);
  xmlSaveTree(ctx, xml);
  xmlSaveClose(ctx);

  g_assert(info->plugin->extra_message == NULL);
  info->plugin->extra_message = g_strdup_printf(
    _("in document %s from connection %s. The request was: %s"),
    document_name,
    connection_str,
    (const gchar*)xmlBufferContent(buffer)
  );

  g_free(connection_str);
  g_free(document_name);
  xmlBufferFree(buffer);

  /* The extra message is being written inside the handler of the
   * InfinotedLog::log-message signal. */
  infinoted_log_error(
    infinoted_plugin_manager_get_log(info->plugin->manager),
    _("Session error: %s"),
    error->message
  );

  g_free(info->plugin->extra_message);
  info->plugin->extra_message = NULL;
}

static void
infinoted_plugin_logging_notify_status_cb(InfSession* session,
                                          GParamSpec* pspec,
                                          gpointer user_data)
{
  InfinotedPluginLoggingSessionInfo* info;
  InfAdoptedAlgorithm* algorithm;

  info = (InfinotedPluginLoggingSessionInfo*)user_data;
  g_assert(INF_ADOPTED_IS_SESSION(session));

  if(inf_session_get_status(session) == INF_SESSION_RUNNING)
  {
    algorithm =
      inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));

    g_signal_connect(
      G_OBJECT(algorithm),
      "execute-request",
      G_CALLBACK(infinoted_plugin_logging_execute_request_before_cb),
      info
    );

    g_signal_connect_after(
      G_OBJECT(algorithm),
      "execute-request",
      G_CALLBACK(infinoted_plugin_logging_execute_request_after_cb),
      info
    );
  }
}

static void
infinoted_plugin_logging_info_initialize(gpointer plugin_info)
{
  InfinotedPluginLogging* plugin;
  plugin = (InfinotedPluginLogging*)plugin_info;

  /* default values: log everything */
  plugin->log_connections = TRUE;
  plugin->log_connection_errors = TRUE;
  plugin->log_session_errors = TRUE;
  plugin->log_session_request_extra = TRUE;
}

static gboolean
infinoted_plugin_logging_initialize(InfinotedPluginManager* manager,
                                    gpointer plugin_info,
                                    GError** error)
{
  InfinotedPluginLogging* plugin;
  plugin = (InfinotedPluginLogging*)plugin_info;

  plugin->manager = manager;

  g_signal_connect(
    G_OBJECT(infinoted_plugin_manager_get_log(manager)),
    "log-message",
    G_CALLBACK(infinoted_plugin_logging_log_message_cb),
    plugin
  );

  plugin->extra_message = NULL;
  plugin->current_session = NULL;
  plugin->current_request = NULL;
  plugin->current_user = NULL;
}

static void
infinoted_plugin_logging_deinitialize(gpointer plugin_info)
{
  InfinotedPluginLogging* plugin;
  plugin = (InfinotedPluginLogging*)plugin_info;

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(infinoted_plugin_manager_get_log(plugin->manager)),
    G_CALLBACK(infinoted_plugin_logging_log_message_cb),
    plugin
  );
}

static void
infinoted_plugin_logging_connection_added(InfXmlConnection* connection,
                                          gpointer plugin_info,
                                          gpointer connection_info)
{
  InfinotedPluginLogging* plugin;
  gchar* remote_id;

  plugin = (InfinotedPluginLogging*)plugin_info;

  if(plugin->log_connection_errors)
  {
    g_signal_connect(
      G_OBJECT(connection),
      "error",
      G_CALLBACK(infinoted_plugin_logging_connection_error_cb),
      plugin
    );
  }

  if(plugin->log_connections)
  {
    g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);

    infinoted_log_info(
      infinoted_plugin_manager_get_log(plugin->manager),
      _("%s connected"),
      remote_id
    );

    g_free(remote_id);
  }
}

static void
infinoted_plugin_logging_connection_removed(InfXmlConnection* connection,
                                            gpointer plugin_info,
                                            gpointer connection_info)
{
  InfinotedPluginLogging* plugin;
  gchar* remote_id;

  plugin = (InfinotedPluginLogging*)plugin_info;

  if(plugin->log_connection_errors)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(connection),
      G_CALLBACK(infinoted_plugin_logging_connection_error_cb),
      plugin
    );
  }

  if(plugin->log_connections)
  {
    g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);

    infinoted_log_info(
      infinoted_plugin_manager_get_log(plugin->manager),
      _("%s disconnected"),
      remote_id
    );

    g_free(remote_id);
  }
}

static void
infinoted_plugin_logging_session_added(const InfBrowserIter* iter,
                                       InfSessionProxy* proxy,
                                       gpointer plugin_info,
                                       gpointer session_info)
{
  InfinotedPluginLoggingSessionInfo* info;
  InfSession* session;
  InfAdoptedAlgorithm* algorithm;

  info = (InfinotedPluginLoggingSessionInfo*)session_info;
  info->plugin = (InfinotedPluginLogging*)plugin_info;
  info->proxy = proxy;
  info->iter = *iter;

  g_object_ref(proxy);
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  if(info->plugin->log_session_errors)
  {
    g_signal_connect(
      G_OBJECT(session),
      "error",
      G_CALLBACK(infinoted_pluggin_logging_session_error_cb),
      info
    );
  }

  if(INF_ADOPTED_IS_SESSION(session) &&
     info->plugin->log_session_request_extra)
  {
    if(inf_session_get_status(session) == INF_SESSION_RUNNING)
    {
      algorithm =
        inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));

      g_signal_connect(
        G_OBJECT(algorithm),
        "execute-request",
        G_CALLBACK(infinoted_plugin_logging_execute_request_before_cb),
        info
      );

      g_signal_connect_after(
        G_OBJECT(algorithm),
        "execute-request",
        G_CALLBACK(infinoted_plugin_logging_execute_request_after_cb),
        info
      );
    }
    else
    {
      g_signal_connect(
        G_OBJECT(session),
        "notify::status",
        G_CALLBACK(infinoted_plugin_logging_notify_status_cb),
        info
      );
    }
  }

  g_object_unref(session);
}

static void
infinoted_plugin_logging_session_removed(const InfBrowserIter* iter,
                                         InfSessionProxy* proxy,
                                         gpointer plugin_info,
                                         gpointer session_info)
{
  InfinotedPluginLoggingSessionInfo* info;
  InfSession* session;
  InfAdoptedAlgorithm* algorithm;

  info = (InfinotedPluginLoggingSessionInfo*)session_info;
  g_assert(info->proxy == proxy);

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  if(info->plugin->log_session_errors)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(session),
      G_CALLBACK(infinoted_pluggin_logging_session_error_cb),
      info
    );
  }

  if(INF_ADOPTED_IS_SESSION(session) &&
     info->plugin->log_session_request_extra)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(session),
      G_CALLBACK(infinoted_plugin_logging_notify_status_cb),
      info
    );

    if(inf_session_get_status(session) == INF_SESSION_RUNNING)
    {
      algorithm =
        inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(algorithm),
        G_CALLBACK(infinoted_plugin_logging_execute_request_before_cb),
        info
      );

      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(algorithm),
        G_CALLBACK(infinoted_plugin_logging_execute_request_after_cb),
        info
      );
    }
  }

  g_object_unref(info->proxy);
  g_object_unref(session);
}

static const InfinotedParameterInfo INFINOTED_PLUGIN_LOGGING_OPTIONS[] = {
  {
    "log-connections",
    INFINOTED_PARAMETER_BOOLEAN,
    0,
    offsetof(InfinotedPluginLogging, log_connections),
    infinoted_parameter_convert_boolean,
    0,
    N_("Whether to write a log message when a new user connects "
       "or disconnects."),
    NULL
  }, {
    "log-connection-errors",
    INFINOTED_PARAMETER_BOOLEAN,
    0,
    offsetof(InfinotedPluginLogging, log_connection_errors),
    infinoted_parameter_convert_boolean,
    0,
    N_("Whether to write a log message when an error with a connection is "
       "detected, usually leading to disconnection of the user."),
    NULL
  }, {
    "log-session-errors",
    INFINOTED_PARAMETER_BOOLEAN,
    0,
    offsetof(InfinotedPluginLogging, log_session_errors),
    infinoted_parameter_convert_boolean,
    0,
    N_("Whether to write a log message when a session cannot process a "
       "client request, mostly caused by invalid client requests."),
    NULL
  }, {
    "log-session-request-extra",
    INFINOTED_PARAMETER_BOOLEAN,
    0,
    offsetof(InfinotedPluginLogging, log_session_request_extra),
    infinoted_parameter_convert_boolean,
    0,
    N_("Whether to write an additional line into the log when a message "
       "occurs during transformation of an adOPTed request. This is mostly "
       "used for debugging purposes to find problems in the server "
       "implementation itself."),
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
  "logging",
  N_("This plugin writes extra information into the infinoted log. By "
     "default all extra information is logged, but individual events "
     "can be turned off with the plugin options."),
  INFINOTED_PLUGIN_LOGGING_OPTIONS,
  sizeof(InfinotedPluginLogging),
  0,
  sizeof(InfinotedPluginLoggingSessionInfo),
  NULL,
  infinoted_plugin_logging_info_initialize,
  infinoted_plugin_logging_initialize,
  infinoted_plugin_logging_deinitialize,
  infinoted_plugin_logging_connection_added,
  infinoted_plugin_logging_connection_removed,
  infinoted_plugin_logging_session_added,
  infinoted_plugin_logging_session_removed
};

/* vim:set et sw=2 ts=2: */
