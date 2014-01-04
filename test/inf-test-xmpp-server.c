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
#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/common/inf-standalone-io.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void
conn_notify_status_cb(InfXmlConnection* connection,
                      GParamSpec* spec,
                      gpointer user_data)
{
  InfXmlConnectionStatus status;

  g_object_get(
    G_OBJECT(connection),
    "status", &status,
    NULL
  );

  switch(status)
  {
  case INF_XML_CONNECTION_CLOSED:
    fprintf(stderr, "Connection closed\n");
    break;
  case INF_XML_CONNECTION_CLOSING:
    fprintf(stderr, "Connection closing\n");
    break;
  case INF_XML_CONNECTION_OPENING:
    fprintf(stderr, "Connection opening\n");
    break;
  case INF_XML_CONNECTION_OPEN:
    fprintf(stderr, "Connection open\n");
    break;
  }
}

static void
conn_error_cb(InfXmlConnection* connection,
              GError* error,
              gpointer user_data)
{
  fprintf(stderr, "Connection error occured: %s\n", error->message);
}

static void
new_connection_cb(InfdXmlServer* server,
                  InfXmlConnection* connection,
                  gpointer user_data)
{
  fprintf(stderr, "New connection\n");

  g_signal_connect(
    G_OBJECT(connection),
    "error",
    G_CALLBACK(conn_error_cb),
    NULL
  );

  g_signal_connect(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(conn_notify_status_cb),
    NULL
  );

  g_object_ref(G_OBJECT(connection));
}

static void
error_cb(InfdXmppServer* server,
         GError* error,
         gpointer user_data)
{
  fprintf(stderr, "Server Error occured: %s\n", error->message);
}

static void
notify_status_cb(InfdXmlServer* server,
                 GParamSpec* pspec,
                 gpointer user_data)
{
  InfdXmlServerStatus status;

  g_object_get(
    G_OBJECT(server),
    "status", &status,
    NULL
  );

  switch(status)
  {
  case INFD_XML_SERVER_CLOSED:
    printf("Server closed\n");
    inf_standalone_io_loop_quit(INF_STANDALONE_IO(user_data));
    break;
  case INFD_XML_SERVER_CLOSING:
    printf("Server closing\n");
    break;
  case INFD_XML_SERVER_OPENING:
    printf("Server opening\n");
    break;
  case INFD_XML_SERVER_OPEN:
    printf("Server open\n");
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

int main(int argc, char* argv[])
{
  InfStandaloneIo* io;
  InfdTcpServer* server;
  InfdXmppServer* xmpp;
  GError* error;

  gnutls_global_init();
  g_type_init();

  io = inf_standalone_io_new();
  error = NULL;

  server = g_object_new(
    INFD_TYPE_TCP_SERVER,
    "io", io,
    "local-port", 5223,
    NULL
  );

  if(infd_tcp_server_open(server, &error) == FALSE)
  {
    fprintf(stderr, "Could not open server: %s\n", error->message);
    g_error_free(error);
  }
  else
  {
    xmpp = infd_xmpp_server_new(
      server,
      INF_XMPP_CONNECTION_SECURITY_ONLY_UNSECURED,
      NULL,
      NULL,
      NULL
    );

    g_signal_connect(
      G_OBJECT(xmpp),
      "error",
      G_CALLBACK(error_cb),
      io
    );

    g_signal_connect(
      G_OBJECT(xmpp),
      "new-connection",
      G_CALLBACK(new_connection_cb),
      io
    );

    g_signal_connect(
      G_OBJECT(xmpp),
      "notify::status",
      G_CALLBACK(notify_status_cb),
      io
    );

    inf_standalone_io_loop(io);
  }

  g_object_unref(G_OBJECT(io));
  g_object_unref(G_OBJECT(server));

  return 0;
}

/* vim:set et sw=2 ts=2: */
