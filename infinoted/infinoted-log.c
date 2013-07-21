/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#include <infinoted/infinoted-log.h>

#include <libinfinity/adopted/inf-adopted-session.h>
#include <libinfinity/adopted/inf-adopted-algorithm.h>

#include <libinfinity/inf-i18n.h>

#include <libxml/xmlsave.h>

#ifdef G_OS_WIN32
/* Arbitrary; they are not used currently anyway */
# define LOG_ERR 0
# define LOG_WARNING 1
# define LOG_INFO 2
# include <windows.h>
#else
# include <syslog.h>
#endif

#include <time.h>
#include <errno.h>

struct _InfinotedLogSession {
  InfinotedLog* log;
  InfSession* session;
  gchar* path;
};

static void
infinoted_log_logv(InfinotedLog* log,
                   int prio,
                   const char* fmt,
                   va_list ap,
                   const char* extra_log)
{
  time_t cur_time;
  struct tm* cur_tm;
  char time_msg[128];
  va_list ap2;

  if(log->file != NULL)
  {
    cur_time = time(NULL);
    cur_tm = localtime(&cur_time);

    switch(prio)
    {
    case LOG_ERR:
      strftime(time_msg, 128, "[%c]   ERROR: ", cur_tm);
      break;
    case LOG_WARNING:
      strftime(time_msg, 128, "[%c] WARNING: ", cur_tm);
      break;
    case LOG_INFO:
      strftime(time_msg, 128, "[%c]    INFO: ", cur_tm);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    /* Copy the va_list so that we don't corrupt the original that we
     * are going to hand to daemon_logv of vfprintf. */
    va_copy(ap2, ap);
    fputs(time_msg, log->file);
    vfprintf(log->file, fmt, ap2);
    fputc('\n', log->file);

    /* Print extra information about what caused the log message */
    if(extra_log != NULL)
    {
      fprintf(
        log->file,
        "\t%s\n",
        extra_log
      );
    }

    fflush(log->file);
  }

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  daemon_logv(prio, fmt, ap);
#else
#ifdef G_OS_WIN32
  /* On Windows, convert to the character set of the console */
  gchar* out;
  gchar* codeset;
  gchar* converted;

  out = g_strdup_vprintf(fmt, ap);
  codeset = g_strdup_printf("CP%u", (guint)GetConsoleOutputCP());
  converted = g_convert(out, -1, codeset, "UTF-8", NULL, NULL, NULL);
  g_free(out);
  g_free(codeset);

  fprintf(stderr, "%s\n", converted);
  g_free(converted);
#else
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
#endif /* !G_OS_WIN32 */
#endif /* !LIBINFINITY_HAVE_LIBDAEMON */
}

static void
infinoted_log_log(InfinotedLog* log,
                  int prio,
                  const char* extra,
                  const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  infinoted_log_logv(log, prio, fmt, ap, extra);
  va_end(ap);
}

static gchar*
infinoted_log_get_default_extra(InfinotedLog* log)
{
  gchar* request_str;
  const gchar* user_name;
  InfXmlConnection* user_connection;
  gchar* user_connection_str;
  InfAdoptedSessionRecord* record;
  gchar* record_filename;
  gchar* record_basename;
  gchar* document_name;
  gchar* extra;

  if(log->current_session != NULL && log->current_user != NULL &&
     log->current_request != NULL)
  {
    request_str = inf_adopted_state_vector_to_string(
      inf_adopted_request_get_vector(log->current_request)
    );

    user_name = inf_user_get_name(INF_USER(log->current_user));
    user_connection = inf_user_get_connection(INF_USER(log->current_user));

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

    record = NULL;
    record_basename = NULL;
    if(log->record != NULL)
    {
      record = infinoted_record_get_for_session(
        log->record,
        INF_ADOPTED_SESSION(log->current_session->session)
      );

      if(record != NULL)
      {
        g_object_get(G_OBJECT(record), "filename", &record_filename, NULL);
        record_basename = g_path_get_basename(record_filename);
        g_free(record_filename);
      }
    }

    if(record_basename == NULL)
    {
      document_name = g_strdup(log->current_session->path);
    }
    else
    {
      document_name = g_strdup_printf(
        "%s (%s)",
        log->current_session->path,
        record_basename
      );

      g_free(record_basename);
    }

    extra = g_strdup_printf(
      _("\twhen executing request \"%s\" from user %s (%s) in document %s"),
      request_str,
      user_name,
      user_connection_str,
      document_name
    );

    g_free(document_name);
    g_free(user_connection_str);
    g_free(request_str);

    return extra;
  }
  else
  {
    return NULL;
  }
}

static void
infinoted_log_session_error_cb(InfSession* session,
                               InfXmlConnection* connection,
                               xmlNodePtr xml,
                               const GError* error,
                               gpointer user_data)
{
  InfinotedLogSession* log_session;
  InfinotedLog* log;
  InfAdoptedSessionRecord* record;
  gchar* connection_str;
  gchar* record_filename;
  gchar* record_basename;
  gchar* document_name;
  xmlBufferPtr buffer;
  xmlSaveCtxtPtr ctx;
  gchar* extra;

  log_session = (InfinotedLogSession*)user_data;
  log = log_session->log;

  g_object_get(G_OBJECT(connection), "remote-id", &connection_str, NULL);

  record = NULL;
  record_basename = NULL;
  if(log->record != NULL)
  {
    record = infinoted_record_get_for_session(
      log->record,
      INF_ADOPTED_SESSION(session)
    );

    if(record != NULL)
    {
      g_object_get(G_OBJECT(record), "filename", &record_filename, NULL);
      record_basename = g_path_get_basename(record_filename);
      g_free(record_filename);
    }
  }

  if(record_basename == NULL)
  {
    document_name = g_strdup(log_session->path);
  }
  else
  {
    document_name = g_strdup_printf(
      "%s (%s)",
      log_session->path,
      record_basename
    );

    g_free(record_basename);
   }

  buffer = xmlBufferCreate();
  ctx = xmlSaveToBuffer(buffer, "UTF-8", XML_SAVE_FORMAT);
  xmlSaveTree(ctx, xml);
  xmlSaveClose(ctx);

  extra = g_strdup_printf(
    _("in document %s from connection %s. The request was:\n\n%s"),
    document_name,
    connection_str,
    (const gchar*)xmlBufferContent(buffer)
  );

  g_free(connection_str);
  g_free(document_name);
  xmlBufferFree(buffer);

  infinoted_log_log(
    log,
    LOG_WARNING,
    extra,
    "Session error: %s",
    error->message
  );

  g_free(extra);
}

static void
infinoted_log_session_execute_request_before_cb(InfAdoptedAlgorithm* algo,
                                                InfAdoptedUser* user,
                                                InfAdoptedRequest* request,
                                                gboolean apply,
                                                gpointer user_data)
{
  InfinotedLogSession* log_session;
  log_session = (InfinotedLogSession*)user_data;

  g_assert(log_session->log->current_session == NULL);
  g_assert(log_session->log->current_request == NULL);

  /* Don't need to ref these */
  log_session->log->current_session = log_session;
  log_session->log->current_user = user;
  log_session->log->current_request = request;
}

static void
infinoted_log_session_execute_request_after_cb(InfAdoptedAlgorithm* algo,
                                               InfAdoptedUser* user,
                                               InfAdoptedRequest* request,
                                               gboolean apply,
                                               gpointer user_data)
{
  InfinotedLogSession* log_session;
  log_session = (InfinotedLogSession*)user_data;

  g_assert(log_session->log->current_session == log_session);
  g_assert(log_session->log->current_user == user);
  g_assert(log_session->log->current_request == request);

  log_session->log->current_session = NULL;
  log_session->log->current_user = NULL;
  log_session->log->current_request = NULL;
}

static void
infinoted_log_session_notify_status_cb(GObject* object,
                                       GParamSpec* pspec,
                                       gpointer user_data)
{
  InfinotedLogSession* log_session;
  InfAdoptedAlgorithm* algorithm;

  log_session = (InfinotedLogSession*)user_data;

  g_assert(INF_ADOPTED_IS_SESSION(object));

  if(inf_session_get_status(INF_SESSION(object)) == INF_SESSION_RUNNING)
  {
    inf_signal_handlers_disconnect_by_func(
      object,
      G_CALLBACK(infinoted_log_session_notify_status_cb),
      log_session
    );

    algorithm =
      inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(object));

    g_signal_connect(
      G_OBJECT(algorithm),
      "execute-request",
      G_CALLBACK(infinoted_log_session_execute_request_before_cb),
      log_session
    );

    g_signal_connect_after(
      G_OBJECT(algorithm),
      "execute-request",
      G_CALLBACK(infinoted_log_session_execute_request_after_cb),
      log_session
    );
  }
}

