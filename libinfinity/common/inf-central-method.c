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

/**
 * SECTION:inf-central-method
 * @title: Central method implementation
 * @short_description: Implementation of a central connection manager method
 * @include: libinfinity/common/inf-central-method.h
 * @stability: Unstable
 * @see_also: #InfConnectionManager
 *
 * These functions implement a generic #InfConnectionManager method that is
 * independant of the underlying network. It uses a "central" networking
 * approach meaning all messages are relayed by the group's publisher. If the
 * connection to the publisher is lost, then the group members can no longer
 * communicate with another.
 *
 * Note that normally you do not need to call these functions. This is done
 * by the #InfConnectionManager.
 *
 * To use this implementation for a given network "foo", use code similar to
 * the following:
 *
 * |[
 * const InfConnectionManagerMethodDesc METHOD = {
 *   "foo",
 *   "central",
 *   inf_central_method_open,
 *   inf_central_method_join,
 *   inf_central_method_finalize,
 *   inf_central_method_receive_msg,
 *   inf_central_method_receive_ctrl,
 *   inf_central_method_add_connection,
 *   inf_central_method_remove_connection,
 *   inf_central_method_send_to_net
 * };
 * ]|
 */

#include <libinfinity/common/inf-central-method.h>

struct _InfConnectionManagerMethod {
  InfConnectionManagerGroup* group;
  GSList* connections;
  gboolean publisher; /* Whether the local host is publisher of the group */
};

/**
 * inf_central_method_open:
 * @dc: The #InfConnectionManagerMethodDesc the #InfConnectionManagerMethod is
 * instantiated for.
 * @group: The #InfConnectionManagerGroup that is opened.
 *
 * This is called by the #InfConnectionManager every time a group for which
 * the network is handled by @dc is opened. It returns a new
 * #InfConnectionManagerMethod, representing the group for this method.
 *
 * Returns: A new #InfConnectionManagerMethod, to be freed with
 * inf_central_method_finalize().
 */
InfConnectionManagerMethod*
inf_central_method_open(const InfConnectionManagerMethodDesc* dc,
                        InfConnectionManagerGroup* group)
{
  InfConnectionManagerMethod* method;
  method = g_slice_new(InfConnectionManagerMethod);

  method->group = group;
  method->connections = NULL;
  method->publisher = TRUE;

  return method;
}

/**
 * inf_central_method_join:
 * @dc: The #InfConnectionManagerMethodDesc the #InfConnectionManagerMethod is
 * instantiated for.
 * @group: The #InfConnectionManagerGroup that is opened.
 * @publisher_conn: A connection to the group's publisher.
 *
 * This is called by the #InfConnectionManager every time a group for which
 * the network is handled by @dc is joined. It returns a new
 * #InfConnectionManagerMethod, representing the group for this method.
 *
 * Returns: A new #InfConnectionManagerMethod, to be freed with
 * inf_central_method_finalize().
 */
InfConnectionManagerMethod*
inf_central_method_join(const InfConnectionManagerMethodDesc* dc,
                        InfConnectionManagerGroup* group,
                        InfXmlConnection* publisher_conn)
{
  InfConnectionManagerMethod* method;
  method = g_slice_new(InfConnectionManagerMethod);

  method->group = group;
  method->connections = g_slist_prepend(NULL, publisher_conn);
  method->publisher = FALSE;
  inf_connection_manager_register_connection(group, publisher_conn, NULL);
  g_object_ref(publisher_conn);

  return method;
}

/**
 * inf_central_method_finalize:
 * @instance: A #InfConnectionManagerMethod.
 *
 * Frees the resources allocated by a #InfConnectionManagerMethod.
 */
void
inf_central_method_finalize(InfConnectionManagerMethod* instance)
{
  GSList* item;
  for(item = instance->connections; item != NULL; item = g_slist_next(item))
  {
    inf_connection_manager_unregister_connection(
      instance->group,
      INF_XML_CONNECTION(item->data)
    );

    g_object_unref(item->data);
  }

  g_slist_free(instance->connections);
  g_slice_free(InfConnectionManagerMethod, instance);
}

