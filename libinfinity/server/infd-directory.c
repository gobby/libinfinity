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

/**
 * SECTION:infd-directory
 * @short_description: Directory of published documents
 * @see_also: #InfcBrowser, #InfdStorage
 * @include: libinfinity/server/infd-directory.h
 * @stability: Unstable
 *
 * The #InfdDirectory manages a directory of documents. An item in the
 * directory is also called &quot;node&quot;. A node may either be a
 * subdirectory or a document (also called "note"). Notes may be of
 * different type - there may be plain text notes, rich text notes,
 * graphics notes, etc.
 *
 * #InfdStorage defines where the directory structure and the notes are read
 * from and how there are permanently stored.
 **/

#include <libinfinity/server/infd-directory.h>

#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-chat-session.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/common/inf-protocol.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/communication/inf-communication-object.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>

typedef struct _InfdDirectoryNode InfdDirectoryNode;
struct _InfdDirectoryNode {
  InfdDirectoryNode* parent;
  InfdDirectoryNode* prev;
  InfdDirectoryNode* next;

  InfdStorageNodeType type;
  guint id;
  gchar* name;

  union {
    struct {
      /* Running session, or NULL */
      InfdSessionProxy* session;
      /* Session type */
      const InfdNotePlugin* plugin;
      /* Timeout to save the session when inactive for some time */
      gpointer save_timeout;
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

typedef struct _InfdDirectorySessionSaveTimeoutData
  InfdDirectorySessionSaveTimeoutData;
struct _InfdDirectorySessionSaveTimeoutData {
  InfdDirectory* directory;
  InfdDirectoryNode* node;
};

typedef struct _InfdDirectorySyncIn InfdDirectorySyncIn;
struct _InfdDirectorySyncIn {
  InfdDirectory* directory;
  InfdDirectoryNode* parent;
  guint node_id;
  gchar* name;
  const InfdNotePlugin* plugin;
  InfdSessionProxy* proxy;
};

typedef enum _InfdDirectorySubreqType {
  INFD_DIRECTORY_SUBREQ_CHAT,
  INFD_DIRECTORY_SUBREQ_SESSION,
  INFD_DIRECTORY_SUBREQ_ADD_NODE,
  INFD_DIRECTORY_SUBREQ_SYNC_IN,
  INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE
} InfdDirectorySubreqType;

/* Subscription request */
typedef struct _InfdDirectorySubreq InfdDirectorySubreq;
struct _InfdDirectorySubreq {
  InfdDirectorySubreqType type;
  InfXmlConnection* connection;
  /* TODO: Should maybe go to shared as CHAT is not using this: */
  guint node_id;

  union {
    struct {
      InfdSessionProxy* session;
    } session;

    struct {
      InfdDirectoryNode* parent;
      InfCommunicationHostedGroup* group;
      const InfdNotePlugin* plugin;
      gchar* name;
      /* TODO: Isn't group already present in proxy? */
      InfdSessionProxy* proxy;
    } add_node;

    struct {
      InfdDirectoryNode* parent;
      InfCommunicationHostedGroup* synchronization_group;
      InfCommunicationHostedGroup* subscription_group;
      const InfdNotePlugin* plugin;
      gchar* name;
      /* TODO: Aren't the groups already present in proxy? */
      InfdSessionProxy* proxy;
    } sync_in;
  } shared;
};

typedef struct _InfdDirectoryConnectionInfo InfdDirectoryConnectionInfo;
struct _InfdDirectoryConnectionInfo {
  guint seq_id;
};

typedef struct _InfdDirectoryPrivate InfdDirectoryPrivate;
struct _InfdDirectoryPrivate {
  InfIo* io;
  InfdStorage* storage;
  InfCommunicationManager* communication_manager;
  InfCommunicationHostedGroup* group;

  GHashTable* plugins; /* Registered plugins */
  GHashTable* connections; /* Connection infos */

  guint node_counter;
  GHashTable* nodes; /* Mapping from id to node */
  InfdDirectoryNode* root;

  GSList* sync_ins;
  GSList* subscription_requests;

  InfdSessionProxy* chat_session;
};

enum {
  PROP_0,

  PROP_IO,
  PROP_STORAGE,
  PROP_COMMUNICATION_MANAGER,

  /* read only */
  PROP_CHAT_SESSION
};

enum {
  NODE_ADDED,
  NODE_REMOVED,
  ADD_SESSION,
  REMOVE_SESSION,

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
    ((InfdDirectoryNode*)node)->type == INFD_STORAGE_NODE_SUBDIRECTORY \
  )

#define infd_directory_return_val_if_subdir_fail(node, val) \
  g_return_val_if_fail( \
    ((InfdDirectoryNode*)node)->type == INFD_STORAGE_NODE_SUBDIRECTORY, \
    val \
  )

static GObjectClass* parent_class;
static guint directory_signals[LAST_SIGNAL];
static GQuark infd_directory_node_id_quark;

/* Time a session needs to be idle before it is unloaded from RAM */
/* TODO: This should be a property: */
static const guint INFD_DIRECTORY_SAVE_TIMEOUT = 60000;

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
      infd_directory_node_get_path_string(node->parent, string);
 
    g_string_append_c(string, '/');
    g_string_append(string, node->name);
  }
  else
  {
    /* This node has no parent, so it is the root node */
    g_assert(node->name == NULL);
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
  if(node->parent != NULL)
    g_string_append_c(str, '/');

  g_string_append(str, name);

  *path = str->str;
  if(len != NULL)
    *len = str->len;

  g_string_free(str, FALSE);
}

/*
 * Save timeout
 */

/* Required by infd_directory_session_save_timeout_func() */
static void
infd_directory_node_unlink_session(InfdDirectory* directory,
                                   InfdDirectoryNode* node);

static void
infd_directory_session_save_timeout_data_free(gpointer data)
{
  g_slice_free(InfdDirectorySessionSaveTimeoutData, data);
}

static void
infd_directory_session_save_timeout_func(gpointer user_data)
{
  InfdDirectorySessionSaveTimeoutData* timeout_data;
  InfdDirectoryPrivate* priv;
  GError* error;
  gchar* path;
  gboolean result;

  timeout_data = (InfdDirectorySessionSaveTimeoutData*)user_data;

  g_assert(timeout_data->node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(timeout_data->node->shared.note.save_timeout != NULL);
  priv = INFD_DIRECTORY_PRIVATE(timeout_data->directory);
  error = NULL;

  infd_directory_node_get_path(timeout_data->node, &path, NULL);
  result = timeout_data->node->shared.note.plugin->session_write(
    priv->storage,
    infd_session_proxy_get_session(timeout_data->node->shared.note.session),
    path,
    timeout_data->node->shared.note.plugin->user_data,
    &error
  );

  /* TODO: Unset modified flag of buffer if result == TRUE */

  /* The timeout is removed automatically after it has elapsed */
  timeout_data->node->shared.note.save_timeout = NULL;

  if(result == FALSE)
  {
    g_warning(
      _("Failed to save note \"%s\": %s\n\nKeeping it in memory. Another "
        "save attempt will be made when the server is shut down."),
      path,
      error->message
    );

    g_error_free(error);
  }
  else
  {
    infd_directory_node_unlink_session(
      timeout_data->directory,
      timeout_data->node
    );
  }

  g_free(path);
}

static void
infd_directory_start_session_save_timeout(InfdDirectory* directory,
                                          InfdDirectoryNode* node)
{
  InfdDirectoryPrivate* priv;
  InfdDirectorySessionSaveTimeoutData* timeout_data;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  timeout_data = g_slice_new(InfdDirectorySessionSaveTimeoutData);
  timeout_data->directory = directory;
  timeout_data->node = node;

  node->shared.note.save_timeout = inf_io_add_timeout(
    priv->io,
    INFD_DIRECTORY_SAVE_TIMEOUT,
    infd_directory_session_save_timeout_func,
    timeout_data,
    infd_directory_session_save_timeout_data_free
  );
}

static void
infd_directory_session_idle_notify_cb(GObject* object,
                                      GParamSpec* pspec,
                                      gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  gpointer node_id;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(user_data);
  priv = INFD_DIRECTORY_PRIVATE(directory);
  node_id = g_object_get_qdata(object, infd_directory_node_id_quark);
  node = g_hash_table_lookup(priv->nodes, node_id);
  g_assert(node != NULL);

  /* Drop session from memory if it remains idle */
  if(infd_session_proxy_is_idle(INFD_SESSION_PROXY(object)))
  {
    if(node->shared.note.save_timeout == NULL)
    {
      infd_directory_start_session_save_timeout(directory, node);
    }
  }
  else
  {
    if(node->shared.note.save_timeout != NULL)
    {
      inf_io_remove_timeout(priv->io, node->shared.note.save_timeout);
      node->shared.note.save_timeout = NULL;
    }
  }
}

/*
 * Node construction and removal
 */

/* Creates the subscription group for a node, named "InfSession_%u", %u being
 * the node id (which should be unique). */
static InfCommunicationHostedGroup*
infd_directory_create_subscription_group(InfdDirectory* directory,
                                         guint node_id)
{
  InfdDirectoryPrivate* priv;
  InfCommunicationHostedGroup* group;
  gchar* group_name;

  /* TODO: For the moment, there only exist central methods anyway. In the
   * long term, this should probably be a property, though. */
  static const gchar* const methods[] = { "central", NULL };

  priv = INFD_DIRECTORY_PRIVATE(directory);
  group_name = g_strdup_printf("InfSession_%u", node_id);

  group = inf_communication_manager_open_group(
    priv->communication_manager,
    group_name,
    methods
  );

  g_free(group_name);
  return group;
}

static InfdSessionProxy*
infd_directory_create_session_proxy_with_group(InfdDirectory* directory,
                                               InfSession* session,
                                               InfCommunicationHostedGroup* g)
{
  InfdSessionProxy* proxy;

  g_assert(
    inf_communication_group_get_target(INF_COMMUNICATION_GROUP(g)) == NULL
  );

  proxy = INFD_SESSION_PROXY(
    g_object_new(
      INFD_TYPE_SESSION_PROXY,
      "session", session,
      "subscription-group", g,
      NULL
    )
  );

  inf_communication_group_set_target(
    INF_COMMUNICATION_GROUP(g),
    INF_COMMUNICATION_OBJECT(proxy)
  );

  return proxy;
}

static InfdSessionProxy*
infd_directory_create_session_proxy_for_node(InfdDirectory* directory,
                                             guint node_id,
                                             InfSession* session)
{
  InfdDirectoryPrivate* priv;
  InfCommunicationHostedGroup* group;
  InfdSessionProxy* proxy;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  group = infd_directory_create_subscription_group(directory, node_id);

  proxy = infd_directory_create_session_proxy_with_group(
    directory,
    session,
    group
  );

  g_object_unref(group);
  return proxy;
}

static InfdSessionProxy*
infd_directory_create_session_proxy(InfdDirectory* directory,
                                    const InfdNotePlugin* plugin,
                                    InfSessionStatus status,
                                    InfCommunicationHostedGroup* sync_g,
                                    InfXmlConnection* sync_conn,
                                    InfCommunicationHostedGroup* sub_g)
{
  InfdDirectoryPrivate* priv;
  InfSession* session;
  InfdSessionProxy* proxy;

  g_assert(sub_g != NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  session = plugin->session_new(
    priv->io,
    priv->communication_manager,
    status,
    sync_g,
    sync_conn,
    plugin->user_data
  );

  proxy = infd_directory_create_session_proxy_with_group(
    directory,
    session,
    sub_g
  );

  g_object_unref(session);

  if(sync_g != NULL && sync_g != sub_g)
  {
    inf_communication_group_set_target(
      INF_COMMUNICATION_GROUP(sync_g),
      INF_COMMUNICATION_OBJECT(proxy)
    );
  }

  return proxy;
}

static InfdSessionProxy*
infd_directory_create_session_proxy_for_storage(
    InfdDirectory* directory,
    InfdDirectoryNode* parent,
    InfCommunicationHostedGroup* sub_group,
    const gchar* name,
    const InfdNotePlugin* plugin,
    InfSessionStatus status,
    InfCommunicationHostedGroup* sync_group,
    InfXmlConnection* sync_conn,
    GError** error)
{
  InfdDirectoryPrivate* priv;
  gchar* path;
  gboolean ret;
  InfSession* session;
  InfdSessionProxy* proxy;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  proxy = infd_directory_create_session_proxy(
    directory, plugin, status, sync_group, sync_conn, sub_group);
  session = infd_session_proxy_get_session(proxy);

  /* Save session initially */
  infd_directory_node_make_path(parent, name, &path, NULL);

  ret = plugin->session_write(
    priv->storage,
    session,
    path,
    plugin->user_data,
    error
  );

  g_free(path);
  if(ret == FALSE)
  {
    /* Reset communication groups for the proxy, to avoid a warning at
     * final unref. Due do this failing the groups are very likely going to be
     * unrefed as well any time soon. */
    inf_communication_group_set_target(
      INF_COMMUNICATION_GROUP(sub_group),
      NULL
    );

    if(sync_group != NULL && sync_group != sub_group)
    {
      inf_communication_group_set_target(
        INF_COMMUNICATION_GROUP(sync_group),
        NULL
      );
    }

    g_object_unref(proxy);
    return NULL;
  }

  return proxy;
}

/* Links a InfdSessionProxy with a InfdDirectoryNode */
static void
infd_directory_node_link_session(InfdDirectory* directory,
                                 InfdDirectoryNode* node,
                                 InfdSessionProxy* proxy)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryIter iter;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(node->shared.note.session == NULL);

  iter.node = node;
  iter.node_id = node->id;

  g_signal_emit(
    G_OBJECT(directory),
    directory_signals[ADD_SESSION],
    0,
    &iter,
    proxy
  );
}

static void
infd_directory_node_unlink_session(InfdDirectory* directory,
                                   InfdDirectoryNode* node)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryIter iter;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(node->shared.note.session != NULL);

  iter.node = node;
  iter.node_id = node->id;

  /* TODO: We could still weakref the session, to continue using it if
   * others need it anyway. We just need to strongref it again if it becomes
   * non-idle. */

  g_signal_emit(
    G_OBJECT(directory),
    directory_signals[REMOVE_SESSION],
    0,
    &iter,
    node->shared.note.session
  );
}

static void
infd_directory_node_unlink_child_sessions(InfdDirectory* directory,
                                          InfdDirectoryNode* node,
                                          gboolean save_notes)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* child;
  gchar* path;
  GError* error;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  switch(node->type)
  {
  case INFD_STORAGE_NODE_SUBDIRECTORY:
    if(node->shared.subdir.explored == TRUE)
    {
      for(child = node->shared.subdir.child;
          child != NULL;
          child = child->next)
      {
        infd_directory_node_unlink_child_sessions(
          directory,
          child,
          save_notes
        );
      }
    }

    break;
  case INFD_STORAGE_NODE_NOTE:
    if(node->shared.note.session != NULL)
    {
      if(save_notes)
      {
        infd_directory_node_get_path(node, &path, NULL);

        error = NULL;
        node->shared.note.plugin->session_write(
          priv->storage,
          infd_session_proxy_get_session(node->shared.note.session),
          path,
          node->shared.note.plugin->user_data,
          &error
        );

        /* TODO: Unset modified flag of buffer if result == TRUE */

        if(error != NULL)
        {
          /* There is not really anything we could do about it here. Of
           * course, any application should save the sessions explicitely
           * before shutting the directory down, so that it has the chance to
           * cancel the shutdown if the session could not be saved. */
          /* TODO: We could try saving the session somewhere in /tmp, for
           * example via to_xml_sync. */
          g_warning(
            _("Could not write session \"%s\" to storage: %s\n\nAll changes "
              "since the document das been saved are lost."),
            path,
            error->message
          );

          g_error_free(error);
        }

        g_free(path);
      }

      infd_directory_node_unlink_session(directory, node);
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
infd_directory_node_link(InfdDirectoryNode* node,
                         InfdDirectoryNode* parent)
{
  g_return_if_fail(node != NULL);
  g_return_if_fail(parent != NULL);
  infd_directory_return_if_subdir_fail(parent);

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
    g_assert(node->parent->type == INFD_STORAGE_NODE_SUBDIRECTORY);
    node->parent->shared.subdir.child = node->next;
  }

  if(node->next != NULL)
    node->next->prev = node->prev;
}

/* This function takes ownership of name */
static InfdDirectoryNode*
infd_directory_node_new_common(InfdDirectory* directory,
                               InfdDirectoryNode* parent,
                               InfdStorageNodeType type,
                               guint node_id,
                               gchar* name)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(
    g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id)) == NULL
  );

