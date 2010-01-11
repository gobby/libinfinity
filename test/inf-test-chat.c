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

#include <libinfinity/client/infc-browser.h>
#include <libinfinity/communication/inf-communication-manager.h>
#include <libinfinity/common/inf-chat-buffer.h>
#include <libinfinity/common/inf-chat-session.h>
#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-tcp-connection.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-io.h>

#include <string.h>

typedef struct _InfTestChat InfTestChat;
struct _InfTestChat {
  InfStandaloneIo* io;
  InfXmppConnection* conn;
  InfcBrowser* browser;
#ifndef G_OS_WIN32
  int input_fd;
#endif

  InfChatBuffer* buffer;
  InfUser* self;
};

static void
inf_test_chat_input_cb(InfNativeSocket* fd,
                       InfIoEvent io,
                       gpointer user_data)
{
  InfTestChat* test;
  char buffer[1024];

  test = (InfTestChat*)user_data;

  if(io & INF_IO_ERROR)
  {
  }

  if(io & INF_IO_INCOMING)
  {
    if(fgets(buffer, sizeof(buffer), stdin) == NULL)
    {
      inf_standalone_io_loop_quit(test->io);
    }
    else if(strlen(buffer) != sizeof(buffer) ||
            buffer[sizeof(buffer)-2] == '\n')
    {
      buffer[strlen(buffer)-1] = '\0';

      if(test->buffer != NULL && test->self != NULL)
      {
        inf_chat_buffer_add_message(
          test->buffer,
          test->self,
          buffer,
          strlen(buffer),
          time(NULL)
        );
      }
    }
  }
}

static void
inf_chat_test_buffer_receive_message_cb(InfChatSession* session,
                                        InfChatBufferMessage* message,
                                        gpointer user_data)
{
  switch(message->type)
  {
  case INF_CHAT_BUFFER_MESSAGE_NORMAL:
    printf("<%s> %s\n", inf_user_get_name(message->user), message->text);
    break;
  case INF_CHAT_BUFFER_MESSAGE_EMOTE:
    printf(" * %s %s\n", inf_user_get_name(message->user), message->text);
    break;
  case INF_CHAT_BUFFER_MESSAGE_USERJOIN:
    printf(" --> %s has joined\n", inf_user_get_name(message->user));
    break;
  case INF_CHAT_BUFFER_MESSAGE_USERPART:
    printf(" <-- %s has left\n", inf_user_get_name(message->user));
    break;
  }
}

static void
inf_test_chat_userjoin_finished_cb(InfcRequest* request,
                                   InfUser* user,
                                   gpointer user_data)
{
  InfTestChat* test;
  test = (InfTestChat*)user_data;

  printf("User join complete. Start chatting!\n");

#ifndef G_OS_WIN32
  inf_io_watch(
    INF_IO(test->io),
    &test->input_fd,
    INF_IO_INCOMING | INF_IO_ERROR,
    inf_test_chat_input_cb,
    test,
    NULL
  );
#endif

  test->self = user;
}

static void
inf_test_chat_userjoin_failed_cb(InfcRequest* request,
                                 const GError* error,
                                 gpointer user_data)
{
  fprintf(stderr, "User join failed: %s\n", error->message);
  fprintf(stderr, "Chat will be read-only\n");
}

static void
inf_chat_test_session_synchronization_complete_cb(InfSession* session,
                                                  InfXmlConnection* connection,
                                                  gpointer user_data)
{
  InfTestChat* test;
  InfcSessionProxy* proxy;
  InfcUserRequest* request;
  GParameter params[1] = { { "name", { 0 } } };
  GError* error;

  printf("Synchronization complete, joining user...\n");

  test = (InfTestChat*)user_data;
  proxy = infc_browser_get_chat_session(test->browser);

  g_value_init(&params[0].value, G_TYPE_STRING);
  g_value_set_string(&params[0].value, g_get_user_name());

  error = NULL;
  request = infc_session_proxy_join_user(
    proxy,
    params,
    G_N_ELEMENTS(params),
    &error
  );

  g_value_unset(&params[0].value);

  if(!request)
  {
    fprintf(stderr, "User join failed: %s\n", error->message);
    g_error_free(error);

    inf_standalone_io_loop_quit(test->io);
  }
  else
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "failed",
      G_CALLBACK(inf_test_chat_userjoin_failed_cb),
      test
    );

    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(inf_test_chat_userjoin_finished_cb),
      test
    );
  }
}

static void
inf_chat_test_session_synchronization_failed_cb(InfSession* session,
                                                InfXmlConnection* connection,
                                                const GError* error,
                                                gpointer user_data)
{
  InfTestChat* test;
  test = (InfTestChat*)user_data;

  fprintf(stderr, "Synchronization failed: %s\n", error->message);
  inf_standalone_io_loop_quit(test->io);
}

