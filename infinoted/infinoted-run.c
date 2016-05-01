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

#include <infinoted/infinoted-run.h>
#include <infinoted/infinoted-dh-params.h>
#include <infinoted/infinoted-util.h>

#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-filesystem-account-storage.h>
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-discovery-avahi.h>
#include <libinfinity/common/inf-xmpp-manager.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-config.h>

static const guint8 INFINOTED_RUN_IPV6_ANY_ADDR[16] =
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static gboolean
infinoted_run_load_directory(InfinotedRun* run,
                             InfinotedStartup* startup,
                             GError** error)
{
  InfdFilesystemStorage* storage;
  InfdFilesystemAccountStorage* account_storage;
  InfCommunicationManager* communication_manager;

#ifdef G_OS_WIN32
  gchar* module_path;
#endif
  gchar* plugin_path;
  gboolean result;

  storage = infd_filesystem_storage_new(startup->options->root_directory);

  communication_manager = inf_communication_manager_new();

  run->io = inf_standalone_io_new();

  run->directory = infd_directory_new(
    INF_IO(run->io),
    INFD_STORAGE(storage),
    communication_manager
  );

  infd_directory_enable_chat(run->directory, TRUE);

  g_object_unref(communication_manager);

  /* Load server plugins via plugin manager */
#ifdef G_OS_WIN32
  module_path = g_win32_get_package_installation_directory_of_module(NULL);
  plugin_path = g_build_filename(module_path, "lib", PLUGIN_PATH, NULL);
  g_free(module_path);
#else
  plugin_path = g_build_filename(PLUGIN_LIBPATH, PLUGIN_PATH, NULL);
#endif

  run->plugin_manager = infinoted_plugin_manager_new(
    run->directory,
    startup->log,
    startup->credentials
  );

  result = infinoted_plugin_manager_load(
    run->plugin_manager,
    plugin_path,
    (const gchar* const*)startup->options->plugins,
    startup->options->config_key_file,
    error
  );

  g_free(plugin_path);
  infinoted_options_drop_config_file(startup->options);

  if(result == FALSE)
  {
    g_object_unref(storage);

    g_object_unref(run->plugin_manager);
    g_object_unref(run->directory);
    g_object_unref(run->io);
    run->plugin_manager = NULL;
    run->directory = NULL;
    run->io = NULL;
    return FALSE;
  }

  /* Set an account storage, too. This can be replaced by something more fancy
   * by a plugin. */
  /* TODO: This filesystem storage should be moved into a plugin, so that
   * users can enable it optionally. */
  g_object_get(G_OBJECT(run->directory), "account-storage", &account_storage, NULL);
  if(account_storage == NULL)
  {
    account_storage = infd_filesystem_account_storage_new();

    result = infd_filesystem_account_storage_set_filesystem(
      account_storage,
      storage,
      error
    );

    if(result == FALSE)
    {
      g_object_unref(storage);
      g_object_unref(account_storage);

      g_object_unref(run->plugin_manager);
      g_object_unref(run->directory);
      g_object_unref(run->io);
      run->plugin_manager = NULL;
      run->directory = NULL;
      run->io = NULL;

      return FALSE;
    }

    g_object_set(
      G_OBJECT(run->directory),
      "account-storage",
      account_storage,
      NULL
    );
  }

  g_object_unref(storage);
  g_object_unref(account_storage);

  return TRUE;
}

static InfdXmppServer*
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

  infd_tcp_server_set_keepalive(tcp, &startup->keepalive);

  if(!infd_tcp_server_bind(tcp, error))
  {
    g_object_unref(tcp);
    return NULL;
  }

  xmpp = infd_xmpp_server_new(
    tcp,
    startup->options->security_policy,
    startup->credentials,
    startup->sasl_context,
    startup->sasl_context ? "PLAIN" : NULL
  );

  infd_server_pool_add_server(run->pool, INFD_XML_SERVER(xmpp));

#ifdef LIBINFINITY_HAVE_AVAHI
  infd_server_pool_add_local_publisher(
    run->pool,
    xmpp,
    INF_LOCAL_PUBLISHER(run->avahi)
  );
#endif

  g_object_unref(tcp);
  return xmpp;
}

