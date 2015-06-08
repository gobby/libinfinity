/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:infinoted-log
 * @title: InfinotedLog
 * @short_description: A class to handle logging of messages.
 * @include: infinoted/infinoted-log.h
 * @stability: Unstable
 *
 * #InfinotedLog manages a message log. Messages can be written to the log
 * either as informational, warning and error messages. If the log was
 * successfully opened, also a glib logging handler is installed which
 * redirects glib logging to this class. Log output is always shown on
 * stderr and, optionally, can be duplicated to a file as well.
 **/

#include <infinoted/infinoted-log.h>
#include <infinoted/infinoted-util.h>

#include <libinfinity/inf-i18n.h>

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#ifdef LIBINFINITY_HAVE_LIBDAEMON
# include <libdaemon/dlog.h>
#endif

#ifdef G_OS_WIN32
/* Arbitrary; they are not used currently anyway */
# define LOG_ERR 0
# define LOG_WARNING 1
# define LOG_INFO 2
# include <windows.h>
#else
# include <syslog.h>
#endif

typedef struct _InfinotedLogPrivate InfinotedLogPrivate;
struct _InfinotedLogPrivate {
  gchar* file_path;
  FILE* log_file;
  GLogFunc prev_log_handler;
  GRecMutex mutex;

  guint recursion_depth;
};

enum {
  PROP_0,

  /* read only */
  PROP_FILE_PATH
};

enum {
  LOG_MESSAGE,

  LAST_SIGNAL
};

#define INFINOTED_LOG_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFINOTED_TYPE_LOG, InfinotedLogPrivate))

static guint log_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(InfinotedLog, infinoted_log, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfinotedLog))

