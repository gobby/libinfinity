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

#include <infinoted/infinoted-signal.h>
#include <infinoted/infinoted-run.h>
#include <infinoted/infinoted-startup.h>
#include <infinoted/infinoted-util.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-config.h>

#ifdef LIBINFINITY_HAVE_LIBDAEMON
#include <libdaemon/dfork.h>
#include <libdaemon/dpid.h>
#include <libdaemon/dlog.h>
#endif

#include <locale.h>
#include <errno.h>
#include <string.h>

/* TODO: Move to util? Other files use something similar... */
static void
infinoted_main_set_errno_error(GError** error,
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
static const gchar*
infinoted_main_get_pidfile_path(void) {
  static gchar* path = NULL;
  if(path) return path;

  path = g_strdup_printf("%s/.infinoted/infinoted.pid", g_get_home_dir());
  infinoted_util_create_dirname(path, NULL);
  return path;
}
#endif

/* Takes ownership of startup */
static gboolean
infinoted_main_run(InfinotedStartup* startup,
                   GError** error)
{
  InfinotedRun* run;
  InfinotedSignal* sig;

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  pid_t pid;
#endif

  /* infinoted_run_new() takes ownership of startup */
  run = infinoted_run_new(startup, error);
  if(run == NULL)
  {
    infinoted_startup_free(startup);
    return FALSE;
  }

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  if(startup->options->daemonize)
  {
    pid = daemon_fork();
    if(pid < 0)
    {
      /* Translators: fork as in "fork into the background" */
      infinoted_main_set_errno_error(error, errno, _("Failed to fork"));
      infinoted_run_free(run);
      return FALSE;
    }
    else if(pid > 0)
    {
      infinoted_run_free(run);
      return TRUE;
    }
    else
    {
      if(daemon_pid_file_create() != 0)
      {
        if(errno == EPERM)
        {
          daemon_pid_file_proc = infinoted_main_get_pidfile_path;
          if(daemon_pid_file_create() != 0)
          {
            infinoted_main_set_errno_error(
              error,
              errno,
              _("Failed to create PID file")
            );

            infinoted_run_free(run);
            return FALSE;
          }
        }
      }
    }
  }
#endif

  sig = infinoted_signal_register(run);

  /* Now start the server. It can later be stopped by signals. */
  infinoted_run_start(run);

  infinoted_signal_unregister(sig);
  infinoted_run_free(run);

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  if(startup->options->daemonize)
    daemon_pid_file_remove();
#endif

  return TRUE;
}

static gboolean
infinoted_main(int argc,
               char* argv[],
               GError** error)
{
  InfinotedStartup* startup;
  gboolean result;

  startup = infinoted_startup_new(&argc, &argv, error);

  if(startup == NULL)
    return FALSE;

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  if(startup->options->daemonize)
  {
    daemon_pid_file_ident = daemon_ident_from_argv0(argv[0]);
    daemon_log_ident = daemon_pid_file_ident;
  }
#endif

  result = infinoted_main_run(startup, error);

  return result;
}

int
main(int argc,
     char* argv[])
{
  GError* error;

  setlocale(LC_ALL, "");

  error = NULL;
  if(infinoted_main(argc, argv, &error) == FALSE)
  {
    infinoted_util_log_error("%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  return 0;
}

/* vim:set et sw=2 ts=2: */
