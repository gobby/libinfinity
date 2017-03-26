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

#include <libinfinity/server/infd-tcp-server.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-init.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void
conn_notify_status_cb(InfTcpConnection* connection,
                      GParamSpec* spec,
                      gpointer user_data)
{
  InfIpAddress* addr;
  InfTcpConnectionStatus status;
  gchar* str;

  g_object_get(
    G_OBJECT(connection),
    "remote-address", &addr,
    "status", &status,
    NULL
  );

  str = inf_ip_address_to_string(addr);
  inf_ip_address_free(addr);

  if(status == INF_TCP_CONNECTION_CLOSED)
    printf("Connection close from %s\n", str);

  g_free(str);
}

static void
conn_error_cb(InfTcpConnection* connection,
              GError* error,
              gpointer user_data)
{
  InfIpAddress* addr;
  gchar* str;
  g_object_get(G_OBJECT(connection), "remote-address", &addr, NULL);
  str = inf_ip_address_to_string(addr);
  inf_ip_address_free(addr);

  printf("Error from %s: %s\n", str, error->message);
  g_free(str);

  g_object_unref(G_OBJECT(connection));
}

static void
conn_received_cb(InfTcpConnection* connection,
                 gconstpointer data,
                 guint len,
                 gpointer user_data)
{
  InfIpAddress* addr;
  gchar* str;
  g_object_get(G_OBJECT(connection), "remote-address", &addr, NULL);
  str = inf_ip_address_to_string(addr);
  inf_ip_address_free(addr);

  printf("Data from %s: %.*s\n", str, (int)len, (const char*)data);
  g_free(str);
}

static void
new_connection_cb(InfdTcpServer* server,
                  InfTcpConnection* connection,
                  gpointer user_data)
{
  InfIpAddress* addr;
  gchar* str;
  g_object_get(G_OBJECT(connection), "remote-address", &addr, NULL);
  str = inf_ip_address_to_string(addr);
  inf_ip_address_free(addr);

  printf("Connection from %s\n", str);
  g_free(str);

  g_signal_connect(
    G_OBJECT(connection),
    "received",
    G_CALLBACK(conn_received_cb),
    NULL
  );

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
error_cb(InfdTcpServer* server,
         GError* error,
         gpointer user_data)
{
  fprintf(stderr, "Error occurred: %s\n", error->message);
  if(inf_standalone_io_loop_running(INF_STANDALONE_IO(user_data)))
    inf_standalone_io_loop_quit(INF_STANDALONE_IO(user_data));
}

static void
notify_status_cb(InfdTcpServer* server,
                 GParamSpec* pspec,
                 gpointer user_data)
{
  InfdTcpServerStatus status;
  InfIpAddress* addr;
  guint port;
  gchar* addr_str;

  g_object_get(
    G_OBJECT(server),
    "status", &status,
    "local-address", &addr,
    "local-port", &port,
    NULL
  );

  addr_str = inf_ip_address_to_string(addr);
  inf_ip_address_free(addr);

  switch(status)
  {
  case INFD_TCP_SERVER_CLOSED:
    printf("Server closed\n");
    break;
  case INFD_TCP_SERVER_OPEN:
    printf("Server listening on %s:%u\n", addr_str, port);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_free(addr_str);
}

int main(int argc, char* argv[])
{
  InfStandaloneIo* io;
  InfdTcpServer* server;
  GError* error;

  error = NULL;
  if(!inf_init(&error))
  {
    fprintf(stderr, "%s", error->message);
    return 1;
  }

  io = inf_standalone_io_new();

  server = g_object_new(
    INFD_TYPE_TCP_SERVER,
    "io", io,
    "local-port", 5223,
    NULL
  );

  g_signal_connect(
    G_OBJECT(server),
    "error",
    G_CALLBACK(error_cb),
    io
  );

  g_signal_connect(
    G_OBJECT(server),
    "new-connection",
    G_CALLBACK(new_connection_cb),
    io
  );

  g_signal_connect(
    G_OBJECT(server),
    "notify::status",
    G_CALLBACK(notify_status_cb),
    io
  );

  if(infd_tcp_server_open(server, &error) == FALSE)
  {
    fprintf(stderr, "Could not open server: %s\n", error->message);
    g_error_free(error);
  }
  else
  {
    inf_standalone_io_loop(io);
  }

  g_object_unref(G_OBJECT(io));
  g_object_unref(G_OBJECT(server));

  return 0;
}

/* vim:set et sw=2 ts=2: */
