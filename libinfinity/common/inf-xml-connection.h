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

typedef struct _InfXmlConnection InfXmlConnection;
typedef struct _InfXmlConnectionIface InfXmlConnectionIface;

typedef enum _InfXmlConnectionStatus {
  INF_XML_CONNECTION_CLOSED,
  INF_XML_CONNECTION_CLOSING,
  INF_XML_CONNECTION_OPEN,
  INF_XML_CONNECTION_OPENING
} InfXmlConnectionStatus;

struct _InfXmlConnectionIface {
  GTypeInterface parent;

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
