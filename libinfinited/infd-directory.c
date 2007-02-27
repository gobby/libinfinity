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

#include <libinfinited/infd-directory.h>

#include <libinfinity/inf-session.h>
#include <libinfinity/inf-net-object.h>

typedef struct _InfdDirectoryNode InfdDirectoryNode;
struct _InfdDirectoryNode {
  InfdDirectoryNode* parent;

  InfdDirectoryStorageNodeType type;
  guint id;
  gchar* name;

  union {
    struct {
      /* Running session, or NULL */
      InfSession* session; /* TODO: Make InfdSession out of this as soon as we have InfdSession */
    } note;

    struct {
      /* List of nodes this folder contains. */
      GHashTable* nodes;
      /* List of connections that have this folder open and have to be
       * notified if something happens with it. */
      GSList* connections;
    } subdir;
  } shared;
};

typedef struct _InfdDirectoryPrivate InfdDirectoryPrivate;
struct _InfdDirectoryPrivate {
  InfdDirectoryStorage* storage;
  InfConnectionManager* connection_manager;

  GSList* connections;

  guint node_counter;
  GHashTable* nodes; /* Mapping from id to node */
  InfdDirectoryNode* root;
};

enum {
  PROP_0,

  PROP_STORAGE,
  PROP_CONNECTION_MANAGER
};

#define INFD_DIRECTORY_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_DIRECTORY, InfdDirectoryPrivate))

static GObjectClass* parent_class;

/* This function takes ownership of name */
static InfdDirectoryNode*
infd_directory_node_new(InfdDirectory* directory,
                        InfdDirectoryNode* parent,
                        InfdDirectoryStorageNodeType type,
                        gchar* name)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  node = g_slice_new(InfdDirectoryNode);
  node->parent = parent;
  node->type = type;
  node->id = priv->node_counter ++;
  node->name = name;

  switch(type)
  {
  case INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY:
    node->shared.subdir.nodes = g_hash_table_new(
      g_str_hash,
      g_str_equal
    );

    node->shared.subdir.connections = NULL;
    break;
  case INFD_DIRECTORY_STORAGE_NODE_TEXT:
    node->shared.note.session = NULL;
    break;
  case INFD_DIRECTORY_STORAGE_NODE_INK:
    node->shared.note.session = NULL;
    break;
  default:
    g_assert_not_reached();
    break;
  }

#if 0
  if(parent != NULL)
  {
    g_assert(parent->type == INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY);
    g_hash_table_insert(parent->shared.subdir.nodes, name, node);
  }
#endif

  g_hash_table_insert(priv->nodes, GUINT_TO_POINTER(node->id), node);

  return node;
}

/* Required by infd_directory_node_free_foreach_func() */
static void
infd_directory_node_free(InfdDirectory* directory,
                         InfdDirectoryNode* node);

static void
infd_directory_node_free_foreach_func(gpointer key,
                                      gpointer value,
                                      gpointer user_data)
{
  infd_directory_node_free(INFD_DIRECTORY(user_data), value);
}

