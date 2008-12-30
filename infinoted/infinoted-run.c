/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <infinoted/infinoted-run.h>

#include <libinfinity/adopted/inf-adopted-session.h>
#include <libinfinity/adopted/inf-adopted-session-record.h>
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/common/inf-discovery-avahi.h>
#include <libinfinity/common/inf-xmpp-manager.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-config.h>

/* TODO: Put the record stuff into a separate infinoted-record.[hc] */

#include <string.h>
#include <errno.h>

static const guint8 INFINOTED_RUN_IPV6_ANY_ADDR[16] =
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static void
infinoted_run_record_real(InfAdoptedSession* session,
                          const gchar* filename,
                          const gchar* title)
{
  GError* error;
  InfAdoptedSessionRecord* record;

  record = inf_adopted_session_record_new(session);
  error = NULL;

  inf_adopted_session_record_start_recording(record, filename, &error);

  if(error != NULL)
  {
    g_warning(_("Error while writing record for session "
                "\"%s\" into \"%s\": %s"),
              title, filename, error->message);
    g_error_free(error);
  }
  else
  {
    g_object_set_data_full(
      G_OBJECT(session),
      "INFINOTED_SESSION_RECORD",
      record,
      g_object_unref
    );
  }
}

static void
infinoted_run_record(InfAdoptedSession* session,
                     const gchar* title)
{
  gchar* dirname;
  gchar* basename;
  gchar* filename;
  guint i;
  gsize pos;

  dirname = g_build_filename(g_get_home_dir(), ".infinoted-records", NULL);
  basename = g_build_filename(dirname, title, NULL);

  pos = strlen(basename) + 8;
  filename = g_strdup_printf("%s.record-00000.xml", basename);
  g_free(basename);

  i = 0;
  while(g_file_test(filename, G_FILE_TEST_EXISTS) && ++i < 100000)
  {
    g_snprintf(filename + pos, 10, "%05u.xml", i);
  }

  if(i >= 100000)
  {
    g_warning(
      _("Could not create record file for session \"%s\": Could not generate "
        "unused record file in directory \"%s\""),
      title,
      dirname
    );
  }
  else
  {
    /* TODO: Use GetLastError() on Win32 */
    if(g_mkdir_with_parents(dirname, 0700) == -1)
    {
      g_warning(
        _("Could not create record file directory \"%s\": %s"),
        strerror(errno)
      );
    }
    else
    {
      infinoted_run_record_real(session, filename, title);
    }
  }

  g_free(filename);
  g_free(dirname);
}

static void
infinoted_run_directory_add_session_cb(InfdDirectory* directory,
                                       InfdDirectoryIter* iter,
                                       InfdSessionProxy* proxy,
                                       gpointer user_data)
{
  const gchar* title;

  if(INF_ADOPTED_IS_SESSION(infd_session_proxy_get_session(proxy)))
  {
    title = infd_directory_iter_get_name(directory, iter);

    infinoted_run_record(
      INF_ADOPTED_SESSION(infd_session_proxy_get_session(proxy)),
      title
    );
  }
}

static void
infinoted_run_directory_remove_session_cb(InfdDirectory* directory,
                                          InfdDirectoryIter* iter,
                                          InfdSessionProxy* proxy,
                                          gpointer user_data)
{
  InfSession* session;
  session = infd_session_proxy_get_session(proxy);

  g_object_set_data(G_OBJECT(session), "INFINOTED_SESSION_RECORD", NULL);
}

static InfdTcpServer*
infinoted_run_create_server(InfinotedRun* run,
                            InfinotedStartup* startup,
                            InfIpAddress* address)
{
  InfdTcpServer* tcp;
  InfdXmppServer* xmpp;

  tcp = INFD_TCP_SERVER(
    g_object_new(
      INFD_TYPE_TCP_SERVER,
      "io", INF_IO(run->io),
      "local-address", address,
      "local-port", startup->options->port,
      NULL
    )
  );

  xmpp = infd_xmpp_server_new(
    tcp,
    startup->options->security_policy,
    startup->credentials,
    NULL,
    NULL
  );

  infd_server_pool_add_server(run->pool, INFD_XML_SERVER(xmpp));

#ifdef LIBINFINITY_HAVE_AVAHI
  infd_server_pool_add_local_publisher(
    run->pool,
    xmpp,
    INF_LOCAL_PUBLISHER(run->avahi)
  );
#endif

  g_object_unref(xmpp);
  return tcp;
}

/**
 * infinoted_run_new:
 * @startup: Startup parameters for the Infinote Server.
 * @error: Location to store error information, if any.
 *
 * Creates all necessary ressources for running an Infinote server. @startup
 * is used by the #InfinotedRun, so it must not be freed as long as the
 * #InfinotedRun object is still alive.
 *
 * Use infinoted_run_start() to start the server.
 *
 * Returns: A new #InfinotedRun, free with infinoted_run_free(). Or %NULL,
 * on error.
 */
