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
#include <libinfinited/infd-marshal.h>

#include <libinfinity/inf-session.h>
#include <libinfinity/inf-net-object.h>

typedef struct _InfdDirectoryNode InfdDirectoryNode;
struct _InfdDirectoryNode {
  InfdDirectoryNode* parent;
  InfdDirectoryNode* prev;
  InfdDirectoryNode* next;

  InfdDirectoryStorageNodeType type;
  guint id;
  gchar* name;

  union {
    struct {
      /* Running session, or NULL */
      InfSession* session; /* TODO: Make InfdSession out of this as soon as we have InfdSession */
    } note;

    struct {
      /* List of connections that have this folder open and have to be
       * notified if something happens with it. */
      GSList* connections;
      /* First child node */
      InfdDirectoryNode* child;
      /* Whether we requested the node already from the background storage.
       * This is required because the nodes field may be NULL due to an empty
       * subdirectory or due to an unexplored subdirectory. */
      gboolean explored;
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

typedef gboolean(*InfdDirectoryCreateStorageNodeFunc)(InfdDirectoryStorage*,
                                                      const gchar*,
                                                      GError**);

enum {
  PROP_0,

  PROP_STORAGE,
  PROP_CONNECTION_MANAGER
};

enum {
  NODE_ADDED,
  NODE_REMOVED,

  LAST_SIGNAL
};

#define INFD_DIRECTORY_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_DIRECTORY, InfdDirectoryPrivate))

/* These make sure that the node iter points to is contained in directory */
#define infd_directory_return_if_iter_fail(directory, iter) \
  g_return_if_fail( \
    g_hash_table_lookup( \
      INFD_DIRECTORY_PRIVATE(INFD_DIRECTORY(directory))->nodes, \
      GUINT_TO_POINTER((iter)->node_id) \
    ) == (iter)->node \
  )

#define infd_directory_return_val_if_iter_fail(directory, iter, val) \
  g_return_val_if_fail( \
    g_hash_table_lookup( \
      INFD_DIRECTORY_PRIVATE(INFD_DIRECTORY(directory))->nodes, \
      GUINT_TO_POINTER((iter)->node_id) \
    ) == (iter)->node, \
    val \
  )

/* These make sure that node is a subdirectory node */
#define infd_directory_return_if_subdir_fail(node) \
  g_return_if_fail( \
    ((InfdDirectoryNode*)node)->type == \
    INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY \
  )

#define infd_directory_return_val_if_subdir_fail(node, val) \
  g_return_val_if_fail( \
    ((InfdDirectoryNode*)node)->type == \
    INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY, \
    val \
  )

static GObjectClass* parent_class;
static guint directory_signals[LAST_SIGNAL];

/*
 * Node construction and removal
 */

static void
infd_directory_node_link(InfdDirectoryNode* node,
                         InfdDirectoryNode* parent)
{
  g_return_if_fail(node != NULL);
  g_return_if_fail(parent != NULL);
  infd_directory_return_if_subdir_fail(node);

  node->prev = NULL;
  if(parent->shared.subdir.child != NULL)
  {
    parent->shared.subdir.child->prev = node;
    node->next = parent->shared.subdir.child;
  }
  else
  {
    node->next = NULL;
  }

  parent->shared.subdir.child = node;
}

static void
infd_directory_node_unlink(InfdDirectoryNode* node)
{
  g_return_if_fail(node != NULL);
  g_return_if_fail(node->parent != NULL);

  if(node->prev != NULL)
  {
    node->prev->next = node->next;
  }
  else 
  {
    g_assert(node->parent->type == INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY);
    node->parent->shared.subdir.child = node->next;
  }

  if(node->next != NULL)
    node->next->prev = node->prev;
}

/* This function takes ownership of name */
static InfdDirectoryNode*
infd_directory_node_new(InfdDirectory* directory,
                        InfdDirectoryNode* parent,
                        InfdDirectoryStorageNodeType type,
                        gchar* name)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryIter iter;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);

  if(parent != NULL)
  {
    infd_directory_return_val_if_subdir_fail(parent, NULL);
    g_return_val_if_fail(parent->shared.subdir.explored == TRUE, NULL);
  }