static void
infd_directory_node_free(InfdDirectory* directory,
                         InfdDirectoryNode* node)
{
  InfdDirectoryPrivate* priv;
  gboolean removed;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  priv = INFD_DIRECTORY_PRIVATE(directory);

#if 0
  if(node->parent != NULL)
  {
    g_assert(node->parent->type == INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY);

    removed = g_hash_table_remove(
      node->parent->shared.subdir.nodes,
      node->name
    );

    g_assert(removed == TRUE);
  }
#endif

  removed = g_hash_table_remove(priv->nodes, GUINT_TO_POINTER(node->id));
  g_assert(removed == TRUE);

  switch(node->type)
  {
  case INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY:
    g_slist_free(node->shared.subdir.connections);

    /* Free child nodes */
    g_hash_table_foreach(
      node->shared.subdir.nodes,
      infd_directory_node_free_foreach_func,
      directory
    );

    g_hash_table_destroy(node->shared.subdir.nodes);
    break;
  case INFD_DIRECTORY_STORAGE_NODE_TEXT:
    /* TODO: Do not close session, but save into storage. */
    break;
  case INFD_DIRECTORY_STORAGE_NODE_INK:
    /* TODO: Do not close session, but save into storage. */
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_free(node->name);
  g_slice_free(InfdDirectoryNode, node);
}

/* Required by infd_directory_node_remove_connection_foreach_func() */
static void
infd_directory_node_remove_connection(InfdDirectoryNode* node,
                                      GNetworkConnection* connection);

static void
infd_directory_node_remove_connection_foreach_func(gpointer key,
                                                   gpointer value,
                                                   gpointer user_data)
{
  infd_directory_node_remove_connection(
    value,
    GNETWORK_CONNECTION(user_data)
  );
}

static void
infd_directory_node_remove_connection(InfdDirectoryNode* node,
                                      GNetworkConnection* connection)
{
  GSList* item;

  g_return_if_fail(node->type != INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY);

  item = g_slist_find(node->shared.subdir.connections, connection);

  /* Note that if the connection is not in this node's connection list,
   * then it cannot be in a child's list either. */
  if(item != NULL)
  {
    node->shared.subdir.connections = g_slist_delete_link(
      node->shared.subdir.connections,
      item
    );

    g_hash_table_foreach(
      node->shared.subdir.nodes,
      infd_directory_node_remove_connection_foreach_func,
      connection
    );
  }
}

/* Required by infd_directory_connection_notify_status_cb() */
static void
infd_directory_remove_connection(InfdDirectory* directory,
                                 GNetworkConnection* connection);

static void
infd_directory_connection_notify_status_cb(GNetworkConnection* connection,
                                           const gchar* property,
                                           gpointer user_data)
{
  InfdDirectory* directory;
  GNetworkConnectionStatus status;

  directory = INFD_DIRECTORY(user_data);

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == GNETWORK_CONNECTION_CLOSING ||
     status == GNETWORK_CONNECTION_CLOSED)
  {
    infd_directory_remove_connection(directory, connection);
  }
}

static void
infd_directory_remove_connection(InfdDirectory* directory,
                                 GNetworkConnection* connection)
{
  InfdDirectoryPrivate* priv;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->root != NULL)
  {
    infd_directory_node_remove_connection(priv->root, connection);
  }

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(connection),
    G_CALLBACK(infd_directory_connection_notify_status_cb),
    directory
  );

  priv->connections = g_slist_remove(priv->connections, connection);
  g_object_unref(G_OBJECT(connection));
}

static void
infd_directory_set_storage(InfdDirectory* directory,
                           InfdDirectoryStorage* storage)
{
  InfdDirectoryPrivate* priv;
  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->storage != NULL)
  {
    /* priv->root may be NULL if this is called from dispose. */
    if(priv->root != NULL)
    {
      /* Clear directory tree. This will cause all sessions to be saved in
       * storage. */
      g_hash_table_foreach(
        priv->root->shared.subdir.nodes,
        infd_directory_node_free_foreach_func,
        directory
      );

      g_hash_table_remove_all(priv->root->shared.subdir.nodes);
    }

    /* TODO: Tell all connections which opened root folder that the folder
     * was cleared (expunged?), but the folder itself still exists. */

    g_object_unref(G_OBJECT(priv->storage));
  }

  priv->storage = storage;

  if(storage != NULL)
  {
    /* TODO: Send root folder content to all connections that
     * opened root folder. */
    g_object_ref(G_OBJECT(storage));
  }
}