/**
 * infinoted_run_new:
 * @startup: Startup parameters for the Infinote Server.
 * @error: Location to store error information, if any.
 *
 * Creates all necessary ressources for running an Infinote server. The
 * #InfinotedRun has taken ownership of @startup if this function returns
 * non-%NULL.
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
  run->startup = startup;
  run->dh_params = NULL;

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

  local_error = NULL;

  if(startup->options->listen_address != NULL)
  {
    /* Use manually specified listen address */
    address = inf_ip_address_copy(startup->options->listen_address);

    switch(inf_ip_address_get_family(address))
    {
    case INF_IP_ADDRESS_IPV4:
      run->xmpp4 = infinoted_run_create_server(run, startup, address, &local_error);
      run->xmpp6 = NULL;
      break;
    case INF_IP_ADDRESS_IPV6:
      run->xmpp4 = NULL;
      run->xmpp6 = infinoted_run_create_server(run, startup, address, &local_error);
      break;
    }
  }
  else
  {
    address = inf_ip_address_new_raw6(INFINOTED_RUN_IPV6_ANY_ADDR);

    run->xmpp6 = infinoted_run_create_server(run, startup, address, NULL);
    run->xmpp4 = infinoted_run_create_server(run, startup, NULL, &local_error);
  }

  if(run->xmpp4 == NULL)
  {
    /* Ignore if we have an IPv6 server running */
    if(run->xmpp6 != NULL)
    {
      if(local_error != NULL)
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
      run = NULL;
    }
  }

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
  InfdXmlServerStatus status;

  if(inf_standalone_io_loop_running(run->io))
    inf_standalone_io_loop_quit(run->io);

  if(run->xmpp6 != NULL)
  {
    g_object_get(G_OBJECT(run->xmpp6), "status", &status, NULL);
    infd_server_pool_remove_server(run->pool, INFD_XML_SERVER(run->xmpp6));
    if(status != INFD_XML_SERVER_CLOSED)
      infd_xml_server_close(INFD_XML_SERVER(run->xmpp6));
    g_object_unref(run->xmpp6);
  }

  if(run->xmpp4 != NULL)
  {
    g_object_get(G_OBJECT(run->xmpp4), "status", &status, NULL);
    infd_server_pool_remove_server(run->pool, INFD_XML_SERVER(run->xmpp4));
    if(status != INFD_XML_SERVER_CLOSED)
      infd_xml_server_close(INFD_XML_SERVER(run->xmpp4));
    g_object_unref(run->xmpp4);
  }

#ifdef LIBINFINITY_HAVE_AVAHI
  g_object_unref(run->avahi);
#endif

  if(run->plugin_manager != NULL)
  {
    g_object_unref(run->plugin_manager);
    run->plugin_manager = NULL;
  }

  g_object_unref(run->io);
  g_object_unref(run->directory);
  g_object_unref(run->pool);

  if(run->dh_params != NULL)
    gnutls_dh_params_deinit(run->dh_params);

  if(run->startup != NULL)
    infinoted_startup_free(run->startup);

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
  GError* error4;
  GError* error6;
  guint port;
  gboolean result;
  InfdTcpServer* tcp;

  error = NULL;
  error4 = NULL;
  error6 = NULL;

  /* Load DH parameters */
  if(run->startup->credentials)
  {
    result = infinoted_dh_params_ensure(
      run->startup->log, run->startup->credentials, &run->dh_params, &error);

    if(result == FALSE)
    {
      infinoted_log_error(
        run->startup->log,
        _("Failed to generate Diffie-Hellman parameters: %s"),
        error->message
      );

      g_error_free(error);
      return;
    }
  }

  /* Open server sockets, accepting incoming connections... TODO: Prevent
   * code duplication here. */
  if(run->xmpp6 != NULL)
  {
    g_object_get(G_OBJECT(run->xmpp6), "tcp-server", &tcp, NULL);
    if(infd_tcp_server_open(tcp, &error6) == TRUE)
    {
      g_object_get(G_OBJECT(tcp), "local-port", &port, NULL);

      infinoted_log_info(
        run->startup->log,
        _("IPv6 Server running on port %u"),
        port
      );
    }
    else
    {
      g_object_unref(run->xmpp6);
      run->xmpp6 = NULL;
      infd_tcp_server_close(tcp);
    }

    g_object_unref(tcp);
  }

  if(run->xmpp4 != NULL)
  {
    g_object_get(G_OBJECT(run->xmpp4), "tcp-server", &tcp, NULL);
    if(infd_tcp_server_open(tcp, &error4) == TRUE)
    {
      g_object_get(G_OBJECT(tcp), "local-port", &port, NULL);

      infinoted_log_info(
        run->startup->log,
        _("IPv4 Server running on port %u"),
        port
      );
    }
    else
    {
      g_object_unref(run->xmpp4);
      run->xmpp4 = NULL;
      infd_tcp_server_close(tcp);
    }

    g_object_unref(tcp);
  }

  if(run->xmpp4 == NULL && run->xmpp6 == NULL)
  {
    g_assert(error4 != NULL || error6 != NULL);
    error = error4 != NULL ? error4 : error6;

    infinoted_log_error(
      run->startup->log,
      _("Failed to start server: %s"),
      error->message
    );
  }

  if(error4 != NULL) g_error_free(error4);
  if(error6 != NULL) g_error_free(error6);

  /* Make sure messages are shown. This explicit flush is for example
   * required when running in an MSYS shell on Windows. */
  fflush(stderr);

  if(run->xmpp4 != NULL || run->xmpp6 != NULL)
  {
    inf_standalone_io_loop(run->io);

    infinoted_log_info(
      run->startup->log,
      _("Infinoted shutting down...")
    );
  }
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