  node = g_slice_new(InfdDirectoryNode);
  node->parent = parent;
  node->type = type;
  node->id = node_id;
  node->name = name;

  if(parent != NULL)
  {
    infd_directory_node_link(node, parent);
  }
  else
  {
    node->prev = NULL;
    node->next = NULL;
  }

  g_hash_table_insert(priv->nodes, GUINT_TO_POINTER(node->id), node);
  return node;
}

static InfdDirectoryNode*
infd_directory_node_new_subdirectory(InfdDirectory* directory,
                                     InfdDirectoryNode* parent,
                                     guint node_id,
                                     gchar* name)
{
  InfdDirectoryNode* node;

  node = infd_directory_node_new_common(
    directory,
    parent,
    INFD_STORAGE_NODE_SUBDIRECTORY,
    node_id,
    name
  );

  node->shared.subdir.connections = NULL;
  node->shared.subdir.child = NULL;
  node->shared.subdir.explored = FALSE;

  return node;
}

static InfdDirectoryNode*
infd_directory_node_new_note(InfdDirectory* directory,
                             InfdDirectoryNode* parent,
                             guint node_id,
                             gchar* name,
                             const InfdNotePlugin* plugin)
{
  InfdDirectoryNode* node;

  node = infd_directory_node_new_common(
    directory,
    parent,
    INFD_STORAGE_NODE_NOTE,
    node_id,
    name
  );

  node->shared.note.session = NULL;
  node->shared.note.plugin = plugin;
  node->shared.note.save_timeout = NULL;

  return node;
}

/* Required by infd_directory_node_free() */
static void
infd_directory_remove_sync_in(InfdDirectory* directory,
                              InfdDirectorySyncIn* sync_in);
static void
infd_directory_remove_subreq(InfdDirectory* directory,
                             InfdDirectorySubreq* request);

/* Notes are saved into the storage when save_notes is TRUE. */
static void
infd_directory_node_free(InfdDirectory* directory,
                         InfdDirectoryNode* node)
{
  InfdDirectoryPrivate* priv;
  gboolean removed;

  GSList* item;
  GSList* next;
  InfdDirectorySyncIn* sync_in;
  InfdDirectorySubreq* request;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  g_return_if_fail(node != NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  switch(node->type)
  {
  case INFD_STORAGE_NODE_SUBDIRECTORY:
    g_slist_free(node->shared.subdir.connections);

    /* Free child nodes */
    if(node->shared.subdir.explored == TRUE)
    {
      while(node->shared.subdir.child != NULL)
      {
        infd_directory_node_free(directory, node->shared.subdir.child);
      }
    }

    break;
  case INFD_STORAGE_NODE_NOTE:
    /* Sessions must have been explicitely unlinked before, so that the
     * remove-session signal was emitted before any children already have been
     * removed. */
    g_assert(node->shared.note.session == NULL);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  if(node->parent != NULL)
    infd_directory_node_unlink(node);

  /* Remove sync-ins whose parent is gone */
  for(item = priv->sync_ins; item != NULL; item = next)
  {
    next = item->next;
    sync_in = (InfdDirectorySyncIn*)item->data;
    if(sync_in->parent == node)
      infd_directory_remove_sync_in(directory, sync_in);
  }

  for(item = priv->subscription_requests; item != NULL; item = next)
  {
    next = item->next;
    request = (InfdDirectorySubreq*)item->data;

    switch(request->type)
    {
    case INFD_DIRECTORY_SUBREQ_CHAT:
      break;
    case INFD_DIRECTORY_SUBREQ_SESSION:
      /* Keep subscription requests whose parent is gone. They will be
       * released upon client reply. */
      /*if(request->node_id == node->id)
        infd_directory_remove_subreq(directory, request);*/
      break;
    case INFD_DIRECTORY_SUBREQ_ADD_NODE:
      if(request->shared.add_node.parent->id == node->id)
        request->shared.add_node.parent = NULL;
      break;
    case INFD_DIRECTORY_SUBREQ_SYNC_IN:
    case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
      if(request->shared.sync_in.parent->id == node->id)
        request->shared.sync_in.parent = NULL;
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }

  removed = g_hash_table_remove(priv->nodes, GUINT_TO_POINTER(node->id));
  g_assert(removed == TRUE);

  g_free(node->name);
  g_slice_free(InfdDirectoryNode, node);
}

static void
infd_directory_node_remove_connection(InfdDirectoryNode* node,
                                      InfXmlConnection* connection)
{
  InfdDirectoryNode* child;
  GSList* item;

  g_assert(node->type == INFD_STORAGE_NODE_SUBDIRECTORY);
  g_assert(node->shared.subdir.explored == TRUE);

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
        if(child->type == INFD_STORAGE_NODE_SUBDIRECTORY &&
           child->shared.subdir.explored == TRUE)
        {
          infd_directory_node_remove_connection(child, connection);
        }
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

static xmlNodePtr
infd_directory_node_desc_register_to_xml(guint node_id,
                                         InfdDirectoryNode* parent,
                                         const InfdNotePlugin* plugin,
                                         const gchar* name)
{
  xmlNodePtr xml;

  xml = xmlNewNode(NULL, (const xmlChar*)"add-node");

  inf_xml_util_set_attribute_uint(xml, "id", node_id);
  inf_xml_util_set_attribute_uint(xml, "parent", parent->id);
  inf_xml_util_set_attribute(xml, "name", name);

  if(plugin != NULL)
    inf_xml_util_set_attribute(xml, "type", plugin->note_type);
  else
    inf_xml_util_set_attribute(xml, "type", "InfSubdirectory");

  return xml;
}

/* Creates XML request to tell someone about a new node */
static xmlNodePtr
infd_directory_node_register_to_xml(InfdDirectoryNode* node)
{
  const InfdNotePlugin* plugin;

  g_assert(node->parent != NULL);

  switch(node->type)
  {
  case INFD_STORAGE_NODE_SUBDIRECTORY:
    plugin = NULL;
    break;
  case INFD_STORAGE_NODE_NOTE:
    plugin = node->shared.note.plugin;
    break;
  default:
    g_assert_not_reached();
    break;
  }

  return infd_directory_node_desc_register_to_xml(
    node->id,
    node->parent,
    plugin,
    node->name
  );
}

/* Creates XML request to tell someone about a removed node */
static xmlNodePtr
infd_directory_node_unregister_to_xml(InfdDirectoryNode* node)
{
  xmlNodePtr xml;
  gchar id_buf[16];

  sprintf(id_buf, "%u", node->id);

  xml = xmlNewNode(NULL, (const xmlChar*)"remove-node");
  xmlNewProp(xml, (const xmlChar*)"id", (const xmlChar*)id_buf);

  return xml;
}

/* Sends a message to the given connections. We cannot always send to all
 * group members because some messages are only supposed to be sent to
 * clients that explored a certain subdirectory. */
static void
infd_directory_send(InfdDirectory* directory,
                    GSList* connections,
                    InfXmlConnection* exclude,
                    xmlNodePtr xml)
{
  InfdDirectoryPrivate* priv;
  GSList* item;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(connections == NULL ||
     (connections->data == exclude && connections->next == NULL))
  {
    xmlFreeNode(xml);
  }
  else
  {
    for(item = connections; item != NULL; item = g_slist_next(item))
    {
      if(item->data == exclude) continue;

      /* Do not copy this item if it is the last item to be sent because the
       * connection manager takes ownership */
      if(item->next != NULL &&
         (item->next->data != exclude || item->next->next != NULL))
      {
        inf_communication_group_send_message(
          INF_COMMUNICATION_GROUP(priv->group),
          INF_XML_CONNECTION(item->data),
          xmlCopyNode(xml, 1)
        );
      }
      else
      {
        inf_communication_group_send_message(
          INF_COMMUNICATION_GROUP(priv->group),
          INF_XML_CONNECTION(item->data),
          xml
        );
      }
    }
  }
}

/* Announces the presence of a new node. This is not done in
 * infd_directory_node_new because we do not want to do this for all
 * nodes we create (namely not for the root node). */
static void
infd_directory_node_register(InfdDirectory* directory,
                             InfdDirectoryNode* node,
                             InfXmlConnection* except)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryIter iter;
  xmlNodePtr xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);
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
    xml = infd_directory_node_register_to_xml(node);

    infd_directory_send(
      directory,
      node->parent->shared.subdir.connections,
      except,
      xml
    );
  }
}

/* Announces the presence of a new node as a reply to an add-node request
 * from connection with given seq. */
static void
infd_directory_node_register_reply(InfdDirectory* directory,
                                   InfdDirectoryNode* node,
                                   InfXmlConnection* connection,
                                   guint seq)
{
  InfdDirectoryPrivate* priv;
  xmlNodePtr xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  infd_directory_node_register(directory, node, connection);

  xml = infd_directory_node_register_to_xml(node);
  inf_xml_util_set_attribute_uint(xml, "seq", seq);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    xml
  );
}

/* Announces that a node is removed. Again, this is not done in
 * infd_directory_node_free because we do not want to do this for
 * every subnode if a subdirectory is freed. */
static void
infd_directory_node_unregister(InfdDirectory* directory,
                               InfdDirectoryNode* node,
                               InfXmlConnection* seq_conn,
                               guint seq)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryIter iter;
  xmlNodePtr xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  iter.node_id = node->id;
  iter.node = node;

  g_signal_emit(
    G_OBJECT(directory),
    directory_signals[NODE_REMOVED],
    0,
    &iter
  );

  if(seq_conn != NULL)
  {
    xml = infd_directory_node_unregister_to_xml(node);
    inf_xml_util_set_attribute_uint(xml, "seq", seq);

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      seq_conn,
      xml
    );
  }

  if(node->parent->shared.subdir.connections != NULL)
  {
    xml = infd_directory_node_unregister_to_xml(node);

    infd_directory_send(
      directory,
      node->parent->shared.subdir.connections,
      seq_conn,
      xml
    );
  }
}

static gboolean
infd_directory_node_name_equal(const gchar* name1,
                               const gchar* name2)
{
  gchar* f1 = g_utf8_casefold(name1, -1);
  gchar* f2 = g_utf8_casefold(name2, -1);
  gboolean result = (g_utf8_collate(f1, f2) == 0);
  g_free(f2);
  g_free(f1);
  return result;
}

/*
 * Sync-In
 */

