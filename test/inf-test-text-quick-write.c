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

#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/client/infc-note-plugin.h>
#include <libinfinity/client/infc-browser.h>

#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-request-result.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-browser.h>
#include <libinfinity/common/inf-session-proxy.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-user.h>
#include <libinfinity/common/inf-protocol.h>
#include <libinfinity/common/inf-init.h>
#include <libinfinity/common/inf-cert-util.h>

#include <libinfinity/inf-signals.h>

#include <string.h>

typedef struct _InfTestTextQuickWrite InfTestTextQuickWrite;
struct _InfTestTextQuickWrite {
  InfCertificateCredentials* credentials;
  gchar* content;
  gsize content_pos;
  gsize content_length;

  InfStandaloneIo* io;
  InfXmppConnection* conn;
  InfBrowser* browser;

  InfSessionProxy* proxy;
  InfSession* session;
  InfUser* user;

  InfTextBuffer* buffer;
};

static InfSession*
inf_test_text_quick_write_session_new(InfIo* io,
                                      InfCommunicationManager* manager,
                                      InfSessionStatus status,
                                      InfCommunicationGroup* sync_group,
                                      InfXmlConnection* sync_connection,
                                      const gchar* path,
                                      gpointer user_data)
{
  InfTextDefaultBuffer* buffer;
  InfTextSession* session;

  buffer = inf_text_default_buffer_new("UTF-8");
  session = inf_text_session_new(
    manager,
    INF_TEXT_BUFFER(buffer),
    io,
    status,
    sync_group,
    sync_connection
  );
  g_object_unref(buffer);

  return INF_SESSION(session);
}

static const InfcNotePlugin INF_TEST_TEXT_QUICK_WRITE_PLUGIN = {
  NULL, "InfText", inf_test_text_quick_write_session_new
};

static void
inf_test_text_quick_write_reconnect(InfTestTextQuickWrite* test);

static void
inf_test_text_quick_write_schedule_next(InfTestTextQuickWrite* test);

static void
inf_test_text_quick_write_next_cb(gpointer user_data)
{
  InfTestTextQuickWrite* test;
  int action;

  test = (InfTestTextQuickWrite*)user_data;

  /* Decide what to do */
  action = g_random_int_range(0, 100000);

  if(action < 50)
  {
    /* Start from scratch */
    inf_test_text_quick_write_reconnect(test);
  }
  else if(action < 90000)
  {
    /* Write next character. */
    /* TODO: Make this UTF-8 aware */
    inf_text_buffer_insert_text(
      test->buffer,
      MIN(test->content_pos, inf_text_buffer_get_length(test->buffer)),
      &test->content[test->content_pos],
      1,
      1,
      test->user
    );

    /* Move content pointer */
    ++test->content_pos;
    if(test->content[test->content_pos] == '\0')
      test->content_pos = 0;

    /* Schedule next operation */
    inf_test_text_quick_write_schedule_next(test);
  }
  else
  {
    /* Remove last character */
    if(inf_text_buffer_get_length(test->buffer) > 0 && test->content_pos > 0)
    {
      inf_text_buffer_erase_text(
        test->buffer,
        test->content_pos - 1,
        1,
        test->user
      );

      /* Move content pointer */
      g_assert(test->content_length > 0);
      if(test->content_pos == 0)
        test->content_pos = test->content_length - 1;
      else
        --test->content_pos;
    }

    /* Schedule next operation */
    inf_test_text_quick_write_schedule_next(test);
  }
}

static void
inf_test_text_quick_write_schedule_next(InfTestTextQuickWrite* test)
{
  int secs;

  secs = g_random_int_range(10, 50);

  inf_io_add_timeout(
    INF_IO(test->io),
    secs,
    inf_test_text_quick_write_next_cb,
    test,
    NULL
  );
}

static void
inf_test_text_quick_write_user_join_cb(InfRequest* request,
                                       const InfRequestResult* result,
                                       const GError* error,
                                       gpointer user_data)
{
  InfTestTextQuickWrite* test;
  test = (InfTestTextQuickWrite*)user_data;

  if(error != NULL)
  {
    fprintf(stderr, "%s\n", error->message);
    inf_standalone_io_loop_quit(test->io);
  }
  else
  {
    inf_request_result_get_join_user(result, NULL, &test->user);
    g_object_ref(test->user);

    /* We are ready to rumble now. First, delete all
     * text that is in the buffer already. */
    test->buffer = INF_TEXT_BUFFER(inf_session_get_buffer(test->session));
    g_object_ref(test->buffer);

    if(inf_text_buffer_get_length(test->buffer) > 0)
    {
      inf_text_buffer_erase_text(
        test->buffer,
        0,
        inf_text_buffer_get_length(test->buffer),
        test->user
      );
    }

    inf_test_text_quick_write_schedule_next(test);
  }
}

static void
inf_test_text_quick_write_join_user(InfTestTextQuickWrite* test)
{
  inf_text_session_join_user(
    test->proxy,
    "TestUser",
    INF_USER_ACTIVE,
    0.0,
    0,
    0,
    inf_test_text_quick_write_user_join_cb,
    test
  );
}

