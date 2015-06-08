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

#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-session.h>
#include <libinfinity/client/infc-browser.h>
#include <libinfinity/client/infc-session-proxy.h>
#include <libinfinity/adopted/inf-adopted-session.h>
#include <libinfinity/adopted/inf-adopted-algorithm.h>
#include <libinfinity/adopted/inf-adopted-state-vector.h>
#include <libinfinity/common/inf-request-result.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-tcp-connection.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/common/inf-protocol.h>
#include <libinfinity/common/inf-init.h>

#include <string.h>

typedef struct _InfTestMassJoiner InfTestMassJoiner;
struct _InfTestMassJoiner {
  InfCommunicationManager* communication_manager;
  InfcBrowser* browser;
  InfcSessionProxy* session;

  gchar* document;
  gchar* username;
};

typedef struct _InfTestMassJoin InfTestMassJoin;
struct _InfTestMassJoin {
  InfIo* io;
  GSList* joiners;
};

static InfSession*
inf_test_mass_join_session_new(InfIo* io,
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

static const InfcNotePlugin INF_TEST_MASS_JOIN_TEXT_PLUGIN = {
  NULL, "InfText", inf_test_mass_join_session_new
};

static void
inf_test_mass_join_user_join_finished_cb(InfRequest* request,
                                         const InfRequestResult* result,
                                         const GError* error,
                                         gpointer user_data)
{
  InfTestMassJoiner* joiner;
  joiner = (InfTestMassJoiner*)user_data;

  if(error == NULL)
  {
    fprintf(stdout, "Joiner %s: User joined!\n", joiner->username);
  }
  else
  {
    fprintf(
      stderr,
      "Joiner %s: User join failed: %s\n",
      joiner->username,
      error->message
    );

    inf_xml_connection_close(infc_browser_get_connection(joiner->browser));
  }
}

static void
inf_test_mass_join_join_user(InfTestMassJoiner* joiner)
{
  InfSession* session;
  InfAdoptedStateVector* v;
  GParameter params[3] = {
    { "name", { 0 } },
    { "vector", { 0 } },
    { "caret-position", { 0 } }
  };

  g_value_init(&params[0].value, G_TYPE_STRING);
  g_value_init(&params[1].value, INF_ADOPTED_TYPE_STATE_VECTOR);
  g_value_init(&params[2].value, G_TYPE_UINT);

  g_value_set_static_string(&params[0].value, joiner->username);

  g_object_get(G_OBJECT(joiner->session), "session", &session, NULL);
  v = inf_adopted_algorithm_get_current(
    inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session))
  );
  g_object_unref(session);

  g_value_set_boxed(&params[1].value, v);
  g_value_set_uint(&params[2].value, 0u);

  inf_session_proxy_join_user(
    INF_SESSION_PROXY(joiner->session),
    3,
    params,
    inf_test_mass_join_user_join_finished_cb,
    joiner
  );

  g_value_unset(&params[2].value);
  g_value_unset(&params[1].value);
  g_value_unset(&params[0].value);
}

static void
inf_test_mass_join_session_synchronization_failed_cb(InfSession* session,
                                                     InfXmlConnection* connection,
                                                     const GError* error,
                                                     gpointer user_data)
{
  InfTestMassJoiner* joiner;
  joiner = (InfTestMassJoiner*)user_data;

  fprintf(
    stderr,
    "Joiner %s: Session synchronization failed: %s\n",
    joiner->username,
    error->message
  );

  inf_xml_connection_close(infc_browser_get_connection(joiner->browser));
}

static void
inf_test_mass_join_session_synchronization_complete_cb(InfSession* session,
                                                       InfXmlConnection* connection,
                                                       gpointer user_data)
{
  InfTestMassJoiner* joiner;
  joiner = (InfTestMassJoiner*)user_data;
 
  inf_test_mass_join_join_user(joiner);
}

static void
inf_test_mass_join_subscribe_finished_cb(InfRequest* request,
                                         const InfRequestResult* result,
                                         const GError* error,
                                         gpointer user_data)
{
  InfTestMassJoiner* joiner;
  const InfBrowserIter* iter;
  InfSession* session;

  joiner = (InfTestMassJoiner*)user_data;
  inf_request_result_get_subscribe_session(result, NULL, &iter, NULL);

  joiner->session = INFC_SESSION_PROXY(
    inf_browser_get_session(
      INF_BROWSER(joiner->browser),
      iter
    )
  );

  g_assert(joiner->session != NULL);

  g_object_get(G_OBJECT(joiner->session), "session", &session, NULL);
  switch(inf_session_get_status(session))
  {
  case INF_SESSION_PRESYNC:
  case INF_SESSION_SYNCHRONIZING:
    g_signal_connect_after(
      G_OBJECT(session),
      "synchronization-failed",
      G_CALLBACK(inf_test_mass_join_session_synchronization_failed_cb),
      joiner
    );

    g_signal_connect_after(
      G_OBJECT(session),
      "synchronization-complete",
      G_CALLBACK(inf_test_mass_join_session_synchronization_complete_cb),
      joiner
    );

    break;
  case INF_SESSION_RUNNING:
    inf_test_mass_join_join_user(joiner);
    break;
  case INF_SESSION_CLOSED:
    fprintf(
      stderr,
      "Joiner %s: Session closed after subscription\n",
      joiner->username
    );

    inf_xml_connection_close(infc_browser_get_connection(joiner->browser));
    break;
  }

  g_object_unref(session);
}

