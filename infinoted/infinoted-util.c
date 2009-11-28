/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <infinoted/infinoted-util.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-config.h>

#ifdef LIBINFINITY_HAVE_LIBDAEMON
#include <libdaemon/dlog.h>
#include <libdaemon/dpid.h>

#include <unistd.h> /* for access(2) */

#define INFINOTED_PID_FILE_DIRECTORY \
  LOCALSTATEDIR "/run/infinoted-" LIBINFINITY_API_VERSION

#endif

#include <glib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

static void
infinoted_util_logv(int prio, const char* fmt, va_list ap)
{
#ifdef LIBINFINITY_HAVE_LIBDAEMON
  daemon_logv(prio, fmt, ap);
#else
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
#endif
}

#ifdef LIBINFINITY_HAVE_LIBDAEMON
static const gchar*
infinoted_util_get_pidfile_path_user(void) {
  static gchar* path = NULL;
  if(path) return path;

  path = g_strdup_printf(
                 "%s/.infinoted/infinoted-" LIBINFINITY_API_VERSION ".pid",
                 g_get_home_dir());
  infinoted_util_create_dirname(path, NULL);
  return path;
}

static const gchar*
infinoted_util_get_pidfile_path_system(void) {
  return INFINOTED_PID_FILE_DIRECTORY
           "/infinoted-" LIBINFINITY_API_VERSION ".pid";
}
#endif

/**
 * infinoted_util_create_dirname:
 * @path: The filename to create a path to.
 * @error: Location to store error information, if any.
 *
 * Creates directories leading to the given path. Does not create a directory
 * for the last component of the path, assuming that it is a filename that
 * you are going to write into that directory later.
 *
 * Returns: %TRUE on success, or %FALSE on error in which case @error is set.
 */
gboolean
infinoted_util_create_dirname(const gchar* path,
                              GError** error)
{
  gchar* dirname;
  int save_errno;

  dirname = g_path_get_dirname(path);

  if(g_mkdir_with_parents(dirname, 0700) != 0)
  {
    save_errno = errno;

    g_set_error(
      error,
      g_quark_from_static_string("ERRNO_ERROR"),
      save_errno,
      _("Could not create directory \"%s\": %s"),
      dirname,
      strerror(save_errno)
    );

    g_free(dirname);
    return FALSE;
  }

  g_free(dirname);
  return TRUE;
}

/**
 * infinoted_util_log_error:
 * @fmt: A printf-style format string.
 * ...: Format arguments.
 *
 * Logs an error message. If the server is daemonized, log to syslog,
 * otherwise log to stderr.
 */
void
infinoted_util_log_error(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  infinoted_util_logv(LOG_ERR, fmt, ap);
  va_end(ap);
}

/**
 * infinoted_util_log_warning:
 * @fmt: A printf-style format string.
 * ...: Format arguments.
 *
 * Logs a warning message. If the server is daemonized, log to syslog,
 * otherwise log to stderr.
 */
void
infinoted_util_log_warning(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  infinoted_util_logv(LOG_WARNING, fmt, ap);
  va_end(ap);
}

/**
 * infinoted_util_log_info:
 * @fmt: A printf-style format string.
 * ...: Format arguments.
 *
 * Logs an info message. If the server is daemonized, log to syslog,
 * otherwise log to stderr.
 */
void
infinoted_util_log_info(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  infinoted_util_logv(LOG_INFO, fmt, ap);
  va_end(ap);
}

/**
 * infinoted_util_set_errno_error:
 * @error: A pointer to a #GError pointer, or %NULL.
 * @save_errno: An errno variable.
 * @prefix: A prefix string, or %NULL.
 *
 * Sets @error to @save_errno with domain ERRNO_ERROR. If @prefix is
 * non-%NULL, @prefix is prefixed to @error's message, obtained by strerror().
 */
void
infinoted_util_set_errno_error(GError** error,
                               int save_errno,
                               const char* prefix)
{
  if(prefix != NULL)
  {
    g_set_error(
      error,
      g_quark_from_static_string("ERRNO_ERROR"),
      save_errno,
      "%s: %s",
      prefix,
      strerror(save_errno)
    );
  }
  else
  {
    g_set_error(
      error,
      g_quark_from_static_string("ERRNO_ERROR"),
      save_errno,
      "%s",
      strerror(save_errno)
    );
  }
}

#ifdef LIBINFINITY_HAVE_LIBDAEMON
/**
 * infinoted_util_set_daemon_pid_file_proc:
 * @error: Location to store error information, if any.
 *
 * Sets @daemon_pid_file_proc to the location of infinoted's PID file.
 *
 * Returns: %TRUE on success, or %FALSE on error if no suitable location for
 * the PID file was found.
 */
gboolean
infinoted_util_set_daemon_pid_file_proc(GError** error)
{
  int saved_errno;

  if(access(INFINOTED_PID_FILE_DIRECTORY, W_OK) == 0)
  {
    daemon_pid_file_proc = infinoted_util_get_pidfile_path_system;
    return TRUE;
  }
  else
  {
    if(errno != EACCES)
    {
      saved_errno = errno;

      infinoted_util_set_errno_error(
        error,
        saved_errno,
        _("Failed to create PID file")
      );

      errno = saved_errno;
      return FALSE;
    }

    daemon_pid_file_proc = infinoted_util_get_pidfile_path_user;
    return TRUE;
  }
}
#endif

/* vim:set et sw=2 ts=2: */