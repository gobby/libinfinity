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

/**
 * SECTION:inf-communication-manager
 * @title: InfCommunicationManager
 * @short_description: Handling multiple communication sessions
 * @include: libinfinity/communication/inf-communication-manager.h
 * @stability: Unstable
 *
 * #InfCommunicationManager manages multiple communication sessions
 * represented by #InfCommunicationGroup. A #InfCommunicationGroup provides an
 * easy way to send messages between group members, possibly sharing
 * connections with other groups handled by the same #InfCommunicationManager.
 **/

#include <libinfinity/communication/inf-communication-manager.h>
#include <libinfinity/communication/inf-communication-central-factory.h>

#include <string.h>

typedef struct _InfCommunicationManagerJoinedKey
  InfCommunicationManagerJoinedKey;

struct _InfCommunicationManagerJoinedKey {
  /* We can uniquely identify joined groups by network,
   * publisher ID and group name. */
  gchar* network;
  gchar* publisher_id;
  const gchar* group_name;
};

typedef struct _InfCommunicationManagerPrivate InfCommunicationManagerPrivate;
struct _InfCommunicationManagerPrivate {
  InfCommunicationRegistry* registry;
  GPtrArray* factories;

  GHashTable* hosted_groups;
  GHashTable* joined_groups;
};

#define INF_COMMUNICATION_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_COMMUNICATION_TYPE_MANAGER, InfCommunicationManagerPrivate))

G_DEFINE_TYPE_WITH_CODE(InfCommunicationManager, inf_communication_manager, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfCommunicationManager))

static void
inf_communication_manager_joined_key_free(gpointer key_)
{
  InfCommunicationManagerJoinedKey* key;
  key = (InfCommunicationManagerJoinedKey*)key_;

  g_free(key->network);
  g_free(key->publisher_id);
  g_slice_free(InfCommunicationManagerJoinedKey, key);
}

static int
inf_communication_manager_joined_key_cmp(gconstpointer first,
                                         gconstpointer second)
{
  
  const InfCommunicationManagerJoinedKey* first_key;
  const InfCommunicationManagerJoinedKey* second_key;
  int res;

  first_key = (const InfCommunicationManagerJoinedKey*)first;
  second_key = (const InfCommunicationManagerJoinedKey*)second;

  res = strcmp(first_key->group_name, second_key->group_name);
  if(res != 0) return res;

  res = strcmp(first_key->publisher_id, second_key->publisher_id);
  if(res != 0) return res;

  return strcmp(first_key->network, second_key->network);
}

static gboolean
inf_communication_manager_joined_key_equal(gconstpointer first,
                                           gconstpointer second)
{
  return inf_communication_manager_joined_key_cmp(first, second) == 0;
}

static guint
inf_communication_manager_joined_key_hash(gconstpointer key_)
{
  const InfCommunicationManagerJoinedKey* key;
  key = (const InfCommunicationManagerJoinedKey*)key_;
 
  /* TODO: Is this a good hash function? */
  return g_str_hash(key->network)
       ^ g_str_hash(key->publisher_id)
       ^ g_str_hash(key->group_name);
}

static void
inf_communication_manager_hosted_group_unrefed(gpointer data,
                                               GObject* where_the_object_was)
{
  InfCommunicationManager* manager;
  InfCommunicationManagerPrivate* priv;
  GHashTableIter iter;
  gpointer value;

  manager = INF_COMMUNICATION_MANAGER(data);
  priv = INF_COMMUNICATION_MANAGER_PRIVATE(manager);
  g_hash_table_iter_init(&iter, priv->hosted_groups);

  /* We don't have the key here. If we had, then we could still not use it
   * because we wouldn't have the communication manager then. If we would
   * want to have both, we would need to dynamically allocate a structure
   * containing both, and also storing that somewhere to be able to
   * g_weak_unref in dispose() which is pretty much hassle. */
  /* TODO: Let the groups strong-ref the manager, then dispose simply can't
   * run until all groups are freed. */
  while(g_hash_table_iter_next(&iter, NULL, &value))
  {
    if(value == where_the_object_was)
    {
      g_hash_table_iter_remove(&iter);
      break;
    }
  }
}

static void
inf_communication_manager_joined_group_unrefed(gpointer data,
                                               GObject* where_the_object_was)
{
  InfCommunicationManager* manager;
  InfCommunicationManagerPrivate* priv;
  GHashTableIter iter;
  gpointer value;

  manager = INF_COMMUNICATION_MANAGER(data);
  priv = INF_COMMUNICATION_MANAGER_PRIVATE(manager);
  g_hash_table_iter_init(&iter, priv->joined_groups);

  /* We don't have the key here. If we had, then we could still not use it
   * because we wouldn't have the communication manager then. If we would
   * want to have both, we would need to dynamically allocate a structure
   * containing both, and also storing that somewhere to be able to
   * g_weak_unref in dispose() which is pretty much hassle. */
  /* TODO: Let the groups strong-ref the manager, then dispose simply can't
   * run until all groups are freed. */
  while(g_hash_table_iter_next(&iter, NULL, &value))
  {
    if(value == where_the_object_was)
    {
      g_hash_table_iter_remove(&iter);
      break;
    }
  }
}