static void
inf_test_mass_join_explore_finished_cb(InfRequest* request,
                                       const InfRequestResult* result,
                                       const GError* error,
                                       gpointer user_data)
{
  InfTestMassJoiner* joiner;
  InfBrowser* browser;
  InfBrowserIter iter;
  const char* name;
  gboolean document_exists;

  joiner = (InfTestMassJoiner*)user_data;
  browser = INF_BROWSER(joiner->browser);
  inf_browser_get_root(browser, &iter);
  if(inf_browser_get_child(browser, &iter) == FALSE)
  {
    fprintf(
      stderr,
      "Joiner %s: Document %s does not exist\n",
      joiner->username,
      joiner->document
    );

    inf_xml_connection_close(infc_browser_get_connection(joiner->browser));
  }

  document_exists = FALSE;

  do
  {
    name = inf_browser_get_node_name(browser, &iter);
    if(strcmp(name, joiner->document) == 0)
    {
      document_exists = TRUE;

      inf_browser_subscribe(
        browser,
        &iter,
        inf_test_mass_join_subscribe_finished_cb,
        joiner
      );

      break;
    }
  } while(inf_browser_get_next(browser, &iter) == TRUE);

  if(!document_exists)
  {
    fprintf(
      stderr,
      "Joiner %s: Document %s does not exist\n",
      joiner->username,
      joiner->document
    );

    inf_xml_connection_close(infc_browser_get_connection(joiner->browser));
  }
}

static void
inf_test_mass_join_browser_notify_status_cb(GObject* object,
                                            const GParamSpec* pspec,
                                            gpointer user_data)
{
  InfBrowser* browser;
  InfBrowserStatus status;
  InfBrowserIter iter;

  InfTestMassJoin* massjoin;
  InfTestMassJoiner* joiner;
  GSList* item;

  browser = INF_BROWSER(object);
  massjoin = (InfTestMassJoin*)user_data;
  joiner = NULL;
  for(item = massjoin->joiners; item != NULL; item = item->next)
  {
    joiner = (InfTestMassJoiner*)item->data;
    if(INF_BROWSER(joiner->browser) == browser)
      break;
  }

  g_assert(joiner != NULL);

  g_object_get(G_OBJECT(browser), "status", &status, NULL);
  switch(status)
  {
  case INF_BROWSER_OPENING:
    /* nothing to do */
    break;
  case INF_BROWSER_OPEN:
    fprintf(stdout, "Joiner %s: Connected\n", joiner->username);

    inf_browser_get_root(browser, &iter);

    inf_browser_explore(
      browser,
      &iter,
      inf_test_mass_join_explore_finished_cb,
      joiner
    );

    break;
  case INF_BROWSER_CLOSED:
    fprintf(stdout, "Joiner %s: Disconnected\n", joiner->username);
    massjoin->joiners = g_slist_remove(massjoin->joiners, joiner);
    if(massjoin->joiners == NULL)
      inf_standalone_io_loop_quit(INF_STANDALONE_IO(massjoin->io));
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_test_mass_join_connect(InfTestMassJoin* massjoin,
                           const char* hostname,
                           guint port,
                           const char* document,
                           const char* username)
{
  InfIpAddress* addr;
  InfTcpConnection* tcp;
  InfXmppConnection* xmpp;
  InfTestMassJoiner* joiner;
  InfXmlConnection* xml;
  GError* error;

  addr = inf_ip_address_new_from_string(hostname);
  tcp = inf_tcp_connection_new(massjoin->io, addr, port);
  xmpp = inf_xmpp_connection_new(
    tcp,
    INF_XMPP_CONNECTION_CLIENT,
    g_get_host_name(),
    hostname,
    INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS,
    NULL,
    NULL,
    NULL
  );

  joiner = g_slice_new(InfTestMassJoiner);
  joiner->communication_manager = inf_communication_manager_new();
  joiner->browser = infc_browser_new(
    massjoin->io,
    joiner->communication_manager,
    INF_XML_CONNECTION(xmpp)
  );
  joiner->session = NULL;
  joiner->document = g_strdup(document);
  joiner->username = g_strdup(username);

  g_object_unref(xmpp);
  g_object_unref(tcp);
  inf_ip_address_free(addr);

  massjoin->joiners = g_slist_prepend(massjoin->joiners, joiner);
  infc_browser_add_plugin(joiner->browser, &INF_TEST_MASS_JOIN_TEXT_PLUGIN);

  g_signal_connect(
    G_OBJECT(joiner->browser),
    "notify::status",
    G_CALLBACK(inf_test_mass_join_browser_notify_status_cb),
    massjoin
  );

  error = NULL;
  xml = infc_browser_get_connection(joiner->browser);
  if(inf_xml_connection_open(xml, &error) == FALSE)
  {
    fprintf(
      stderr,
      "Joiner %s: Failed to connect to %s: %s\n",
      joiner->username,
      hostname,
      error->message
    );

    g_error_free(error);
    massjoin->joiners = g_slist_remove(massjoin->joiners, joiner);

    if(massjoin->joiners == NULL)
      inf_standalone_io_loop_quit(INF_STANDALONE_IO(massjoin->io));
  }
}

int
main(int argc,
     char* argv[])
{
  InfTestMassJoin massjoin;
  GError* error;
  int i;
  gchar* name;

  error = NULL;
  if(!inf_init(&error))
  {
    fprintf(stderr, "%s\n", error->message);
    return -1;
  }

  massjoin.io = INF_IO(inf_standalone_io_new());
  massjoin.joiners = NULL;

  for(i = 0; i < 128; ++i)
  {
    name = g_strdup_printf("MassJoin%03d", i);

    inf_test_mass_join_connect(
      &massjoin,
      "127.0.0.1",
      inf_protocol_get_default_port(),
      "Test",
      name
    );

    g_free(name);
    //g_usleep(100000);
  }

  inf_standalone_io_loop(INF_STANDALONE_IO(massjoin.io));
  return 0;
}

/* vim:set et sw=2 ts=2: */
