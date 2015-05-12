/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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

#include <libinfgtk/inf-gtk-certificate-manager.h>
#include <libinfgtk/inf-gtk-io.h>

#include <libinfinity/server/infd-xmpp-server.h>
#include <libinfinity/server/infd-xml-server.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-io.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-xmpp-manager.h>

#include <gnutls/x509.h>
#include <gnutls/gnutls.h>

#include <gtk/gtk.h>

typedef enum _InfTestCertificateValidateExpectation {
  INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT,
  INF_TEST_CERTIFICATE_VALIDATE_EXPECT_REJECT,
  INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY,
} InfTestCertificateValidateExpectation;

typedef struct _InfTestCertificateValidateDesc InfTestCertificateValidateDesc;
struct _InfTestCertificateValidateDesc {
  /* Name of the test */
  const gchar* name;

  /* server settings */
  const gchar* key_file;
  const gchar* cert_file;

  /* client settings */
  const gchar* ca_file; /* CA file */
  const gchar* hostname; /* Connected server hostname */
  const gchar* known_hosts_file; /* Known hosts file, or NULL */

  /* Expected result */
  InfTestCertificateValidateExpectation expectation;
};

const InfTestCertificateValidateDesc TESTS[] = {
  {
    "expired-certificate",
    "test-expire-key.pem",
    "test-expire-crt.pem",
    "ca-crt.pem",
    "expire-test.gobby.0x539.de",
    NULL,
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_REJECT
  }, {
    NULL
  }
};

static GQuark
inf_test_certificate_validate_error()
{
  return g_quark_from_static_string("INF_CERTIFICATE_VALIDATE_TEST_ERROR");
}

static void
inf_test_certificate_validate_new_connection_cb(InfdXmlServer* server,
                                                InfXmlConnection* conn,
                                                gpointer user_data)
{
  g_object_ref(conn);

  g_object_set_data_full(
    G_OBJECT(server),
    "client-connection",
    conn,
    g_object_unref
  );
}