static void
infd_directory_sync_in_synchronization_failed_cb(InfSession* session,
                                                 InfXmlConnection* connection,
                                                 const GError* error,
                                                 gpointer user_data)
{
  /* Synchronization failed. We simple remove the sync-in. There is no further
   * notification required since the synchronization failed on the remote site
   * as well. */
  InfdDirectorySyncIn* sync_in;
  sync_in = (InfdDirectorySyncIn*)user_data;

  infd_directory_remove_sync_in(sync_in->directory, sync_in);
}

static void
infd_directory_sync_in_synchronization_complete_cb(InfSession* session,
                                                   InfXmlConnection* conn,
                                                   gpointer user_data)
{
  /* Synchronization done. We can now safely create the node in the directory
   * tree. */
  InfdDirectorySyncIn* sync_in;
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  gchar* path;
  gboolean ret;
  GError* error;

  sync_in = (InfdDirectorySyncIn*)user_data;
  directory = sync_in->directory;
  priv = INFD_DIRECTORY_PRIVATE(directory);

  node = infd_directory_node_new_note(
    directory,
    sync_in->parent,
    sync_in->node_id,
    sync_in->name,
    sync_in->plugin
  );

  infd_directory_node_link_session(directory, node, sync_in->proxy);

  /* Save session initially */
  infd_directory_node_get_path(node, &path, NULL);

  error = NULL;
  ret = sync_in->plugin->session_write(
    priv->storage,
    session,
    path,
    sync_in->plugin->user_data,
    &error
  );

  if(ret == FALSE)
  {
    /* Note that while indeed this may fail in theory we have already
     * (successfully) written the session before we started the sync-in, so
     * the name of the node is accepted by the storage backend. */
    g_warning(
      _("Session \"%s\" could not be saved: %s\nAnother attempt will "
        "be made when the session is unused for a while or the server is "
        "shut down."),
      path,
      error->message
    );

    g_error_free(error);
  }

  g_free(path);

  sync_in->name = NULL; /* Don't free, we passed ownership */
  infd_directory_remove_sync_in(directory, sync_in);

  /* Don't send to conn since the completed synchronization already lets the
   * remote site know that the node was inserted. */
  infd_directory_node_register(directory, node, conn);
}

static InfdDirectorySyncIn*
infd_directory_add_sync_in(InfdDirectory* directory,
                           InfdDirectoryNode* parent,
                           guint node_id,
                           const gchar* name,
                           const InfdNotePlugin* plugin,
                           InfdSessionProxy* proxy)
{
  InfdDirectoryPrivate* priv;
  InfdDirectorySyncIn* sync_in;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  sync_in = g_slice_new(InfdDirectorySyncIn);

  sync_in->directory = directory;
  sync_in->parent = parent;
  sync_in->node_id = node_id;
  sync_in->name = g_strdup(name);
  sync_in->plugin = plugin;
  sync_in->proxy = proxy;
  g_object_ref(sync_in->proxy);

  g_signal_connect(
    G_OBJECT(infd_session_proxy_get_session(sync_in->proxy)),
    "synchronization-failed",
    G_CALLBACK(infd_directory_sync_in_synchronization_failed_cb),
    sync_in
  );

  g_signal_connect(
    G_OBJECT(infd_session_proxy_get_session(sync_in->proxy)),
    "synchronization-complete",
    G_CALLBACK(infd_directory_sync_in_synchronization_complete_cb),
    sync_in
  );

  priv->sync_ins = g_slist_prepend(priv->sync_ins, sync_in);
  return sync_in;
}

static void
infd_directory_remove_sync_in(InfdDirectory* directory,
                              InfdDirectorySyncIn* sync_in)
{
  InfdDirectoryPrivate* priv;
  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(infd_session_proxy_get_session(sync_in->proxy)),
    G_CALLBACK(infd_directory_sync_in_synchronization_failed_cb),
    sync_in
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(infd_session_proxy_get_session(sync_in->proxy)),
    G_CALLBACK(infd_directory_sync_in_synchronization_complete_cb),
    sync_in
  );

  /* This cancels the synchronization: */
  g_object_unref(sync_in->proxy);

  g_free(sync_in->name);
  g_slice_free(InfdDirectorySyncIn, sync_in);

  priv->sync_ins = g_slist_remove(priv->sync_ins, sync_in);
}

static InfdDirectorySyncIn*
infd_directory_find_sync_in_by_name(InfdDirectory* directory,
                                    InfdDirectoryNode* parent,
                                    const gchar* name)
{
  InfdDirectoryPrivate* priv;
  GSList* item;
  InfdDirectorySyncIn* sync_in;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  for(item = priv->sync_ins; item != NULL; item = item->next)
  {
    sync_in = (InfdDirectorySyncIn*)item->data;
    if(sync_in->parent == parent &&
       infd_directory_node_name_equal(sync_in->name, name) == TRUE)
    {
      return sync_in;
    }
  }

  return NULL;
}

/*
 * Subscription requests.
 */

static InfdDirectorySubreq*
infd_directory_add_subreq_common(InfdDirectory* directory,
                                 InfdDirectorySubreqType type,
                                 InfXmlConnection* connection,
                                 guint node_id)
{
  InfdDirectoryPrivate* priv;
  InfdDirectorySubreq* request;
  
  priv = INFD_DIRECTORY_PRIVATE(directory);
  request = g_slice_new(InfdDirectorySubreq);

  request->type = type;
  request->connection = connection;
  request->node_id = node_id;

  priv->subscription_requests =
    g_slist_prepend(priv->subscription_requests, request);

  return request;
}

static InfdDirectorySubreq*
infd_directory_add_subreq_chat(InfdDirectory* directory,
                               InfXmlConnection* connection)
{
  InfdDirectorySubreq* request;

  request = infd_directory_add_subreq_common(
    directory,
    INFD_DIRECTORY_SUBREQ_CHAT,
    connection,
    0
  );

  return request;
}

static InfdDirectorySubreq*
infd_directory_add_subreq_session(InfdDirectory* directory,
                                  InfXmlConnection* connection,
                                  guint node_id,
                                  InfdSessionProxy* proxy)
{
  InfdDirectorySubreq* request;

  request = infd_directory_add_subreq_common(
    directory,
    INFD_DIRECTORY_SUBREQ_SESSION,
    connection,
    node_id
  );

  request->shared.session.session = proxy; /* take ownership */
  return request;
}

static InfdDirectorySubreq*
infd_directory_add_subreq_add_node(InfdDirectory* directory,
                                   InfXmlConnection* connection,
                                   guint node_id,
                                   InfdDirectoryNode* parent,
                                   InfCommunicationHostedGroup* group,
                                   const InfdNotePlugin* plugin,
                                   const gchar* name,
                                   GError** error)
{
  InfdDirectorySubreq* request;
  InfdSessionProxy* proxy;

  proxy = infd_directory_create_session_proxy_for_storage(
    directory,
    parent,
    group,
    name,
    plugin,
    INF_SESSION_RUNNING,
    NULL,
    NULL,
    error
  );

  if(proxy == NULL)
    return NULL;

  request = infd_directory_add_subreq_common(
    directory,
    INFD_DIRECTORY_SUBREQ_ADD_NODE,
    connection,
    node_id
  );

  request->shared.add_node.parent = parent;
  request->shared.add_node.group = group;
  request->shared.add_node.plugin = plugin;
  request->shared.add_node.name = g_strdup(name);
  request->shared.add_node.proxy = proxy;

  g_object_ref(group);
  return request;
}

static InfdDirectorySubreq*
infd_directory_add_subreq_sync_in(InfdDirectory* directory,
                                  InfXmlConnection* connection,
                                  guint node_id,
                                  InfdDirectoryNode* parent,
                                  InfCommunicationHostedGroup* sync_group,
                                  InfCommunicationHostedGroup* sub_group,
                                  const InfdNotePlugin* plugin,
                                  const gchar* name,
                                  GError** error)
{
  InfdDirectorySubreq* request;
  InfdSessionProxy* proxy;

  /* Keep proxy in PRESYNC state, until we have the confirmation from the
   * remote site that the chosen method is OK and we can go on. */
  proxy = infd_directory_create_session_proxy_for_storage(
    directory,
    parent,
    sub_group,
    name,
    plugin,
    INF_SESSION_PRESYNC,
    sync_group,
    connection,
    error
  );

  if(proxy == NULL)
    return NULL;

  request = infd_directory_add_subreq_common(
    directory,
    sync_group == sub_group ?
      INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE :
      INFD_DIRECTORY_SUBREQ_SYNC_IN,
    connection,
    node_id
  );

  request->shared.sync_in.parent = parent;
  request->shared.sync_in.synchronization_group = sync_group;
  request->shared.sync_in.subscription_group = sub_group;
  request->shared.sync_in.plugin = plugin;
  request->shared.sync_in.name = g_strdup(name);
  request->shared.sync_in.proxy = proxy;

  g_object_ref(sync_group);
  g_object_ref(sub_group);

  return request;
}

static void
infd_directory_free_subreq(InfdDirectorySubreq* request)
{
  switch(request->type)
  {
  case INFD_DIRECTORY_SUBREQ_CHAT:
    break;
  case INFD_DIRECTORY_SUBREQ_SESSION:
    g_object_unref(request->shared.session.session);
    break;
  case INFD_DIRECTORY_SUBREQ_ADD_NODE:
    g_free(request->shared.add_node.name);
    g_object_unref(request->shared.add_node.group);
    g_object_unref(request->shared.add_node.proxy);
    break;
  case INFD_DIRECTORY_SUBREQ_SYNC_IN:
  case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
    g_free(request->shared.sync_in.name);
    g_object_unref(request->shared.sync_in.synchronization_group);
    g_object_unref(request->shared.sync_in.subscription_group);
    g_object_unref(request->shared.sync_in.proxy);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_slice_free(InfdDirectorySubreq, request);
}

static void
infd_directory_unlink_subreq(InfdDirectory* directory,
                             InfdDirectorySubreq* request)
{
  InfdDirectoryPrivate* priv;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  priv->subscription_requests =
    g_slist_remove(priv->subscription_requests, request);
}

static void
infd_directory_remove_subreq(InfdDirectory* directory,
                             InfdDirectorySubreq* request)
{
  infd_directory_unlink_subreq(directory, request);
  infd_directory_free_subreq(request);
}

static InfdDirectorySubreq*
infd_directory_find_subreq_by_name(InfdDirectory* directory,
                                   InfdDirectoryNode* parent,
                                   const gchar* name)
{
  InfdDirectoryPrivate* priv;
  GSList* item;
  InfdDirectorySubreq* request;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  for(item = priv->subscription_requests; item != NULL; item = item->next)
  {
    request = (InfdDirectorySubreq*)item->data;

    switch(request->type)
    {
    case INFD_DIRECTORY_SUBREQ_CHAT:
    case INFD_DIRECTORY_SUBREQ_SESSION:
      /* These don't occupy names */
      break;
    case INFD_DIRECTORY_SUBREQ_ADD_NODE:
      if(request->shared.add_node.parent == parent &&
         infd_directory_node_name_equal(request->shared.add_node.name, name))
      {
        return request;
      }
      break;
    case INFD_DIRECTORY_SUBREQ_SYNC_IN:
    case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
      if(request->shared.sync_in.parent == parent &&
         infd_directory_node_name_equal(request->shared.sync_in.name, name))
      {
        return request;
      }
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }

  return NULL;
}

/*
 * Directory tree operations.
 */

static InfdDirectoryNode*
infd_directory_node_find_child_by_name(InfdDirectoryNode* parent,
                                       const gchar* name)
{
  InfdDirectoryNode* node;

  infd_directory_return_val_if_subdir_fail(parent, NULL);

  for(node = parent->shared.subdir.child; node != NULL; node = node->next)
    if(infd_directory_node_name_equal(node->name, name))
      return node;

  return NULL;
}

/* Checks whether a node with the given name can be created in the given
 * parent directory. */
static gboolean
infd_directory_node_is_name_available(InfdDirectory* directory,
                                      InfdDirectoryNode* parent,
                                      const gchar* name,
                                      GError** error)
{
  if(strchr(name, '/') != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_INVALID_NAME,
      "Name \"%s\" is an invalid name: contains \"/\"",
      name
    );

    return FALSE;
  }

  if(infd_directory_node_find_child_by_name(parent, name)         != NULL ||
     infd_directory_find_sync_in_by_name(directory, parent, name) != NULL ||
     infd_directory_find_subreq_by_name(directory, parent, name)  != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NODE_EXISTS,
      "A node with name \"%s\" exists already",
      name
    );

    return FALSE;
  }

  return TRUE;
}