static void
infd_directory_set_connection_manager(InfdDirectory* directory,
                                      InfConnectionManager* manager)
{
  InfdDirectoryPrivate* priv;
  GSList* item;
  gboolean result;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->connection_manager != NULL)
  {
    /* Unassociate from this connection manager, so it does no longer
     * forward incoming data to us. */
    for(item = priv->connections; item != NULL; item = g_slist_next(item))
    {
      inf_connection_manager_remove_object(
        priv->connection_manager,
        GNETWORK_CONNECTION(item->data),
        INF_NET_OBJECT(directory)
      );
    }

    g_object_unref(G_OBJECT(priv->connection_manager));
  }

  priv->connection_manager = manager;

  if(manager != NULL)
  {
    /* Add connections to the new connection manager (if they are not
     * already) and tell it to forward data. */
    for(item = priv->connections; item != NULL; item = g_slist_next(item))
    {
      result = inf_connection_manager_has_connection(
        priv->connection_manager,
        GNETWORK_CONNECTION(item->data)
      );

      if(result == FALSE)
      {
        inf_connection_manager_add_connection(
          priv->connection_manager,
          GNETWORK_CONNECTION(item->data)
        );
      }

      inf_connection_manager_add_object(
        priv->connection_manager,
        GNETWORK_CONNECTION(item->data),
        INF_NET_OBJECT(directory),
        "InfDirectory"
      );
    }

    g_object_ref(G_OBJECT(manager));
  }
}

static void
infd_directory_init(GTypeInstance* instance,
                    gpointer g_class)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(instance);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  priv->storage = NULL;
  priv->connection_manager = NULL;

  priv->connections = NULL;

  priv->node_counter = 0;
  priv->nodes = g_hash_table_new(NULL, NULL);

  priv->root = infd_directory_node_new(
    directory,
    NULL,
    INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY,
    NULL /* The root node has no name */
  );
}

static void
infd_directory_dispose(GObject* object)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(object);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* This frees the complete directory tree. */
  infd_directory_node_free(directory, priv->root);
  priv->root = NULL;

  g_hash_table_destroy(priv->nodes);
  priv->nodes = NULL;

  while(priv->connections != NULL)
    infd_directory_remove_connection(directory, priv->connections->data);

  /* We have droppen all references to connections now, so these do not try
   * to tell anyone that the directory tree has gone or whatever. */
  infd_directory_set_connection_manager(directory, NULL);
  infd_directory_set_storage(directory, NULL);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