static void
infinoted_log_handler(const gchar* log_domain,
                      GLogLevelFlags log_level,
                      const gchar* message,
                      gpointer user_data)
{
  InfinotedLog* log;
  log = INFINOTED_LOG(user_data);

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

static void
infinoted_log_write(InfinotedLog* log,
                    guint prio,
                    guint depth,
                    const gchar* text)
{
  InfinotedLogPrivate* priv;
  time_t cur_time;
  struct tm* cur_tm;
  char time_msg[128];
  gchar* final_text;

  priv = INFINOTED_LOG_PRIVATE(log);

  if(depth == 0)
  {
    cur_time = time(NULL);
    cur_tm = localtime(&cur_time);

    switch(prio)
    {
    case LOG_ERR:
      strftime(time_msg, 128, "[%c]   ERROR", cur_tm);
      break;
    case LOG_WARNING:
      strftime(time_msg, 128, "[%c] WARNING", cur_tm);
      break;
    case LOG_INFO:
      strftime(time_msg, 128, "[%c]    INFO", cur_tm);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    final_text = g_strdup_printf("%s: %s", time_msg, text);
  }
  else
  {
    final_text = g_strdup_printf("\t%s", text);
  }

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  daemon_log(prio, "%s", final_text);
#else
#ifdef G_OS_WIN32
  /* On Windows, convert to the character set of the console */
  gchar* codeset;
  gchar* converted;

  codeset = g_strdup_printf("CP%u", (guint)GetConsoleOutputCP());
  converted = g_convert(final_text, -1, codeset, "UTF-8", NULL, NULL, NULL);
  g_free(codeset);

  fprintf(stderr, "%s\n", converted);
  g_free(converted);
#else
  fprintf(stderr, "%s\n", final_text);
#endif /* !G_OS_WIN32 */
#endif /* !LIBINFINITY_HAVE_LIBDAEMON */

  if(priv->log_file != NULL)
  {
    fprintf(priv->log_file, "%s\n", final_text);
    fflush(priv->log_file);
  }

  g_free(final_text);
}

static void
infinoted_log_entry(InfinotedLog* log,
                    guint prio,
                    const gchar* fmt,
                    va_list args)
{
  /* This is an entry point for the three public functions. */
  InfinotedLogPrivate* priv;
  gchar* text;
  guint depth;

  priv = INFINOTED_LOG_PRIVATE(log);
  text = g_strdup_vprintf(fmt, args);

  g_rec_mutex_lock(&priv->mutex);

  depth = priv->recursion_depth++;

  g_signal_emit(log, log_signals[LOG_MESSAGE], 0, prio, depth, text);

  g_assert(priv->recursion_depth == depth + 1);
  --priv->recursion_depth;

  g_rec_mutex_unlock(&priv->mutex);

  g_free(text);
}

static void
infinoted_log_init(InfinotedLog* log)
{
  InfinotedLogPrivate* priv;
  priv = INFINOTED_LOG_PRIVATE(log);

  priv->file_path = NULL;
  priv->log_file = NULL;
  priv->prev_log_handler = NULL;
  priv->recursion_depth = 0;

  g_rec_mutex_init(&priv->mutex);
}

static void
infinoted_log_finalize(GObject* object)
{
  InfinotedLog* log;
  InfinotedLogPrivate* priv;

  log = INFINOTED_LOG(object);
  priv = INFINOTED_LOG_PRIVATE(log);

  if(priv->log_file != NULL)
    infinoted_log_close(log);

  g_rec_mutex_clear(&priv->mutex);

  G_OBJECT_CLASS(infinoted_log_parent_class)->finalize(object);
}

static void
infinoted_log_set_property(GObject* object,
                           guint prop_id,
                           const GValue* value,
                           GParamSpec* pspec)
{
  InfinotedLog* log;
  InfinotedLogPrivate* priv;

  log = INFINOTED_LOG(object);
  priv = INFINOTED_LOG_PRIVATE(log);

  switch(prop_id)
  {
  case PROP_FILE_PATH:
    /* read only */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infinoted_log_get_property(GObject* object,
                           guint prop_id,
                           GValue* value,
                           GParamSpec* pspec)
{
  InfinotedLog* log;
  InfinotedLogPrivate* priv;

  log = INFINOTED_LOG(object);
  priv = INFINOTED_LOG_PRIVATE(log);

  switch(prop_id)
  {
  case PROP_FILE_PATH:
    g_value_set_string(value, priv->file_path);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infinoted_log_log_message(InfinotedLog* log,
                          guint prio,
                          guint depth,
                          const gchar* text)
{
  InfinotedLogPrivate* priv;
  priv = INFINOTED_LOG_PRIVATE(log);

  g_assert(priv->recursion_depth == depth+1);
  infinoted_log_write(log, prio, depth, text);
}

static void
infinoted_log_class_init(InfinotedLogClass* log_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(log_class);

  object_class->finalize = infinoted_log_finalize;
  object_class->set_property = infinoted_log_set_property;
  object_class->get_property = infinoted_log_get_property;

  log_class->log_message = infinoted_log_log_message;

  g_object_class_install_property(
    object_class,
    PROP_FILE_PATH,
    g_param_spec_string(
      "file-path",
      "File Path",
      "Path to the log file",
      NULL,
      G_PARAM_READABLE
    )
  );

  /**
   * InfinotedLog::log-message:
   * @log: The #InfinotedLog that is logging a message.
   * @prio: The priority of the logged message.
   * @depth: The recursion depth of the logged message.
   * @text: The logged message text.
   *
   * This signal is emitted when a new line of log message is written to the
   * log.
   */
  log_signals[LOG_MESSAGE] = g_signal_new(
    "log-message",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_FIRST,
    G_STRUCT_OFFSET(InfinotedLogClass, log_message),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    3,
    G_TYPE_UINT,
    G_TYPE_UINT,
    G_TYPE_STRING
  );
}

/**
 * infinoted_log_new: (constructor)
 *
 * Creates a new #InfinotedLog.
 *
 * Returns: (transfer full): A new #InfinotedLog. Free with g_object_unref()
 * when no longer needed.
 */
InfinotedLog*
infinoted_log_new(void)
{
  GObject* object = g_object_new(INFINOTED_TYPE_LOG, NULL);
  return INFINOTED_LOG(object);
}

/**
 * infinoted_log_open:
 * @log: A #InfinotedLog.
 * @path: (type filename) (allow-none): The path to the log file to write,
 * or %NULL.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Attempts to open the log file at the given path. If the log file could not
 * be opened the function returns %FALSE and @error is set. If the log file
 * exists already then new log messages will be appended.
 *
 * If @path is %NULL no log file is opened and logging only occurs to stderr.
 *
 * Returns: %TRUE on success, or %FALSE otherwise.
 */
gboolean
infinoted_log_open(InfinotedLog* log,
                   const gchar* path,
                   GError** error)
{
  InfinotedLogPrivate* priv;

  g_return_val_if_fail(INFINOTED_IS_LOG(log), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INFINOTED_LOG_PRIVATE(log);
  g_rec_mutex_lock(&priv->mutex);

  g_assert(priv->prev_log_handler == NULL);

  if(path != NULL)
  {
    g_assert(priv->log_file == NULL);
    priv->log_file = fopen(path, "a");
    if(priv->log_file == NULL)
    {
      infinoted_util_set_errno_error(error, errno, "Failed to open log file");
      return FALSE;
    }

    g_assert(priv->file_path == NULL);
    priv->file_path = g_strdup(path);
  }

  priv->prev_log_handler = g_log_set_default_handler(
    infinoted_log_handler,
    log
  );

  g_rec_mutex_unlock(&priv->mutex);

  if(path != NULL)
    g_object_notify(G_OBJECT(log), "file-path");

  return TRUE;
}

/**
 * infinoted_log_close:
 * @log: A #InfinotedLog.
 *
 * Closes a #InfinotedLog object opened with infinoted_log_open(). After the
 * log was closed it can be opened again with a different file. The log is
 * closed automatically on destruction.
 */
void
infinoted_log_close(InfinotedLog* log)
{
  InfinotedLogPrivate* priv;
  
  g_return_if_fail(INFINOTED_IS_LOG(log));
  priv = INFINOTED_LOG_PRIVATE(log);

  g_rec_mutex_lock(&priv->mutex);
  g_assert(priv->prev_log_handler != NULL);

  if(priv->log_file != NULL)
  {
    g_assert(priv->file_path != NULL);

    fclose(priv->log_file);
    priv->log_file = NULL;

    g_free(priv->file_path);
    priv->file_path = NULL;
  }

  g_assert(priv->file_path == NULL);

  g_log_set_default_handler(priv->prev_log_handler, NULL);
  priv->prev_log_handler = NULL;
  g_rec_mutex_unlock(&priv->mutex);

  g_object_notify(G_OBJECT(log), "file-path");
}

/**
 * infinoted_log_log:
 * @log: A #InfinotedLog.
 * @prio: Priority of the logged message.
 * @fmt: A printf-style format string.
 * @...: Format arguments.
 *
 * Logs a message with the given priority. The priority is one of %LOG_ERR,
 * %LOG_WARNING or %LOG_INFO.  If the server is daemonized, log to syslog,
 * otherwise log to stderr. If a logfile is given in the options when @log
 * was created, the logfile is written to as well.
 */
void
infinoted_log_log(InfinotedLog* log,
                  guint prio,
                  const char* fmt,
                  ...)
{
  va_list ap;
  va_start(ap, fmt);
  infinoted_log_entry(log, prio, fmt, ap);
  va_end(ap);
}

/**
 * infinoted_log_error:
 * @log: A #InfinotedLog.
 * @fmt: A printf-style format string.
 * @...: Format arguments.
 *
 * Logs an error message. If the server is daemonized, log to syslog,
 * otherwise log to stderr. If a logfile is given in the options when @log
 * was created, the logfile is written to as well.
 */
void
infinoted_log_error(InfinotedLog* log,
                    const char* fmt,
                    ...)
{
  va_list ap;
  va_start(ap, fmt);
  infinoted_log_entry(log, LOG_ERR, fmt, ap);
  va_end(ap);
}

/**
 * infinoted_log_warning:
 * @log: A #InfinotedLog.
 * @fmt: A printf-style format string.
 * @...: Format arguments.
 *
 * Logs a warning message. If the server is daemonized, log to syslog,
 * otherwise log to stderr. If a logfile is given in the options when @log
 * was created, the logfile is written to as well.
 */
void
infinoted_log_warning(InfinotedLog* log,
                      const char* fmt,
                      ...)
{
  va_list ap;
  va_start(ap, fmt);
  infinoted_log_entry(log, LOG_WARNING, fmt, ap);
  va_end(ap);
}

/**
 * infinoted_log_info:
 * @log: A #InfinotedLog.
 * @fmt: A printf-style format string.
 * @...: Format arguments.
 *
 * Logs an info message. If the server is daemonized, log to syslog,
 * otherwise log to stderr. If a logfile is given in the options when @log
 * was created, the logfile is written to as well.
 */
void
infinoted_log_info(InfinotedLog* log,
                   const char* fmt,
                   ...)
{
  va_list ap;
  va_start(ap, fmt);
  infinoted_log_entry(log, LOG_INFO, fmt, ap);
  va_end(ap);
}

/* vim:set et sw=2 ts=2: */
