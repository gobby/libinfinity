/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_CONNECTION_MANAGER_H__
#define __INF_CONNECTION_MANAGER_H__

#include <libinfinity/common/inf-net-object.h>
#include <libinfinity/common/inf-xml-connection.h>

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_CONNECTION_MANAGER                 (inf_connection_manager_get_type())
#define INF_CONNECTION_MANAGER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_CONNECTION_MANAGER, InfConnectionManager))
#define INF_CONNECTION_MANAGER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_CONNECTION_MANAGER, InfConnectionManagerClass))
#define INF_IS_CONNECTION_MANAGER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_CONNECTION_MANAGER))
#define INF_IS_CONNECTION_MANAGER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_CONNECTION_MANAGER))
#define INF_CONNECTION_MANAGER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_CONNECTION_MANAGER, InfConnectionManagerClass))

#define INF_TYPE_CONNECTION_MANAGER_GROUP           (inf_connection_manager_group_get_type())

typedef struct _InfConnectionManager InfConnectionManager;
typedef struct _InfConnectionManagerClass InfConnectionManagerClass;

typedef struct _InfConnectionManagerGroup InfConnectionManagerGroup;

typedef struct _InfConnectionManagerMethod InfConnectionManagerMethod;
typedef struct _InfConnectionManagerMethodDesc InfConnectionManagerMethodDesc;

/**
 * InfConnectionManagerScope:
 * @INF_CONNECTION_MANAGER_POINT_TO_POINT: The message has only a single
 * recipient.
 * @INF_CONNECTION_MANAGER_NETWORK: The message is sent to all group members
 * that are on the same network.
 * @INF_CONNECTION_MANAGER_GROUP: The message is sent to all group members.
 *
 * #InfConnectionManagerScope defines the scope of a message sent through the
 * network.
 */
typedef enum _InfConnectionManagerScope {
  INF_CONNECTION_MANAGER_POINT_TO_POINT,
  INF_CONNECTION_MANAGER_NETWORK,
  INF_CONNECTION_MANAGER_GROUP
} InfConnectionManagerScope;

/**
 * InfConnectionManagerMethodDesc:
 * @network: The name of the network the method operates on, such as "local"
 * or "jabber". Note that "local" is used for direct TCP connections which
 * might as well be used in a WAN, so that name is slightly misleading.
 * @name: The name of the method. This should be unique for all methods on
 * @network as it is used to identify the method.
 * @open: This is called to create a new #InfConnectionManagerMethod that
 * handles all connections in @group for the method's network. This is called
 * when the local host has created a new group.
 * @join: This is also called to create a new #InfConnectionManagerMethod
 * to handle all connections in @group for the method's network, but it is
 * called when a group opened by another host is joined. @publisher_conn is
 * an open connection to the host publishing the group.
 * @finalize: This is called when the local host leaves the group. It is
 * supposed to do any necessary cleanup.
 * @receive_msg: This is called for every message received by a registered
 * connection. If @can_forward is %TRUE, and the scope of the message is
 * %INF_CONNECTION_MANAGER_GROUP, then this method should make sure that
 * everyone in the group receives the message, relaying the message to other
 * hosts if necessary.
 * @receive_ctrl: This is called for every control message received by a
 * registered connection. Control messages can be used internally by the
 * method.
 * @add_connection: Called when a new connection on method's network
 * is added to the group. This is only called if the local host is publisher,
 * which means the method has been created via @open.
 * @remove_connection: Called when a connection from method's network is
 * removed from the group. This is only called if the local host is publisher,
 * or the connection to the publisher is no longer available.
 * @send_to_net: Send a message to all connections on the network.
 *
 * This interface needs to be implemented by connection manager methods. A
 * connection manager method defines how messages are sent to other group
 * members in the same network.
 */
struct _InfConnectionManagerMethodDesc {
  const gchar* network;
  const gchar* name;

  InfConnectionManagerMethod*(*open)(const InfConnectionManagerMethodDesc* dc,
                                     InfConnectionManagerGroup* group);
  InfConnectionManagerMethod*(*join)(const InfConnectionManagerMethodDesc* dc,
                                     InfConnectionManagerGroup* group,
                                     InfXmlConnection* publisher_conn);

  void (*finalize)(InfConnectionManagerMethod* instance);

  /* can_forward specifies whether we can forward this message to other group
   * members. Normally, messages that should only be processed by the
   * recipient should not be forwarded, even if the sender requests this
   * explicitely via scope="group". I'm still wondering whether there are
   * methods that would need to know can_forward. */
  void (*receive_msg)(InfConnectionManagerMethod* instance,
                      InfConnectionManagerScope scope,
                      gboolean can_forward,
                      InfXmlConnection* connection,
                      xmlNodePtr xml);
  void (*receive_ctrl)(InfConnectionManagerMethod* instance,
                       InfXmlConnection* connection,
                       xmlNodePtr xml);