static void
infinoted_log_add_session(InfinotedLog* log,
                          const InfBrowserIter* iter,
                          InfSession* session)
{
  InfinotedLogSession* log_session;
  InfAdoptedAlgorithm* algorithm;

  log_session = g_slice_new(InfinotedLogSession);

  log_session->log = log;
  log_session->session = session;
  log_session->path = inf_browser_get_path(INF_BROWSER(log->directory), iter);
  g_object_ref(session);

  g_signal_connect(
    G_OBJECT(session),
    "error",
    G_CALLBACK(infinoted_log_session_error_cb),
    log_session
  );
  
  if(INF_ADOPTED_IS_SESSION(session))
  {
    if(inf_session_get_status(session) == INF_SESSION_RUNNING)
    {
      algorithm =
        inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));

      g_signal_connect(
        G_OBJECT(algorithm),
        "execute-request",
        G_CALLBACK(infinoted_log_session_execute_request_before_cb),
        log_session
      );

      g_signal_connect_after(
        G_OBJECT(algorithm),
        "execute-request",
        G_CALLBACK(infinoted_log_session_execute_request_after_cb),
        log_session
      );
    }
    else
    {
      g_signal_connect(
        G_OBJECT(session),
        "notify::status",
        G_CALLBACK(infinoted_log_session_notify_status_cb),
        log_session
      );
    }
  }

  log->sessions = g_slist_prepend(log->sessions, log_session);
}