/*
 * GObject overrides.
 */

static void
inf_communication_manager_init(InfCommunicationManager* manager)
{
  InfCommunicationManagerPrivate* priv;
  priv = INF_COMMUNICATION_MANAGER_PRIVATE(manager);

  priv->registry = g_object_new(INF_COMMUNICATION_TYPE_REGISTRY, NULL);
  priv->factories = g_ptr_array_new();
  priv->hosted_groups = g_hash_table_new(g_str_hash, g_str_equal);

  priv->joined_groups = g_hash_table_new_full(
    inf_communication_manager_joined_key_hash,
    inf_communication_manager_joined_key_equal,
    inf_communication_manager_joined_key_free,
    NULL
  );

  /* We always support the "central" method. This is used as a fallback for
   * hosted groups. */
  g_ptr_array_add(
    priv->factories,
    g_object_ref(inf_communication_central_factory_get_default())
  );
}

static void
inf_communication_manager_dispose(GObject* object)
{
  InfCommunicationManager* manager;
  InfCommunicationManagerPrivate* priv;

  manager = INF_COMMUNICATION_MANAGER(object);
  priv = INF_COMMUNICATION_MANAGER_PRIVATE(manager);

  /* TODO: weak unref the groups */
  if(g_hash_table_size(priv->hosted_groups) > 0)
  {
    g_warning("Communication manager containing hosted groups was unrefed");
  }

  if(g_hash_table_size(priv->joined_groups) > 0)
  {
    g_warning("Communication manager containing joined groups was unrefed");
  }

  g_hash_table_unref(priv->hosted_groups);
  g_hash_table_unref(priv->joined_groups);

  g_ptr_array_foreach(priv->factories, (GFunc)g_object_unref, NULL);
  g_ptr_array_free(priv->factories, TRUE);

  if(priv->registry != NULL)
  {
    g_object_unref(priv->registry);
    priv->registry = NULL;
  }

  G_OBJECT_CLASS(inf_communication_manager_parent_class)->dispose(object);
}

static void
inf_communication_manager_class_init(
  InfCommunicationManagerClass* manager_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(manager_class);

  object_class->dispose = inf_communication_manager_dispose;
}

/**
 * inf_communication_manager_new:
 *
 * Creates a new #InfCommunicationManager.
 *
 * Returns: A new #InfCommunicationManager.
 */
InfCommunicationManager*
inf_communication_manager_new(void)
{
  return g_object_new(INF_COMMUNICATION_TYPE_MANAGER, NULL);
}

/**
 * inf_communication_manager_open_group:
 * @manager: A #InfCommunicationManager.
 * @group_name: A name for the new group.
 * @methods: Methods to support, or %NULL.
 *
 * Opens a new communication group published by the local host. @group_name
 * is an identifier for the group via which other hosts can join the group
 * using inf_communication_manager_join_group(). It needs to be unique among
 * all groups opened by the local host.
 *
 * @methods specifies what communication methods the group should use, in
 * order of priority. If a method is not supported for a given network, then
 * the next one in the array is tried. If none is supported, then the
 * "central" method will be used, which is guaranteed to be supported for
 * all networks.
 *
 * Returns: A #InfCommunicationHostedGroup. Free with g_object_unref() to
 * leave the group.
 */
InfCommunicationHostedGroup*
inf_communication_manager_open_group(InfCommunicationManager* manager,
                                     const gchar* group_name,
                                     const gchar* const* methods)
{
  InfCommunicationManagerPrivate* priv;
  InfCommunicationHostedGroup* group;
  const gchar* const* method;

  g_return_val_if_fail(INF_COMMUNICATION_IS_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);

  priv = INF_COMMUNICATION_MANAGER_PRIVATE(manager);

  g_return_val_if_fail(
    g_hash_table_lookup(priv->hosted_groups, group_name) == NULL,
    NULL
  );

  group = g_object_new(
    INF_COMMUNICATION_TYPE_HOSTED_GROUP,
    "communication-manager", manager,
    "communication-registry", priv->registry,
    "name", group_name,
    NULL
  );

  if(methods != NULL)
  {
    for(method = methods; *method != NULL; ++ method)
      inf_communication_hosted_group_add_method(group, *method);
  }

  g_hash_table_insert(
    priv->hosted_groups,
    *(gpointer*) (gpointer) &group_name, /* cast const away without warning */
    group
  );

  g_object_weak_ref(
    G_OBJECT(group),
    inf_communication_manager_hosted_group_unrefed,
    manager
  );

  return group;
}

/**
 * inf_communication_manager_join_group:
 * @manager: A #InfCommunicationManager.
 * @group_name: The group to join.
 * @publisher_conn: A #InfXmlConnection to the publishing host.
 * @method: The communication method to use.
 *
 * Joins a communication group published by a remote host. @publisher_conn
 * needs to be a to the publishing host with status %INF_XML_CONNECTION_OPEN
 * or %INF_XML_CONNECTION_OPENING. @group_name specifies the name of the group
 * to join.
 *
 * @method specifies the communication method to use. It must match the
 * communication method the publisher has chosen for @publisher_conn's network
 * (see inf_communication_group_get_method_for_network()). The function
 * returns %NULL if @method is not supported (which means
 * inf_communication_manager_get_factory_for() for @publisher_conn's network
 * and @method returns %NULL).
 *
 * Returns: A new #InfCommunicationJoinedGroup, or %NULL. Free with
 * g_object_unref() to leave the group.
 */