  void (*add_connection)(InfConnectionManagerMethod* instance,
                         InfXmlConnection* connection);
  void (*remove_connection)(InfConnectionManagerMethod* instance,
                            InfXmlConnection* connection);

  /* TODO: Remove this and just rely on registered connections. */
  /*
  gboolean (*has_connection)(InfConnectionManagerMethod* instance,
                             InfXmlConnection* connection);
  InfXmlConnection* (*lookup_connection)(InfConnectionManagerMethod* instance,
                                         const gchar* id);
  */

  void (*send_to_net)(InfConnectionManagerMethod* instance,
                      InfXmlConnection* except,
                      xmlNodePtr xml);
};

/**
 * InfConnectionManagerMethod:
 *
 * This is an opaque data structure representing an instantiation of a
 * connection manager method, see #InfConnectionManagerMethodDesc.
 */

/**
 * InfConnectionManagerClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfConnectionManagerClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfConnectionManager:
 *
 * #InfConnectionManager is an opaque data type. You should only access it
 * via the public API functions.
 */
struct _InfConnectionManager {
  /*< private >*/
  GObject parent;
};

GType
inf_connection_manager_group_get_type(void) G_GNUC_CONST;

GType
inf_connection_manager_get_type(void) G_GNUC_CONST;

InfConnectionManager*
inf_connection_manager_new(void);

InfConnectionManagerGroup*
inf_connection_manager_open_group(InfConnectionManager* manager,
                                  const gchar* group_name,
                                  InfNetObject* net_object,
                                  const InfConnectionManagerMethodDesc* const*
                                          methods);

InfConnectionManagerGroup*
inf_connection_manager_join_group(InfConnectionManager* manager,
                                  const gchar* group_name,
                                  InfXmlConnection* publisher_conn,
                                  InfNetObject* object,
                                  const InfConnectionManagerMethodDesc* meth);

InfConnectionManagerGroup*
inf_connection_manager_lookup_group(InfConnectionManager* manager,
                                    const gchar* group_name,
                                    InfXmlConnection* publisher);

InfConnectionManagerGroup*
inf_connection_manager_lookup_group_by_id(InfConnectionManager* manager,
                                          const gchar* group_name,
                                          const gchar* publisher_id);

void
inf_connection_manager_group_ref(InfConnectionManagerGroup* group);

void
inf_connection_manager_group_unref(InfConnectionManagerGroup* group);

const InfConnectionManagerMethodDesc*
inf_connection_manager_group_get_method_for_network(InfConnectionManagerGroup* g,
                                                    const gchar* network);

void
inf_connection_manager_group_set_object(InfConnectionManagerGroup* group,
                                        InfNetObject* object);

#if 0
InfXmlConnection*
inf_connection_manager_group_get_publisher(InfConnectionManagerGroup* group);

const gchar*
inf_connection_manager_group_get_publisher_id(InfConnectionManagerGroup* grp);
#endif

gboolean
inf_connection_manager_group_has_connection(InfConnectionManagerGroup* group,
                                            InfXmlConnection* conn);

const gchar*
inf_connection_manager_group_get_name(InfConnectionManagerGroup* group);

gboolean
inf_connection_manager_group_add_connection(InfConnectionManagerGroup* group,
                                            InfXmlConnection* conn,
                                            InfConnectionManagerGroup* prnt);

void
inf_connection_manager_group_remove_connection(InfConnectionManagerGroup* grp,
                                               InfXmlConnection* conn);

#if 0
InfXmlConnection*
inf_connection_manager_group_lookup_connection(InfConnectionManagerGroup* grp,
                                               const gchar* network,
                                               const gchar* id);
#endif

void
inf_connection_manager_group_send_to_connection(InfConnectionManagerGroup* g,
                                                InfXmlConnection* connection,
                                                xmlNodePtr xml);

void
inf_connection_manager_group_send_to_group(InfConnectionManagerGroup* group,
                                           InfXmlConnection* except,
                                           xmlNodePtr xml);

void
inf_connection_manager_group_clear_queue(InfConnectionManagerGroup* group,
                                         InfXmlConnection* connection);

/* semi-private: */
void
inf_connection_manager_register_connection(InfConnectionManagerGroup* group,
                                           InfXmlConnection* connection,
                                           InfConnectionManagerGroup* parent);

void
inf_connection_manager_unregister_connection(InfConnectionManagerGroup* group,
                                             InfXmlConnection* connection);

void
inf_connection_manager_send_msg(InfConnectionManagerGroup* group,
                                InfXmlConnection* connection,
                                InfConnectionManagerScope scope,
                                xmlNodePtr xml);

void
inf_connection_manager_send_ctrl(InfConnectionManagerGroup* group,
                                 InfXmlConnection* connection,
                                 xmlNodePtr xml);

G_END_DECLS

#endif /* __INF_CONNECTION_MANAGER_H__ */

/* vim:set et sw=2 ts=2: */
