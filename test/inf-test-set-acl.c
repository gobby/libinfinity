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
#include <libinfinity/common/inf-cert-util.h>

#include <string.h>

typedef struct _InfTestSetAcl InfTestSetAcl;
struct _InfTestSetAcl {
  InfStandaloneIo* io;
  InfXmppConnection* conn;
  InfBrowser* browser;
};

static void
inf_test_set_acl_set_acl_finished_cb(InfRequest* request,
                                     const InfRequestResult* result,
                                     const GError* error,
                                     gpointer user_data)
{
  InfTestSetAcl* test;
  const InfAclSheetSet* sheet_set;
  const InfBrowserIter* iter;
  guint i;

  test = (InfTestSetAcl*)user_data;

  if(error != NULL)
  {
    fprintf(stderr, "Failed to change root node ACL: %s\n", error->message);
  }
  else
  {
    inf_request_result_get_set_acl(result, NULL, &iter);
    sheet_set = inf_browser_get_acl(test->browser, iter);
    fprintf(stderr, "New root node ACL:\n");
    for(i = 0; i < sheet_set->n_sheets; ++i)
    {
      fprintf(
        stderr,
        "  %s: mask=%llx, perms=%llx\n",
        sheet_set->sheets[i].account->id,
        (unsigned long long)sheet_set->sheets[i].mask.mask[0],
        (unsigned long long)sheet_set->sheets[i].perms.mask[0]
      );
    }
  }

  inf_xml_connection_close(INF_XML_CONNECTION(test->conn));
}

static void
inf_test_set_acl_query_acl_finished_cb(InfRequest* request,
                                       const InfRequestResult* result,
                                       const GError* error,
                                       gpointer user_data)
{
  InfTestSetAcl* test;
  InfAclSheetSet* sheet_set;
  const InfAclAccount* account;
  InfAclSheet* sheet;
  const InfBrowserIter* iter;
  guint i;

  test = (InfTestSetAcl*)user_data;

  if(error != NULL)
  {
    fprintf(stderr, "ACL query failed: %s\n", error->message);
    inf_xml_connection_close(INF_XML_CONNECTION(test->conn));
  }
  else
  {
    inf_request_result_get_query_acl(result, NULL, &iter, NULL);

    sheet_set =
      inf_acl_sheet_set_copy(inf_browser_get_acl(test->browser, iter));

    fprintf(stderr, "Root node ACL:\n");
    for(i = 0; i < sheet_set->n_sheets; ++i)
    {
      fprintf(
        stderr,
        "  %s: mask=%llx, perms=%llx\n",
        sheet_set->sheets[i].account->id,
        (unsigned long long)sheet_set->sheets[i].mask.mask[0],
        (unsigned long long)sheet_set->sheets[i].perms.mask[0]
      );
    }

    account = inf_browser_lookup_acl_account(test->browser, "default");
    sheet = inf_acl_sheet_set_add_sheet(sheet_set, account);

    fprintf(stderr, "Requesting CAN_SET_ACL permission for the root node\n");
    inf_acl_mask_or1(&sheet->mask, INF_ACL_CAN_SET_ACL);
    inf_acl_mask_or1(&sheet->perms, INF_ACL_CAN_SET_ACL);

    inf_browser_set_acl(
      test->browser,
      iter,
      sheet_set,
      inf_test_set_acl_set_acl_finished_cb,
      test
    );

    inf_acl_sheet_set_free(sheet_set);
  }
}

static void
inf_test_set_acl_query_account_list_finished_cb(InfRequest* request,
                                                const InfRequestResult* res,
                                                const GError* error,
                                                gpointer user_data)
{
  InfTestSetAcl* test;
  const InfAclAccount** accounts;
  guint n_accounts;
  guint i;

  InfBrowserIter iter;

  test = (InfTestSetAcl*)user_data;

  if(error != NULL)
  {
    fprintf(stderr, "Account List query failed: %s\n", error->message);
    inf_xml_connection_close(INF_XML_CONNECTION(test->conn));
  }
  else
  {
    printf("Account List:\n");
    accounts = inf_browser_get_acl_account_list(test->browser, &n_accounts);
    for(i = 0; i < n_accounts; ++i)
      printf("  * %s (%s)\n", accounts[i]->id, accounts[i]->name);
    g_free(accounts);

    fprintf(stderr, "Querying root node ACL...\n");

    inf_browser_get_root(test->browser, &iter);

    inf_browser_query_acl(
      test->browser,
      &iter,
      inf_test_set_acl_query_acl_finished_cb,
      test
    );
  }
}

