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

#include <libinfinity/common/inf-tcp-connection.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-init.h>

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
  printf("Received: \033[00;32m%.*s\033[00;00m", (int)len, (const char*)buffer);
  fflush(stdout);
}

static void
sent_cb(InfTcpConnection* connection,
        gconstpointer buffer,
        guint len,
        gpointer user_data)
{
  printf("Sent: \033[00;34m%.*s\033[00;00m", (int)len, (const char*)buffer);
  fflush(stdout);
}

static void
error_cb(InfTcpConnection* connection,
         GError* error,
         gpointer user_data)
{
  fprintf(stderr, "Connection Error occured: %s\n", error->message);
  if(inf_standalone_io_loop_running(INF_STANDALONE_IO(user_data)))
    inf_standalone_io_loop_quit(INF_STANDALONE_IO(user_data));
}

static void
resolved_cb(InfNameResolver* resolver,
            const GError* error,
            gpointer user_data)
{
  gchar* hostname;
  gchar* service;
  gchar* srv;
  guint i;
  gchar* str;

  if(error != NULL)
  {
    fprintf(stderr, "Resolver error: %s\n", error->message);
  }
  else
  {
    g_object_get(
      G_OBJECT(resolver),
      "hostname", &hostname,
      "service", &service,
      "srv", &srv,
      NULL
    );

    printf(
      "Resolved hostname %s (SRV %s):\n",
      hostname,
      srv ? srv : "(nul)"
    );

    g_free(hostname);
    g_free(service);
    g_free(srv);

    for(i = 0; i < inf_name_resolver_get_n_addresses(resolver); ++i)
    {
      str = inf_ip_address_to_string(
        inf_name_resolver_get_address(resolver, i)
      );

      printf(
        "  %u: %s (port %u)\n",
        i,
        str,
        inf_name_resolver_get_port(resolver, i)
      );

      g_free(str);
    }
  }
}

static void
notify_status_cb(InfTcpConnection* connection,
                 GParamSpec* pspec,
                 gpointer user_data)
{
  InfTcpConnectionStatus status;
  InfIpAddress* addr;
  guint port;
  InfNameResolver* resolver;
  gchar* addr_str_tmp;
  gchar* addr_str;

  g_object_get(
    G_OBJECT(connection),
    "status", &status,
    "remote-address", &addr,
    "remote-port", &port,
    "resolver", &resolver,
    NULL
  );

  if(addr != NULL)
  {
    addr_str_tmp = inf_ip_address_to_string(addr);
    addr_str = g_strdup_printf("%s:%u", addr_str_tmp, port);
    g_free(addr_str_tmp);
  }
  else
  {
    g_object_get(G_OBJECT(resolver), "hostname", &addr_str, NULL);
  }

  if(addr != NULL)
    inf_ip_address_free(addr);
  if(resolver != NULL)
    g_object_unref(resolver);

  switch(status)
  {
  case INF_TCP_CONNECTION_CONNECTING:
    printf("Connecting to %s\n", addr_str);
    break;
  case INF_TCP_CONNECTION_CONNECTED:
    printf("Connected to %s\n", addr_str);

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
    printf("Connection to %s closed\n", addr_str);
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
  InfStandaloneIo* io;
  InfNameResolver* resolver;
  InfTcpConnection* connection;
  GError* error;

  error = NULL;
  if(inf_init(&error) == FALSE)
  {
    fprintf(stderr, "%s", error->message);
    g_error_free(error);
    return 1;
  }

  io = inf_standalone_io_new();

  resolver =
    inf_name_resolver_new(INF_IO(io), "0x539.de", "5223", "_jabber._tcp");

  g_signal_connect(
    G_OBJECT(resolver),
    "resolved",
    G_CALLBACK(resolved_cb),
    io
  );

  connection = inf_tcp_connection_new_resolve(INF_IO(io), resolver);
  g_object_unref(resolver);

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

/* vim:set et sw=2 ts=2: */