static void
inf_test_text_quick_write_session_notify_status_cb(GObject* object,
                                                   GParamSpec* pspec,
                                                   gpointer user_data)
{
  InfTestTextQuickWrite* test;
  test = (InfTestTextQuickWrite*)user_data;

  if(inf_session_get_status(test->session) == INF_SESSION_RUNNING)
    inf_test_text_quick_write_join_user(test);

  if(inf_session_get_status(test->session) == INF_SESSION_CLOSED)
  {
    fprintf(stderr, "Session closed remotely\n");
    inf_standalone_io_loop_quit(test->io);
  }
}

static void
inf_test_text_quick_write_subscribe_cb(InfRequest* request,
                                       const InfRequestResult* result,
                                       const GError* error,
                                       gpointer user_data)
{
  InfTestTextQuickWrite* test;
  test = (InfTestTextQuickWrite*)user_data;

  if(error != NULL)
  {
    fprintf(stderr, "%s\n", error->message);
    inf_standalone_io_loop_quit(test->io);
  }
  else
  {
    inf_request_result_get_subscribe_session(
      result,
      NULL,
      NULL,
      &test->proxy
    );

    g_object_ref(test->proxy);

    g_object_get(test->proxy, "session", &test->session, NULL);

    g_signal_connect(
      G_OBJECT(test->session),
      "notify::status",
      G_CALLBACK(inf_test_text_quick_write_session_notify_status_cb),
      test
    );

    if(inf_session_get_status(test->session) == INF_SESSION_RUNNING)
      inf_test_text_quick_write_join_user(test);
  }
}

static void
inf_test_text_quick_write_explore_cb(InfRequest* request,
                                     const InfRequestResult* result,
                                     const GError* error,
                                     gpointer user_data)
{
  InfTestTextQuickWrite* test;
  InfBrowserIter iter;
  gboolean have_iter;

  test = (InfTestTextQuickWrite*)user_data;

  /* The root node has been explored. Subscribe to the "/test" document. */
  if(error != NULL)
  {
    fprintf(stderr, "%s\n", error->message);
    inf_standalone_io_loop_quit(test->io);
  }
  else
  {
    inf_browser_get_root(test->browser, &iter);
    for(have_iter = inf_browser_get_child(test->browser, &iter);
        have_iter == TRUE;
        have_iter = inf_browser_get_next(test->browser, &iter))
    {
      if(strcmp(inf_browser_get_node_name(test->browser, &iter), "test") == 0)
      {
        inf_browser_subscribe(
          test->browser,
          &iter,
          inf_test_text_quick_write_subscribe_cb,
          test
        );

        break;
      }
    }

    if(have_iter == FALSE)
    {
      fprintf(stderr, "No document named /test\n");
      inf_standalone_io_loop_quit(test->io);
    }
  }
}

static void
inf_test_text_quick_write_error_cb(InfcBrowser* browser,
                                   GError* error,
                                   gpointer user_data)
{
  fprintf(stderr, "Connection error: %s\n", error->message);
}

static void
inf_test_text_quick_write_notify_status_cb(GObject* object,
                                           GParamSpec* pspec,
                                           gpointer user_data)
{
  InfTestTextQuickWrite* test;
  InfBrowserStatus status;
  InfBrowserIter iter;

  test = (InfTestTextQuickWrite*)user_data;
  g_object_get(G_OBJECT(test->browser), "status", &status, NULL);

  if(status == INF_BROWSER_OPEN)
  {
    printf("Connection established\n");

    /* Explore root node */
    inf_browser_get_root(test->browser, &iter);

    inf_browser_explore(
      test->browser,
      &iter,
      inf_test_text_quick_write_explore_cb,
      test
    );
  }

  if(status == INF_BROWSER_CLOSED)
  {
    if(inf_standalone_io_loop_running(test->io))
      inf_standalone_io_loop_quit(test->io);
  }
}

static void
inf_test_text_quick_write_disconnect(InfTestTextQuickWrite* test)
{
  if(test->buffer != NULL)
  {
    g_object_unref(test->buffer);
    test->buffer = NULL;
  }

  if(test->user != NULL)
  {
    g_object_unref(test->user);
    test->user = NULL;
  }

  if(test->session != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(test->session),
      G_CALLBACK(inf_test_text_quick_write_session_notify_status_cb),
      test
    );

    g_object_unref(test->session);
    test->session = NULL;
  }

  if(test->proxy != NULL)
  {
    g_object_unref(test->proxy);
    test->proxy = NULL;
  }

  if(test->browser != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(test->browser),
      G_CALLBACK(inf_test_text_quick_write_notify_status_cb),
      test
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(test->browser),
      G_CALLBACK(inf_test_text_quick_write_error_cb),
      test
    );

    g_object_unref(test->browser);
    test->browser = NULL;
  }

  if(test->conn != NULL)
  {
    g_object_unref(test->conn);
    test->conn = NULL;
  }
}