InfCommunicationJoinedGroup*
inf_communication_manager_join_group(InfCommunicationManager* manager,
                                     const gchar* group_name,
                                     InfXmlConnection* publisher_conn,
                                     const gchar* method)
{
  InfCommunicationManagerPrivate* priv;
  InfCommunicationManagerJoinedKey* key;
  gchar* network;
  gchar* publisher_id;
  InfXmlConnectionStatus status;
  InfCommunicationJoinedGroup* group;

  g_return_val_if_fail(INF_COMMUNICATION_IS_MANAGER(manager), NULL);
  g_return_val_if_fail(group_name != NULL, NULL);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(publisher_conn), NULL);
  g_return_val_if_fail(method != NULL, NULL);

  priv = INF_COMMUNICATION_MANAGER_PRIVATE(manager);

  g_object_get(
    G_OBJECT(publisher_conn),
    "network", &network,
    "remote-id", &publisher_id,
    "status", &status,
    NULL
  );

  /* TODO: Do we need to support OPENING somewhere? I don't think it's a good
   * idea to do here. When we change this remember to change docs above. */
  if(status == INF_XML_CONNECTION_CLOSING ||
     status == INF_XML_CONNECTION_CLOSED)
  {
    g_free(network);
    g_free(publisher_id);
    g_return_val_if_reached(NULL);
  }

  key = g_slice_new(InfCommunicationManagerJoinedKey);
  key->network = network;
  key->publisher_id = publisher_id;
  key->group_name = group_name;

  group = g_hash_table_lookup(priv->joined_groups, key);
  if(group != NULL)
  {
    inf_communication_manager_joined_key_free(key);
    g_return_val_if_reached(NULL);
  }

  if(!inf_communication_manager_get_factory_for(manager, network, method))
  {
    inf_communication_manager_joined_key_free(key);
    return NULL; /* ordinary failure for now */
  }

  group = g_object_new(
    INF_COMMUNICATION_TYPE_JOINED_GROUP,
    "communication-manager", manager,
    "communication-registry", priv->registry,
    "name", group_name,
    "publisher", publisher_conn,
    "method", method,
    NULL
  );

  key->group_name =
    inf_communication_group_get_name(INF_COMMUNICATION_GROUP(group));

  g_hash_table_insert(priv->joined_groups, key, group);

  g_object_weak_ref(
    G_OBJECT(group),
    inf_communication_manager_joined_group_unrefed,
    manager
  );

  return group;
}

/**
 * inf_communication_manager_add_factory:
 * @manager: A #InfCommunicationManager.
 * @factory: The #InfCommunicationFactory to add.
 *
 * Adds a new #InfCommunicationFactory to @manager. This makes @manager
 * support all method/network combinations that @factory supports. If multiple
 * added factories support the same combination, the one which was added first
 * will be used to instantiate the #InfCommunicationMethod.
 */
void
inf_communication_manager_add_factory(InfCommunicationManager* manager,
                                      InfCommunicationFactory* factory)
{
  InfCommunicationManagerPrivate* priv;

  g_return_if_fail(INF_COMMUNICATION_IS_MANAGER(manager));
  g_return_if_fail(INF_COMMUNICATION_IS_FACTORY(factory));

  priv = INF_COMMUNICATION_MANAGER_PRIVATE(manager);
  g_ptr_array_add(priv->factories, factory);
  g_object_ref(factory);
}

/**
 * inf_communication_manager_get_factory_for:
 * @manager: A #InfCommunicationManager.
 * @network: A network identifier.
 * @method_name: A method name.
 *
 * Returns the #InfCommunicationFactory that @manager will use to instantiate
 * a #InfCommunicationMethod for @method_name on @network, or %NULL if the
 * network/method combination is not supported.
 *
 * Returns: A #InfCommunicationFactory, or %NULL.
 */
InfCommunicationFactory*
inf_communication_manager_get_factory_for(InfCommunicationManager* manager,
                                          const gchar* network,
                                          const gchar* method_name)
{
  InfCommunicationManagerPrivate* priv;
  InfCommunicationFactory* factory;
  gboolean supported;
  guint i;

  g_return_val_if_fail(INF_COMMUNICATION_IS_MANAGER(manager), NULL);
  g_return_val_if_fail(network != NULL, NULL);
  g_return_val_if_fail(method_name != NULL, NULL);

  priv = INF_COMMUNICATION_MANAGER_PRIVATE(manager);
  for(i = 0; i < priv->factories->len; ++ i)
  {
    factory = g_ptr_array_index(priv->factories, i);

    supported = inf_communication_factory_supports_method(
      factory,
      network,
      method_name
    );

    if(supported == TRUE)
      return factory;
  }

  return NULL;
}

/* vim:set et sw=2 ts=2: */