static void
inf_chat_test_session_close_cb(InfSession* session,
                               gpointer user_data)
{
  InfTestChat* test;
  test = (InfTestChat*)user_data;

  printf("The server closed the chat session\n");
  if(inf_standalone_io_loop_running(test->io))
    inf_standalone_io_loop_quit(test->io);
}

static void
inf_chat_test_subscribe_finished_cb(InfcNodeRequest* request,
                                    const InfcBrowserIter* iter,
                                    gpointer user_data)
{
  InfTestChat* test;
  InfSession* session;
  test = (InfTestChat*)user_data;

  printf("Subscription successful, waiting for synchronization...\n");

  session = infc_session_proxy_get_session(
    infc_browser_get_chat_session(test->browser));

  test->buffer = INF_CHAT_BUFFER(inf_session_get_buffer(session));

  /* TODO: Show backlog after during/after synchronization */

  g_signal_connect_after(
    G_OBJECT(session),
    "receive-message",
    G_CALLBACK(inf_chat_test_buffer_receive_message_cb),
    test
  );

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-complete",
    G_CALLBACK(inf_chat_test_session_synchronization_complete_cb),
    test
  );

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-failed",
    G_CALLBACK(inf_chat_test_session_synchronization_failed_cb),
    test
  );

  /* This can happen when the server disables the chat without being
   * shutdown. */
  g_signal_connect_after(
    G_OBJECT(session),
    "close",
    G_CALLBACK(inf_chat_test_session_close_cb),
    test
  );
}

static void
inf_chat_test_subscribe_failed_cb(InfcRequest* request,
                                  const GError* error,
                                  gpointer user_data)
{
  InfTestChat* test;
  test = (InfTestChat*)user_data;

  fprintf(stderr, "Subscription failed: %s\n", error->message);
  inf_standalone_io_loop_quit(test->io);
}

static void
inf_test_chat_notify_status_cb(GObject* object,
                               GParamSpec* pspec,
                               gpointer user_data)
{
  InfTestChat* test;
  InfcBrowserStatus status;
  InfcNodeRequest* request;

  test = (InfTestChat*)user_data;
  status = infc_browser_get_status(test->browser);

  if(status == INFC_BROWSER_CONNECTED)
  {
    printf("Connection established, subscribing to chat...\n");

    /* Subscribe to chat */
    request = infc_browser_subscribe_chat(test->browser);

    g_signal_connect_after(
      G_OBJECT(request),
      "failed",
      G_CALLBACK(inf_chat_test_subscribe_failed_cb),
      test
    );

    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(inf_chat_test_subscribe_finished_cb),
      test
    );
  }

  if(status == INFC_BROWSER_DISCONNECTED)
  {
    printf("Connection closed\n");
    if(inf_standalone_io_loop_running(test->io))
      inf_standalone_io_loop_quit(test->io);
  }
}

static void
inf_test_chat_error_cb(InfXmppConnection* xmpp,
                          GError* error,
                          gpointer user_data)
{
  /* status notify will close conn: */
  fprintf(stderr, "Connection error: %s\n", error->message);
}

int
main(int argc, char* argv[])
{
  InfTestChat test;
  InfIpAddress* address;
  InfCommunicationManager* manager;
  InfTcpConnection* tcp_conn;
  GError* error;

  gnutls_global_init();
  g_type_init();

  test.io = inf_standalone_io_new();
#ifndef G_OS_WIN32
  test.input_fd = STDIN_FILENO;
#endif
  test.buffer = NULL;

  address = inf_ip_address_new_loopback4();

  error = NULL;
  tcp_conn =
    inf_tcp_connection_new_and_open(INF_IO(test.io), address, 6523, &error);

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
    test.browser = infc_browser_new(
      INF_IO(test.io),
      manager,
      INF_XML_CONNECTION(test.conn)
    );

    g_signal_connect_after(
      G_OBJECT(test.browser),
      "notify::status",
      G_CALLBACK(inf_test_chat_notify_status_cb),
      &test
    );

    g_signal_connect(
      G_OBJECT(test.browser),
      "error",
      G_CALLBACK(inf_test_chat_error_cb),
      &test
    );

    inf_standalone_io_loop(test.io);
    g_object_unref(G_OBJECT(manager));
    g_object_unref(G_OBJECT(test.browser));

    /* TODO: Wait until the XMPP connection is in status closed */
    g_object_unref(G_OBJECT(test.conn));
  }

  g_object_unref(G_OBJECT(test.io));
  return 0;
}

/* vim:set et sw=2 ts=2: */