  priv = INFD_DIRECTORY_PRIVATE(directory);

  node = g_slice_new(InfdDirectoryNode);
  node->parent = parent;
  node->type = type;
  node->id = priv->node_counter ++;
  node->name = name;

  if(parent != NULL)
    infd_directory_node_link(node, parent);

  g_hash_table_insert(priv->nodes, GUINT_TO_POINTER(node->id), node);

  switch(type)
  {
  case INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY:
    node->shared.subdir.connections = NULL;
    node->shared.subdir.child = NULL;
    node->shared.subdir.explored = FALSE;
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

  return node;
}

static void
infd_directory_node_free(InfdDirectory* directory,
                         InfdDirectoryNode* node)
{
  InfdDirectoryPrivate* priv;
  gboolean removed;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  g_return_if_fail(node != NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  switch(node->type)
  {
  case INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY:
    g_slist_free(node->shared.subdir.connections);

    /* Free child nodes */
    if(node->shared.subdir.explored == TRUE)
    {
      while(node->shared.subdir.child != NULL)
        infd_directory_node_free(directory, node->shared.subdir.child);
    }

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

  if(node->parent != NULL)
    infd_directory_node_unlink(node);

  removed = g_hash_table_remove(priv->nodes, GUINT_TO_POINTER(node->id));
  g_assert(removed == TRUE);

  g_free(node->name);
  g_slice_free(InfdDirectoryNode, node);
}

static void
infd_directory_node_remove_connection(InfdDirectoryNode* node,
                                      GNetworkConnection* connection)
{
  InfdDirectoryNode* child;
  GSList* item;

  infd_directory_return_if_subdir_fail(node);
  g_return_if_fail(node->shared.subdir.explored == FALSE);

  item = g_slist_find(node->shared.subdir.connections, connection);

  /* Note that if the connection is not in this node's connection list,
   * then it cannot be in a child's list either. */
  if(item != NULL)
  {
    node->shared.subdir.connections = g_slist_delete_link(
      node->shared.subdir.connections,
      item
    );

    if(node->shared.subdir.explored == TRUE)
    {
      for(child = node->shared.subdir.child;
          child != NULL;
          child = child->next)
      {
        infd_directory_node_remove_connection(child, connection);
      }
    }
    else
    {
      g_assert(node->shared.subdir.connections == NULL);
    }
  }
}

/*
 * Node synchronization.
 */

/* Announces the presence of a new node. This is not done in
 * infd_directory_node_new because we do not want to do this for all
 * nodes we create (namely not for the root node). */
static void
infd_directory_node_register(InfdDirectory* directory,
                             InfdDirectoryNode* node)
{
  InfdDirectoryIter iter;
  xmlNodePtr xml;
  gchar id_buf[16];
  gchar parent_buf[16];
  const gchar* typename;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  g_return_if_fail(node != NULL);
  g_return_if_fail(node->parent != NULL);
  infd_directory_return_if_subdir_failed(node->parent);

  iter.node_id = node->id;
  iter.node = node;

  g_signal_emit(
    G_OBJECT(directory),
    directory_signals[NODE_ADDED],
    0,
    &iter
  );

  if(node->parent->shared.subdir.connections != NULL)
  {
    g_sprintf(id_buf, "%u", node->id);
    g_sprintf(parent_buf, "%u", node->parent->id);

    switch(node->type)
    {
    case INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY:
      typename = "InfSubdirectory";
      break;
    case INFD_DIRECTORY_STORAGE_NODE_TEXT:
      typename = "InfText";
      break;
    case INFD_DIRECTORY_STORAGE_NODE_INK:
      typename = "InfInk";
      break;
    default:
      g_assert_not_reached();
      break;
    }

    xml = xmlNewNode((const xmlChar*)"add-node");

    xmlAddProp(xml, (const xmlChar*)"id", (const xmlChar*)id_buf);
    xmlAddProp(xml, (const xmlChar*)"parent", (const xmlChar*)parent_buf);
    xmlAddProp(xml, (const xmlChar*)"name", (const xmlChar*)node->name);
    xmlAddProp(xml, (const xmlChar*)"type", (const xmlChar*)typename);

    /* TODO: Send xml to all connections */
  }
}

/* Announces that a node is removed. Again, this is not done in
 * infd_directory_node_free because we do not want to do this for
 * every subnode if a subdirectory is freed. */
static void
infd_directory_node_unregister(InfdDirectory* directory,
                               InfdDirectoryNode* node)
{
  InfdDirectoryIter iter;
  xmlNodePtr xml;

  iter.node_id = node->id;
  iter.node = node;

  g_signal_emit(
    G_OBJECT(directory),
    directory_signals[NODE_REMOVED],
    0,
    &iter
  );

  /* TODO: Announce in network */
}

/*
 * Path handling.
 */

/* Returns the complete path to this node in the given GString */
static void
infd_directory_node_get_path_string(InfdDirectoryNode* node,
                                    GString* string)
{
  g_return_if_fail(node != NULL);
  g_return_if_fail(string != NULL);

  if(node->parent != NULL)
  {
    /* Each node except the root node has a name */
    g_assert(node->name != NULL);

    /* Make sure to not recurse if our parent is the root node because
     * this would add an additional slash */
    if(node->parent->parent != NULL)
      infd_directory_get_path_string(directory, node->parent, string);
 
    g_string_append_c(string, '/');
    g_string_append(string, node->name);
  }
  else
  {
    /* This node has no parent, so it is the root node */
    gassert(node->name == NULL);
    g_string_append_c(string, '/');
  }
}

static void
infd_directory_node_get_path(InfdDirectoryNode* node,
                             gchar** path,
                             gsize* len)
{
  GString* str;

  g_return_if_fail(node != NULL);
  g_return_if_fail(path != NULL);

  str = g_string_sized_new(128);

  infd_directory_node_get_path_string(node, str);
  *path = str->str;

  if(len != NULL)
    *len = str->len;

  g_string_free(str, FALSE);
}

static void
infd_directory_node_make_path(InfdDirectoryNode* node,
                              const gchar* name,
                              gchar** path,
                              gsize* len)
{
  GString* str;

  g_return_if_fail(node != NULL);
  g_return_if_fail(name != NULL);
  g_return_if_fail(path != NULL);

  str = g_string_sized_new(128);

  infd_directory_node_get_path_string(node, str);
  g_string_append_c(str, '/');
  g_string_append(str, name);

  *path = str->str;
  if(len != NULL)
    *len = str->len;

  g_string_free(str, FALSE);
}

/*
 * Directory tree operations.
 */

static gboolean
infd_directory_node_explore(InfdDirectory* directory,
                            InfdDirectoryNode* node,
                            GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryStorageNode* storage_node;
  InfdDirectoryNode* new_node;
  GError* local_error;
  GSList* list;
  GSList* item;
  gchar* path;
  gsize len;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(priv->storage != NULL, FALSE);
  g_assert(node->type == INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY);
  g_assert(node->shared.subdir.explored == FALSE);

  infd_directory_node_get_path(node, &path, &len);

  local_error = NULL;

  list = infd_directory_storage_read_subdirectory(
    priv->storage,
    path,
    &local_error
  );

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  for(item = list; item != NULL; item = g_slist_next(item))
  {
    storage_node = (InfdDirectoryStorageNode*)item->data;

    g_assert(strlen(storage_node->path) > len);

    /* storage_node->path + len + 1 results in the name of the node */
    new_node = infd_directory_node_new(
      directory,
      node,
      storage_node->type,
      storage_node->path + len + 1
    );

    /* Announce the new node. In most cases, this does nothing on the network
     * because there are no connections that have this node open (otherwise,
     * we would already have explored the node earlier). However, if the
     * background storage is replaced by a new one, the root folder of the
     * new storage will be explored immediately (see below in
     * infd_directory_set_storage()) and there might still be connections
     * interesting in root folder changes (because they opened the root
     * folder from the old storage). Also, local users might be interested
     * in the new node. */
    infd_directory_node_register(directory, node);
  }

  infd_directory_storage_node_list_free(list);

  node->shared.subdir.explored = TRUE;
  return TRUE;
}

static InfdDirectoryNode*
infd_directory_node_add_text(InfdDirectory* directory,
                             InfdDirectoryNode* parent,
                             const gchar* name,
                             GError** error)
{
  InfdDirectoryClass* directory_class;
  InfdDirectoryPrivate* priv;
  InfSession* session;
  gboolean result;
  gchar* path;

  directory_class = INFD_DIRECTORY_CLASS(directory);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(priv->storage != NULL);
  g_assert(directory_class->text_session_new != NULL);

  session = directory_class->text_session_new(directory);
  
}

static InfdDirectoryNode*
infd_directory_node_add_subdirectory(InfdDirectory* directory,
                                     InfdDirectoryNode* parent,
                                     const gchar* name,
                                     GError** error)
{
  InfdDirectoryPrivate* priv;
  gboolean result;
  gchar* path;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_assert(priv->storage != NULL);

  infd_directory_node_make_path(parent, name, &path, NULL);

  result = infd_directory_storage_create_subdirectory(
    priv->storage,
    path,
    error
  );

  g_free(path);
  if(result == FALSE) return FALSE;

  node = infd_directory_node_new(
    directory,
    parent->node,
    INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY,
    name
  );

  infd_directory_node_register(directory, node);
}

/*
 * Network command handling.
 */

/*
 * Signal handlers.
 */

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

/*
 * Property modification.
 */

static void
infd_directory_set_storage(InfdDirectory* directory,
                           InfdDirectoryStorage* storage)
{
  InfdDirectoryPrivate* priv;

  InfdDirectoryNode* child;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->storage != NULL)
  {
    /* priv->root may be NULL if this is called from dispose. */
    if(priv->root != NULL && priv->root->shared.subdir.explored == TRUE)
    {
      /* Clear directory tree. This will cause all sessions to be saved in
       * storage. */
      while((child = priv->root->shared.subdir.child) != NULL)
      {
        infd_directory_node_unregister(directory, child);
        infd_directory_node_free(directory, child);
      }
    }

    g_object_unref(G_OBJECT(priv->storage));
  }

  priv->storage = storage;

  if(storage != NULL)
  {
    /* root folder was explored before storage change, so keep it
     * explored. */
    if(priv->root->shared.subdir.explored == TRUE)
    {
      /* Need to set explored flag to FALSE to meet preconditions of
       * infd_directory_node_explore(). */
      priv->root->shared.subdir.explored = FALSE;
      infd_directory_node_explore(directory, priv->root);
    }

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

/*
 * GObject overrides.
 */

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

  /* We have dropped all references to connections now, so these do not try
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

/*
 * InfNetObject implementation.
 */
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

/*
 * GType registration.
 */
static void
infd_directory_class_init(gpointer g_class,
                          gpointer class_data)
{
  GObjectClass* object_class;
  InfdDirectoryClass* directory_class;

  object_class = G_OBJECT_CLASS(g_class);
  directory_class = INFD_DIRECTORY_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdDirectoryPrivate));

  object_class->dispose = infd_directory_dispose;
  object_class->set_property = infd_directory_set_property;
  object_class->get_property = infd_directory_get_property;

  directory_class->node_added = NULL;
  directory_class->node_removed = NULL;

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

  directory_signals[NODE_ADDED] = g_signal_new(
    "node-added",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdDirectoryClass, node_added),
    NULL, NULL,
    infd_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INFD_TYPE_DIRECTORY_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
  );

  directory_signals[NODE_REMOVED] = g_signal_new(
    "node-removed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdDirectoryClass, node_removed),
    NULL, NULL,
    infd_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INFD_TYPE_DIRECTORY_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
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
infd_directory_iter_get_type(void)
{
  static GType directory_iter_type = 0;

  if(!directory_iter_type)
  {
    directory_iter_type = g_boxed_type_register_static(
      "InfDirectoryIter",
      (GBoxedCopyFunc)infd_directory_iter_copy,
      (GBoxedFreeFunc)infd_directory_iter_free
    );
  }

  return directory_iter_type;
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

/*
 * Public API.
 */

/** infd_directory_iter_copy:
 *
 * @iter: A @InfdDirectoryIter.
 *
 * Makes a dynamically-allocated copy of @iter. This should not be used by
 * applications because you can copy the structs by value.
 *
 * Return Value: A newly-allocated copy of @iter.
 **/
InfdDirectoryIter*
infd_directory_iter_copy(InfdDirectoryIter* iter)
{
  InfdDirectoryIter* new_iter;

  g_return_val_if_fail(iter != NULL, NULL);

  new_iter = g_slice_new(InfdDirectoryIter);
  *new_iter = *iter;

  return new_iter;
}

/** infd_directory_iter_free:
 *
 * @iter: A #InfdDirectoryIter.
 *
 * Frees a #InfdDirectoryIter allocated with infd_directory_iter_copy().
 **/
void
infd_directory_iter_free(InfdDirectoryIter* iter)
{
  g_return_if_fail(iter != NULL);

  g_slice_free(InfdDirectoryIter, iter);
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

/** infd_directory_iter_get_root:
 *
 * @directory: A #InfdDirectory
 * @iter An uninitalized #InfdDirectoryIter.
 *
 * Sets @iter to point to the root node of the directory.
 **/
void
infd_directory_iter_get_root(InfdDirectory* directory,
                             InfdDirectoryIter* iter)
{
  InfdDirectoryPrivate* priv;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  g_return_if_fail(iter != NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_assert(priv->root != NULL);

  iter->node_id = priv->root->id;
  iter->node = priv->root;
}

/** infd_directory_iter_get_next:
 *
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to some node in @directory.
 *
 * Sets @iter to point to the next node within the same subdirectory. If there
 * is no next node, @iter is left untouched and the function returns %FALSE.
 *
 * Return Value: %TRUE, if @iter was set. 
 **/
gboolean
infd_directory_iter_get_next(InfdDirectory* directory,
                             InfdDirectoryIter* iter)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);
  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;

  if(node->next != NULL)
  {
    iter->node_id = node->next->id;
    iter->node = node->next;

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/** infd_directory_iter_get_prev:
 *
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to some node in @directory.
 *
 * Sets @iter to point to the previous node within the same subdirectory. If
 * there is no such node, @iter is left untouched and the function returns
 * %FALSE.
 *
 * Return Value: %TRUE, if @iter was set. 
 **/
gboolean
infd_directory_iter_get_prev(InfdDirectory* directory,
                             InfdDirectoryIter* iter)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);
  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;

  if(node->prev != NULL)
  {
    iter->node_id = node->prev->id;
    iter->node = node->prev;

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/** infd_directory_iter_get_parent:
 *
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to some node in @directory.
 *
 * Sets @iter to point to the parent node of @iter. This node is guaranteed
 * to be a subdirectory node. If there is no such node (i.e. @iter points
 * to the root node), @iter is left untouched and the function returns %FALSE.
 *
 * Return Value: %TRUE, if @iter was set. 
 **/
gboolean
infd_directory_iter_get_parent(InfdDirectory* directory,
                               InfdDirectoryIter* iter)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);
  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;

  if(node->parent != NULL)
  {
    iter->node_id = node->parent->id;
    iter->node = node->parent;

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/** infd_directory_iter_get_child:
 *
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to a subdirectory node in @directory.
 * @error: Location to store error information.
 *
 * Sets @iter to point to first child node of @iter. This requires that @iter
 * points to a subdirectory node. If the subdirectory @iter points to has
 * no children, the function returns %FALSE and @iter is left untouched.
 *
 * The function might fail if this node's children have not yet been read
 * from the background storage and an error occurs while reading them. In
 * this case, %FALSE is returned and @error is set.
 *
 * Return Value: %TRUE, if @iter was set. 
 **/
gboolean
infd_directory_iter_get_child(InfdDirectory* directory,
                              InfdDirectoryIter* iter,
                              GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);
  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);
  infd_directory_return_val_if_subdir_fail(iter->node, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;

  if(node->shared.subdir.explored == FALSE)
  {
    if(infd_directory_node_explore(directory, node, error) == FALSE)
      return FALSE;

    g_assert(node->shared.subdir.explored == TRUE);
  }

  if(node->shared.subdir.child != NULL)
  {
    iter->node_id = node->shared.subdir.child->id;
    iter->node = node->shared.subdir.child;

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/** infd_directory_add_subdirectory:
 *
 * @directory: A #InfdDirectory.
 * @parent: A #InfdDirectoryIter pointing to a subdirectory node in @directory.
 * @iter: An uninitalized #InfdDirectoryIter.
 * @error: Location to store error information.
 *
 * Adds a subdirectory to the directory tree. The new subdirectory will be
 * a child the subdirectory @parent points to. @iter is modified to point to
 * the new subdirectory. If creation fails, the function returns FALSE and
 * @error is set.
 *
 * Return Value: %TRUE if the subdirectory was created successfully.
 **/
gboolean
infd_directory_add_subdirectory(InfdDirectory* directory,
                                InfdDirectoryIter* parent,
                                const gchar* name,
                                InfdDirectoryIter* iter,
                                GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  gchar* parent_path;
  gchar* path;
  gboolean result;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(parent != NULL, FALSE);
  g_return_val_if_fail(name != NULL, FALSE);
  infd_directory_return_val_if_iter_fail(directory, parent, FALSE);
  infd_directory_return_val_if_subdir_fail(parent->node, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_return_val_if_fail(priv->storage != NULL, FALSE);

  /* TODO: Explore parent if it is not already */

  infd_directory_get_path(parent->node, &parent_path, NULL);
  path = g_strconcat(parent_path, "/", name, NULL);
  g_free(parent_path);

  if(iter != NULL)
  {
    iter->node_id = node->id;
    iter->node = node;
  }

  return TRUE;
}

/** infd_directory_add_text:
 *
 * @directory: A #InfdDirectory.
 * @parent: A #InfdDirectoryIter pointing to a subdirectory node in @directory.
 * @iter: An uninitialized #InfdDirectoryIter.
 * @error: Location to store error information.
 *
 * Creates a new text note in @directory. It will be child of the subdirectory
 * node @parent points to. @iter is set to point to the new node. If an
 * error occurs, the function returns %FALSE and @error is set.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_add_text(InfdDirectory* directory,
                        InfdDirectoryIter* parent,
                        InfdDirectoryIter* iter,
                        GError** error)
{
  /* TODO: Implement */
  return FALSE;
}

/** infd_directory_add_ink:
 *
 * @directory: A #InfdDirectory.
 * @parent: A #InfdDirectoryIter pointing to a subdirectory node in @directory.
 * @iter: An uninitialized #InfdDirectoryIter.
 * @error: Location to store error information.
 *
 * Creates a new ink note in @directory. It will be child of the subdirectory
 * node @parent points to. @iter is set to point to the new node. If an
 * error occurs, the function returns %FALSE and @error is set.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_add_ink(InfdDirectory* directory,
                       InfdDirectoryIter* parent,
                       InfdDirectoryIter* iter,
                       GError** error)
{
  /* TODO: Implement */
  return FALSE;
}

/** infd_directory_remove_node:
 *
 * @directory: A #InfdDirectory
 * @iter: A #InfdDirectoryIter pointing to some node in @directory.
 * @error: Location to store error information.
 *
 * Removes the node @iter points to. If it is a subdirectory node, every
 * node it contains will also be removed. If the function fails, %FALSE is
 * returned and @error is set.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_remove_node(InfdDirectory* directory,
                           InfdDirectoryIter* iter,
                           GError** error)
{
  /* TODO: Implement */
  return FALSE;
}

/** infd_directory_iter_get_node_type:
 *
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to some node in @directory.
 *
 * Returns the type of the node @iter points to.
 *
 * Returns: A #InfdDirectoryStorageNodeType.
 **/
InfdDirectoryStorageNodeType
infd_directory_iter_get_node_type(InfdDirectory* directory,
                                  InfdDirectoryIter* iter)
{
  /* TODO: Implement */
  return INFD_DIRECTORY_STORAGE_NODE_SUBDIRECTORY;
}

/** infd_directory_iter_get_session:
 *
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to a note in @directory.
 * @error: Location to store error information.
 *
 * Returns the running session in which the note @iter points to is currently
 * edited. If the session does not exist, it is created. However, this might
 * fail if the loading from the background storage fails. In this case, %NULL
 * is returned and @error is set.
 *
 * Return Value: A #InfSession for the note @iter points to.
 **/
InfSession*
infd_directory_iter_get_session(InfdDirectory* directory,
                                InfdDirectoryIter* iter,
                                GError** error)
{
  /* TODO: Implement */
  return NULL;
}