static void
infinoted_log_remove_session(InfinotedLog* log,
                             InfSession* session)
{
  InfinotedLogSession* log_session;
  GSList* item;
  InfAdoptedAlgorithm* algorithm;

  for(item = log->sessions; item != NULL; item = item->next)
    if( ((InfinotedLogSession*)item->data)->session == session)
      break;

  g_assert(item != NULL);
  log_session = (InfinotedLogSession*)item->data;

  if(INF_ADOPTED_IS_SESSION(session))
  {
    inf_signal_handlers_disconnect_by_func(
      session,
      G_CALLBACK(infinoted_log_session_notify_status_cb),
      log_session
    );

    algorithm =
      inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));

    inf_signal_handlers_disconnect_by_func(
      algorithm,
      G_CALLBACK(infinoted_log_session_execute_request_before_cb),
      log_session
    );

    inf_signal_handlers_disconnect_by_func(
      algorithm,
      G_CALLBACK(infinoted_log_session_execute_request_after_cb),
      log_session
    );
  }

  inf_signal_handlers_disconnect_by_func(
    session,
    G_CALLBACK(infinoted_log_session_error_cb),
    log_session
  );

  /* If we are in the middle of an execute of this session, then clear the
   * current pointers, because we won't get notified upon execution finish
   * anymore. */
  if(log_session == log->current_session)
  {
    log->current_session = NULL;
    log->current_user = NULL;
    log->current_request = NULL;
  }

  log->sessions = g_slist_delete_link(log->sessions, item);

  g_free(log_session->path);
  g_object_unref(log_session->session);
  g_slice_free(InfinotedLogSession, log_session);
}

static void
infinoted_log_subscribe_session_cb(InfBrowser* browser,
                                   const InfBrowserIter* iter,
                                   InfSessionProxy* proxy,
                                   gpointer user_data)
{
  InfinotedLog* log;
  InfSession* session;

  log = (InfinotedLog*)user_data;

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  infinoted_log_add_session(log, iter, session);
  g_object_unref(session);
}

static void
infinoted_log_unsubscribe_session_cb(InfBrowser* browser,
                                     const InfBrowserIter* iter,
                                     InfSessionProxy* proxy,
                                     gpointer user_data)
{
  InfinotedLog* log;
  InfSession* session;

  log = (InfinotedLog*)user_data;

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);
  infinoted_log_remove_session(log, session);
  g_object_unref(session);
}


static void
infinoted_log_connection_added_cb(InfdDirectory* directory,
                                  InfXmlConnection* connection,
                                  gpointer user_data)
{
  InfinotedLog* log;
  gchar* remote_id;

  log = (InfinotedLog*)user_data;
  g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);

  infinoted_log_info(
    log,
    _("%s connected"),
    remote_id
  );

  g_free(remote_id);
}

static void
infinoted_log_connection_removed_cb(InfdDirectory* directory,
                                    InfXmlConnection* connection,
                                    gpointer user_data)
{
  InfinotedLog* log;
  gchar* remote_id;

  log = (InfinotedLog*)user_data;
  g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);

  infinoted_log_info(
    log,
    _("%s disconnected"),
    remote_id
  );

  g_free(remote_id);
}

