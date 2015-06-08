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

/**
 * SECTION:inf-xml-connection
 * @title: InfXmlConnection
 * @short_description: Exchange of XML messages
 * @include: libinfinity/common/inf-xml-connection.h
 * @see_also: #InfXmppConnection
 * @stability: Unstable
 *
 * #InfXmlConnection provides a generic interface for sending an receiving
 * messages in the form of XML nodes. The rest of the libinfinity library
 * works with #InfXmlConnection<!-- -->s to transfer data between nodes.
 * Therefore, simply implementing this interface allows to use the core
 * functionality of the library with any kind of network or transport.
 *
 * Apart from the virtual functions, implementations also need to provide the
 * #InfXmlConnection:remote-id and #InfXmlConnection:local-id properties.
 * These properties represent string identifiers that are unique to the
 * particular hosts in the network, such as IP addresses for IP connections.
 * If the connection is supposed to be used with other communication methods
 * (see #InfCommunicationMethod) than the &quot;central&quot; one, these
 * IDs must be unique and every host must see the same ID for the other hosts
 * in the network. This is no longer fulfilled by simple IP addresses, but for
 * example for JIDs when sending XML messages over a jabber server.
 */

#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-certificate-chain.h>
#include <libinfinity/inf-define-enum.h>

static const GEnumValue inf_xml_connection_status_values[] = {
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
  }, {
    0,
    NULL,
    NULL
  }
};

INF_DEFINE_ENUM_TYPE(InfXmlConnectionStatus, inf_xml_connection_status, inf_xml_connection_status_values)
G_DEFINE_INTERFACE(InfXmlConnection, inf_xml_connection, G_TYPE_OBJECT)

enum {
  SENT,
  RECEIVED,
  ERROR,

  LAST_SIGNAL
};

static guint connection_signals[LAST_SIGNAL];

static void
inf_xml_connection_default_init(InfXmlConnectionInterface* iface)
{
  /**
   * InfXmlConnection::sent:
   * @connection: The #InfXmlConnection through which @node has been sent 
   * @node: An #xmlNodePtr refering to the XML node that has been sent
   *
   * Signal which is emitted when an XML node has been successfully
   * transmitted with this connection.
   */
  connection_signals[SENT] = g_signal_new(
    "sent",
    INF_TYPE_XML_CONNECTION,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfXmlConnectionInterface, sent),
    NULL, NULL,
    g_cclosure_marshal_VOID__POINTER,
    G_TYPE_NONE,
    1,
    G_TYPE_POINTER
  );

  /**
   * InfXmlConnection::received:
   * @connection: The #InfXmlConnection through which @node has been received
   * @node: An #xmlNodePtr refering to the XML node that has been received
   *
   * Signal which is emitted when an XML node has been received by this
   * connection.
   */
  connection_signals[RECEIVED] = g_signal_new(
    "received",
    INF_TYPE_XML_CONNECTION,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfXmlConnectionInterface, received),
    NULL, NULL,
    g_cclosure_marshal_VOID__POINTER,
    G_TYPE_NONE,
    1,
    G_TYPE_POINTER
  );

  /**
   * InfXmlConnection::error:
   * @connection: The erroneous #InfXmlConnection
   * @error: A pointer to a #GError object with details on the error
   *
   * This signal is emitted when an error occurs for this connection.
   * For example, if the connection cannot be established and the status
   * changes from %INF_XML_CONNECTION_OPENING to %INF_XML_CONNECTION_CLOSED,
   * then this signal is usually emitted with more details on the error.
   *
   * Note however that the error may or may not be fatal for the
   * connection. If it is fatal, then a status notify to
   * %INF_XML_CONNECTION_CLOSING or %INF_XML_CONNECTION_CLOSED will follow.
   */
  connection_signals[ERROR] = g_signal_new(
    "error",
    INF_TYPE_XML_CONNECTION,
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfXmlConnectionInterface, error),
    NULL, NULL,
    g_cclosure_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    G_TYPE_ERROR
  );

  g_object_interface_install_property(
    iface,
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
    iface,
    g_param_spec_string(
      "network",
      "Network",
      "An identifier for the type of network this connection is on",
      NULL,
      G_PARAM_READABLE
    )
  );

  g_object_interface_install_property(
    iface,
    g_param_spec_string(
      "local-id",
      "Local ID",
      "A unique identification on the network for the local site",
      NULL,
      G_PARAM_READABLE
    )
  );

  g_object_interface_install_property(
    iface,
    g_param_spec_string(
      "remote-id",
      "Remote ID",
      "A unique identification on the network for the remote site",
      NULL,
      G_PARAM_READABLE
    )
  );

  g_object_interface_install_property(
    iface,
    g_param_spec_pointer(
      "local-certificate",
      "Local Certificate",
      "The X.509 certificate (gnutls_x509_crt_t) of the local site",
      G_PARAM_READABLE
    )
  );

  g_object_interface_install_property(
    iface,
    g_param_spec_boxed(
      "remote-certificate",
      "Remote Certificate",
      "The X.509 certificate of the remote site",
      INF_TYPE_CERTIFICATE_CHAIN,
      G_PARAM_READABLE
    )
  );
}

/**
 * inf_xml_connection_open:
 * @connection: A #infXmlConnection.
 * @error: Location to store error information, if any.
 *
 * Attempts to open the given XML connection. If the process fails, @error
 * will be set. The connection needs to be in status
 * %INF_XML_CONNECTION_CLOSED for this function to be called. Even if this
 * function succeeds, the connection process can fail later. In that case
 * the status of @connection will be reset to %INF_XML_CONNECTION_CLOSED
 * and the #InfXmlConnection::error signal will be emitted.
 *
 * Returns: %TRUE on succes, or %FALSE on error.
 */
gboolean
inf_xml_connection_open(InfXmlConnection* connection,
                        GError** error)
{
  InfXmlConnectionInterface* iface;

  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  iface = INF_XML_CONNECTION_GET_IFACE(connection);
  g_return_val_if_fail(iface->open != NULL, FALSE);

  return iface->open(connection, error);
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
  InfXmlConnectionInterface* iface;

  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  iface = INF_XML_CONNECTION_GET_IFACE(connection);
  g_return_if_fail(iface->close != NULL);

  iface->close(connection);
}

/**
 * inf_xml_connection_send:
 * @connection: A #InfXmlConnection.
 * @xml: (transfer full): A XML message to send. The function takes ownership
 * of the XML node.
 *
 * Sends the given XML message to the remote host.
 **/
void inf_xml_connection_send(InfXmlConnection* connection,
                             xmlNodePtr xml)
{
  InfXmlConnectionInterface* iface;

  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(xml != NULL);

  iface = INF_XML_CONNECTION_GET_IFACE(connection);
  g_return_if_fail(iface->send != NULL);

  iface->send(connection, xml);
}

/**
 * inf_xml_connection_sent:
 * @connection: A #InfXmlConnection.
 * @xml: (transfer none): The XML message that has been sent.
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
 * @xml: (transfer none): The XML message that has been received.
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