static gboolean
infd_directory_node_explore(InfdDirectory* directory,
                            InfdDirectoryNode* node,
                            GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdStorageNode* storage_node;
  InfdDirectoryNode* new_node;
  InfdNotePlugin* plugin;
  GError* local_error;
  GSList* list;
  GSList* item;
  gchar* path;
  gsize len;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(priv->storage != NULL);
  g_assert(node->type == INFD_STORAGE_NODE_SUBDIRECTORY);
  g_assert(node->shared.subdir.explored == FALSE);

  local_error = NULL;
  infd_directory_node_get_path(node, &path, &len);
  list = infd_storage_read_subdirectory(priv->storage, path, &local_error);
  g_free(path);

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  for(item = list; item != NULL; item = g_slist_next(item))
  {
    storage_node = (InfdStorageNode*)item->data;
    new_node = NULL;

    /* TODO: Transfer ownership of storade_node->name to
     * infd_directory_new_*? */
    switch(storage_node->type)
    {
    case INFD_STORAGE_NODE_SUBDIRECTORY:
      new_node = infd_directory_node_new_subdirectory(
        directory,
        node,
        priv->node_counter ++,
        g_strdup(storage_node->name)
      );
      
      break;
    case INFD_STORAGE_NODE_NOTE:
      /* TODO: Currently we ignore notes of unknown type. Perhaps we should
       * report some error. */
      plugin = g_hash_table_lookup(priv->plugins, storage_node->identifier);
      if(plugin != NULL)
      {
        new_node = infd_directory_node_new_note(
          directory,
          node,
          priv->node_counter ++,
          g_strdup(storage_node->name),
          plugin
        );
      }

      break;
    default:
      g_assert_not_reached();
      break;
    }

    if(new_node != NULL)
    {
      /* Announce the new node. In most cases, this does nothing on the
       * network because there are no connections that have this node open
       * (otherwise, we would already have explored the node earlier).
       * However, if the background storage is replaced by a new one, the root
       * folder of the new storage will be explored immediately (see below in
       * infd_directory_set_storage()) and there might still be connections
       * interesting in root folder changes (because they opened the root
       * folder from the old storage). Also, local users might be interested
       * in the new node. */
      infd_directory_node_register(directory, new_node, NULL);
    }
  }

  infd_storage_node_list_free(list);

  node->shared.subdir.explored = TRUE;
  return TRUE;
}

static InfdDirectoryNode*
infd_directory_node_add_subdirectory(InfdDirectory* directory,
                                     InfdDirectoryNode* parent,
                                     const gchar* name,
                                     InfXmlConnection* seq_conn,
                                     guint seq,
                                     GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  gboolean result;
  gchar* path;

  infd_directory_return_val_if_subdir_fail(parent, NULL);
  g_return_val_if_fail(parent->shared.subdir.explored == TRUE, NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_assert(priv->storage != NULL);

  if(!infd_directory_node_is_name_available(directory, parent, name, error))
  {
    return NULL;
  }
  else
  {
    infd_directory_node_make_path(parent, name, &path, NULL);

    result = infd_storage_create_subdirectory(priv->storage, path, error);

    g_free(path);
    if(result == FALSE) return NULL;

    node = infd_directory_node_new_subdirectory(
      directory,
      parent,
      priv->node_counter ++,
      g_strdup(name)
    );

    if(seq_conn != NULL)
      infd_directory_node_register_reply(directory, node, seq_conn, seq);
    else
      infd_directory_node_register(directory, node, NULL);

    return node;
  }
}

static InfdDirectoryNode*
infd_directory_node_create_new_note(InfdDirectory* directory,
                                    InfdDirectoryNode* parent,
                                    InfCommunicationHostedGroup* group,
                                    guint node_id,
                                    const gchar* name,
                                    const InfdNotePlugin* plugin,
                                    GError** error)
{
  InfdSessionProxy* proxy;
  InfdDirectoryNode* node;

  proxy = infd_directory_create_session_proxy_for_storage(
    directory,
    parent,
    group,
    name,
    plugin,
    INF_SESSION_RUNNING,
    NULL,
    NULL,
    error
  );

  if(proxy == NULL)
    return NULL;

  node = infd_directory_node_new_note(
    directory,
    parent,
    node_id,
    g_strdup(name),
    plugin
  );

  infd_directory_node_link_session(directory, node, proxy);
  g_object_unref(proxy);

  return node;
}

static gboolean
infd_directory_node_add_note(InfdDirectory* directory,
                             InfdDirectoryNode* parent,
                             const gchar* name,
                             const InfdNotePlugin* plugin,
                             InfXmlConnection* seq_conn,
                             guint seq,
                             gboolean subscribe_seq_conn,
                             GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  guint node_id;
  InfCommunicationHostedGroup* group;
  xmlNodePtr xml;
  xmlNodePtr child;
  const gchar* method;
  InfdDirectorySubreq* subreq;
  gboolean result;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_subdir_fail(parent, FALSE);
  g_return_val_if_fail(parent->shared.subdir.explored == TRUE, FALSE);

  if(!infd_directory_node_is_name_available(directory, parent, name, error))
  {
    return FALSE;
  }
  else
  {
    node_id = priv->node_counter++;
    group = infd_directory_create_subscription_group(directory, node_id);

    if(seq_conn != NULL && subscribe_seq_conn == TRUE)
    {
      subreq = infd_directory_add_subreq_add_node(
        directory,
        seq_conn,
        node_id,
        parent,
        group,
        plugin,
        name,
        error
      );

      if(subreq != NULL)
      {
        xml = infd_directory_node_desc_register_to_xml(
          node_id,
          parent,
          plugin,
          name
        );

        inf_xml_util_set_attribute_uint(xml, "seq", seq);

        child = xmlNewChild(xml, NULL, (const xmlChar*)"subscribe", NULL);
        inf_xml_util_set_attribute(
          child,
          "group",
          inf_communication_group_get_name(INF_COMMUNICATION_GROUP(group))
        );

        method = inf_communication_group_get_method_for_connection(
          INF_COMMUNICATION_GROUP(group),
          seq_conn
        );

        /* "central" method should always be available */
        g_assert(method != NULL);

        inf_xml_util_set_attribute(child, "method", method);

        inf_communication_group_send_message(
          INF_COMMUNICATION_GROUP(priv->group),
          seq_conn,
          xml
        );

        result = TRUE;
      }
    }
    else
    {
      node = infd_directory_node_create_new_note(
        directory,
        parent,
        group,
        node_id,
        name,
        plugin,
        error
      );

      if(node != NULL)
      {
        if(seq_conn != NULL && subscribe_seq_conn == FALSE)
        {
          infd_directory_node_register_reply(directory, node, seq_conn, seq);
        }
        else if(seq_conn == NULL)
        {
          infd_directory_node_register(directory, node, NULL);
        }
        else
        {
          g_assert_not_reached();
        }

        result = TRUE;
      }
      else
      {
        result = FALSE;
      }
    }

    g_object_unref(group);

    return result;
  }
}

static gboolean
infd_directory_node_add_sync_in(InfdDirectory* directory,
                                InfdDirectoryNode* parent,
                                const gchar* name,
                                const InfdNotePlugin* plugin,
                                InfXmlConnection* sync_conn,
                                gboolean subscribe_sync_conn,
                                guint seq,
                                GError** error)
{
  InfdDirectoryPrivate* priv;
  InfCommunicationHostedGroup* subscription_group;
  InfCommunicationHostedGroup* synchronization_group;
  guint node_id;

  gchar* sync_group_name;
  const gchar* method;
  InfdDirectorySubreq* subreq;

  xmlNodePtr xml;
  xmlNodePtr child;

  /* Synchronization is always between only two peers, so central method
   * is enough. */
  static const gchar* const sync_methods[] = { "central", NULL };

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(!infd_directory_node_is_name_available(directory, parent, name, error))
    return FALSE;

  node_id = priv->node_counter++;

  subscription_group =
    infd_directory_create_subscription_group(directory, node_id);

  if(subscribe_sync_conn == TRUE)
  {
    synchronization_group = subscription_group;
    g_object_ref(synchronization_group);
  }
  else
  {
    sync_group_name = g_strdup_printf("InfSession_SyncIn_%u", node_id);

    synchronization_group = inf_communication_manager_open_group(
      priv->communication_manager,
      sync_group_name,
      sync_methods
    );

    g_free(sync_group_name);
  }

  method = inf_communication_group_get_method_for_connection(
    INF_COMMUNICATION_GROUP(synchronization_group),
    sync_conn
  );

  /* "central" should always be available */
  g_assert(method != NULL);

  subreq = infd_directory_add_subreq_sync_in(
    directory,
    sync_conn,
    node_id,
    parent,
    synchronization_group,
    subscription_group,
    plugin,
    name,
    error
  );

  if(subreq == NULL)
  {
    g_object_unref(synchronization_group);
    g_object_unref(subscription_group);
    return FALSE;
  }

  xml = xmlNewNode(NULL, (const xmlChar*)"sync-in");
  inf_xml_util_set_attribute_uint(xml, "id", node_id);
  inf_xml_util_set_attribute_uint(xml, "parent", parent->id);

  inf_xml_util_set_attribute(
    xml,
    "group",
    inf_communication_group_get_name(
      INF_COMMUNICATION_GROUP(synchronization_group)
    )
  );

  inf_xml_util_set_attribute(xml, "method", method);
  if(seq != 0) inf_xml_util_set_attribute_uint(xml, "seq", seq);

  inf_xml_util_set_attribute(xml, "name", name);
  inf_xml_util_set_attribute(xml, "type", plugin->note_type);

  if(subscribe_sync_conn == TRUE)
  {
    /* Note that if subscribe_sync_conn is set, then sync_group is the same
     * as the subscription group, so we don't need to query the subscription
     * group here. */
    child = xmlNewChild(xml, NULL, (const xmlChar*)"subscribe", NULL);
    inf_xml_util_set_attribute(child, "method", method);

    inf_xml_util_set_attribute(
      child,
      "group",
      inf_communication_group_get_name(
        INF_COMMUNICATION_GROUP(subscription_group)
      )
    );
  }

  g_object_unref(synchronization_group);
  g_object_unref(subscription_group);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    sync_conn,
    xml
  );

  return TRUE;
}

static gboolean
infd_directory_node_remove(InfdDirectory* directory,
                           InfdDirectoryNode* node,
                           InfXmlConnection* seq_conn,
                           guint seq,
                           GError** error)
{
  InfdDirectoryPrivate* priv;
  gboolean result;
  gchar* path;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_assert(priv->storage != NULL);

  /* Cannot remove the root node */
  g_assert(node->parent != NULL);

  infd_directory_node_get_path(node, &path, NULL);
  result = infd_storage_remove_node(
    priv->storage,
    node->type == INFD_STORAGE_NODE_NOTE ?
      node->shared.note.plugin->note_type :
      NULL,
    path,
    error
  );

  g_free(path);

  if(result == FALSE)
    return FALSE;

  /* Need to unlink child sessions explicitely before unregistering, so
   * remove-session is emitted before node-removed. Don't save changes since
   * we just removed the note anyway. */
  infd_directory_node_unlink_child_sessions(directory, node, FALSE);
  infd_directory_node_unregister(directory, node, seq_conn, seq);
  infd_directory_node_free(directory, node);
  return TRUE;
}

/* Returns the session for the given node. This does not link the session
 * (if it isn't already). This means that the next time this function is
 * called, the session will be created again if you don't link it yourself,
 * or if you don't create a subscription request for it. Unref the result.
 * See infd_directory_node_get_and_link_session() to get a linked session. */
static InfdSessionProxy*
infd_directory_node_get_session(InfdDirectory* directory,
                                InfdDirectoryNode* node,
                                GError** error)
{
  InfdDirectoryPrivate* priv;
  InfSession* session;
  GSList* item;
  InfdDirectorySubreq* request;
  InfdSessionProxy* proxy;
  gchar* path;

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_assert(priv->storage != NULL);

  if(node->shared.note.session != NULL)
  {
    proxy = node->shared.note.session;

    g_object_ref(proxy);
    return proxy;
  }

  /* The session could already exist in a subscribe-session subreq */
  proxy = NULL;
  for(item = priv->subscription_requests; item != NULL; item = item->next)
  {
    request = (InfdDirectorySubreq*)item->data;
    if(request->type == INFD_DIRECTORY_SUBREQ_SESSION)
    {
      if(request->node_id == node->id)
      {
        proxy = request->shared.session.session;
        break;
      }
    }
  }

  if(proxy != NULL)
  {
    g_object_ref(proxy);
    return proxy;
  }

  infd_directory_node_get_path(node, &path, NULL);
  session = node->shared.note.plugin->session_read(
    priv->storage,
    priv->io,
    priv->communication_manager,
    path,
    node->shared.note.plugin->user_data,
    error
  );
  g_free(path);
  if(session == NULL) return NULL;

  /* Buffer might have been marked as modified while reading the session, but
   * as we just read it from the storage, we don't consider it modified. */
  inf_buffer_set_modified(inf_session_get_buffer(session), FALSE);

  proxy = infd_directory_create_session_proxy_for_node(
    directory,
    node->id,
    session
  );

  g_object_unref(session);

  return proxy;
}

static InfdSessionProxy*
infd_directory_node_get_and_link_session(InfdDirectory* directory,
                                         InfdDirectoryNode* node,
                                         GError** error)
{
  InfdSessionProxy* proxy;
  g_assert(node->type == INFD_STORAGE_NODE_NOTE);

  proxy = infd_directory_node_get_session(directory, node, error);
  if(!proxy) return NULL;

  infd_directory_node_link_session(directory, node, proxy);
  g_object_unref(proxy);

  return node->shared.note.session;
}

/*
 * Network command handling.
 */

