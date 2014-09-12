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

#include <libinfinity/server/infd-xml-server.h>
#include <libinfinity/inf-define-enum.h>

static const GEnumValue infd_xml_server_status_values[] = {
  {
    INFD_XML_SERVER_CLOSED,
    "INFD_XML_SERVER_CLOSED",
    "closed"
  }, {
    INFD_XML_SERVER_CLOSING,
    "INFD_XML_SERVER_CLOSING",
    "closing"
  }, {
    INFD_XML_SERVER_OPEN,
    "INFD_XML_SERVER_OPEN",
    "open"
  }, {
    INFD_XML_SERVER_OPENING,
    "INFD_XML_SERVER_OPENING",
    "opening"
  }, {
    0,
    NULL,
    NULL
  }
};

INF_DEFINE_ENUM_TYPE(InfdXmlServerStatus, infd_xml_server_status, infd_xml_server_status_values)
G_DEFINE_INTERFACE(InfdXmlServer, infd_xml_server, G_TYPE_OBJECT)

enum {
  NEW_CONNECTION,

  LAST_SIGNAL
};

static guint server_signals[LAST_SIGNAL];

static void
infd_xml_server_default_init(InfdXmlServerInterface* iface)
{
  server_signals[NEW_CONNECTION] = g_signal_new(
    "new-connection",
    INFD_TYPE_XML_SERVER,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdXmlServerInterface, new_connection),
    NULL, NULL,
    g_cclosure_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XML_CONNECTION
  );

  g_object_interface_install_property(
    iface,
    g_param_spec_enum(
      "status",
      "XmlServer Status",
      "The status of the server",
      INFD_TYPE_XML_SERVER_STATUS,
      INFD_XML_SERVER_CLOSED,
      G_PARAM_READABLE
    )
  );
}

/**
 * infd_xml_server_close:
 * @server: A #InfdXmlServer.
 *
 * Closes @server.
 **/
void
infd_xml_server_close(InfdXmlServer* server)
{
  InfdXmlServerInterface* iface;

  g_return_if_fail(INFD_IS_XML_SERVER(server));

  iface = INFD_XML_SERVER_GET_IFACE(server);
  g_return_if_fail(iface->close);

  iface->close(server);
}

/**
 * infd_xml_server_new_connection:
 * @server: A #InfdXmlServer.
 * @connection: A #InfXmlConnection.
 *
 * Emits the "new-connection" signal on @server.
 **/
void
infd_xml_server_new_connection(InfdXmlServer* server,
                               InfXmlConnection* connection)
{
  g_return_if_fail(INFD_IS_XML_SERVER(server));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_signal_emit(
    G_OBJECT(server),
    server_signals[NEW_CONNECTION],
    0,
    connection
  );
}

/* vim:set et sw=2 ts=2: */