static void
infinoted_log_connection_error_cb(InfXmlConnection* connection,
                                  gpointer error,
                                  gpointer user_data)
{
  const GError* err;
  InfinotedLog* log;
  gchar* remote_id;

  err = (const GError*)error;
  log = (InfinotedLog*)user_data;
  g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);

  infinoted_log_error(
    log,
    _("Error from connection %s: %s"),
    remote_id,
    err->message
  );

  g_free(remote_id);
}

static void
infinoted_log_set_directory_remove_func(InfXmlConnection* connection,
                                        gpointer user_data)
{
  InfinotedLog* log;
  log = (InfinotedLog*)user_data;

  inf_signal_handlers_disconnect_by_func(
    connection,
    G_CALLBACK(infinoted_log_connection_error_cb),
    log
  );
}

static void
infinoted_log_set_directory_add_func(InfXmlConnection* connection,
                                     gpointer user_data)
{
  InfinotedLog* log;
  log = (InfinotedLog*)user_data;

  g_signal_connect(
    connection,
    "error",
    G_CALLBACK(infinoted_log_connection_error_cb),
    log
  );
}

static void
infinoted_log_handler(const gchar* log_domain,
                      GLogLevelFlags log_level,
                      const gchar* message,
                      gpointer user_data)
{
  InfinotedLog* log;
  log = (InfinotedLog*)user_data;

  switch(log_level & G_LOG_LEVEL_MASK)
  {
  case G_LOG_LEVEL_ERROR:
  case G_LOG_LEVEL_CRITICAL:
    if(log_domain)
      infinoted_log_error(log, "%s: %s", log_domain, message);
    else
      infinoted_log_error(log, "%s", message);
    break;
  case G_LOG_LEVEL_WARNING:
    if(log_domain)
      infinoted_log_warning(log, "%s: %s", log_domain, message);
    else
      infinoted_log_warning(log, "%s", message);
    break;
  case G_LOG_LEVEL_MESSAGE:
  case G_LOG_LEVEL_INFO:
  case G_LOG_LEVEL_DEBUG:
    if(log_domain)
      infinoted_log_info(log, "%s: %s", log_domain, message);
    else
      infinoted_log_info(log, "%s", message);
    break;
  }

  if(log_level & G_LOG_FLAG_FATAL)
    abort();
}

/**
 * infinoted_log_new:
 * @options: A #InfinotedOptions object.
 * @error: Location to store error information, if any.
 *
 * Creates a new #InfinotedLog. The log path is read from the options object.
 *
 * Returns: A new #InfinotedLog, or %NULL on error.
 */
InfinotedLog*
infinoted_log_new(InfinotedOptions* options,
                  GError** error)
{
  InfinotedLog* log;
  log = g_slice_new(InfinotedLog);
  log->file = NULL;
  log->directory = NULL;
  log->connections = NULL;
  log->sessions = NULL;
  log->current_session = NULL;
  log->current_user = NULL;
  log->current_request = NULL;

  if(options->log_path != NULL)
  {
    log->file = fopen(options->log_path, "a");
    if(log->file == NULL)
    {
      infinoted_util_set_errno_error(error, errno, "Failed to open log file");
      infinoted_log_free(log);
      return FALSE;
    }
  }

  log->prev_log_handler = g_log_set_default_handler(
    infinoted_log_handler,
    log
  );

  return log;
}

/**
 * infinoted_log_free:
 * @log: A #InfinotedLog object created with infinoted_log_new().
 *
 * Frees @log so that it can no longer be used. Allocated resources
 * are returned to the system.
 */
void
infinoted_log_free(InfinotedLog* log)
{
  if(log->directory != NULL)
    infinoted_log_set_directory(log, NULL);

  g_assert(log->current_session == NULL);
  g_assert(log->current_user == NULL);
  g_assert(log->current_request == NULL);
  g_assert(log->sessions == NULL);
  g_assert(log->connections == NULL);

  if(log->file != NULL)
    fclose(log->file);

  g_log_set_default_handler(log->prev_log_handler, NULL);
  g_slice_free(InfinotedLog, log);
}

/**
 * infinoted_log_set_directory:
 * @log: A #InfinotedLog.
 * @directory: A #InfdDirectory being monitored by @log, or %NULL.
 *
 * If @directory is non-%NULL then @log will monitor interesting events that
 * happen to the directory, such as new connections, new sessions or node
 * creation or removal. Those events are written to the log file.
 */
