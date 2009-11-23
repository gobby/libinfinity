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

#include <infinoted/infinoted-run.h>
#include <infinoted/infinoted-creds.h>
#include <infinoted/infinoted-util.h>
#include <infinoted/infinoted-note-plugin.h>

#include <libinfinity/adopted/inf-adopted-session.h>
#include <libinfinity/adopted/inf-adopted-session-record.h>
#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-discovery-avahi.h>
#include <libinfinity/common/inf-xmpp-manager.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-config.h>

/* TODO: Put the record stuff into a separate infinoted-record.[hc] */

#include <glib/gstdio.h>

#include <sys/stat.h>
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

static gboolean
infinoted_run_load_dh_params(InfinotedRun* run,
                             GError** error)
{
  gnutls_dh_params_t dh_params;
  gchar* filename;
  struct stat st;

  /* We don't need DH params when there are no credentials,
   * i.e. we don't use TLS */
  if(run->creds != NULL)
  {
    dh_params = NULL;
    filename =
      g_build_filename(g_get_home_dir(), ".infinoted", "dh.pem", NULL);

    if(g_stat(filename, &st) == 0)
    {
      /* DH params expire every week */
      if(st.st_mtime + 60 * 60 * 24 * 7 > time(NULL))
        dh_params = infinoted_creds_read_dh_params(filename, NULL);
    }

    if(dh_params == NULL)
    {
      infinoted_util_create_dirname(filename, NULL);

      printf(_("Generating 2048 bit Diffie-Hellman parameters...\n"));
      dh_params = infinoted_creds_create_dh_params(error);

      if(dh_params == NULL)
      {
        g_free(filename);
        return FALSE;
      }

      infinoted_creds_write_dh_params(dh_params, filename, NULL);
    }

    g_free(filename);

    gnutls_certificate_set_dh_params(run->creds, dh_params);
  }

  return TRUE;
}

static gboolean
infinoted_run_load_directory(InfinotedRun* run,
                             InfinotedStartup* startup,
                             GError** error)
{
  /* TODO: Allow different storage plugins */
  InfdFilesystemStorage* storage;
  InfCommunicationManager* communication_manager;

#ifdef G_OS_WIN32
  gchar* module_path;
#endif
  gchar* plugin_path;

  storage = infd_filesystem_storage_new(startup->options->root_directory);

  communication_manager = inf_communication_manager_new();

  run->io = inf_standalone_io_new();

  run->directory = infd_directory_new(
    INF_IO(run->io),
    INFD_STORAGE(storage),
    communication_manager
  );

  infd_directory_enable_chat(run->directory, TRUE);

  g_object_unref(storage);
  g_object_unref(communication_manager);

#ifdef G_OS_WIN32
  module_path = g_win32_get_package_installation_directory_of_module(NULL);
  plugin_path = g_build_filename(module_path, "lib", PLUGIN_BASEPATH, NULL);
  g_free(module_path);
#else
  plugin_path = g_build_filename(PLUGIN_LIBPATH, PLUGIN_BASEPATH, NULL);
#endif

  if(!infinoted_note_plugin_load_directory(plugin_path, run->directory))
  {
    g_free(plugin_path);

    g_object_unref(run->directory);
    g_object_unref(run->io);
    run->directory = NULL;
    run->io = NULL;

    g_set_error(
      error,
      g_quark_from_static_string("INFINOTED_STARTUP_ERROR"),
      0,
      "Failed to load note plugins"
    );

    return FALSE;
  }

  g_free(plugin_path);
  return TRUE;
}

static InfdTcpServer*
infinoted_run_create_server(InfinotedRun* run,
                            InfinotedStartup* startup,
                            InfIpAddress* address,
                            GError** error)
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

  if(!infd_tcp_server_bind(tcp, error))
  {
    g_object_unref(tcp);
    return NULL;
  }

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
  GError* local_error;

  run = g_slice_new(InfinotedRun);
  run->creds = startup->credentials;

  if(infinoted_run_load_directory(run, startup, error) == FALSE)
  {
    g_slice_free(InfinotedRun, run);
    return NULL;
  }

  run->pool = infd_server_pool_new(run->directory);

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
  run->tcp6 = infinoted_run_create_server(run, startup, address, NULL);

  local_error = NULL;
  run->tcp4 = infinoted_run_create_server(run, startup, NULL, &local_error);

  if(run->tcp4 == NULL)
  {
    /* Ignore if we have an IPv6 server running */
    if(run->tcp6 != NULL)
    {
      g_error_free(local_error);
    }
    else
    {
      g_propagate_error(error, local_error);
#ifdef LIBINFINITY_HAVE_AVAHI
      g_object_unref(run->avahi);
#endif
      g_object_unref(run->pool);
      g_object_unref(run->directory);
      g_object_unref(run->io);
      g_slice_free(InfinotedRun, run);
      return NULL;
    }
  }

  inf_ip_address_free(address);

  g_signal_connect(
    G_OBJECT(run->directory),
    "add-session",
    G_CALLBACK(infinoted_run_directory_add_session_cb),
    run
  );

  g_signal_connect(
    G_OBJECT(run->directory),
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

  if(run->creds)
    if(run->dh_params != NULL)
      gnutls_dh_params_deinit(run->dh_params);

  g_slice_free(InfinotedRun, run);
}

/**
 * infinoted_run_start:
 * @run: A #InfinotedRun.
 *
 * Starts the infinote server. This runs in a loop until infinoted_run_stop()
 * is called. This may fail in theory, but hardly does in practise. If it
 * fails, it prints an error message to stderr and returns. It may also block
 * before starting to generate DH parameters for key exchange.
 */
void
infinoted_run_start(InfinotedRun* run)
{
  GError* error;
  guint port;

  error = NULL;

  /* Load DH parameters */
  if(infinoted_run_load_dh_params(run, &error) == FALSE)
  {
    printf(_("Failed to generate Diffie-Hellman parameters: %s\n"),
           error->message);
    g_error_free(error);
    return;
  }

  /* Open server sockets, accepting incoming connections */
  if(run->tcp6 != NULL)
  {
    if(infd_tcp_server_open(run->tcp6, &error) == TRUE)
    {
      g_object_get(G_OBJECT(run->tcp6), "local-port", &port, NULL);
      fprintf(stderr, _("IPv6 Server running on port %u\n"), port);
    }
    else
    {
      fprintf(stderr, _("Failed to start IPv6 server: %s\n"), error->message);
      g_error_free(error);
      error = NULL;

      g_object_unref(run->tcp6);
      run->tcp6 = NULL;
    }
  }

  if(run->tcp4 != NULL)
  {
    if(infd_tcp_server_open(run->tcp4, &error) == TRUE)
    {
      g_object_get(G_OBJECT(run->tcp4), "local-port", &port, NULL);
      fprintf(stderr, _("IPv4 Server running on port %u\n"), port);
    }
    else
    {
      fprintf(stderr, _("Failed to start IPv4 server: %s\n"), error->message);
      g_error_free(error);
      error = NULL;

      g_object_unref(run->tcp4);
      run->tcp4 = NULL;
    }
  }

  /* Make sure messages are shown. This explicit flush is for example
   * required when running in an MSYS shell on Windows. */
  fflush(stderr);

  if(run->tcp4 != NULL || run->tcp6 != NULL)
    inf_standalone_io_loop(run->io);
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