static void
infd_directory_send_welcome_message(InfdDirectory* directory,
                                    InfXmlConnection* connection)
{
  InfdDirectoryPrivate* priv;
  xmlNodePtr xml;
  xmlNodePtr plugins;
  xmlNodePtr child;
  GHashTableIter iter;
  gpointer value;
  const InfdNotePlugin* plugin;
  InfdDirectoryConnectionInfo* info;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  xml = xmlNewNode(NULL, (const xmlChar*) "welcome");
  inf_xml_util_set_attribute(
    xml,
    "protocol-version",
    inf_protocol_get_version());

  info = g_hash_table_lookup(priv->connections, connection);
  g_assert(info != NULL);

  inf_xml_util_set_attribute_uint(xml, "sequence-id", info->seq_id);

  plugins = xmlNewChild(xml, NULL, (const xmlChar*) "note-plugins", NULL);

  g_hash_table_iter_init(&iter, priv->plugins);
  while(g_hash_table_iter_next(&iter, NULL, &value))
  {
    plugin = (const InfdNotePlugin*)value;

    child = xmlNewChild(plugins, NULL, (const xmlChar*) "note-plugin", NULL);
    inf_xml_util_set_attribute(child, "type", plugin->note_type);
  }

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    xml);
}

static InfdDirectorySubreq*
infd_directory_get_subreq_from_xml(InfdDirectory* directory,
                                   InfXmlConnection* connection,
                                   xmlNodePtr xml,
                                   GError** error)
{
  InfdDirectoryPrivate* priv;
  guint node_id;
  gboolean has_node;
  GSList* item;
  InfdDirectorySubreq* request;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  has_node = inf_xml_util_get_attribute_uint(
    xml,
    "id",
    &node_id,
    error
  );

  if(has_node == FALSE)
  {
    /* subscription requests without node ID are for server chat */
    for(item = priv->subscription_requests; item != NULL; item = item->next)
    {
      request = (InfdDirectorySubreq*)item->data;
      if(request->type == INFD_DIRECTORY_SUBREQ_CHAT)
        return request;
    }

    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NO_SUCH_SUBSCRIPTION_REQUEST,
      _("No subscription request for the server chat")
    );
  }
  else
  {
    for(item = priv->subscription_requests; item != NULL; item = item->next)
    {
      request = (InfdDirectorySubreq*)item->data;
      if(request->type != INFD_DIRECTORY_SUBREQ_CHAT &&
         request->connection == connection && request->node_id == node_id)
      {
        return request;
      }
    }

    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NO_SUCH_SUBSCRIPTION_REQUEST,
      _("No subscription request with ID %u"),
      node_id
    );
  }

  return NULL;
}

static InfdDirectoryNode*
infd_directory_get_node_from_xml(InfdDirectory* directory,
                                 xmlNodePtr xml,
                                 const gchar* attrib,
                                 GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  guint node_id;
  gboolean has_node;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  has_node = inf_xml_util_get_attribute_uint_required(
    xml,
    attrib,
    &node_id,
    error
  );

  if(has_node == FALSE) return NULL;

  node = g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id));
  if(node == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NO_SUCH_NODE,
      "%s",
      inf_directory_strerror(INF_DIRECTORY_ERROR_NO_SUCH_NODE)
    );

    return NULL;
  }

  return node;
}

static InfdDirectoryNode*
infd_directory_get_node_from_xml_typed(InfdDirectory* directory,
                                       xmlNodePtr xml,
                                       const gchar* attrib,
                                       InfdStorageNodeType type,
                                       GError** error)
{
  InfdDirectoryNode* node;
  node = infd_directory_get_node_from_xml(directory, xml, attrib, error);

  if(node != NULL && node->type != type)
  {
    switch(type)
    {
    case INFD_STORAGE_NODE_SUBDIRECTORY:
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY,
        "%s",
        inf_directory_strerror(INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY)
      );

      return NULL;
    case INFD_STORAGE_NODE_NOTE:
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NOT_A_NOTE,
        "%s",
        inf_directory_strerror(INF_DIRECTORY_ERROR_NOT_A_NOTE)
      );

      return NULL;
    default:
      g_assert_not_reached();
      return NULL;
    }
  }

  return node;
}

static gboolean
infd_directory_handle_explore_node(InfdDirectory* directory,
                                   InfXmlConnection* connection,
                                   const xmlNodePtr xml,
                                   GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryNode* child;
  xmlChar* seq_attr;
  xmlNodePtr reply_xml;
  guint total;
  gchar total_str[16];

  priv = INFD_DIRECTORY_PRIVATE(directory);

  node = infd_directory_get_node_from_xml_typed(
    directory,
    xml,
    "id",
    INFD_STORAGE_NODE_SUBDIRECTORY,
    error
  );

  if(node->shared.subdir.explored == FALSE)
    if(infd_directory_node_explore(directory, node, error) == FALSE)
      return FALSE;

  if(g_slist_find(node->shared.subdir.connections, connection) != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ALREADY_EXPLORED,
      "%s",
      inf_directory_strerror(INF_DIRECTORY_ERROR_ALREADY_EXPLORED)
    );

    return FALSE;
  }

  total = 0;
  for(child = node->shared.subdir.child; child != NULL; child = child->next)
    ++ total;
  sprintf(total_str, "%u", total);

  seq_attr = xmlGetProp(xml, (const xmlChar*)"seq");
  reply_xml = xmlNewNode(NULL, (const xmlChar*)"explore-begin");
  xmlNewProp(reply_xml, (const xmlChar*)"total", (const xmlChar*)total_str);
  if(seq_attr != NULL)
    xmlNewProp(reply_xml, (const xmlChar*)"seq", seq_attr);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  for(child = node->shared.subdir.child; child != NULL; child = child->next)
  {
    reply_xml = infd_directory_node_register_to_xml(child);
    if(seq_attr != NULL)
      xmlNewProp(reply_xml, (const xmlChar*)"seq", seq_attr);

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      connection,
      reply_xml
    );
  }

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"explore-end");

  if(seq_attr != NULL)
  {
    xmlNewProp(reply_xml, (const xmlChar*)"seq", seq_attr);
    xmlFree(seq_attr);
  }

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  /* Remember that this connection explored that node so that it gets
   * notified when changes occur. */
  node->shared.subdir.connections = g_slist_prepend(
    node->shared.subdir.connections,
    connection
  );

  return TRUE;
}

static gboolean
infd_directory_handle_add_node(InfdDirectory* directory,
                               InfXmlConnection* connection,
                               const xmlNodePtr xml,
                               GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* parent;
  InfdDirectoryNode* node;
  InfdNotePlugin* plugin;
  xmlChar* name;
  xmlChar* type;
  gboolean has_seq;
  guint seq;
  GError* local_error;

  xmlNodePtr child;
  gboolean perform_sync_in;
  gboolean subscribe_sync_conn;
  gboolean node_added;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  parent = infd_directory_get_node_from_xml_typed(
    directory,
    xml,
    "parent",
    INFD_STORAGE_NODE_SUBDIRECTORY,
    error
  );

  if(parent == NULL)
    return FALSE;

  type = inf_xml_util_get_attribute_required(xml, "type", error);
  if(type == NULL) return FALSE;

  if(strcmp((const gchar*)type, "InfSubdirectory") == 0)
  {
    /* No plugin because we want to create a directory */
    plugin = NULL;
    xmlFree(type);
  }
  else
  {
    plugin = g_hash_table_lookup(priv->plugins, (const gchar*)type);
    xmlFree(type);

    if(plugin == NULL)
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_TYPE_UNKNOWN,
        "%s",
        inf_directory_strerror(INF_DIRECTORY_ERROR_TYPE_UNKNOWN)
      );

      return FALSE;
    }
  }

  local_error = NULL;
  has_seq = inf_xml_util_get_attribute_uint(xml, "seq", &seq, &local_error);
  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  name = inf_xml_util_get_attribute_required(xml, "name", error);
  if(name == NULL) return FALSE;

  /* Note that seq is only passed uninitialized here if it is not used
   * anyway because seq_conn is NULL. */
  if(plugin == NULL)
  {
    node = infd_directory_node_add_subdirectory(
      directory,
      parent,
      (const gchar*)name,
      (has_seq == TRUE) ? connection : NULL,
      seq,
      error
    );

    if(node == NULL)
      node_added = FALSE;
    else
      node_added = TRUE;
  }
  else
  {
    /* Check for sync-in/subscribe flags */
    perform_sync_in = subscribe_sync_conn = FALSE;
    for(child = xml->children; child != NULL; child = child->next)
    {
      if(strcmp((const char*)child->name, "sync-in") == 0)
        perform_sync_in = TRUE;
      else if(strcmp((const char*)child->name, "subscribe") == 0)
        subscribe_sync_conn = TRUE;
    }

    if(perform_sync_in == FALSE)
    {
      node_added = infd_directory_node_add_note(
        directory,
        parent,
        (const gchar*)name,
        plugin,
        (has_seq == TRUE) ? connection : NULL,
        seq,
        subscribe_sync_conn,
        error
      );
    }
    else
    {
      node_added = infd_directory_node_add_sync_in(
        directory,
        parent,
        (const char*)name,
        plugin,
        connection,
        subscribe_sync_conn,
        (has_seq == TRUE) ? seq : 0,
        error
      );

      /* Note: The sync-in can still fail for various reasons. Maybe the
       * client doesn't support the communication method, the synchronization
       * fails or the parent folder is removed. */
    }
  }

  xmlFree(name);
  return node_added;
}

static gboolean
infd_directory_handle_remove_node(InfdDirectory* directory,
                                  InfXmlConnection* connection,
                                  const xmlNodePtr xml,
                                  GError** error)
{
  InfdDirectoryNode* node;
  guint seq;
  gboolean result;
  gboolean has_seq;

  node = infd_directory_get_node_from_xml(directory, xml, "id", error);
  if(node == NULL) return FALSE;

  if(node->parent == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ROOT_NODE_REMOVE_ATTEMPT,
      _("The root node cannot be removed")
    );

    return FALSE;
  }
  else
  {
    has_seq =
      inf_xml_util_get_attribute_uint_required(xml, "seq", &seq, error);

    /* Note that seq is only passed uninitialized here if it is not used
     * anyway because seq_conn is NULL. */
    result = infd_directory_node_remove(
      directory,
      node,
      (has_seq == TRUE) ? connection : NULL,
      seq,
      error
    );

    return result;
  }
}

static gboolean
infd_directory_handle_subscribe_session(InfdDirectory* directory,
                                        InfXmlConnection* connection,
                                        xmlNodePtr xml,
                                        GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdSessionProxy* proxy;
  InfCommunicationGroup* group;
  const gchar* method;
  xmlChar* seq_attr;
  xmlNodePtr reply_xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  node = infd_directory_get_node_from_xml_typed(
    directory,
    xml,
    "id",
    INFD_STORAGE_NODE_NOTE,
    error
  );

  if(node == NULL)
    return FALSE;

  /* TODO: Bail if this connection is either currently being synchronized to
   * or is already subscribed */
  /* TODO: Bail if a subscription request for this connection is pending. */

  proxy = infd_directory_node_get_session(directory, node, error);
  if(proxy == NULL)
    return FALSE;

  g_object_get(G_OBJECT(proxy), "subscription-group", &group, NULL);

  method = inf_communication_group_get_method_for_connection(
    group,
    connection
  );

  /* We should always be able to fallback to "central" */
  g_assert(method != NULL);

  /* Reply that subscription was successful (so far, synchronization may
   * still fail) and tell identifier. */
  reply_xml = xmlNewNode(NULL, (const xmlChar*)"subscribe-session");

  xmlNewProp(
    reply_xml,
    (const xmlChar*)"group",
    (const xmlChar*)inf_communication_group_get_name(group)
  );
  
  xmlNewProp(
    reply_xml,
    (const xmlChar*)"method",
    (const xmlChar*)method
  );

  g_object_unref(group);
  inf_xml_util_set_attribute_uint(reply_xml, "id", node->id);

  seq_attr = xmlGetProp(xml, (const xmlChar*)"seq");
  if(seq_attr != NULL)
  {
    xmlNewProp(reply_xml, (const xmlChar*)"seq", seq_attr);
    xmlFree(seq_attr);
  }

  /* This gives ownership of proxy to the subscription request */
  infd_directory_add_subreq_session(directory, connection, node->id, proxy);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  return TRUE;
}

static gboolean
infd_directory_handle_save_session(InfdDirectory* directory,
                                   InfXmlConnection* connection,
                                   const xmlNodePtr xml,
                                   GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  xmlChar* seq_attr;
  xmlNodePtr reply_xml;
  gchar* path;
  gboolean result;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_assert(priv->storage != NULL);

  /* TODO: Don't do anything if buffer is not modified */

  /* TODO: Authentication, we could also allow specific connections to save
   * without being subscribed. */
  node = infd_directory_get_node_from_xml_typed(
    directory,
    xml,
    "id",
    INFD_STORAGE_NODE_NOTE,
    error
  );

  if(node->shared.note.session == NULL ||
     !infd_session_proxy_is_subscribed(node->shared.note.session, connection))
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_UNSUBSCRIBED,
      _("The requesting connection is not subscribed to the session")
    );

    return FALSE;
  }

  /* We only need this if we are saving asynchronously: */
  /* TODO: Which we should do, of course. */
#if 0
  reply_xml = xmlNewNode(NULL, (const xmlChar*)"session-save-in-progress");
  seq_attr = xmlGetProp(xml, (const xmlChar*)"seq");
  if(seq_attr != NULL)
  {
    xmlNewProp(reply_xml, (const xmlChar*)"seq", seq_attr);
    xmlFree(seq_attr);
  }

  inf_connection_manager_group_send_to_connection(
    priv->group,
    connection,
    reply_xml
  );