void
infinoted_log_set_directory(InfinotedLog* log,
                            InfdDirectory* directory)
{
  if(log->directory != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      log->directory,
      G_CALLBACK(infinoted_log_connection_added_cb),
      log
    );

    inf_signal_handlers_disconnect_by_func(
      log->directory,
      G_CALLBACK(infinoted_log_connection_removed_cb),
      log
    );

    inf_signal_handlers_disconnect_by_func(
      log->directory,
      G_CALLBACK(infinoted_log_subscribe_session_cb),
      log
    );

    inf_signal_handlers_disconnect_by_func(
      log->directory,
      G_CALLBACK(infinoted_log_unsubscribe_session_cb),
      log
    );

    infd_directory_foreach_connection(
      log->directory,
      infinoted_log_set_directory_remove_func,
      log
    );

    /* Remove all sessions */
    while(log->sessions != NULL)
    {
      infinoted_log_remove_session(
        log,
        ((InfinotedLogSession*)log->sessions->data)->session
      );
    }

    g_object_unref(log->directory);
  }

  log->directory = directory;

  if(directory)
  {
    g_object_ref(directory);

    g_signal_connect(
      G_OBJECT(directory),
      "connection-added",
      G_CALLBACK(infinoted_log_connection_added_cb),
      log
    );

    g_signal_connect(
      G_OBJECT(directory),
      "connection-removed",
      G_CALLBACK(infinoted_log_connection_removed_cb),
      log
    );

    g_signal_connect(
      G_OBJECT(directory),
      "subscribe-session",
      G_CALLBACK(infinoted_log_subscribe_session_cb),
      log
    );

    g_signal_connect(
      G_OBJECT(directory),
      "unsubscribe-session",
      G_CALLBACK(infinoted_log_unsubscribe_session_cb),
      log
    );

    infd_directory_foreach_connection(
      log->directory,
      infinoted_log_set_directory_add_func,
      log
    );

    /* TODO: Add all running sessions in directory */
  }
}

/**
 * infinoted_log_set_record:
 * @log: A #InfinotedLog.
 * @record: The #InfinotedRecord to set, or %NULL.
 *
 * Sets the record for @log to @record. If a record is set, then for error
 * messages that appear while executing a request the filename of the record
 * is being logged as well, so that it makes it simple to debug it with the
 * corresponding record file, should the need arise.
 */
void
infinoted_log_set_record(InfinotedLog* log,
                         InfinotedRecord* record)
{
  log->record = record;
}

/**
 * infinoted_log_error:
 * @log: A #InfinotedLog.
 * @fmt: A printf-style format string.
 * ...: Format arguments.
 *
 * Logs an error message. If the server is daemonized, log to syslog,
 * otherwise log to stderr. If a logfile is given in the options when @log
 * was created, the logfile is written to as well.
 */
void
infinoted_log_error(InfinotedLog* log, const char* fmt, ...)
{
  va_list ap;
  gchar* extra;

  extra = infinoted_log_get_default_extra(log);
  va_start(ap, fmt);
  infinoted_log_logv(log, LOG_ERR, fmt, ap, extra);
  va_end(ap);
  g_free(extra);
}

/**
 * infinoted_log_warning:
 * @log: A #InfinotedLog.
 * @fmt: A printf-style format string.
 * ...: Format arguments.
 *
 * Logs a warning message. If the server is daemonized, log to syslog,
 * otherwise log to stderr. If a logfile is given in the options when @log
 * was created, the logfile is written to as well.
 */
void
infinoted_log_warning(InfinotedLog* log, const char* fmt, ...)
{
  va_list ap;
  gchar* extra;

  extra = infinoted_log_get_default_extra(log);
  va_start(ap, fmt);
  infinoted_log_logv(log, LOG_WARNING, fmt, ap, extra);
  va_end(ap);
  g_free(extra);
}

/**
 * infinoted_log_info:
 * @log: A #InfinotedLog.
 * @fmt: A printf-style format string.
 * ...: Format arguments.
 *
 * Logs an info message. If the server is daemonized, log to syslog,
 * otherwise log to stderr. If a logfile is given in the options when @log
 * was created, the logfile is written to as well.
 */
void
infinoted_log_info(InfinotedLog* log, const char* fmt, ...)
{
  va_list ap;
  gchar* extra;

  extra = infinoted_log_get_default_extra(log);
  va_start(ap, fmt);
  infinoted_log_logv(log, LOG_INFO, fmt, ap, extra);
  va_end(ap);
  g_free(extra);
}

/* vim:set et sw=2 ts=2: */
