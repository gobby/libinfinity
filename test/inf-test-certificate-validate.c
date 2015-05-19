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

#include <libinfinity/server/infd-xmpp-server.h>
#include <libinfinity/server/infd-xml-server.h>
#include <libinfinity/common/inf-certificate-verify.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-xmpp-manager.h>
#include <libinfinity/common/inf-init.h>

#include <gnutls/x509.h>
#include <gnutls/gnutls.h>

#include <glib/gstdio.h>

typedef enum _InfTestCertificateValidateExpectation {
  INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT,
  INF_TEST_CERTIFICATE_VALIDATE_EXPECT_REJECT,
  INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_ACCEPT,
  INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_REJECT,
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
  const gchar* pinned_certificate;

  /* Expected result */
  InfTestCertificateValidateExpectation expectation;
  gboolean accept_query; /* If there is a query, accept it? */
  gboolean expect_pinned; /* Whether the cert should end up pinned or not */
};

typedef struct _InfTestCertificateValidateCheckCertificateData
  InfTestCertificateValidateCheckCertificateData;
struct _InfTestCertificateValidateCheckCertificateData {
  gboolean accept_query;
  gboolean did_query;
};

const InfTestCertificateValidateDesc TESTS[] = {
  {
    "expired-trusted",
    "test-expire-key.pem",
    "test-expire-crt.pem",
    "ca-crt.pem",
    "expire-test.gobby.0x539.de",
    NULL,
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_REJECT,
    FALSE,
    FALSE
  }, {
    "expired-pinned",
    "test-expire-key.pem",
    "test-expire-crt.pem",
    NULL,
    "expire-test.gobby.0x539.de",
    "test-expire-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_REJECT,
    FALSE,
    TRUE /* cert was pinned before, and rejection doesn't un-pin it. That's
            good so that if the server gets an updated certificate we
            remember that the previous one has expired. */
  }, {
    "expired-pinned-to-good",
    "test-expire-good-key.pem",
    "test-expire-good-crt.pem",
    "ca-crt.pem",
    "expire-test.gobby.0x539.de",
    "test-expire-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT,
    FALSE,
    FALSE
  }, {
    "expired-pinned-to-good-mismatch-query-accept",
    "test-expire-good-key.pem",
    "test-expire-good-crt.pem",
    "ca-crt.pem",
    "expire-test-mismatch.gobby.0x539.de",
    "test-expire-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_ACCEPT,
    TRUE,
    TRUE
  }, {
    "expired-pinned-to-good-mismatch-query-reject",
    "test-expire-good-key.pem",
    "test-expire-good-crt.pem",
    "ca-crt.pem",
    "expire-test-mismatch.gobby.0x539.de",
    "test-expire-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_REJECT,
    FALSE,
    FALSE /* The old certificate will remain pinned, but not the new one */
  }, {
    "expired-pinned-to-good-mismatch-query-accept",
    "test-expire-good-key.pem",
    "test-expire-good-crt.pem",
    "ca-crt.pem",
    "expire-test-mismatch.gobby.0x539.de",
    "test-expire-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_ACCEPT,
    TRUE,
    TRUE
  }, {
    "expired-pinned-to-good-mismatch-query-reject",
    "test-expire-good-key.pem",
    "test-expire-good-crt.pem",
    "ca-crt.pem",
    "expire-test-mismatch.gobby.0x539.de",
    "test-expire-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_REJECT,
    FALSE,
    FALSE /* The old certificate will remain pinned, but not the new one */
  }, {
    "good",
    "test-good-key.pem",
    "test-good-crt.pem",
    "ca-crt.pem",
    "test-good.gobby.0x539.de",
    NULL,
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT,
    FALSE,
    FALSE
  }, {
    "good-pinned",
    "test-good-key.pem",
    "test-good-crt.pem",
    "ca-crt.pem",
    "test-good.gobby.0x539.de",
    "test-good-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT,
    FALSE,
    FALSE
  }, {
    "good-pinned-to-other",
    "test-good-key.pem",
    "test-good-crt.pem",
    "ca-crt.pem",
    "test-good.gobby.0x539.de",
    "test-expire-good-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT,
    FALSE,
    FALSE
  }, {
    "good-pinned-mismatch",
    "test-good-key.pem",
    "test-good-crt.pem",
    "ca-crt.pem",
    "good-test-mismatch.gobby.0x539.de",
    "test-good-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT,
    FALSE,
    TRUE
  }, {
    "good-pinned-to-other-mismatch-query-accept",
    "test-good-key.pem",
    "test-good-crt.pem",
    "ca-crt.pem",
    "good-test-mismatch.gobby.0x539.de",
    "test-expire-good-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_ACCEPT,
    TRUE,
    TRUE
  }, {
    "good-pinned-to-other-mismatch-query-reject",
    "test-good-key.pem",
    "test-good-crt.pem",
    "ca-crt.pem",
    "good-test-mismatch.gobby.0x539.de",
    "test-expire-good-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_REJECT,
    FALSE,
    FALSE /* The old certificate will remain pinned, but not the new one */
  }, {
    "good-mismatch-query-accept",
    "test-good-key.pem",
    "test-good-crt.pem",
    "ca-crt.pem",
    "good-test-mismatch.gobby.0x539.de",
    NULL,
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_ACCEPT,
    TRUE,
    TRUE
  }, {
    "good-mismatch-query-reject",
    "test-good-key.pem",
    "test-good-crt.pem",
    "ca-crt.pem",
    "good-test-mismatch.gobby.0x539.de",
    NULL,
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_REJECT,
    FALSE,
    FALSE
  }, {
    "good-untrusted-query-accept",
    "test-good-key.pem",
    "test-good-crt.pem",
    NULL,
    "test-good.gobby.0x539.de",
    NULL,
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_ACCEPT,
    TRUE,
    TRUE
  }, {
    "good-untrusted-query-reject",
    "test-good-key.pem",
    "test-good-crt.pem",
    NULL,
    "test-good.gobby.0x539.de",
    NULL,
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_REJECT,
    FALSE,
    FALSE
  }, {
    "good-pinned-untrusted",
    "test-good-key.pem",
    "test-good-crt.pem",
    NULL,
    "test-good.gobby.0x539.de",
    "test-good-crt.pem",
    INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT,
    FALSE,
    TRUE
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

  creds = inf_certificate_credentials_new();
  if(ca_file != NULL)
  {
    cas = inf_cert_util_read_certificate(ca_file, NULL, error);
    if(cas == NULL)
    {
      inf_certificate_credentials_unref(creds);
      return NULL;
    }

    res = gnutls_certificate_set_x509_trust(
      inf_certificate_credentials_get(creds),
      (gnutls_x509_crt_t*)cas->pdata,
      cas->len
    );

    for(i = 0; i < cas->len; ++i)
      gnutls_x509_crt_deinit(cas->pdata[i]);
    g_ptr_array_free(cas, TRUE);
  }

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

  inf_certificate_credentials_unref(creds);
  if(inf_tcp_connection_open(conn, error) == FALSE)
  {
    g_object_unref(conn);
    g_object_unref(xmpp);
    return NULL;
  }

  g_object_unref(conn);
  return xmpp;
}

static gchar*
inf_test_validate_setup_pin(const gchar* pinned_hostname,
                            const gchar* pinned_certificate,
                            GError** error)
{
  GPtrArray* certs;
  gchar* target_file;
  GHashTable* table;
  guint i;
  gboolean result;

  if(pinned_hostname != NULL && pinned_certificate != NULL)
  {
    certs = inf_cert_util_read_certificate(pinned_certificate, NULL, error);
    if(certs == NULL) return NULL;
  }
  else
  {
    certs = g_ptr_array_new();
  }

  target_file = g_build_filename(g_get_tmp_dir(), "pinned-test", NULL);

  table = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    NULL,
    (GDestroyNotify)gnutls_x509_crt_deinit
  );

  for(i = 0; i < certs->len; ++i)
    g_hash_table_insert(table, (gpointer)pinned_hostname, certs->pdata[i]);
  g_ptr_array_free(certs, TRUE);

  result = inf_cert_util_write_certificate_map(table, target_file, error);

  g_hash_table_destroy(table);
  if(result == FALSE)
  {
    g_free(target_file);
    target_file = NULL;
  }

  return target_file;
}