static void
inf_test_text_quick_write_reconnect(InfTestTextQuickWrite* test)
{
  const char* hostname;
  InfNameResolver* resolver;
  InfTcpConnection* tcp_conn;
  InfCommunicationManager* manager;
  GError* error;
  
  test->content_pos = 0;

  error = NULL;
  inf_test_text_quick_write_disconnect(test);

  hostname = "londeroth.org";

  resolver = inf_name_resolver_new(
    INF_IO(test->io),
    hostname,
    "6523",
    "_infinote._tcp"
  );

  tcp_conn = inf_tcp_connection_new_resolve(INF_IO(test->io), resolver);

  g_object_unref(resolver);

  if(inf_tcp_connection_open(tcp_conn, &error) == FALSE)
  {
    fprintf(stderr, "Could not open TCP connection: %s\n", error->message);
    g_error_free(error);

    g_object_unref(tcp_conn);
    inf_standalone_io_loop_quit(test->io);
  }
  else
  {
    test->conn = inf_xmpp_connection_new(
      tcp_conn,
      INF_XMPP_CONNECTION_CLIENT,
      NULL,
      "localhost",
      INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS,
      test->credentials,
      NULL,
      NULL
    );

    g_object_unref(tcp_conn);

    manager = inf_communication_manager_new();

    test->browser = INF_BROWSER(
      infc_browser_new(
        INF_IO(test->io),
        manager,
        INF_XML_CONNECTION(test->conn)
      )
    );

    g_object_unref(manager);
    
    infc_browser_add_plugin(
      INFC_BROWSER(test->browser),
      &INF_TEST_TEXT_QUICK_WRITE_PLUGIN
    );

    g_signal_connect_after(
      G_OBJECT(test->browser),
      "notify::status",
      G_CALLBACK(inf_test_text_quick_write_notify_status_cb),
      test
    );

    g_signal_connect(
      G_OBJECT(test->browser),
      "error",
      G_CALLBACK(inf_test_text_quick_write_error_cb),
      test
    );
  }
}

static void
inf_test_text_quick_write_start_cb(gpointer user_data)
{
  inf_test_text_quick_write_reconnect((InfTestTextQuickWrite*)user_data);
}

static InfCertificateCredentials*
inf_test_text_quick_write_load_credentials(const char* filename,
                                           GError** error)
{
  InfCertificateCredentials* creds;
  GPtrArray* certs;
  gnutls_x509_privkey_t key;
  guint i;

  key = inf_cert_util_read_private_key(filename, error);
  if(key == NULL) return NULL;

  certs = inf_cert_util_read_certificate(filename, NULL, error);
  if(certs == NULL)
  {
    gnutls_x509_privkey_deinit(key);
    return NULL;
  }

  creds = inf_certificate_credentials_new();

  gnutls_certificate_set_x509_key(
    inf_certificate_credentials_get(creds),
    (gnutls_x509_crt_t*)certs->pdata,
    certs->len,
    key
  );
  
  gnutls_certificate_set_x509_trust(
    inf_certificate_credentials_get(creds),
    (gnutls_x509_crt_t*)&certs->pdata[certs->len - 1],
    certs->len
  );

  gnutls_x509_privkey_deinit(key);
  for(i = 0; i < certs->len; ++i)
    gnutls_x509_crt_deinit(certs->pdata[i]);
  g_ptr_array_free(certs, TRUE);

  return creds;
}

int
main(int argc, char* argv[])
{
  guint32 seed;
  const gchar* filename;
  const gchar* credentials;
  InfTestTextQuickWrite test;
  GError* error;

  error = NULL;
  if(!inf_init(&error))
  {
    fprintf(stderr, "%s\n", error->message);
    return 1;
  }

  seed = time(NULL);
  printf("Random seed: %u\n", seed);
  g_random_set_seed(seed);

  if(argc < 2)
    filename = "inf-test-quick-write.c";
  else
    filename = argv[1];
  credentials = NULL;

  g_file_get_contents(filename, &test.content, &test.content_length, &error);
  
  if(error != NULL)
  {
    fprintf(stderr, "%s: %s\n", filename, error->message);
    g_error_free(error);
    return 1;
  }
  
  if(test.content_length == 0)
  {
    fprintf(stderr, "%s: File is empty\n", filename);
    return 1;
  }

  test.credentials = NULL;
  test.io = NULL;
  test.conn = NULL;
  test.browser = NULL;

  test.proxy = NULL;
  test.session = NULL;
  test.user = NULL;
  test.buffer = NULL;

  if(credentials != NULL)
  {
    test.credentials = inf_test_text_quick_write_load_credentials(
      credentials,
      &error
    );

    if(test.credentials == NULL)
    {
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
      return 1;
    }
  }

  test.io = inf_standalone_io_new();

  inf_io_add_dispatch(
    INF_IO(test.io),
    inf_test_text_quick_write_start_cb,
    &test,
    NULL
  );

  inf_standalone_io_loop(test.io);

  g_object_unref(test.io);
  inf_certificate_credentials_unref(test.credentials);
  g_free(test.content);
  return 0;
}

/* vim:set et sw=2 ts=2: */