/**
 * inf_central_method_receive_msg:
 * @instance: A #InfConnectionManagerMethod.
 * @scope: The scope of the received message.
 * @can_forward: Whether the message is allowed to be forwarded to other group
 * members.
 * @connection: The connection the message comes from.
 * @xml: The XML message data.
 *
 * This is called every time by the connection manager when a message from
 * @connection has been received. The function forwards the message to the
 * other group members if the local host is the group's publisher, if
 * @scope is %INF_CONNECTION_MANAGER_GROUP and if @can_forward is %TRUE.
 */
void
inf_central_method_receive_msg(InfConnectionManagerMethod* instance,
                               InfConnectionManagerScope scope,
                               gboolean can_forward,
                               InfXmlConnection* connection,
                               xmlNodePtr xml)
{
  GSList* item;

  /* Forward group messages to group if we are publisher */
  if(scope == INF_CONNECTION_MANAGER_GROUP && can_forward == TRUE &&
     instance->publisher == TRUE)
  {
    for(item = instance->connections; item != NULL; item = g_slist_next(item))
    {
      if(item->data != connection)
      {
        inf_connection_manager_send_msg(
          instance->group,
          INF_XML_CONNECTION(item->data),
          INF_CONNECTION_MANAGER_GROUP,
          xmlCopyNode(xml, 1)
        );
      }
    }
  }
}

/**
 * inf_central_method_receive_ctrl:
 * @instance: A #InfConnectionManagerMethod.
 * @connection: The connection the message comes from.
 * @xml: The XML message data.
 *
 * This function is called every time a control message is received from
 * @connection. This is a no-op, since control messages are not needed for
 * central communication.
 */
void
inf_central_method_receive_ctrl(InfConnectionManagerMethod* instance,
                                InfXmlConnection* connection,
                                xmlNodePtr xml)
{
  /* Ignore ctrl messages */
}

/**
 * inf_central_method_add_connection:
 * @instance: A #InfConnectionManagerMethod.
 * @connection: The #InfXmlConnection to add.
 *
 * This function is called every time a connection was added to the group.
 */
void
inf_central_method_add_connection(InfConnectionManagerMethod* instance,
                                  InfXmlConnection* connection)
{
  instance->connections = g_slist_prepend(instance->connections, connection);
  /*inf_connection_manager_register_connection(instance->group, connection);*/
  g_object_ref(connection);
}

/**
 * inf_central_method_remove_connection:
 * @inst: A #InfConnectionManagerMethod.
 * @connection: The #InfXmlConnection to add.
 *
 * This function is called every time a connection was removed from the group.
 */
void
inf_central_method_remove_connection(InfConnectionManagerMethod* inst,
                                     InfXmlConnection* connection)
{
  inst->connections = g_slist_remove(inst->connections, connection);
  /*inf_connection_manager_unregister_connection(inst->group, connection);*/
  g_object_unref(connection);
}

/**
 * inf_central_method_send_to_net:
 * @instance: A #InfConnectionManagerMethod.
 * @except: A #InfXmlConnection not to send the message to, or %NULL.
 * @xml: The XML message data.
 *
 * This function is called every time a message is sent to all group members
 * within the method's network. If the local host is not the publisher, then
 * the function sends the message to the publisher only (with
 * %INF_CONNECTION_MANAGER_GROUP scope), otherwise the message is sent to all
 * connections of the group.
 */
void
inf_central_method_send_to_net(InfConnectionManagerMethod* instance,
                               InfXmlConnection* except,
                               xmlNodePtr xml)
{
  GSList* item;
  InfXmlConnection* first;

  first = NULL;
  for(item = instance->connections; item != NULL; item = g_slist_next(item))
  {
    if(item->data == except) continue;
    if(first == NULL) { first = INF_XML_CONNECTION(item->data); continue; }

    inf_connection_manager_send_msg(
      instance->group,
      INF_XML_CONNECTION(item->data),
      INF_CONNECTION_MANAGER_GROUP,
      xmlCopyNode(xml, 1)
    );
  }

  if(first != NULL)
  {
    inf_connection_manager_send_msg(
      instance->group,
      first,
      INF_CONNECTION_MANAGER_GROUP,
      xml
    );
  }
  else
  {
    xmlFreeNode(xml);
  }
}

/* vim:set et sw=2 ts=2: */