static void
inf_test_set_acl_error_cb(InfcBrowser* browser,
                          GError* error,
                          gpointer user_data)
{
  fprintf(stderr, "Connection error: %s\n", error->message);
}

static void
inf_test_set_acl_notify_status_cb(GObject* object,
                                  GParamSpec* pspec,
                                  gpointer user_data)
{
  InfTestSetAcl* test;
  InfBrowserStatus status;
  const InfAclAccount* account;

  test = (InfTestSetAcl*)user_data;
  g_object_get(G_OBJECT(test->browser), "status", &status, NULL);

  if(status == INF_BROWSER_OPEN)
  {
    account = inf_browser_get_acl_local_account(test->browser);

    fprintf(stderr, "Connection established, querying account list...\n");
    fprintf(stderr, "Local account: %s (%s)\n", account->id, account->name);

    inf_browser_query_acl_account_list(
      test->browser,
      inf_test_set_acl_query_account_list_finished_cb,
      test
    );
  }
  else if(status == INF_BROWSER_CLOSED)
  {
    if(inf_standalone_io_loop_running(test->io))
      inf_standalone_io_loop_quit(test->io);
  }
}

int
main(int argc, char* argv[])
{
  InfTestSetAcl test;
  InfIpAddress* address;
  InfCommunicationManager* manager;
  InfTcpConnection* tcp_conn;
  GPtrArray* array;
  gnutls_x509_privkey_t key;
  gnutls_x509_crt_t* certs;
  guint n_certs;
  guint i;
  InfCertificateCredentials* creds;
  GError* error;

  key = NULL;
  certs = NULL;
  n_certs = 0;
  creds = NULL;

  error = NULL;
  inf_init(NULL);

  test.io = inf_standalone_io_new();
  address = inf_ip_address_new_loopback4();

  tcp_conn = inf_tcp_connection_new_and_open(
    INF_IO(test.io),
    address,
    inf_protocol_get_default_port(),
    &error
  );

  inf_ip_address_free(address);

  if(tcp_conn == NULL)
  {
    fprintf(stderr, "Could not open TCP connection: %s\n", error->message);
    g_error_free(error);
    return EXIT_FAILURE;
  }

  if(argc >= 2)
  {
    array = inf_cert_util_read_certificate(argv[1], NULL, &error);

    if(error != NULL)
    {
      fprintf(stderr, "Failed to read certificate: %s\n", error->message);
      g_error_free(error);
      return EXIT_FAILURE;
    }

    n_certs = array->len;
    certs = (gnutls_x509_crt_t*)g_ptr_array_free(array, FALSE);

    if(n_certs == 0)
    {
      fprintf(stderr, "File does not contain a certificate\n");
      return EXIT_FAILURE;
    }

    key = inf_cert_util_read_private_key(argv[1], &error);

    if(error != NULL)
    {
      fprintf(stderr, "Failed to read key: %s\n", error->message);
      g_error_free(error);
      return EXIT_FAILURE;
    }

    creds = inf_certificate_credentials_new();

    gnutls_certificate_set_x509_key(
      inf_certificate_credentials_get(creds),
      certs,
      n_certs,
      key
    );
  }

  test.conn = inf_xmpp_connection_new(
    tcp_conn,
    INF_XMPP_CONNECTION_CLIENT,
    NULL,
    "localhost",
    INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS,
    creds,
    NULL,
    NULL
  );

  g_object_unref(tcp_conn);

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
    G_CALLBACK(inf_test_set_acl_notify_status_cb),
    &test
  );

  g_signal_connect(
    G_OBJECT(test.browser),
    "error",
    G_CALLBACK(inf_test_set_acl_error_cb),
    &test
  );

  inf_standalone_io_loop(test.io);
  g_object_unref(manager);
  g_object_unref(test.browser);
  /* TODO: Wait until the XMPP connection is in status closed */
  g_object_unref(test.conn);

  if(creds != NULL) inf_certificate_credentials_unref(creds);
  for(i = 0; i < n_certs; ++i)
    gnutls_x509_crt_deinit(certs[i]);
  g_free(certs);
  if(key != NULL) gnutls_x509_privkey_deinit(key);

  g_object_unref(test.io);
  return 0;
}

/* vim:set et sw=2 ts=2: */
