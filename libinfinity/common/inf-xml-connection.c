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

#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/inf-marshal.h>

enum {
  SENT,
  RECEIVED,
  ERROR,

  LAST_SIGNAL
};

static guint connection_signals[LAST_SIGNAL];

static void
inf_xml_connection_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    connection_signals[SENT] = g_signal_new(
      "sent",
      INF_TYPE_XML_CONNECTION,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfXmlConnectionIface, sent),
      NULL, NULL,
      inf_marshal_VOID__POINTER,
      G_TYPE_NONE,
      1,
      G_TYPE_POINTER
    );

    connection_signals[RECEIVED] = g_signal_new(
      "received",
      INF_TYPE_XML_CONNECTION,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfXmlConnectionIface, received),
      NULL, NULL,
      inf_marshal_VOID__POINTER,
      G_TYPE_NONE,
      1,
      G_TYPE_POINTER
    );

    connection_signals[ERROR] = g_signal_new(
      "error",
      INF_TYPE_XML_CONNECTION,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfXmlConnectionIface, error),
      NULL, NULL,
      inf_marshal_VOID__POINTER,
      G_TYPE_NONE,
      1,
      G_TYPE_POINTER
    );

    g_object_interface_install_property(
      g_class,
      g_param_spec_enum(
        "status",
        "XmlConnection Status",
        "The status of the connection.",
        INF_TYPE_XML_CONNECTION_STATUS,
        INF_XML_CONNECTION_CLOSED,
        G_PARAM_READABLE
      )
    );

    /* The network of a connection should not change through lifetime. All
     * users on a given network should be able to create direct connections
     * between each user. For example, 'jabber' might be such a network,
     * 'local' another one. All jabber user can have connections to other
     * jabber users, but not to those on a local network. */
    g_object_interface_install_property(
      g_class,
      g_param_spec_string(
        "network",
        "Network",
        "An identifier for the type of network this connection is on",
        NULL,
        G_PARAM_READABLE
      )
    );

    g_object_interface_install_property(
      g_class,
      g_param_spec_string(
        "local-id",
        "Local ID",
        "A unique identification on the network for the local site",
        NULL,
        G_PARAM_READABLE
      )
    );

    g_object_interface_install_property(
      g_class,
      g_param_spec_string(
        "remote-id",
        "Remote ID",
        "A unique identification on the network for the remote site",
        NULL,
        G_PARAM_READABLE
      )
    );

    initialized = TRUE;
  }
}

GType
inf_xml_connection_status_get_type(void)
{
  static GType connection_status_type = 0;

  if(!connection_status_type)
  {
    static const GEnumValue connection_status_values[] = {
      {
        INF_XML_CONNECTION_CLOSED,
        "INF_XML_CONNECTION_CLOSED",
        "closed"
      }, {
        INF_XML_CONNECTION_CLOSING,
        "INF_XML_CONNECTION_CLOSING",
        "closing"
      }, {
        INF_XML_CONNECTION_OPEN,
        "INF_XML_CONNECTION_OPEN",
        "open"
      }, {
        INF_XML_CONNECTION_OPENING,
        "INF_XML_CONNECTION_OPENING",
        "opening"
      }
    };

    connection_status_type = g_enum_register_static(
      "InfXmlConnectionStatus",
      connection_status_values
    );
  }

  return connection_status_type;
}

GType
inf_xml_connection_get_type(void)
{
  static GType xml_connection_type = 0;

  if(!xml_connection_type)
  {
    static const GTypeInfo xml_connection_info = {
      sizeof(InfXmlConnectionIface),     /* class_size */
      inf_xml_connection_base_init,      /* base_init */
      NULL,                              /* base_finalize */
      NULL,                              /* class_init */
      NULL,                              /* class_finalize */
      NULL,                              /* class_data */
      0,                                 /* instance_size */
      0,                                 /* n_preallocs */
      NULL,                              /* instance_init */
      NULL                               /* value_table */
    };

    xml_connection_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfXmlConnection",
      &xml_connection_info,
      0
    );

    g_type_interface_add_prerequisite(xml_connection_type, G_TYPE_OBJECT);
  }

  return xml_connection_type;
}

/**
 * inf_xml_connection_close:
 * @connection: A #InfXmlConnection.
 *
 * Closes the given connection.
 **/
void
inf_xml_connection_close(InfXmlConnection* connection)
{
  InfXmlConnectionIface* iface;

  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  iface = INF_XML_CONNECTION_GET_IFACE(connection);
  g_return_if_fail(iface->close != NULL);

  iface->close(connection);
}

/**
 * inf_xml_connection_send:
 * @connection: A #InfXmlConnection.
 * @xml: A XML message to send. The function takes ownership of the XML node.
 *
 * Sends the given XML message to the remote host.
 **/
void inf_xml_connection_send(InfXmlConnection* connection,
                             xmlNodePtr xml)
{
  InfXmlConnectionIface* iface;

  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  iface = INF_XML_CONNECTION_GET_IFACE(connection);
  g_return_if_fail(iface->send != NULL);

  iface->send(connection, xml);
}

/**
 * inf_xml_connection_sent:
 * @connection: A #InfXmlConnection.
 * @xml: The XML message that has been sent.
 *
 * Emits the "sent" signal on @connection. This will most likely only be
 * useful to implementors.
 **/
void inf_xml_connection_sent(InfXmlConnection* connection,
                             const xmlNodePtr xml)
{
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  g_signal_emit(
    G_OBJECT(connection),
    connection_signals[SENT],
    0,
    xml
  );
}

/**
 * inf_xml_connection_received:
 * @connection: A #InfXmlConnection.
 * @xml: The XML message that has been received.
 *
 * Emits the "received" signal on @connection. This will most likely only
 * be useful to implementors.
 **/
void inf_xml_connection_received(InfXmlConnection* connection,
                                 const xmlNodePtr xml)
{
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  g_signal_emit(
    G_OBJECT(connection),
    connection_signals[RECEIVED],
    0,
    xml
  );
}

/**
 * inf_xml_connection_error:
 * @connection: A #InfXmlConnection.
 * @error: The error that occured.
 *
 * Emits the "error" signal on @connection. This will most likely only
 * be useful to implementors.
 *
 * Note that the error may or may not be fatal for the connection. If it
 * is fatal, then a status notify to %INF_XML_CONNECTION_CLOSING or
 * %INF_XML_CONNECTION_CLOSED will follow. If you are implementing a custom
 * class implementing #InfXmlConnection, make sure to always emit the "error"
 * signal before doing the status notify because many users of the connection
 * will release their reference when the connection is no longer connected.
 **/
void
inf_xml_connection_error(InfXmlConnection* connection,
                         const GError* error)
{
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(error != NULL);

  g_signal_emit(
    G_OBJECT(connection),
    connection_signals[ERROR],
    0,
    error
  );
}

/* vim:set et sw=2 ts=2: */
