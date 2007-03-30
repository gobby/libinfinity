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

#ifndef __INFD_XML_SERVER_H__
#define __INFD_XML_SERVER_H__

#include <libinfinity/common/inf-xml-connection.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_XML_SERVER                 (infd_xml_server_get_type())
#define INFD_XML_SERVER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_XML_SERVER, InfdXmlServer))
#define INFD_IS_XML_SERVER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_XML_SERVER))
#define INFD_XML_SERVER_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INFD_TYPE_XML_SERVER, InfdXmlServerIface))

#define INFD_TYPE_XML_SERVER_STATUS          (infd_xml_server_status_get_type())

typedef struct _InfdXmlServer InfdXmlServer;
typedef struct _InfdXmlServerIface InfdXmlServerIface;

typedef enum _InfdXmlServerStatus {
  INFD_XML_SERVER_CLOSED,
  INFD_XML_SERVER_CLOSING,
  INFD_XML_SERVER_OPEN,
  INFD_XML_SERVER_OPENING
} InfXmlServerStatus;

struct _InfdXmlServerIface {
  GTypeInterface parent;

  /* Virtual Table */
  void (*close)(InfdXmlServer* server);

  /* Signals */
  void (*new_connection)(InfdXmlServer* server,
                         InfXmlConnection* connection);
};

GType
infd_xml_server_status_get_type(void) G_GNUC_CONST;

GType
infd_xml_server_get_type(void) G_GNUC_CONST;

void
infd_xml_server_close(InfdXmlServer* server);

void
infd_xml_server_new_connection(InfdXmlServer* server,
                               InfXmlConnection* connection);

G_END_DECLS

#endif /* __INFD_XML_SERVER_H__ */