InfinotedRun*
infinoted_run_new(InfinotedStartup* startup,
                  GError** error)
{
  InfIpAddress* address;
#ifdef LIBINFINITY_HAVE_AVAHI
  InfXmppManager* xmpp_manager;
#endif

  InfinotedRun* run;

  /* TODO: Find out why the startup needs to create a directory, and an IO
   * object. It would be better to create both here, so we don't have to
   * rely on IO being a InfStandaloneIo. */

  run = g_slice_new(InfinotedRun);
  run->io = INF_STANDALONE_IO(infd_directory_get_io(startup->directory));
  run->directory = startup->directory;
  g_object_ref(run->io);
  g_object_ref(run->directory);

  g_signal_connect(
    G_OBJECT(startup->directory),
    "add-session",
    G_CALLBACK(infinoted_run_directory_add_session_cb),
    run
  );

  g_signal_connect(
    G_OBJECT(startup->directory),
    "remove-session",
    G_CALLBACK(infinoted_run_directory_remove_session_cb),
    run
  );

  if(startup->options->autosave_interval > 0)
  {
    run->autosave = infinoted_autosave_new(
      run->directory,
      startup->options->autosave_interval
    );
  }
  else
  {
    run->autosave = NULL;
  }

  run->pool = infd_server_pool_new(startup->directory);

#ifdef LIBINFINITY_HAVE_AVAHI
  xmpp_manager = inf_xmpp_manager_new();

  run->avahi = inf_discovery_avahi_new(
    INF_IO(run->io),
    xmpp_manager,
    startup->credentials,
    NULL,
    NULL
  );

  g_object_unref(xmpp_manager);
#endif

  address = inf_ip_address_new_raw6(INFINOTED_RUN_IPV6_ANY_ADDR);
  run->tcp4 = infinoted_run_create_server(run, startup, NULL);
  run->tcp6 = infinoted_run_create_server(run, startup, address);
  inf_ip_address_free(address);

  return run;
}

/**
 * infinoted_run_free:
 * @run: A #InfinotedRun.
 *
 * Frees the given #InfinotedRun, so that it can no longer be used.
 */
void
infinoted_run_free(InfinotedRun* run)
{
  if(inf_standalone_io_loop_running(run->io))
    inf_standalone_io_loop_quit(run->io);

  if(run->tcp6 != NULL)
    g_object_unref(run->tcp6);
  if(run->tcp4 != NULL)
    g_object_unref(run->tcp4);

#ifdef LIBINFINITY_HAVE_AVAHI
  g_object_unref(run->avahi);
#endif

  if(run->autosave != NULL)
    infinoted_autosave_free(run->autosave);

  g_object_unref(run->io);
  g_object_unref(run->directory);
  g_object_unref(run->pool);
  g_slice_free(InfinotedRun, run);
}

/**
 * infinoted_run_start:
 * @run: A #InfinotedRun.
 * @error: Location to store error information, if any.
 *
 * Starts the infinote server. This runs in a loop until infinoted_run_stop()
 * is called. If the server could not be started, the function returns %FALSE
 * and @error is set.
 *
 * Returns: %TRUE if the server was started, %FALSE otherwise.
 */
gboolean
infinoted_run_start(InfinotedRun* run,
                    GError** error)
{
  GError* local_error;
  guint port;

  if(infd_tcp_server_open(run->tcp6, NULL) == TRUE)
  {
    g_object_get(G_OBJECT(run->tcp6), "local-port", &port, NULL);
    fprintf(stderr, _("IPv6 Server running on port %u\n"), port);
  }
  else
  {
    g_object_unref(run->tcp6);
    run->tcp6 = NULL;
  }

  /* On Linux, the IPv6 server also handles IPv4 connections, and opening a
   * separate IPv4 server does not work because the address is already on use.
   * On Windows however, we need to start both servers. */
  local_error = NULL;
  if(infd_tcp_server_open(run->tcp4, &local_error) == TRUE)
  {
    g_object_get(G_OBJECT(run->tcp4), "local-port", &port, NULL);
    fprintf(stderr, _("IPv4 Server running on port %u\n"), port);
  }
  else
  {
    g_object_unref(run->tcp4);
    run->tcp4 = NULL;

    /* Ignore if we have an IPv6 server running */
    if(run->tcp6 != NULL)
      g_error_free(local_error);
    else
    {
      g_propagate_error(error, local_error);
      return FALSE;
    }
  }

  /* Make sure messages are shown. This explicit flush is for example
   * required when running in an MSYS shell on Windows. */
  fflush(stderr);

  inf_standalone_io_loop(run->io);

  return TRUE;
}

/**
 * infinoted_run_stop:
 * @run: A #InfinotedRun.
 *
 * Stops a running infinote server.
 */
void
infinoted_run_stop(InfinotedRun* run)
{
  inf_standalone_io_loop_quit(run->io);
}

/* vim:set et sw=2 ts=2: */
