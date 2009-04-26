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

#include <libinfinity/common/inf-xmpp-connection.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-tcp-connection.h>
#include <libinfinity/common/inf-ip-address.h>
#include <libinfinity/common/inf-standalone-io.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static void
error_cb(InfTcpConnection* connection,
         GError* error,
	 gpointer user_data)
{
  fprintf(stderr, "Error occured: %s\n", error->message);
/*  if(inf_standalone_io_loop_running(INF_STANDALONE_IO(user_data)))
    inf_standalone_io_loop_quit(INF_STANDALONE_IO(user_data));*/
}

static void
notify_status_cb(InfXmppConnection* xmpp,
                 const gchar* property,
		 gpointer user_data)
{
  InfXmlConnectionStatus status;

  g_object_get(G_OBJECT(xmpp), "status", &status,NULL);

  switch(status)
  {
  case INF_XML_CONNECTION_OPENING:
    printf("Opening\n");
    break;
  case INF_XML_CONNECTION_OPEN:
    printf("Opened\n");
    inf_xml_connection_close(INF_XML_CONNECTION(xmpp));
    break;
  case INF_XML_CONNECTION_CLOSING:
    printf("Closing\n");
    break;
  case INF_XML_CONNECTION_CLOSED:
    printf("Closed\n");
    inf_standalone_io_loop_quit(INF_STANDALONE_IO(user_data));
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

int main(int argc, char* argv[])
{
  InfIpAddress* addr;
  InfStandaloneIo* io;
  InfTcpConnection* connection;
  InfXmppConnection* xmpp;
  GError* error;

  gnutls_global_init();
  g_type_init();

#if 0
  addr = inf_ip_address_new_from_string("88.198.49.206"); /* This is jabber.0x539.de aka durotan.0x539.de */
#else
  addr = inf_ip_address_new_from_string("127.0.0.1"); /* This is localhost aka loopback */
#endif
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

  if(inf_tcp_connection_open(connection, &error) == FALSE)
  {
    fprintf(stderr, "Could not open connection: %s\n", error->message);
    g_error_free(error);
  }
  else
  {
    xmpp = inf_xmpp_connection_new(
      connection,
      INF_XMPP_CONNECTION_CLIENT,
      NULL,
      "jabber.0x539.de",
      INF_XMPP_CONNECTION_SECURITY_BOTH_PREFER_TLS,
      NULL,
      NULL,
      NULL
    );

    g_signal_connect(G_OBJECT(xmpp), "error", G_CALLBACK(error_cb), io);
    g_signal_connect(G_OBJECT(xmpp), "notify::status", G_CALLBACK(notify_status_cb), io);
    inf_standalone_io_loop(io);
  }

  g_object_unref(G_OBJECT(io));
  g_object_unref(G_OBJECT(connection));

  gnutls_global_deinit();
  return 0;
}

/* vim:set et sw=2 ts=2: */
