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

#ifndef __INF_XMPP_MANAGER_H__
#define __INF_XMPP_MANAGER_H__

#include <libinfinity/common/inf-xmpp-connection.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_XMPP_MANAGER                 (inf_xmpp_manager_get_type())
#define INF_XMPP_MANAGER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_XMPP_MANAGER, InfXmppManager))
#define INF_XMPP_MANAGER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_XMPP_MANAGER, InfXmppManagerClass))
#define INF_IS_XMPP_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_XMPP_MANAGER))
#define INF_IS_XMPP_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_XMPP_MANAGER))
#define INF_XMPP_MANAGER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_XMPP_MANAGER, InfXmppManagerClass))

typedef struct _InfXmppManager InfXmppManager;
typedef struct _InfXmppManagerClass InfXmppManagerClass;

struct _InfXmppManagerClass {
  GObjectClass parent_class;

  void(*add_connection)(InfXmppManager* manager,
                        InfXmppConnection* connection);
};

struct _InfXmppManager {
  GObject parent;
};

GType
inf_xmpp_manager_get_type(void) G_GNUC_CONST;

InfXmppManager*
inf_xmpp_manager_new(void);

InfXmppConnection*
inf_xmpp_manager_lookup_connection_by_address(InfXmppManager* manager,
                                              InfIpAddress* address,
                                              guint port);

gboolean
inf_xmpp_manager_contains_connection(InfXmppManager* manager,
                                     InfXmppConnection* connection);

void
inf_xmpp_manager_add_connection(InfXmppManager* manager,
                                InfXmppConnection* connection);

G_END_DECLS

#endif /* __INF_XMPP_MANAGER_H__ */

/* vim:set et sw=2 ts=2: */
