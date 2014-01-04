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

#include <libinfinity/client/infc-browser.h>
#include <libinfinity/communication/inf-communication-manager.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-tcp-connection.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-io.h>
#include <libinfinity/common/inf-protocol.h>

#include <string.h>

typedef struct _InfTestCertificateRequest InfTestCertificateRequest;
struct _InfTestCertificateRequest {
  InfStandaloneIo* io;
  InfXmppConnection* conn;
  InfBrowser* browser;
  gnutls_x509_privkey_t key;
};

static void
inf_test_certificate_request_finished_cb(InfcCertificateRequest* request,
                                         InfCertificateChain* chain,
                                         const GError* error,
                                         gpointer user_data)
{
  InfTestCertificateRequest* test;
  guint n_certs;
  guint i;
  gnutls_datum_t datum;

  gnutls_x509_crt_t cert;
  size_t cert_size;
  gchar* cert_pem;

  test = (InfTestCertificateRequest*)user_data;

  if(error != NULL)
  {
    fprintf(stderr, "Error: %s\n", error->message);
  }
  else
  {
    fprintf(stderr, "Certificate generated!\n\n");

    n_certs = inf_certificate_chain_get_n_certificates(chain);
    for(i = 0; i < n_certs; ++i)
    {
      fprintf(stderr, "Certificate %d", i);
      if(i == 0) fprintf(stderr, " (own)");
      if(i == 1) fprintf(stderr, " (issuer)");
      if(i == n_certs - 1) fprintf(stderr, " (CA)");
      fprintf(stderr, ":\n\n");

      cert = inf_certificate_chain_get_nth_certificate(chain, i);
      gnutls_x509_crt_print(cert, GNUTLS_CRT_PRINT_FULL, &datum);

      fprintf(stderr, "%s\n", datum.data);

      gnutls_free(datum.data);
    }

    for(i = 0; i < n_certs; ++i)
    {
      cert = inf_certificate_chain_get_nth_certificate(chain, i);
      gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, NULL, &cert_size);
      cert_pem = g_malloc(cert_size);
      gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, cert_pem, &cert_size);
      printf("%s\n\n", cert_pem);
      g_free(cert_pem);
    }

    gnutls_x509_privkey_export(
      test->key,
      GNUTLS_X509_FMT_PEM,
      NULL,
      &cert_size
    );

    cert_pem = g_malloc(cert_size);

    gnutls_x509_privkey_export(
      test->key,
      GNUTLS_X509_FMT_PEM,
      NULL,
      &cert_size
    );

    printf("%s\n", cert_pem);
    g_free(cert_pem);
  }

  if(inf_standalone_io_loop_running(test->io))
    inf_standalone_io_loop_quit(test->io);
}

static void
inf_test_certificate_request_error_cb(InfcBrowser* browser,
                                      GError* error,
                                      gpointer user_data)
{
  fprintf(stderr, "Connection error: %s\n", error->message);
}

static void
inf_test_certificate_request_notify_status_cb(GObject* object,
                                              GParamSpec* pspec,
                                              gpointer user_data)
{
  InfTestCertificateRequest* test;
  InfBrowserStatus status;
  gnutls_x509_crq_t crq;
  InfcCertificateRequest* request;
  GError* error;
  int res;

  test = (InfTestCertificateRequest*)user_data;
  g_object_get(G_OBJECT(test->browser), "status", &status, NULL);

  if(status == INF_BROWSER_OPEN)
  {
    fprintf(stderr, "Connection established, creating key... (4096 bit)\n");

    /* TODO: Some error checking here */
    gnutls_x509_privkey_init(&test->key);
    gnutls_x509_privkey_generate(test->key, GNUTLS_PK_RSA, 4096, 0);

    fprintf(stderr, "Done, sending the certificate request\n");

    gnutls_x509_crq_init(&crq);

    gnutls_x509_crq_set_key(crq, test->key);
    gnutls_x509_crq_set_key_usage(crq, GNUTLS_KEY_DIGITAL_SIGNATURE);
    gnutls_x509_crq_set_version(crq, 3);

    gnutls_x509_crq_set_dn_by_oid(
      crq,
      GNUTLS_OID_X520_COMMON_NAME,
      0,
      "Armin Burgmeier",
      strlen("Armin Burgmeier")
    );

    /* TODO: gnutls_x509_crq_sign2 is deprecated in favour of
     * gnutls_x509_crq_privkey_sign, but the latter returns the error code
     * GNUTLS_E_UNIMPLEMENTED_FEATURE, so we keep using the deprecated
     * version here. */
    /*gnutls_x509_crq_privkey_sign(crq, key, GNUTLS_DIG_SHA1, 0);*/
    gnutls_x509_crq_sign2(crq, test->key, GNUTLS_DIG_SHA1, 0);

    error = NULL;
    request = infc_browser_request_certificate(
      INFC_BROWSER(object),
      crq,
      "Administrator",
      inf_test_certificate_request_finished_cb,
      test,
      &error
    );

    if(error != NULL)
    {
      fprintf(stderr, "Failed to request certificate: %s\n", error->message);
      g_error_free(error);

      if(inf_standalone_io_loop_running(test->io))
        inf_standalone_io_loop_quit(test->io);
    }

    gnutls_x509_crq_deinit(crq);
  }

  if(status == INF_BROWSER_CLOSED)
  {
    if(inf_standalone_io_loop_running(test->io))
      inf_standalone_io_loop_quit(test->io);
  }
}

int
main(int argc, char* argv[])
{
  InfTestCertificateRequest test;
  InfIpAddress* address;
  InfCommunicationManager* manager;
  InfTcpConnection* tcp_conn;
  GError* error;

  error = NULL;
  inf_init(NULL);
/*  gnutls_global_init();
  g_type_init();*/

  test.key = NULL;
  test.io = inf_standalone_io_new();
  address = inf_ip_address_new_loopback4();

  tcp_conn =
    inf_tcp_connection_new_and_open(INF_IO(test.io), address, inf_protocol_get_default_port(), &error);

  inf_ip_address_free(address);

  if(tcp_conn == NULL)
  {
    fprintf(stderr, "Could not open TCP connection: %s\n", error->message);
    g_error_free(error);
  }
  else
  {
    test.conn = inf_xmpp_connection_new(
      tcp_conn,
      INF_XMPP_CONNECTION_CLIENT,
      NULL,
      "localhost",
      INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS,
      NULL,
      NULL,
      NULL
    );

    g_object_unref(G_OBJECT(tcp_conn));

    manager = inf_communication_manager_new();
    test.browser = INF_BROWSER(
      infc_browser_new(
        INF_IO(test.io),
        manager,
        INF_XML_CONNECTION(test.conn)
      )
    );

    g_signal_connect_after(
      G_OBJECT(test.browser),
      "notify::status",
      G_CALLBACK(inf_test_certificate_request_notify_status_cb),
      &test
    );

    g_signal_connect(
      G_OBJECT(test.browser),
      "error",
      G_CALLBACK(inf_test_certificate_request_error_cb),
      &test
    );

    inf_standalone_io_loop(test.io);
    g_object_unref(G_OBJECT(manager));
    g_object_unref(G_OBJECT(test.browser));

    /* TODO: Wait until the XMPP connection is in status closed */
    g_object_unref(G_OBJECT(test.conn));
  }

  g_object_unref(G_OBJECT(test.io));
  if(test.key != NULL) gnutls_x509_privkey_deinit(test.key);
  return 0;
}

/* vim:set et sw=2 ts=2: */