infd_directory_set_property(GObject* object,
                            guint prop_id,
                            const GValue* value,
                            GParamSpec* pspec)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(object);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  switch(prop_id)
  {
  case PROP_STORAGE:
    infd_directory_set_storage(
      directory,
      INFD_DIRECTORY_STORAGE(g_value_get_object(value))
    );

    break;
  case PROP_CONNECTION_MANAGER:
    infd_directory_set_connection_manager(
      directory,
      INF_CONNECTION_MANAGER(g_value_get_object(value))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_directory_get_property(GObject* object,
                            guint prop_id,
                            GValue* value,
                            GParamSpec* pspec)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(object);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  switch(prop_id)
  {
  case PROP_STORAGE:
    g_value_set_object(value, G_OBJECT(priv->storage));
    break;
  case PROP_CONNECTION_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->connection_manager));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_directory_net_object_sent(InfNetObject* net_object,
                               GNetworkConnection* connection,
                               const xmlNodePtr node)
{
  /* TODO: Implement */
}

static void
infd_directory_net_object_received(InfNetObject* net_object,
                                   GNetworkConnection* connection,
                                   const xmlNodePtr node)
{
  /* TODO: Implement */
}

static void
infd_directory_class_init(gpointer g_class,
                          gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdDirectoryPrivate));

  object_class->dispose = infd_directory_dispose;
  object_class->set_property = infd_directory_set_property;
  object_class->get_property = infd_directory_get_property;

  g_object_class_install_property(
    object_class,
    PROP_STORAGE,
    g_param_spec_object(
      "storage",
      "Storage backend",
      "The storage backend to use",
      INFD_TYPE_DIRECTORY_STORAGE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CONNECTION_MANAGER,
    g_param_spec_object(
      "connection-manager",
      "Connection manager",
      "The connection manager for the directory",
      INF_TYPE_CONNECTION_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
infd_directory_net_object_init(gpointer g_iface,
                               gpointer iface_data)
{
  InfNetObjectIface* iface;
  iface = (InfNetObjectIface*)g_iface;

  iface->sent = infd_directory_net_object_sent;
  iface->received = infd_directory_net_object_received;
}

GType
infd_directory_get_type(void)
{
  static GType directory_type = 0;

  if(!directory_type)
  {
    static const GTypeInfo directory_type_info = {
      sizeof(InfdDirectoryClass),  /* class_size */
      NULL,                        /* base_init */
      NULL,                        /* base_finalize */
      infd_directory_class_init,   /* class_init */
      NULL,                        /* class_finalize */
      NULL,                        /* class_data */
      sizeof(InfdDirectory),       /* instance_size */
      0,                           /* n_preallocs */
      infd_directory_init,         /* instance_init */
      NULL                         /* value_table */
    };

    static const GInterfaceInfo net_object_info = {
      infd_directory_net_object_init,
      NULL,
      NULL
    };

    directory_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdDirectory",
      &directory_type_info,
      0
    );

    g_type_add_interface_static(
      directory_type,
      INF_TYPE_NET_OBJECT,
      &net_object_info
    );
  }

  return directory_type;
}

/** infd_directory_new:
 *
 * @storage: Storage backend that is used to read/write notes from
 * permanent memory into #InfBuffer objects.
 * @connection_manager: A #InfConnectionManager to register added
 * connections to and which forwards incoming data to the directory
 * or running sessions.
 *
 * Creates a new #InfdDirectory.
 *
 * Return Value: A new #InfdDirectory.
 **/
InfdDirectory*
infd_directory_new(InfdDirectoryStorage* storage,
                   InfConnectionManager* connection_manager)
{
  GObject* object;

  g_return_val_if_fail(INFD_IS_DIRECTORY_STORAGE(storage), NULL);
  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(connection_manager), NULL);

  object = g_object_new(
    INFD_TYPE_DIRECTORY,
    "storage", storage,
    "connection-manager", connection_manager,
    NULL
  );

  return INFD_DIRECTORY(object);
}

/** infd_directory_get_storage:
 *
 * @directory: A #InfdDirectory:
 *
 * Returns the storage backend in use by the directory.
 *
 * Return Value: An #InfdDirectoryStorage.
 **/
InfdDirectoryStorage*
infd_directory_get_storage(InfdDirectory* directory)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  return INFD_DIRECTORY_PRIVATE(directory)->storage;
}

/** infd_directory_get_connection_manager:
 *
 * @directory: A #InfdDirectory.
 *
 * Returns the connection manager of the directory.
 *
 * Return Value: An #InfConnectionManager.
 **/
InfConnectionManager*
infd_directory_get_connection_manager(InfdDirectory* directory)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  return INFD_DIRECTORY_PRIVATE(directory)->connection_manager;
}

/**
 * infd_directory_add_connection:
 *
 * @directory: A #InfdDirectory.
 * @connection: A #GNetworkConnection.
 *
 * Adds @connection to the connections of @directory (and to its
 * #InfConnectionManager, if not already). The directory will then
 * receive requests from @connection.
 **/
void
infd_directory_add_connection(InfdDirectory* directory,
                              GNetworkConnection* connection)
{
  InfdDirectoryPrivate* priv;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  g_return_if_fail(GNETWORK_IS_CONNECTION(connection));

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_return_if_fail(INF_IS_CONNECTION_MANAGER(priv->connection_manager));

  inf_connection_manager_add_connection(
    priv->connection_manager,
    connection
  );

  g_signal_connect_after(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(infd_directory_connection_notify_status_cb),
    directory
  );

  priv->connections = g_slist_prepend(priv->connections, connection);
  g_object_ref(G_OBJECT(connection));
}
