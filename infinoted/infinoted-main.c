/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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

#include <infinoted/infinoted-creds.h>
#include <infinoted/infinoted-note-plugin.h>

#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-server-pool.h>
#include <libinfinity/server/infd-directory.h>
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/server/infd-storage.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-connection-manager.h>
#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-discovery-avahi.h>
#include <libinfinity/inf-config.h>

#include <glib-object.h>
#include <glib/goption.h>
#include <glib/gstrfuncs.h>

#include <string.h>
#include <locale.h>
#include <stdio.h>
#include <errno.h>

static gchar* key_file = NULL;
static gchar* cert_file = NULL;
static gboolean create_key = FALSE;
static gboolean create_certificate = FALSE;
static gint port_number = 6523;

static const GOptionEntry entries[] = 
{
  {
    "key-file", 'k',
    0,
    G_OPTION_ARG_FILENAME, &key_file,
    "The server's private key", "KEY-FILE"
  }, {
    "certificate-file", 'c',
    0,
    G_OPTION_ARG_FILENAME, &cert_file,
    "The server's certificate", "CERTIFICATE-FILE"
  }, {
    "create-key", 0,
    0,
    G_OPTION_ARG_NONE, &create_key,
    "Creates a new random private key", NULL
  }, {
    "create-certificate", 0,
    0,
    G_OPTION_ARG_NONE, &create_certificate,
    "Creates a new self-signed certificate using the given key", 0
  }, {
    "port-number", 'p',
    0,
    G_OPTION_ARG_INT, &port_number,
    "The port number to listen on", "PORT"
  }, {
    NULL, 0,
    0,
    G_OPTION_ARG_NONE, NULL,
    NULL, 0
  }
};

static gboolean
infinoted_main_create_dirname(const gchar* path,
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
      "Could not create directory `%s': %s",
      dirname,
      strerror(save_errno)
    );
    
    g_free(dirname);
    return FALSE;
  }

  g_free(dirname);
  return TRUE;
}

static gboolean
infinoted_main_run(gnutls_certificate_credentials_t credentials,
                   InfdDirectory* directory,
                   guint port,
                   GError** error)
{
  InfdTcpServer* tcp;
  InfdServerPool* pool;
  InfdXmppServer* server;
#ifdef INFINOTE_HAVE_AVAHI
  InfXmppManager* xmpp_manager;
  InfDiscoveryAvahi* avahi;
#endif

  tcp = INFD_TCP_SERVER(
    g_object_new(
      INFD_TYPE_TCP_SERVER,
      "io", infd_directory_get_io(directory),
      "local-port", port,
      NULL
    )
  );

  if(infd_tcp_server_open(tcp, error) == FALSE)
    return FALSE;

  pool = infd_server_pool_new(directory);
  server = infd_xmpp_server_new(tcp, g_get_host_name(), credentials, NULL);
  g_object_unref(G_OBJECT(tcp));
  
  infd_server_pool_add_server(pool, INFD_XML_SERVER(server));

#ifdef INFINOTE_HAVE_AVAHI
  xmpp_manager = inf_xmpp_manager_new();
  avahi = inf_discovery_avahi_new(
    infd_directory_get_io(directory),
    xmpp_manager,
    credentials,
    NULL
  );
  g_object_unref(G_OBJECT(xmpp_manager));

  infd_server_pool_add_local_publisher(
    pool,
    server,
    INF_LOCAL_PUBLISHER(avahi)
  );
  g_object_unref(G_OBJECT(avahi));
#endif
  g_object_unref(G_OBJECT(server));

  fprintf(stderr, "Server running on port %u\n", port);
  inf_standalone_io_loop(INF_STANDALONE_IO(infd_directory_get_io(directory)));

  g_object_unref(G_OBJECT(pool));
  return TRUE;
}