#endif

  infd_directory_node_get_path(node, &path, NULL);

  result = node->shared.note.plugin->session_write(
    priv->storage,
    infd_session_proxy_get_session(node->shared.note.session),
    path,
    node->shared.note.plugin->user_data,
    error
  );

  /* TODO: unset modified flag of buffer if result == TRUE */

  /* The timeout should only be set when there aren't any connections
   * subscribed, however we just made sure that the connection the request
   * comes from is subscribed. */
  g_assert(node->shared.note.save_timeout == NULL);

  g_free(path);

  if(result == FALSE)
    return FALSE;

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"session-saved");
  seq_attr = xmlGetProp(xml, (const xmlChar*)"seq");
  if(seq_attr != NULL)
  {
    xmlNewProp(reply_xml, (const xmlChar*)"seq", seq_attr);
    xmlFree(seq_attr);
  }

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  return TRUE;
}

static gboolean
infd_directory_handle_subscribe_chat(InfdDirectory* directory,
                                     InfXmlConnection* connection,
                                     xmlNodePtr xml,
                                     GError** error)
{
  InfdDirectoryPrivate* priv;
  InfCommunicationGroup* group;
  const gchar* method;
  xmlChar* seq_attr;
  xmlNodePtr reply_xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* TODO: Bail if this connection is either currently being synchronized to
   * or is already subscribed */
  /* TODO: Bail if a subscription request for this connection is pending. */

  if(priv->chat_session == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_CHAT_DISABLED,
      _("The chat is disabled")
    );

    return FALSE;
  }

  g_object_get(
    G_OBJECT(priv->chat_session),
    "subscription-group", &group,
    NULL
  );

  method = inf_communication_group_get_method_for_connection(
    group,
    connection
  );

  /* We should always be able to fallback to "central" */
  g_assert(method != NULL);

  /* Reply that subscription was successful (so far, synchronization may
   * still fail) and tell identifier. */
  reply_xml = xmlNewNode(NULL, (const xmlChar*)"subscribe-chat");

  xmlNewProp(
    reply_xml,
    (const xmlChar*)"group",
    (const xmlChar*)inf_communication_group_get_name(group)
  );
  
  xmlNewProp(
    reply_xml,
    (const xmlChar*)"method",
    (const xmlChar*)method
  );

  g_object_unref(group);

  seq_attr = xmlGetProp(xml, (const xmlChar*)"seq");
  if(seq_attr != NULL)
  {
    xmlNewProp(reply_xml, (const xmlChar*)"seq", seq_attr);
    xmlFree(seq_attr);
  }

  infd_directory_add_subreq_chat(directory, connection);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  return TRUE;
}

static gboolean
infd_directory_handle_subscribe_ack(InfdDirectory* directory,
                                    InfXmlConnection* connection,
                                    const xmlNodePtr xml,
                                    GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectorySubreq* request;
  InfdDirectoryNode* node;
  InfdDirectorySyncIn* sync_in;
  InfdSessionProxy* proxy;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  request = infd_directory_get_subreq_from_xml(
    directory,
    connection,
    xml,
    error
  );

  if(request == NULL) return FALSE;

  /* Unlink, so that the subreq itself does not cause the is_name_available
   * assertions below to fail. */
  infd_directory_unlink_subreq(directory, request);

  switch(request->type)
  {
  case INFD_DIRECTORY_SUBREQ_CHAT:
    /* Note that it doesn't matter whether the chat was disabled+enabled
     * between subreq generation and <subscribe-ack/> handling - the group
     * is always called InfChat so the client joins the correct group in
     * all cases. */
    if(priv->chat_session != NULL)
    {
      infd_session_proxy_subscribe_to(priv->chat_session, connection, TRUE);
    }
    else
    {
      /* The chat does not exist anymore - just create a temporary chat,
       * subscribe the client and then close the chat right aftewards, to
       * notify the client about the chat not existing anymore. */

      /* TODO: Make a helper function creating the chat proxy but not
       * setting the member var / doing the property notify */
      g_object_freeze_notify(G_OBJECT(directory));

      infd_directory_enable_chat(directory, TRUE);
      infd_session_proxy_subscribe_to(priv->chat_session, connection, TRUE);
      infd_directory_enable_chat(directory, FALSE);

      g_object_thaw_notify(G_OBJECT(directory));
    }

    break;
  case INFD_DIRECTORY_SUBREQ_SESSION:
    node = g_hash_table_lookup(
      priv->nodes,
      GUINT_TO_POINTER(request->node_id)
    );

    /* The node this client wants to subscribe might have been removed in the
     * meanwhile. */
    if(node != NULL)
    {
      g_assert(node->type == INFD_STORAGE_NODE_NOTE);

      g_assert(node->shared.note.session == NULL ||
               node->shared.note.session == request->shared.session.session);

      if(node->shared.note.session == NULL)
      {
        infd_directory_node_link_session(
          directory,
          node,
          request->shared.session.session
        );
      }
    }

    infd_session_proxy_subscribe_to(
      request->shared.session.session,
      connection,
      TRUE
    );

    break;
  case INFD_DIRECTORY_SUBREQ_ADD_NODE:
    if(request->shared.add_node.parent != NULL)
    {
      g_assert(
        infd_directory_node_is_name_available(
          directory,
          request->shared.add_node.parent,
          request->shared.add_node.name,
          NULL
        ) == TRUE
      );

      proxy = request->shared.add_node.proxy;
      g_object_ref(proxy);

      node = infd_directory_node_new_note(
        directory,
        request->shared.add_node.parent,
        request->node_id,
        g_strdup(request->shared.add_node.name),
        request->shared.add_node.plugin
      );

      infd_directory_node_link_session(directory, node, proxy);

      /* register to all but conn */
      infd_directory_node_register(directory, node, connection);
    }
    else
    {
      /* The add-node request can't be performed properly because the parent
       * node has been removed. Still create a session proxy and subscribe to
       * connection before unrefing it again, so that the remote host gets
       * notified that this session is no longer active. */
      proxy = request->shared.add_node.proxy;
      g_object_ref(proxy);
    }

    /* Don't sync session to client if the client added this node, since the
     * node is empty anyway. */
    infd_session_proxy_subscribe_to(proxy, connection, FALSE);
    g_object_unref(proxy);

    break;
  case INFD_DIRECTORY_SUBREQ_SYNC_IN:
  case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
    /* Group and method are OK for the client, so start synchronization */
    inf_session_synchronize_from(
      infd_session_proxy_get_session(request->shared.sync_in.proxy)
    );

    if(request->shared.sync_in.parent != NULL)
    {
      g_assert(
        infd_directory_node_is_name_available(
          directory,
          request->shared.sync_in.parent,
          request->shared.sync_in.name,
          NULL
        ) == TRUE
      );

      proxy = request->shared.sync_in.proxy;
      g_object_ref(proxy);

      sync_in = infd_directory_add_sync_in(
        directory,
        request->shared.sync_in.parent,
        request->node_id,
        request->shared.sync_in.name,
        request->shared.sync_in.plugin,
        proxy
      );
    }
    else
    {
      /* The sync-in can't be performed properly because the parent node of
       * the node to sync-in has been removed. Still create the corresponding
       * session and close it immediately (cancelling the synchronization, to
       * tell the client). */
      proxy = request->shared.sync_in.proxy;
      g_object_ref(proxy);
    }

    if(request->type == INFD_DIRECTORY_SUBREQ_SYNC_IN)
    {
      /* No subscription, so add connection to synchronization group
       * explicitely. */
      inf_communication_hosted_group_add_member(
        request->shared.sync_in.synchronization_group,
        connection
      );
    }
    else
    {
      /* subscribe_to adds connection to subscription group which is the
       * same as the synchronization group. */
      infd_session_proxy_subscribe_to(proxy, connection, FALSE);
    }

    g_object_unref(proxy);

    break;
  default:
    g_assert_not_reached();
    break;
  }

  infd_directory_free_subreq(request);
  return TRUE;
}

static gboolean
infd_directory_handle_subscribe_nack(InfdDirectory* directory,
                                     InfXmlConnection* connection,
                                     const xmlNodePtr xml,
                                     GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectorySubreq* request;
  gchar* path;
  gboolean result;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  request = infd_directory_get_subreq_from_xml(
    directory,
    connection,
    xml,
    error
  );

  if(request == NULL) return FALSE;

  result = TRUE;
  if(request->type == INFD_DIRECTORY_SUBREQ_ADD_NODE)
  {
    infd_directory_node_make_path(
      request->shared.add_node.parent,
      request->shared.add_node.name,
      &path,
      NULL);

    result = infd_storage_remove_node(
      priv->storage,
      request->shared.add_node.plugin->note_type,
      path,
      error
    );

    g_free(path);
  }

  infd_directory_remove_subreq(directory, request);
  return result;
}

/*
 * Signal handlers.
 */

static void
infd_directory_connection_notify_status_cb(GObject* object,
                                           GParamSpec* pspec,
                                           gpointer user_data)
{
  InfXmlConnectionStatus status;
  g_object_get(object, "status", &status, NULL);

  if(status == INF_XML_CONNECTION_OPEN)
  {
    infd_directory_send_welcome_message(
      INFD_DIRECTORY(user_data),
      INF_XML_CONNECTION(object)
    );
  }
}

static void
infd_directory_member_removed_cb(InfCommunicationGroup* group,
                                 InfXmlConnection* connection,
                                 gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  GSList* item;
  InfdDirectorySubreq* request;
  InfdDirectoryConnectionInfo* info;

  directory = INFD_DIRECTORY(user_data);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->root != NULL && priv->root->shared.subdir.explored)
  {
    infd_directory_node_remove_connection(priv->root, connection);
  }

  /* Remove all subscription requests for this connection */
  item = priv->subscription_requests;
  while(item != NULL)
  {
    request = (InfdDirectorySubreq*)item->data;
    item = item->next;

    if(request->connection == connection)
      infd_directory_remove_subreq(directory, request);
  }

  info = g_hash_table_lookup(priv->connections, connection);
  g_slice_free(InfdDirectoryConnectionInfo, info);

  g_signal_handlers_disconnect_by_func(G_OBJECT(connection),
    G_CALLBACK(infd_directory_connection_notify_status_cb),
    directory);

  g_hash_table_remove(priv->connections, connection);
  g_object_unref(G_OBJECT(connection));
}

/*
 * Property modification.
 */

static void
infd_directory_set_storage(InfdDirectory* directory,
                           InfdStorage* storage)
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
       * storage. Note that sessions are not closed, but further
       * modifications to the sessions will not be stored in storage. */
      while((child = priv->root->shared.subdir.child) != NULL)
      {
        infd_directory_node_unlink_child_sessions(directory, child, TRUE);
        infd_directory_node_unregister(directory, child, NULL, 0);
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

      /* TODO: Error handling? */
      infd_directory_node_explore(directory, priv->root, NULL);
    }

    g_object_ref(G_OBJECT(storage));
  }
}

static void
infd_directory_set_communication_manager(InfdDirectory* directory,
                                         InfCommunicationManager* manager)
{
  InfdDirectoryPrivate* priv;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* construct/only */
  g_assert(priv->communication_manager == NULL);
  priv->communication_manager = manager;
  g_object_ref(manager);
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

  priv->io = NULL;
  priv->storage = NULL;
  priv->communication_manager = NULL;

  priv->plugins = g_hash_table_new(g_str_hash, g_str_equal);
  priv->connections = g_hash_table_new(NULL, NULL);

  priv->node_counter = 1;
  priv->nodes = g_hash_table_new(NULL, NULL);

  /* The root node has no name. */
  priv->root = infd_directory_node_new_subdirectory(directory, NULL, 0, NULL);
  priv->sync_ins = NULL;
  priv->subscription_requests = NULL;

  priv->chat_session = NULL;
}

static GObject*
infd_directory_constructor(GType type,
                           guint n_construct_properties,
                           GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  /* We only use central method for directory handling */
  static const gchar* const methods[] = { "centrol", NULL };

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  directory = INFD_DIRECTORY(object);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* TODO: Use default communication manager in case none is set */
  g_assert(priv->communication_manager != NULL);
  
  priv->group = inf_communication_manager_open_group(
    priv->communication_manager,
    "InfDirectory",
    methods
  );

  g_signal_connect(
    G_OBJECT(priv->group),
    "member-removed",
    G_CALLBACK(infd_directory_member_removed_cb),
    directory
  );

  inf_communication_group_set_target(
    INF_COMMUNICATION_GROUP(priv->group),
    INF_COMMUNICATION_OBJECT(directory)
  );

  g_assert(g_hash_table_size(priv->connections) == 0);
  return object;
}

