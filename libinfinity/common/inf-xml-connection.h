/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_XML_CONNECTION_H__
#define __INF_XML_CONNECTION_H__

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_XML_CONNECTION                 (inf_xml_connection_get_type())
#define INF_XML_CONNECTION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_XML_CONNECTION, InfXmlConnection))
#define INF_IS_XML_CONNECTION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_XML_CONNECTION))
#define INF_XML_CONNECTION_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_XML_CONNECTION, InfXmlConnectionIface))

#define INF_TYPE_XML_CONNECTION_STATUS          (inf_xml_connection_status_get_type())

/**
 * InfXmlConnection:
 *
 * #InfXmlConnection is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfXmlConnection InfXmlConnection;
typedef struct _InfXmlConnectionIface InfXmlConnectionIface;

/**
 * InfXmlConnectionStatus:
 * @INF_XML_CONNECTION_CLOSED: The connection is currently not established.
 * @INF_XML_CONNECTION_CLOSING: The connection is in the process of being
 * closed, no more data can be sent.
 * @INF_XML_CONNECTION_OPEN: The connection is up and data can be transmitted.
 * @INF_XML_CONNECTION_OPENING: The connection is being established.
 *
 * The possible connection status of an #InfXmlConnection.
 */
typedef enum _InfXmlConnectionStatus {
  INF_XML_CONNECTION_CLOSED,
  INF_XML_CONNECTION_CLOSING,
  INF_XML_CONNECTION_OPEN,
  INF_XML_CONNECTION_OPENING
} InfXmlConnectionStatus;

/**
 * InfXmlConnectionIface:
 * @open: Virtual function to start the connection.
 * @close: Virtual function to stop the connection.
 * @send: Virtual function to transmit data over the connection.
 * @sent: Default signal handler of the #InfXmlConnection::sent signal.
 * @received: Default signal handler of the #InfXmlConnection::received
 * signal.
 * @error: Default signal handler of the #InfXmlConnection::error signal.
 *
 * Virtual functions and default signal handlers for the #InfXmlConnection
 * interface.
 */
struct _InfXmlConnectionIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/

  /* Virtual table */
  gboolean (*open)(InfXmlConnection* connection,
                   GError** error);
  void (*close)(InfXmlConnection* connection);
  void (*send)(InfXmlConnection* connection,
               xmlNodePtr xml);

  /* Signals */
  void (*sent)(InfXmlConnection* connection,
               const xmlNodePtr xml);
  void (*received)(InfXmlConnection* connection,
                   const xmlNodePtr xml);
  void (*error)(InfXmlConnection* connection,
                const GError* error);
};

GType
inf_xml_connection_status_get_type(void) G_GNUC_CONST;

GType
inf_xml_connection_get_type(void) G_GNUC_CONST;

gboolean
inf_xml_connection_open(InfXmlConnection* connection,
                        GError** error);

void
inf_xml_connection_close(InfXmlConnection* connection);

void
inf_xml_connection_send(InfXmlConnection* connection,
                        xmlNodePtr xml);

void
inf_xml_connection_sent(InfXmlConnection* connection,
                        const xmlNodePtr xml);

void
inf_xml_connection_received(InfXmlConnection* connection,
                            const xmlNodePtr xml);

void
inf_xml_connection_error(InfXmlConnection* connection,
                         const GError* error);

G_END_DECLS

#endif /* __INF_XML_CONNECTION_H__ */

/* vim:set et sw=2 ts=2: */