static gboolean
infinoted_main(int argc,
               char* argv[],
               GError** error)
{
  GOptionContext* context;

  gnutls_x509_privkey_t key;
  gnutls_x509_crt_t cert;
  gnutls_dh_params_t dh_params;
  gnutls_certificate_credentials_t credentials;
  const gchar* key_path;
  const gchar* cert_path;

  gchar* root_directory;
  InfStandaloneIo* io;
  InfConnectionManager* connection_manager;
  InfdStorage* storage;
  InfdDirectory* directory;

  key = NULL;
  cert = NULL;
  dh_params = NULL;
  credentials = NULL;
  directory = NULL;

  setlocale(LC_ALL, "");
  context = g_option_context_new("- infinote dedicated server");
  g_option_context_add_main_entries(context, entries, NULL);

  if(g_option_context_parse(context, &argc, &argv, error) == FALSE)
    goto error;

  if(create_key == TRUE && create_certificate == FALSE)
  {
    g_set_error(
      error,
      g_quark_from_static_string("INFINOTED_MAIN_ERROR"),
      0,
      "Creating a new private key also requires creating a new certificate "
      "signed with it."
    );

    goto error;
  }

  g_type_init();
  gnutls_global_init();

  /* TODO: Allow different storage plugins, allow custom root directory */
  root_directory = g_build_filename(g_get_home_dir(), ".infinote", NULL);
  storage = INFD_STORAGE(infd_filesystem_storage_new(root_directory));
  g_free(root_directory);

  io = inf_standalone_io_new();
  connection_manager = inf_connection_manager_new();
  directory = infd_directory_new(INF_IO(io), storage, connection_manager);
  g_object_unref(G_OBJECT(io));
  g_object_unref(G_OBJECT(storage));
  g_object_unref(G_OBJECT(connection_manager));

  infinoted_note_plugin_load_directory(PLUGIN_PATH, directory);

  key_path = DEFAULT_KEYPATH;
  if(key_file != NULL) key_path = key_file;

  cert_path = DEFAULT_CERTPATH;
  if(cert_file != NULL) cert_path = cert_file;

  if(create_key == TRUE)
  {
    fprintf(stderr, "Generating 2048 bit RSA private key...\n");
    key = infinoted_creds_create_key(error);

    if(key != NULL)
    {
      if(infinoted_main_create_dirname(key_path, error) == FALSE ||
         infinoted_creds_write_key(key, key_path, error) == FALSE)
      {
        gnutls_x509_privkey_deinit(key);
        key = NULL;
      }
    }
  }
  else
  {
    key = infinoted_creds_read_key(key_path, error);
  }

  g_free(key_file);
  key_file = NULL;
  if(key == NULL) goto error;

  if(create_certificate == TRUE)
  {
    fprintf(stderr, "Generating self-signed certificate...\n");
    cert = ininoted_creds_create_self_signed_certificate(key, error);

    if(cert != NULL)
    {
      if(infinoted_main_create_dirname(key_path, error) == FALSE ||
         infinoted_creds_write_certificate(cert, cert_path, error) == FALSE)
      {
        gnutls_x509_crt_deinit(cert);
        cert = NULL;
      }
    }
  }
  else
  {
    cert = infinoted_creds_read_certificate(cert_path, error);
  }

  g_free(cert_file);
  cert_file = NULL;
  if(cert == NULL) goto error;

  /* TODO: Later we should probably always generate new params, or store
   * an expiry date with them. */
  dh_params = infinoted_creds_read_dh_params("dh.pem", NULL);
  if(dh_params == NULL)
  {
    fprintf(stderr, "Generating 2048 bit Diffie-Hellman parameters...\n");
    dh_params = infinoted_creds_create_dh_params(error);
    if(dh_params == NULL) goto error;

    infinoted_creds_write_dh_params(dh_params, "dh.pem", NULL);
  }

  credentials = infinoted_creds_create_credentials(
    dh_params,
    key,
    cert,
    error
  );

  if(credentials == NULL)
    goto error;

  if(infinoted_main_run(credentials, directory, port_number, error) == FALSE)
    goto error;

  gnutls_certificate_free_credentials(credentials);
  gnutls_dh_params_deinit(dh_params);
  gnutls_x509_crt_deinit(cert);
  gnutls_x509_privkey_deinit(key);
  g_object_unref(G_OBJECT(directory));
  gnutls_global_deinit();

  return TRUE;
error:
  if(credentials != NULL) gnutls_certificate_free_credentials(credentials);
  if(dh_params != NULL) gnutls_dh_params_deinit(dh_params);
  if(cert != NULL) gnutls_x509_crt_deinit(cert);
  if(key != NULL) gnutls_x509_privkey_deinit(key);
  if(directory != NULL) g_object_unref(G_OBJECT(directory));
  gnutls_global_deinit();

  g_free(key_file);
  g_free(cert_file);

  return FALSE;
}

int
main(int argc,
     char* argv[])
{
  GError* error;

  error = NULL;
  if(infinoted_main(argc, argv, &error) == FALSE)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  return 0;
}