static void
infd_directory_dispose(GObject* object)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  GHashTableIter iter;
  gpointer key;

  directory = INFD_DIRECTORY(object);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* Disable chat if any */
  if(priv->chat_session != NULL)
    infd_directory_enable_chat(directory, FALSE);

  /* Cancel sync-ins */
  while(priv->sync_ins != NULL)
    infd_directory_remove_sync_in(directory, priv->sync_ins->data);

  /* This frees the complete directory tree and saves sessions into the
   * storage. */
  infd_directory_node_unlink_child_sessions(directory, priv->root, TRUE);
  infd_directory_node_free(directory, priv->root);
  priv->root = NULL;

  g_hash_table_destroy(priv->nodes);
  priv->nodes = NULL;
  
  for(g_hash_table_iter_init(&iter, priv->connections);
      g_hash_table_iter_next(&iter, &key, NULL);
      g_hash_table_iter_init(&iter, priv->connections))
  {
    inf_communication_hosted_group_remove_member(
      priv->group,
      INF_XML_CONNECTION(key)
    );
  }

  /* Should have been cleared by removing all connections */
  g_assert(priv->subscription_requests == NULL);

  /* We have dropped all references to connections now, so these do not try
   * to tell anyone that the directory tree has gone or whatever. */

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->group),
    G_CALLBACK(infd_directory_member_removed_cb),
    directory
  );

  g_object_unref(priv->group);
  g_object_unref(priv->communication_manager);
  infd_directory_set_storage(directory, NULL);

  g_hash_table_destroy(priv->plugins);
  priv->plugins = NULL;

  if(priv->io != NULL)
  {
    g_object_unref(G_OBJECT(priv->io));
    priv->io = NULL;
  }

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
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct only */
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_STORAGE:
    infd_directory_set_storage(
      directory,
      INFD_STORAGE(g_value_get_object(value))
    );

    break;
  case PROP_COMMUNICATION_MANAGER:
    infd_directory_set_communication_manager(
      directory,
      INF_COMMUNICATION_MANAGER(g_value_get_object(value))
    );

    break;
  case PROP_CHAT_SESSION:
    /* read only */
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
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_STORAGE:
    g_value_set_object(value, G_OBJECT(priv->storage));
    break;
  case PROP_COMMUNICATION_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->communication_manager));
    break;
  case PROP_CHAT_SESSION:
    g_value_set_object(value, G_OBJECT(priv->chat_session));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * Default signal handler implementation.
 */
static void
infd_directory_add_session(InfdDirectory* directory,
                           InfdDirectoryIter* iter,
                           InfdSessionProxy* session)
{
  InfdDirectoryNode* node;

  infd_directory_return_if_iter_fail(directory, iter);

  node = (InfdDirectoryNode*)iter->node;
  g_assert(node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(node->shared.note.session == NULL);
  
  node->shared.note.session = session;
  g_object_ref(session);

  g_object_set_qdata(
    G_OBJECT(session),
    infd_directory_node_id_quark,
    GUINT_TO_POINTER(node->id)
  );

  g_signal_connect(
    G_OBJECT(session),
    "notify::idle",
    G_CALLBACK(infd_directory_session_idle_notify_cb),
    directory
  );

  if(infd_session_proxy_is_idle(node->shared.note.session))
  {
    infd_directory_start_session_save_timeout(directory, node);
  }
}

static void
infd_directory_remove_session(InfdDirectory* directory,
                              InfdDirectoryIter* iter,
                              InfdSessionProxy* session)
{
  InfdDirectoryNode* node;
  InfdDirectoryPrivate* priv;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_if_iter_fail(directory, iter);
  node = (InfdDirectoryNode*)iter->node;

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(node->shared.note.session == session);

  if(node->shared.note.save_timeout != NULL)
  {
    inf_io_remove_timeout(priv->io, node->shared.note.save_timeout);
    node->shared.note.save_timeout = NULL;
  }

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(infd_directory_session_idle_notify_cb),
    directory
  );

  g_object_set_qdata(
    G_OBJECT(session),
    infd_directory_node_id_quark,
    NULL
  );

  g_object_unref(session);
  node->shared.note.session = NULL;
}

/*
 * InfCommunicationObject implementation.
 */

static InfCommunicationScope
infd_directory_communication_object_received(InfCommunicationObject* object,
                                             InfXmlConnection* connection,
                                             const xmlNodePtr node,
                                             GError** error)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  GError* local_error;
  xmlNodePtr reply_xml;
  xmlChar* seq_attr;

  directory = INFD_DIRECTORY(object);
  priv = INFD_DIRECTORY_PRIVATE(directory);
  local_error = NULL;

  if(strcmp((const char*)node->name, "explore-node") == 0)
  {
    infd_directory_handle_explore_node(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "add-node") == 0)
  {
    infd_directory_handle_add_node(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "remove-node") == 0)
  {
    infd_directory_handle_remove_node(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "subscribe-session") == 0)
  {
    infd_directory_handle_subscribe_session(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "save-session") == 0)
  {
    infd_directory_handle_save_session(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "subscribe-chat") == 0)
  {
    infd_directory_handle_subscribe_chat(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "subscribe-ack") == 0)
  {
    /* Don't reply to subscribe-ack. */
    infd_directory_handle_subscribe_ack(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "subscribe-nack") == 0)
  {
    /* Don't reply to subscribe-nack. */
    infd_directory_handle_subscribe_nack(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else
  {
    g_set_error(
      &local_error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE,
      "%s",
      inf_directory_strerror(INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE)
    );
  }

  if(local_error != NULL)
  {
    /* TODO: If error is not from the InfDirectoryError error domain, the
     * client cannot reconstruct the error because he possibly does not know
     * the error domain (it might even come from a storage plugin). */

    /* An error happened, so tell the client that the request failed and
     * what has gone wrong. */
    reply_xml = xmlNewNode(NULL, (const xmlChar*)"request-failed");
    inf_xml_util_set_attribute_uint(reply_xml, "code", local_error->code);

    xmlNewProp(
      reply_xml,
      (const xmlChar*)"domain",
      (const xmlChar*)g_quark_to_string(local_error->domain)
    );

    seq_attr = xmlGetProp(node, (const xmlChar*)"seq");
    if(seq_attr != NULL)
    {
      xmlNewProp(reply_xml, (const xmlChar*)"seq", seq_attr);
      xmlFree(seq_attr);
    }

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      connection,
      reply_xml
    );

    g_propagate_error(error, local_error);
  }

  /* Never forward directory messages */
  return INF_COMMUNICATION_SCOPE_PTP;
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

  object_class->constructor = infd_directory_constructor;
  object_class->dispose = infd_directory_dispose;
  object_class->set_property = infd_directory_set_property;
  object_class->get_property = infd_directory_get_property;

  directory_class->node_added = NULL;
  directory_class->node_removed = NULL;
  directory_class->add_session = infd_directory_add_session;
  directory_class->remove_session = infd_directory_remove_session;

  infd_directory_node_id_quark =
    g_quark_from_static_string("INFD_DIRECTORY_NODE_ID");

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "IO object to watch sockets and schedule timeouts",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_STORAGE,
    g_param_spec_object(
      "storage",
      "Storage backend",
      "The storage backend to use",
      INFD_TYPE_STORAGE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_COMMUNICATION_MANAGER,
    g_param_spec_object(
      "communication-manager",
      "Communication manager",
      "The communication manager for the directory",
      INF_COMMUNICATION_TYPE_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CHAT_SESSION,
    g_param_spec_object(
      "chat-session",
      "Chat session",
      "The server's chat session",
      INFD_TYPE_SESSION_PROXY,
      G_PARAM_READABLE
    )
  );

  /**
   * InfdDirectory::node-added:
   * @directory: The #InfdDirectory emitting the signal.
   * @iter: A #InfdDirectoryIter pointing to the added node.
   *
   * This signal is emitted when a new node has been created. It can either
   * be created by API calls such as infd_directory_add_note() and
   * infd_directory_add_subdirectory() or by a client making a corresponding
   * request.
   **/
  directory_signals[NODE_ADDED] = g_signal_new(
    "node-added",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdDirectoryClass, node_added),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INFD_TYPE_DIRECTORY_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
  );

  /**
   * InfdDirectory::node-removed:
   * @directory: The #InfdDirectory emitting the signal.
   * @iter: A #InfdDirectoryIter pointing to the removed node.
   *
   * This signal is emitted when a node has been removed. If a subdirectory
   * node is removed, all contained nodes are removed as well. Node removal
   * can either happen through a call to infd_directory_remove_node(), or by
   * a client making a corresponding request.
   **/
  directory_signals[NODE_REMOVED] = g_signal_new(
    "node-removed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdDirectoryClass, node_removed),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INFD_TYPE_DIRECTORY_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
  );

  /**
   * InfdDirectory::add-session:
   * @directory: The #InfdDirectory emitting the signal.
   * @iter: A #InfdDirectoryIter pointing to the affected node.
   * @session: The #InfdSessionProxy proxying the added session.
   *
   * This signal is emitted, when a session has been associated with a node.
   * This happens when infd_directory_iter_get_session() is called on a node,
   * when a remote client subscribes to a session or a new node was created.
   *
   * When a session has been created for a node, the session is kept until it
   * is idle for some time. Then, it is removed again after having been stored
   * into the background storage.
   */
  directory_signals[ADD_SESSION] = g_signal_new(
    "add-session",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdDirectoryClass, add_session),
    NULL, NULL,
    inf_marshal_VOID__BOXED_OBJECT,
    G_TYPE_NONE,
    2,
    INFD_TYPE_DIRECTORY_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INFD_TYPE_SESSION_PROXY
  );

  /**
   * InfdDirectory::remove-session:
   * @directory: The #InfdDirectory emitting the signal.
   * @iter: A #InfdDirectoryIter pointing to the affected node.
   * @session: The #InfdSessionProxy proxying the removed session.
   *
   * This signal is emitted when a previously added session was removed. This
   * happens when a session is idle for some time, or when the corresponding
   * node has been removed.
   */
  directory_signals[REMOVE_SESSION] = g_signal_new(
    "remove-session",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdDirectoryClass, remove_session),
    NULL, NULL,
    inf_marshal_VOID__BOXED_OBJECT,
    G_TYPE_NONE,
    2,
    INFD_TYPE_DIRECTORY_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INFD_TYPE_SESSION_PROXY
  );
}

static void
infd_directory_communication_object_init(gpointer g_iface,
                                         gpointer iface_data)
{
  InfCommunicationObjectIface* iface;
  iface = (InfCommunicationObjectIface*)g_iface;

  iface->received = infd_directory_communication_object_received;
}

GType
infd_directory_iter_get_type(void)
{
  static GType directory_iter_type = 0;

  if(!directory_iter_type)
  {
    directory_iter_type = g_boxed_type_register_static(
      "InfdDirectoryIter",
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

    static const GInterfaceInfo communication_object_info = {
      infd_directory_communication_object_init,
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
      INF_COMMUNICATION_TYPE_OBJECT,
      &communication_object_info
    );
  }

  return directory_type;
}

/*
 * Public API.
 */

/**
 * infd_directory_iter_copy:
 * @iter: A #InfdDirectoryIter.
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

/**
 * infd_directory_iter_free:
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

/**
 * infd_directory_new:
 * @io: IO object to watch connections and schedule timeouts.
 * @storage: Storage backend that is used to read/write notes from
 * permanent memory into #InfBuffer objects.
 * @comm_manager: A #InfCommunicationManager to register added
 * connections to and which forwards incoming data to the directory
 * or running sessions.
 *
 * Creates a new #InfdDirectory.
 *
 * Return Value: A new #InfdDirectory.
 **/
InfdDirectory*
infd_directory_new(InfIo* io,
                   InfdStorage* storage,
                   InfCommunicationManager* comm_manager)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(INFD_IS_STORAGE(storage), NULL);
  g_return_val_if_fail(INF_COMMUNICATION_IS_MANAGER(comm_manager), NULL);

  object = g_object_new(
    INFD_TYPE_DIRECTORY,
    "io", io,
    "storage", storage,
    "communication-manager", comm_manager,
    NULL
  );

  return INFD_DIRECTORY(object);
}

/**
 * infd_directory_get_io:
 * @directory: A #InfdDirectory.
 *
 * Returns the IO object in use by the directory.
 *
 * Return Value: A #InfIo.
 **/
InfIo*
infd_directory_get_io(InfdDirectory* directory)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  return INFD_DIRECTORY_PRIVATE(directory)->io;
}

/**
 * infd_directory_get_storage:
 * @directory: A #InfdDirectory:
 *
 * Returns the storage backend in use by the directory.
 *
 * Return Value: An #InfdDirectoryStorage.
 **/
InfdStorage*
infd_directory_get_storage(InfdDirectory* directory)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  return INFD_DIRECTORY_PRIVATE(directory)->storage;
}

/**
 * infd_directory_get_communication_manager:
 * @directory: A #InfdDirectory.
 *
 * Returns the connection manager of the directory.
 *
 * Return Value: An #InfCommunicationManager.
 **/
InfCommunicationManager*
infd_directory_get_communication_manager(InfdDirectory* directory)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  return INFD_DIRECTORY_PRIVATE(directory)->communication_manager;
}

/**
 * infd_directory_add_plugin:
 * @directory: A #InfdDirectory.
 * @plugin: A #InfdNotePlugin.
 *
 * Adds @plugin to @directory. This allows the directory to create sessions
 * of the plugin's type. Only one plugin of each type can be added to the
 * directory. The plugin's storage_type must match the storage of @directory.
 *
 * Return Value: Whether the plugin was added successfully.
 **/