static InfdXmppServer*
inf_test_certificate_setup_server(InfIo* io,
                                  const char* key_file,
                                  const char* cert_file,
                                  GError** error)
{
  InfdTcpServer* tcp;
  InfdXmppServer* xmpp;

  gnutls_x509_privkey_t key;
  GPtrArray* certs;
  InfCertificateCredentials* creds;
  guint i;
  int res;

  key = inf_cert_util_read_private_key(key_file, error);
  if(!key) return NULL;

  certs = inf_cert_util_read_certificate(cert_file, NULL, error);
  if(!certs)
  {
    gnutls_x509_privkey_deinit(key);
    return NULL;
  }

  creds = inf_certificate_credentials_new();
  res = gnutls_certificate_set_x509_key(
    inf_certificate_credentials_get(creds),
    (gnutls_x509_crt_t*)certs->pdata,
    certs->len,
    key
  );

  gnutls_x509_privkey_deinit(key);
  for(i = 0; i < certs->len; ++i)
    gnutls_x509_crt_deinit(certs->pdata[i]);
  g_ptr_array_free(certs, TRUE);

  if(res != 0)
  {
    inf_certificate_credentials_unref(creds);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  tcp = g_object_new(
    INFD_TYPE_TCP_SERVER,
    "io", io,
    "local-port", 6524,
    NULL
  );

  if(infd_tcp_server_open(tcp, error) == FALSE)
  {
    inf_certificate_credentials_unref(creds);
    return NULL;
  }

  xmpp = infd_xmpp_server_new(
    tcp,
    INF_XMPP_CONNECTION_SECURITY_ONLY_TLS,
    creds,
    NULL,
    NULL
  );

  /* Keep client connections alive */
  g_signal_connect(
    G_OBJECT(xmpp),
    "new-connection",
    G_CALLBACK(inf_test_certificate_validate_new_connection_cb),
    NULL
  );

  inf_certificate_credentials_unref(creds);
  return xmpp;
}

static InfXmppConnection*
inf_test_certificate_validate_setup_client(InfIo* io,
                                           const gchar* ca_file,
                                           const gchar* remote_hostname,
                                           GError** error)
{
  InfIpAddress* addr;
  InfTcpConnection* conn;
  InfXmppConnection* xmpp;

  InfCertificateCredentials* creds;
  GPtrArray* cas;
  int res;
  guint i;

  cas = inf_cert_util_read_certificate(ca_file, NULL, error);
  if(!cas) return NULL;

  creds = inf_certificate_credentials_new();
  res = gnutls_certificate_set_x509_trust(
    inf_certificate_credentials_get(creds),
    (gnutls_x509_crt_t*)cas->pdata,
    cas->len
  );

  for(i = 0; i < cas->len; ++i)
    gnutls_x509_crt_deinit(cas->pdata[i]);
  g_ptr_array_free(cas, TRUE);

  addr = inf_ip_address_new_loopback4();
  conn = inf_tcp_connection_new(io, addr, 6524);
  inf_ip_address_free(addr);

  xmpp = inf_xmpp_connection_new(
    conn,
    INF_XMPP_CONNECTION_CLIENT,
    g_get_host_name(),
    remote_hostname,
    INF_XMPP_CONNECTION_SECURITY_ONLY_TLS,
    creds,
    NULL,
    NULL
  );

  if(inf_tcp_connection_open(conn, error) == FALSE)
  {
    g_object_unref(conn);
    g_object_unref(xmpp);
    return NULL;
  }

  g_object_unref(conn);
  return xmpp;
}

static void
inf_test_validate_certificate_notify_status_cb(GObject* object,
                                               GParamSpec* pspec,
                                               gpointer user_data)
{
  InfXmlConnectionStatus status;
  g_object_get(G_OBJECT(object), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_OPEN ||
     status == INF_XML_CONNECTION_CLOSING ||
     status == INF_XML_CONNECTION_CLOSED)
  {
    if(gtk_main_level() > 0)
      gtk_main_quit();
  }
}

static void
inf_test_validate_certificate_error_cb(InfXmlConnection* conn,
                                       const GError* error,
                                       gpointer user_data)
{
  GError** out_error;
  out_error = (GError**)user_data;

  if(*out_error == NULL)
    *out_error = g_error_copy(error);
}

static gboolean
inf_test_certificate_validate_run(const InfTestCertificateValidateDesc* desc,
                                  GError** error)
{
  InfIo* io;
  InfdXmppServer* server;

  InfXmppManager* xmpp_manager;
  InfGtkCertificateManager* manager;
  InfXmppConnection* client;

  InfXmlConnectionStatus status;
  gboolean result;

  GError* conn_error;

  /* Setup server */
  io = INF_IO(inf_gtk_io_new());

  server = inf_test_certificate_setup_server(
    io,
    desc->key_file,
    desc->cert_file,
    error
  );

  if(server == NULL)
  {
    g_object_unref(io);
    return FALSE;
  }

  /* Create client */
  xmpp_manager = inf_xmpp_manager_new();

  manager = inf_gtk_certificate_manager_new(
    NULL,
    xmpp_manager,
    desc->known_hosts_file
  );

  client = inf_test_certificate_validate_setup_client(
    io,
    desc->ca_file,
    desc->hostname,
    error
  );

  g_object_unref(io);

  if(client == NULL)
  {
    g_object_unref(xmpp_manager);
    g_object_unref(manager);
    g_object_unref(server);
    return FALSE;
  }

  inf_xmpp_manager_add_connection(xmpp_manager, client);

  /* Okay, now watch for status changes on the client or whether a dialog
   * appears. */
  g_signal_connect(
    G_OBJECT(client),
    "notify::status",
    G_CALLBACK(inf_test_validate_certificate_notify_status_cb),
    NULL
  );

  conn_error = NULL;
  g_signal_connect(
    G_OBJECT(client),
    "error",
    G_CALLBACK(inf_test_validate_certificate_error_cb),
    &conn_error
  );

  /* TODO: Detect whether the dialog opens */

  gtk_main();

  /* Evaluate result */
  result = TRUE;
  g_object_get(G_OBJECT(client), "status", &status, NULL);
  if(status == INF_XML_CONNECTION_OPEN)
  {
    g_assert(conn_error == NULL);

    if(desc->expectation != INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT)
    {
      g_set_error(
        error,
        inf_test_certificate_validate_error(),
        0,
        "Certificate accepted but not expected to"
      );

      result = FALSE;
    }
  }
  else
  {
    g_assert(conn_error != NULL);

    /* TODO: The certificate verification result is not preserved at
     * the moment. We could change this in
     * inf_xmpp_connection_certificate_verify_cancel such that the existing
     * error is used if any, or otherwise our own is created. */
    if(conn_error->domain != inf_xmpp_connection_error_quark() &&
       conn_error->code != INF_XMPP_CONNECTION_ERROR_CERTIFICATE_NOT_TRUSTED)
    {
      if(error) *error = g_error_copy(conn_error);
      result = FALSE;
    }
    else if(desc->expectation != INF_TEST_CERTIFICATE_VALIDATE_EXPECT_REJECT)
    {
      g_set_error(
        error,
        inf_test_certificate_validate_error(),
        1,
        "Certificate rejected but not expected to"
      );

      result = FALSE;
    }

    g_error_free(conn_error);
  }

  g_object_unref(xmpp_manager);
  g_object_unref(manager);
  g_object_unref(server);
  g_object_unref(client);
  return result;
}

int
main(int argc,
     char** argv)
{
  const InfTestCertificateValidateDesc* test;
  GError* error;
  int res;

  /* So that the certificate files are found */
  chdir("certs");

  gtk_init(&argc, &argv);

  res = EXIT_SUCCESS;
  for(test = TESTS; test->name != NULL; ++test)
  {
    printf("%s...", test->name);

    error = NULL;
    if(inf_test_certificate_validate_run(test, &error) == FALSE)
    {
      printf(" %s\n", error->message);
      g_error_free(error);
      res = EXIT_FAILURE;
    }
    else
    {
      printf(" OK\n");
    }
  }

  return res;
}

/* vim:set et sw=2 ts=2: */
