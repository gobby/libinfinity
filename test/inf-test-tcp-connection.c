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

#include <libinfinity/common/inf-tcp-connection.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-standalone-io.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void
received_cb(InfTcpConnection* connection,
            gconstpointer buffer,
	    guint len,
	    gpointer user_data)
{
  printf("\033[00;32m%.*s\033[00;00m", (int)len, (const char*)buffer);
  fflush(stdout);
}

static void
sent_cb(InfTcpConnection* connection,
        gconstpointer buffer,
	guint len,
	gpointer user_data)
{
  printf("\033[00;34m%.*s\033[00;00m", (int)len, (const char*)buffer);
  fflush(stdout);
}

static void
error_cb(InfTcpConnection* connection,
         GError* error,
	 gpointer user_data)
{
  fprintf(stderr, "Error occured: %s\n", error->message);
  if(inf_standalone_io_loop_running(INF_STANDALONE_IO(user_data)))
    inf_standalone_io_loop_quit(INF_STANDALONE_IO(user_data));
}

static void
notify_status_cb(InfTcpConnection* connection,
                 const gchar* property,
		 gpointer user_data)
{
  InfTcpConnectionStatus status;
  InfIpAddress* addr;
  guint port;
  gchar* addr_str;

  g_object_get(
    G_OBJECT(connection),
    "status", &status,
    "remote-address", &addr,
    "remote-port", &port,
    NULL
  );

  addr_str = inf_ip_address_to_string(addr);
  inf_ip_address_free(addr);

  switch(status)
  {
  case INF_TCP_CONNECTION_CONNECTING:
    printf("Connecting to %s:%u\n", addr_str, port);
    break;
  case INF_TCP_CONNECTION_CONNECTED:
    printf("Connected to %s:%u\n", addr_str, port);

    g_object_get(
      G_OBJECT(connection),
      "local-address", &addr,
      "local-port", &port,
      NULL
    );

    g_free(addr_str);
    addr_str = inf_ip_address_to_string(addr);
    inf_ip_address_free(addr);

    printf("Connected from %s:%u\n", addr_str, port);
    inf_tcp_connection_send(connection, "Hello, World!\n", 14);
    break;
  case INF_TCP_CONNECTION_CLOSED:
    printf("Connection to %s:%u closed\n", addr_str, port);
    if(inf_standalone_io_loop_running(INF_STANDALONE_IO(user_data)))
      inf_standalone_io_loop_quit(INF_STANDALONE_IO(user_data));
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_free(addr_str);
}

int main(int argc, char* argv[])
{
  InfIpAddress* addr;
  InfStandaloneIo* io;
  InfTcpConnection* connection;
  GError* error;

  g_type_init();

  addr = inf_ip_address_new_loopback4();
  io = inf_standalone_io_new();
  error = NULL;

  connection = g_object_new(
    INF_TYPE_TCP_CONNECTION,
    "io", io,
    "remote-address", addr,
    "remote-port", 5223,
    NULL
  );

  inf_ip_address_free(addr);

  g_signal_connect(
    G_OBJECT(connection),
    "received",
    G_CALLBACK(received_cb),
    io
  );

  g_signal_connect(
    G_OBJECT(connection),
    "sent",
    G_CALLBACK(sent_cb),
    io
  );

  g_signal_connect(
    G_OBJECT(connection),
    "error",
    G_CALLBACK(error_cb),
    io
  );

  g_signal_connect(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(notify_status_cb),
    io
  );

  if(inf_tcp_connection_open(connection, &error) == FALSE)
  {
    fprintf(stderr, "Could not open connection: %s\n", error->message);
    g_error_free(error);
  }
  else
  {
    inf_standalone_io_loop(io);
  }

  g_object_unref(G_OBJECT(io));
  g_object_unref(G_OBJECT(connection));

  return 0;
}
