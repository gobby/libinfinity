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

/* Takes ownership of startup */
static gboolean
infinoted_main_run(InfinotedStartup* startup,
                   GError** error)
{
  InfinotedRun* run;
  InfinotedSignal* sig;

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  pid_t pid;
  int saved_errno;
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
    if(daemon_retval_init() == -1)
    {
      infinoted_run_free(run);
      return FALSE; /* libdaemon already wrote an error message */
    }

    pid = daemon_fork();
    if(pid < 0)
    {
      /* Translators: fork as in "fork into the background" */
      infinoted_util_set_errno_error(error, errno, _("Failed to fork"));
      infinoted_run_free(run);
      daemon_retval_done();
      return FALSE;
    }
    else if(pid > 0)
    {
      infinoted_run_free(run);
      saved_errno = daemon_retval_wait(5);
      if(saved_errno == 0)
      {
        return TRUE;
      }
      if(saved_errno == -1)
      {
        infinoted_util_set_errno_error(error, errno,
          _("Failed to wait for daemonized child's return value"));
        return FALSE;
      }
      else
      {
        /* on -1, the child process would have subtracted one from
         * errno before passing it back to us. */
        if(saved_errno < 0) ++saved_errno;
        infinoted_util_set_errno_error(
          error, saved_errno, _("Failed to create PID file"));
        return FALSE;
      }
    }
    else
    {
      if(!infinoted_util_set_daemon_pid_file_proc(error))
      {
        infinoted_run_free(run);
        return FALSE;
      }

      if(daemon_pid_file_create() != 0)
      {
        saved_errno = errno;
        infinoted_util_set_errno_error(
          error,
          saved_errno,
          _("Failed to create PID file")
        );
        if(saved_errno < 0) --saved_errno;
        daemon_retval_send(saved_errno);

        infinoted_run_free(run);
        return FALSE;
      }

      daemon_retval_send(0);
    }

    /* libdaemon < 0.14 sets our umask to 0777, preventing the file storage
     * backend from working correctly. 0.14 uses 0077, but the documentation
     * still says 0777, and, anyway, 0.14 does not seem widespread enough
     * right now. */
    umask(0077);
  }
#endif

  sig = infinoted_signal_register(run);

  /* Now start the server. It can later be stopped by signals. */
  infinoted_run_start(run);

  infinoted_signal_unregister(sig);

#ifdef LIBINFINITY_HAVE_LIBDAEMON
  /* startup might be invalid at this point in case a config reload happened,
   * so use run->startup instead (which is revalidated by config reload). */
  if(run->startup->options->daemonize)
    daemon_pid_file_remove();
#endif

  infinoted_run_free(run);
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
    if(error)
    {
      infinoted_util_log_error("%s", error->message);
      g_error_free(error);
    }

    return -1;
  }

  return 0;
}

/* vim:set et sw=2 ts=2: */
