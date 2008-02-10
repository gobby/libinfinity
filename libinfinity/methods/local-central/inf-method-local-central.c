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

#include <libinfinity/common/inf-connection-manager.h>

#include <string.h>

struct _InfConnectionManagerMethod {
  InfConnectionManagerGroup* group;
  GSList* connections;
};

static InfConnectionManagerMethod*
inf_method_local_central_open(const InfConnectionManagerMethodDesc* dc,
                              InfConnectionManagerGroup* group)
{
  InfConnectionManagerMethod* method;
  method = g_slice_new(InfConnectionManagerMethod);

  method->group = group;
  method->connections = NULL;

  return method;
}

static InfConnectionManagerMethod*
inf_method_local_central_join(const InfConnectionManagerMethodDesc* dc,
                              InfConnectionManagerGroup* group,
                              InfXmlConnection* publisher_conn)
{
  InfConnectionManagerMethod* method;
  method = g_slice_new(InfConnectionManagerMethod);

  method->group = group;
  method->connections = g_slist_prepend(NULL, publisher_conn);
  inf_connection_manager_register_connection(group, publisher_conn);
  g_object_ref(publisher_conn);

  return method;
}

static void
inf_method_local_central_finalize(InfConnectionManagerMethod* instance)
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

static void
inf_method_local_central_receive_msg(InfConnectionManagerMethod* instance,
                                     InfConnectionManagerScope scope,
                                     gboolean can_forward,
                                     InfXmlConnection* connection,
                                     xmlNodePtr xml)
{
  GSList* item;

  /* Forward group messages to group if we are publisher */
  if(scope == INF_CONNECTION_MANAGER_GROUP && can_forward == TRUE &&
     inf_connection_manager_group_get_publisher_id(instance->group) == NULL)
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

static void
inf_method_local_central_receive_ctrl(InfConnectionManagerMethod* instance,
                                      InfXmlConnection* connection,
                                      xmlNodePtr xml)
{
  /* Ignore ctrl messages */
}

static void
inf_method_local_central_add_connection(InfConnectionManagerMethod* instance,
                                        InfXmlConnection* connection)
{
  instance->connections = g_slist_prepend(instance->connections, connection);
  inf_connection_manager_register_connection(instance->group, connection);
  g_object_ref(connection);
}

static void
inf_method_local_central_remove_connection(InfConnectionManagerMethod* inst,
                                           InfXmlConnection* connection)
{
  inst->connections = g_slist_remove(inst->connections, connection);
  inf_connection_manager_unregister_connection(inst->group, connection);
  g_object_unref(connection);
}

static gboolean
inf_method_local_central_has_connection(InfConnectionManagerMethod* instance,
                                        InfXmlConnection* connection)
{
  GSList* item;
  for(item = instance->connections; item != NULL; item = g_slist_next(item))
    if(item->data == connection)
      return TRUE;

  return FALSE;
}

static InfXmlConnection*
inf_method_local_central_lookup_connection(InfConnectionManagerMethod* inst,
                                           const gchar* id)
{
  GSList* item;
  InfXmlConnection* connection;
  gchar* remote_id;

  for(item = inst->connections; item != NULL; item = g_slist_next(item))
  {
    connection = INF_XML_CONNECTION(item->data);
    g_object_get(G_OBJECT(connection), "remote-id", &remote_id, NULL);
    if(strcmp(id, remote_id) == 0)
    {
      g_free(remote_id);
      return connection;
    }

    g_free(remote_id);
  }

  return NULL;
}

static void
inf_method_local_central_send_to_net(InfConnectionManagerMethod* instance,
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

const InfConnectionManagerMethodDesc INF_METHOD_PLUGIN = {
  "local",
  "central",
  inf_method_local_central_open,
  inf_method_local_central_join,
  inf_method_local_central_finalize,
  inf_method_local_central_receive_msg,
  inf_method_local_central_receive_ctrl,
  inf_method_local_central_add_connection,
  inf_method_local_central_remove_connection,
  inf_method_local_central_has_connection,
  inf_method_local_central_lookup_connection,
  inf_method_local_central_send_to_net
};