gboolean
infd_directory_add_plugin(InfdDirectory* directory,
                          const InfdNotePlugin* plugin)
{
  InfdDirectoryPrivate* priv;
  const gchar* storage_type;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(plugin != NULL, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  storage_type = g_type_name(G_TYPE_FROM_INSTANCE(priv->storage));
  g_return_val_if_fail(
    strcmp(plugin->storage_type, storage_type) == 0,
    FALSE
  );

  if(g_hash_table_lookup(priv->plugins, plugin->note_type) != NULL)
    return FALSE;

  g_hash_table_insert(
    priv->plugins,
    (gpointer)plugin->note_type,
    (gpointer)plugin
  );

  return TRUE;
}

/**
 * infd_directory_lookup_plugin:
 * @directory: A #InfdDirectory.
 * @note_type: A note type for which to lookup the plugin.
 *
 * Returns the #InfdNotePlugin that handles the given note type, or %NULL
 * in case no corresponding plugin was added.
 *
 * Return Value: A #InfdNotePlugin, or %NULL.
 **/
const InfdNotePlugin*
infd_directory_lookup_plugin(InfdDirectory* directory,
                             const gchar* note_type)
{
  InfdDirectoryPrivate* priv;
  
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  g_return_val_if_fail(note_type != NULL, NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  return (const InfdNotePlugin*)g_hash_table_lookup(priv->plugins, note_type);
}

/**
 * infd_directory_add_connection:
 * @directory: A #InfdDirectory.
 * @connection: A #InfConnection.
 *
 * Adds @connection to the connections of @directory. The directory will then
 * receive requests from @connection. If the directory's method manager does
 * not contain a "central" method for connection's network, then the
 * connection will not be added and the function returns %FALSE.
 *
 * Returns: Whether the connection was added to the directory.
 **/
gboolean
infd_directory_add_connection(InfdDirectory* directory,
                              InfXmlConnection* connection)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryConnectionInfo* info;
  InfXmlConnectionStatus status;
  GHashTableIter iter;
  gpointer value;
  guint seq_id;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_return_val_if_fail(priv->communication_manager != NULL, FALSE);
  g_return_val_if_fail(
    g_hash_table_lookup(priv->connections, connection) == NULL,
    FALSE
  );

  inf_communication_hosted_group_add_member(priv->group, connection);

  /* Find a free seq id */
  seq_id = 1;
  g_hash_table_iter_init(&iter, priv->connections);
  while(g_hash_table_iter_next(&iter, NULL, &value))
  {
    info = (InfdDirectoryConnectionInfo*)value;
    if(info->seq_id >= seq_id)
      seq_id = info->seq_id + 1;
    g_assert(seq_id > info->seq_id); /* check for overflow */
  }

  info = g_slice_new(InfdDirectoryConnectionInfo);
  info->seq_id = seq_id;

  g_hash_table_insert(priv->connections, connection, info);
  g_object_ref(connection);

  g_signal_connect(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(infd_directory_connection_notify_status_cb),
    directory
  );

  g_object_get(G_OBJECT(connection), "status", &status, NULL);
  if(status == INF_XML_CONNECTION_OPEN)
    infd_directory_send_welcome_message(directory, connection);

  return TRUE;
}

/**
 * infd_directory_iter_get_name:
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to a node in @directory.
 *
 * Returns the name of the node @iter points to.
 *
 * Returns: The node's name. The returned string must not be freed.
 */
const gchar*
infd_directory_iter_get_name(InfdDirectory* directory,
                             InfdDirectoryIter* iter)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  g_return_val_if_fail(iter != NULL, NULL);
  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;

  return node->name;
}

/**
 * infd_directory_iter_get_path:
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to a node in @directory.
 *
 * Returns the complete path to the node @iter points to. The path to a node
 * is the name of the node and the name of all parent nodes separated by '/',
 * as a filesystem path on Unix.
 *
 * Returns: The node's path. Free with g_free() when no longer in use.
 */
gchar*
infd_directory_iter_get_path(InfdDirectory* directory,
                             InfdDirectoryIter* iter)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  gchar* path;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  g_return_val_if_fail(iter != NULL, NULL);
  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;

  infd_directory_node_get_path(node, &path, NULL);
  return path;
}

/**
 * infd_directory_iter_get_root:
 * @directory: A #InfdDirectory
 * @iter: An uninitalized #InfdDirectoryIter.
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

/**
 * infd_directory_iter_get_next:
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

/**
 * infd_directory_iter_get_prev:
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

/**
 * infd_directory_iter_get_parent:
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

/**
 * infd_directory_iter_get_child:
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
 * The function guarantees not to set @error if the node is already explored,
 * i.e. infd_directory_iter_get_explored() returns %TRUE for @directory and
 * @iter.
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

/**
 * infd_directory_iter_get_explored:
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to a subdirectory node in @directory.
 *
 * Returns whether the subdirectory node pointed to by @iter has already
 * been read from the background storage. If not, then no connections can
 * be subscribed to any child nodes.
 *
 * Returns: Whether the node @iter points to has been explored.
 */
gboolean
infd_directory_iter_get_explored(InfdDirectory* directory,
                                 InfdDirectoryIter* iter)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);
  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);
  infd_directory_return_val_if_subdir_fail(iter->node, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;

  return node->shared.subdir.explored;
}

/**
 * infd_directory_add_subdirectory:
 * @directory: A #InfdDirectory.
 * @parent: A #InfdDirectoryIter pointing to a subdirectory node
 * in @directory.
 * @name: The name of the new node.
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

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(parent != NULL, FALSE);
  g_return_val_if_fail(name != NULL, FALSE);
  infd_directory_return_val_if_iter_fail(directory, parent, FALSE);
  infd_directory_return_val_if_subdir_fail(parent->node, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_return_val_if_fail(priv->storage != NULL, FALSE);

  if( ((InfdDirectoryNode*)parent->node)->shared.subdir.explored == FALSE)
    if(infd_directory_node_explore(directory, parent->node, error) == FALSE)
      return FALSE;

  node = infd_directory_node_add_subdirectory(
    directory,
    parent->node,
    name,
    NULL,
    0,
    error
  );

  if(node == NULL)
    return FALSE;

  if(iter != NULL)
  {
    iter->node_id = node->id;
    iter->node = node;
  }

  return TRUE;
}

/**
 * infd_directory_add_note:
 * @directory: A #InfdDirectory.
 * @parent: A #InfdDirectoryIter pointing to a subdirectory node
 * in @directory.
 * @name: The name of the new node.
 * @plugin: The plugin to use for the node. Must have been added with
 * infd_directory_add_plugin().
 * @iter: An uninitialized #InfdDirectoryIter.
 * @error: Location to store error information.
 *
 * Creates a new note in @directory. It will be child of the subdirectory
 * node @parent points to. @iter is set to point to the new node. If an
 * error occurs, the function returns %FALSE and @error is set.
 *
 * Return Value: %TRUE on success.
 **/
gboolean
infd_directory_add_note(InfdDirectory* directory,
                        InfdDirectoryIter* parent,
                        const gchar* name,
                        const InfdNotePlugin* plugin,
                        InfdDirectoryIter* iter,
                        GError** error)
{
  InfdDirectoryPrivate* priv;
  guint node_id;
  InfCommunicationHostedGroup* group;
  InfdDirectoryNode* node;
  
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(parent != NULL, FALSE);
  g_return_val_if_fail(name != NULL, FALSE);
  g_return_val_if_fail(plugin != NULL, FALSE);

  infd_directory_return_val_if_iter_fail(directory, parent, FALSE);
  infd_directory_return_val_if_subdir_fail(parent->node, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if( ((InfdDirectoryNode*)parent->node)->shared.subdir.explored == FALSE)
    if(infd_directory_node_explore(directory, parent->node, error) == FALSE)
      return FALSE;

  node_id = priv->node_counter++;
  group = infd_directory_create_subscription_group(directory, node_id);

  node = infd_directory_node_create_new_note(
    directory,
    parent->node,
    group,
    node_id,
    name,
    plugin,
    error
  );

  if(node == NULL)
    return FALSE;

  infd_directory_node_register(directory, node, NULL);

  if(iter != NULL)
  {
    iter->node = node;
    iter->node_id = node->id;
  }

  return TRUE;
}

/**
 * infd_directory_remove_node:
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
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);
  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  node = iter->node;
  g_return_val_if_fail(node->parent != NULL, FALSE);

  return infd_directory_node_remove(directory, node, NULL, 0, error);
}

/**
 * infd_directory_iter_get_node_type:
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to some node in @directory.
 *
 * Returns the type of the node @iter points to.
 *
 * Returns: A #InfdDirectoryStorageNodeType.
 **/
InfdStorageNodeType
infd_directory_iter_get_node_type(InfdDirectory* directory,
                                  InfdDirectoryIter* iter)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), INFD_STORAGE_NODE_NOTE);

  infd_directory_return_val_if_iter_fail(
    directory,
    iter,
    INFD_STORAGE_NODE_NOTE
  );

  return ((InfdDirectoryNode*)iter->node)->type;
}

/**
 * infd_directory_iter_get_plugin:
 * @directory: A #InfdDirectory.
 * @iter: a #InfdDirectoryIter pointing to a note in @directory.
 *
 * Returns the plugin that is used to create a session for the note @iter
 * points to.
 *
 * Return Value: The plugin for the note @iter points to.
 **/
const InfdNotePlugin*
infd_directory_iter_get_plugin(InfdDirectory* directory,
                               InfdDirectoryIter* iter)
{
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type != INFD_STORAGE_NODE_NOTE, NULL);

  return node->shared.note.plugin;
}

/**
 * infd_directory_iter_get_session:
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to a note in @directory.
 * @error: Location to store error information.
 *
 * Returns the running session in which the note @iter points to is currently
 * edited. If the session does not exist, it is created. However, this might
 * fail if the loading from the background storage fails. In this case, %NULL
 * is returned and @error is set.
 *
 * Return Value: A #InfdSessionProxy for the note @iter points to.
 **/
InfdSessionProxy*
infd_directory_iter_get_session(InfdDirectory* directory,
                                InfdDirectoryIter* iter,
                                GError** error)
{
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_NOTE, NULL);

  return infd_directory_node_get_and_link_session(directory, node, error);
}

/**
 * infd_directory_iter_peek_session:
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to a note in @directory.
 *
 * Returns the running session in which the note @iter points to is currently
 * edited. If the session does not exist because nobody is editing it at the
 * moment, the function returns %NULL.
 *
 * Return Value: A #InfdSessionProxy for the note @iter points to, or %NULL.
 */
InfdSessionProxy*
infd_directory_iter_peek_session(InfdDirectory* directory,
                                 InfdDirectoryIter* iter)
{
  InfdDirectoryNode* node;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_NOTE, NULL);

  return node->shared.note.session;
}

/**
 * infd_directory_iter_save_session:
 * @directory: A #InfdDirectory.
 * @iter: A #InfdDirectoryIter pointing to a note in @directory.
 * @error: Location to store error information.
 *
 * Attempts to store the session the node @iter points to represents into the
 * background storage.
 *
 * Return Value: %TRUE if the operation succeeded, %FALSE otherwise.
 */
gboolean
infd_directory_iter_save_session(InfdDirectory* directory,
                                 InfdDirectoryIter* iter,
                                 GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  gchar* path;
  gboolean result;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_NOTE, FALSE);

  infd_directory_node_get_path(node, &path, NULL);

  result = node->shared.note.plugin->session_write(
    priv->storage,
    infd_session_proxy_get_session(node->shared.note.session),
    path,
    node->shared.note.plugin->user_data,
    error
  );

  /* TODO: Unset modified flag of buffer if result == TRUE */

  g_free(path);
  return result;
}

/**
 * infd_directory_enable_chat:
 * @directory: A #InfdDirectory.
 * @enable: Whether to enable or disable the chat.
 *
 * If @enable is %TRUE, this enables the chat on the server. This allows
 * clients subscribing to the chat via infc_browser_subscribe_chat(). If
 * @enable is %FALSE the chat is disabled which means closing an existing
 * chat session if any and no longer allowing subscription to the chat.
 * The chat is initially disabled.
 */
void
infd_directory_enable_chat(InfdDirectory* directory,
                           gboolean enable)
{
  InfdDirectoryPrivate* priv;
  InfCommunicationHostedGroup* group;
  InfChatSession* session;

  /* TODO: For the moment, there only exist central methods anyway. In the
   * long term, this should probably be a property, though. */
  static const gchar* const methods[] = { "central", NULL };

  g_return_if_fail(INFD_IS_DIRECTORY(directory));

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(enable)
  {
    if(priv->chat_session == NULL)
    {
      group = inf_communication_manager_open_group(
        priv->communication_manager,
        "InfChat",
        methods
      );

      session = inf_chat_session_new(
        priv->communication_manager,
        256,
        INF_SESSION_RUNNING,
        NULL,
        NULL
      );

      priv->chat_session = INFD_SESSION_PROXY(
        g_object_new(
          INFD_TYPE_SESSION_PROXY,
          "session", session,
          "subscription-group", group,
          NULL
        )
      );

      inf_communication_group_set_target(
        INF_COMMUNICATION_GROUP(group),
        INF_COMMUNICATION_OBJECT(priv->chat_session)
      );

      g_object_unref(session);
      g_object_unref(group);

      g_object_notify(G_OBJECT(directory), "chat-session");
    }
  }
  else
  {
    if(priv->chat_session != NULL)
    {
      inf_session_close(infd_session_proxy_get_session(priv->chat_session));

      g_object_unref(priv->chat_session);
      priv->chat_session = NULL;

      g_object_notify(G_OBJECT(directory), "chat-session");
    }
  }
}

/**
 * infd_directory_get_chat_session:
 * @directory: A #InfdDirectory.
 *
 * Returns a #InfdSessionProxy for the chat session, if any. If the chat is
 * enabled (see infd_directory_enable_chat()) this returns a #InfdSessionProxy
 * that can be used to join local users to the chat, or to get chat contents
 * (by getting the #InfChatBuffer from the proxy's #InfChatSession). If the
 * chat is disabled the function returns %NULL.
 *
 * Returns: A #InfdSessionProxy, or %NULL if the chat is disabled.
 */
InfdSessionProxy*
infd_directory_get_chat_session(InfdDirectory* directory)
{
  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  return INFD_DIRECTORY_PRIVATE(directory)->chat_session;
}

/* vim:set et sw=2 ts=2: */
