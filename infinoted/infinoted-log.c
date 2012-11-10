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

#include <infinoted/infinoted-log.h>

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

static void
infinoted_log_logv(InfinotedLog* log,
                   int prio,
                   const char* fmt,
                   va_list ap)
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
  g_assert(log->sessions == NULL);
  g_assert(log->connections == NULL);

  if(log->file != NULL)
    fclose(log->file);

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
  /* TODO */
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
  va_start(ap, fmt);
  infinoted_log_logv(log, LOG_ERR, fmt, ap);
  va_end(ap);
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
  va_start(ap, fmt);
  infinoted_log_logv(log, LOG_WARNING, fmt, ap);
  va_end(ap);
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
  va_start(ap, fmt);
  infinoted_log_logv(log, LOG_INFO, fmt, ap);
  va_end(ap);
}

/* vim:set et sw=2 ts=2: */