static void
inf_test_validate_certificate_notify_status_cb(GObject* object,
                                               GParamSpec* pspec,
                                               gpointer user_data)
{
  InfStandaloneIo* io;
  InfXmlConnectionStatus status;

  io = INF_STANDALONE_IO(user_data);
  g_object_get(G_OBJECT(object), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_OPEN ||
     status == INF_XML_CONNECTION_CLOSING ||
     status == INF_XML_CONNECTION_CLOSED)
  {
    if(inf_standalone_io_loop_running(io))
      inf_standalone_io_loop_quit(io);
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

static void
inf_test_certificate_validate_check_certificate(InfCertificateVerify* verify,
                                                InfXmppConnection* conn,
                                                InfCertificateChain* chain,
                                                gnutls_x509_crt_t pinned,
                                                InfCertificateVerifyFlags flg,
                                                gpointer user_data)
{
  InfTestCertificateValidateCheckCertificateData* data;
  data = (InfTestCertificateValidateCheckCertificateData*)user_data;

  data->did_query = TRUE;

  /* Close the connection so that we return from the IO loop */
  if(data->accept_query)
    inf_certificate_verify_checked(verify, conn, TRUE);
  else
    inf_certificate_verify_checked(verify, conn, FALSE);
}

static gboolean
inf_test_certificate_validate_run(const InfTestCertificateValidateDesc* desc,
                                  GError** error)
{
  InfIo* io;
  InfdXmppServer* server;

  InfXmppManager* xmpp_manager;
  InfCertificateVerify* verify;
  InfXmppConnection* client;
  gchar* pinned_file;

  InfXmlConnectionStatus status;
  InfTestCertificateValidateCheckCertificateData check_certificate_data;
  gboolean result;

  GError* conn_error;
  GHashTable* pinned;
  gnutls_x509_crt_t pinned_cert;
  InfCertificateChain* current_cert;
  gboolean cert_equal;

  /* Setup server */
  io = INF_IO(inf_standalone_io_new());

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
  pinned_file = inf_test_validate_setup_pin(
    desc->hostname,
    desc->pinned_certificate,
    error
  );

  if(pinned_file == NULL)
  {
    g_object_unref(server);
    g_object_unref(io);
    return FALSE;
  }

  xmpp_manager = inf_xmpp_manager_new();
  verify = inf_certificate_verify_new(xmpp_manager, pinned_file);

  check_certificate_data.did_query = FALSE;
  check_certificate_data.accept_query = desc->accept_query;
  g_signal_connect(
    G_OBJECT(verify),
    "check-certificate",
    G_CALLBACK(inf_test_certificate_validate_check_certificate),
    &check_certificate_data
  );

  client = inf_test_certificate_validate_setup_client(
    io,
    desc->ca_file,
    desc->hostname,
    error
  );

  if(client == NULL)
  {
    g_unlink(pinned_file);
    g_free(pinned_file);
    g_object_unref(io);
    g_object_unref(xmpp_manager);
    g_object_unref(verify);
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
    io
  );

  conn_error = NULL;
  g_signal_connect(
    G_OBJECT(client),
    "error",
    G_CALLBACK(inf_test_validate_certificate_error_cb),
    &conn_error
  );

  inf_standalone_io_loop(INF_STANDALONE_IO(io));
  g_object_unref(io);

  /* Evaluate result */
  result = TRUE;
  g_object_get(G_OBJECT(client), "status", &status, NULL);
  if(status == INF_XML_CONNECTION_OPEN)
  {
    g_assert(conn_error == NULL);

    if(check_certificate_data.did_query == TRUE &&
       desc->expectation != INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_ACCEPT)
    {
      g_set_error(
        error,
        inf_test_certificate_validate_error(),
        3,
        "Certificate queried and accepted but not expected to"
      );

      result = FALSE;
    }
    else if(check_certificate_data.did_query == FALSE &&
            desc->expectation != INF_TEST_CERTIFICATE_VALIDATE_EXPECT_ACCEPT)
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
    g_assert(check_certificate_data.did_query || conn_error != NULL);

    /* TODO: The certificate verification result is not preserved at
     * the moment. We could change this in
     * inf_xmpp_connection_certificate_verify_cancel such that the existing
     * error is used if any, or otherwise our own is created. */
    if(conn_error != NULL &&
       conn_error->domain != inf_xmpp_connection_error_quark() &&
       conn_error->code != INF_XMPP_CONNECTION_ERROR_CERTIFICATE_NOT_TRUSTED)
    {
      g_propagate_error(error, conn_error);
      conn_error = NULL;
      result = FALSE;
    }
    else if(check_certificate_data.did_query == TRUE &&
            desc->expectation != INF_TEST_CERTIFICATE_VALIDATE_EXPECT_QUERY_REJECT)
    {
      g_set_error(
        error,
        inf_test_certificate_validate_error(),
        2,
        "Certificate queried and rejected but not expected to"
      );

      result = FALSE;
    }
    else if(check_certificate_data.did_query == FALSE &&
            desc->expectation != INF_TEST_CERTIFICATE_VALIDATE_EXPECT_REJECT)
    {
      g_set_error(
        error,
        inf_test_certificate_validate_error(),
        1,
        "Certificate rejected but not expected to"
      );

      result = FALSE;
    }

    if(conn_error != NULL)
    {
      g_error_free(conn_error);
      conn_error = NULL;
    }
  }

  /* If we got the expected result, check whether the host was correctly
   * pinned or not. */
  if(result == TRUE)
  {
    pinned = inf_cert_util_read_certificate_map(pinned_file, error);
    if(pinned == NULL)
    {
      result = FALSE;
    }
    else
    {
      pinned_cert = g_hash_table_lookup(pinned, desc->hostname);

      cert_equal = FALSE;
      if(pinned_cert != NULL)
      {
        g_object_get(
          G_OBJECT(client),
          "remote-certificate", &current_cert,
          NULL
        );

        cert_equal = inf_cert_util_compare_fingerprint(
          pinned_cert,
          inf_certificate_chain_get_own_certificate(current_cert),
          &conn_error
        );

        inf_certificate_chain_unref(current_cert);
      }

      if(conn_error != NULL)
      {
        g_propagate_error(error, conn_error);
        conn_error = NULL;
      }
      else if(cert_equal == TRUE && desc->expect_pinned == FALSE)
      {
        g_set_error(
          error,
          inf_test_certificate_validate_error(),
          4,
          "Certificate was pinned but not expected to"
        );

        result = FALSE;
      }
      else if(pinned_cert == NULL && desc->expect_pinned == TRUE)
      {
        g_set_error(
          error,
          inf_test_certificate_validate_error(),
          5,
          "Certificate was not pinned but expected to"
        );

        result = FALSE;
      }

      g_hash_table_destroy(pinned);
    }
  }

  g_unlink(pinned_file);
  g_free(pinned_file);
  g_object_unref(xmpp_manager);
  g_object_unref(verify);
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

  error = NULL;
  if(inf_init(&error) == FALSE)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return EXIT_FAILURE;
  }

  /* So that the certificate files are found */
  chdir("certs");

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
