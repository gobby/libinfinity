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

/**
 * SECTION:infc-browser
 * @short_description: Browse remote directories
 * @see_also: #InfdDirectory
 * @include: libinfinity/client/infc-browser.h
 * @stability: Unstable
 *
 * The #InfcBrowser is used to browse a remote directory and can be used
 * to subscribe to sessions.
 **/

#include <libinfinity/client/infc-browser.h>
#include <libinfinity/client/infc-request-manager.h>
#include <libinfinity/client/infc-explore-request.h>

#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>

#include <libinfinity/inf-marshal.h>

#include <string.h>

typedef enum _InfcBrowserNodeType {
  INFC_BROWSER_NODE_SUBDIRECTORY = 1 << 0,
  /* We found a plugin to handle the note type */
  INFC_BROWSER_NODE_NOTE_KNOWN   = 1 << 1,
  /* There was no plugin registered to handle the note's type */
  INFC_BROWSER_NODE_NOTE_UNKNOWN = 1 << 2
} InfcBrowserNodeType;

typedef struct _InfcBrowserIterGetExploreRequestForeachData
  InfcBrowserIterGetExploreRequestForeachData;
struct _InfcBrowserIterGetExploreRequestForeachData {
  InfcBrowserIter* iter;
  InfcExploreRequest* result;
};

typedef struct _InfcBrowserIterGetNodeRequestForeachData
  InfcBrowserIterGetNodeRequestForeachData;
struct _InfcBrowserIterGetNodeRequestForeachData {
  InfcBrowserIter* iter;
  InfcNodeRequest* result;
};

typedef struct _InfcBrowserIterGetSyncInRequestsForeachData
  InfcBrowserIterGetSyncInRequestsForeachData;
struct _InfcBrowserIterGetSyncInRequestsForeachData {
  InfcBrowserIter* iter;
  GSList* result;
};

typedef struct _InfcBrowserNode InfcBrowserNode;
struct _InfcBrowserNode {
  InfcBrowserNode* parent;
  InfcBrowserNode* prev;
  InfcBrowserNode* next;

  guint id;
  gchar* name;
  InfcBrowserNodeType type;
  InfcBrowserNodeStatus status;

  union {
    struct {
      InfcSessionProxy* session;
      const InfcNotePlugin* plugin;
    } known;

    struct {
      gchar* type;
    } unknown;

    struct {
      /* First child node */
      InfcBrowserNode* child;
      /* Whether we requested the node already from the server.
       * This is required because the child field may be NULL due to an empty
       * subdirectory or due to an unexplored subdirectory. */
      gboolean explored;
    } subdir;
  } shared;
};

typedef struct _InfcBrowserSyncIn InfcBrowserSyncIn;
struct _InfcBrowserSyncIn {
  InfcBrowser* browser;
  InfcBrowserNode* node;

  InfXmlConnection* connection; /* The connection we are synchronizing to */
  /*const InfcNotePlugin* plugin;*/
  InfcSessionProxy* proxy;
};

typedef struct _InfcBrowserPrivate InfcBrowserPrivate;
struct _InfcBrowserPrivate {
  InfIo* io;
  InfConnectionManager* connection_manager;
  InfMethodManager* method_manager;
  InfConnectionManagerGroup* group; /* TODO: This should be a property */
  InfXmlConnection* connection;

  InfcRequestManager* request_manager;

  GHashTable* plugins; /* Registered plugins */

  GHashTable* nodes; /* Mapping from id to node */
  InfcBrowserNode* root;

  GSList* sync_ins;
};

#define INFC_BROWSER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_BROWSER, InfcBrowserPrivate))

enum {
  PROP_0,

  PROP_IO,
  PROP_CONNECTION_MANAGER,
  PROP_METHOD_MANAGER,
  PROP_CONNECTION
};

enum {
  NODE_ADDED,
  NODE_REMOVED,
  SUBSCRIBE_SESSION,
  BEGIN_EXPLORE,
  BEGIN_SUBSCRIBE,

  LAST_SIGNAL
};

/* These make sure that the node iter points to is contained in browser */
#define infc_browser_return_if_iter_fail(browser, iter) \
  g_return_if_fail( \
    g_hash_table_lookup( \
      INFC_BROWSER_PRIVATE(INFC_BROWSER(browser))->nodes, \
      GUINT_TO_POINTER((iter)->node_id) \
    ) == (iter)->node \
  )

#define infc_browser_return_val_if_iter_fail(browser, iter, val) \
  g_return_val_if_fail( \
    g_hash_table_lookup( \
      INFC_BROWSER_PRIVATE(INFC_BROWSER(browser))->nodes, \
      GUINT_TO_POINTER((iter)->node_id) \
    ) == (iter)->node, \
    val \
  )

/* These make sure that node is a subdirectory node */
#define infc_browser_return_if_subdir_fail(node) \
  g_return_if_fail( \
    ((InfcBrowserNode*)node)->type == INFC_BROWSER_NODE_SUBDIRECTORY \
  )

#define infc_browser_return_val_if_subdir_fail(node, val) \
  g_return_val_if_fail( \
    ((InfcBrowserNode*)node)->type == INFC_BROWSER_NODE_SUBDIRECTORY, \
    val \
  )

static GObjectClass* parent_class;
static guint browser_signals[LAST_SIGNAL];
static GQuark infc_browser_session_proxy_quark;
static GQuark infc_browser_sync_in_session_quark;
static GQuark infc_browser_sync_in_plugin_quark;

/*
 * Callbacks
 */

static void
infc_browser_iter_get_explore_request_foreach_func(InfcRequest* request,
                                                   gpointer user_data)
{
  InfcBrowserIterGetExploreRequestForeachData* data;
  InfcExploreRequest* explore_request;
  guint node_id;

  data = (InfcBrowserIterGetExploreRequestForeachData*)user_data;
  g_assert(INFC_IS_EXPLORE_REQUEST(request));

  explore_request = INFC_EXPLORE_REQUEST(request);
  g_object_get(G_OBJECT(explore_request), "node-id", &node_id, NULL);

  /* TODO: Stop foreach when we found the request. Requires changes in
   * InfcRequestManager. */
  if(node_id == data->iter->node_id)
    data->result = explore_request;
}

static void
infc_browser_iter_get_node_request_foreach_func(InfcRequest* request,
                                                gpointer user_data)
{
  InfcBrowserIterGetNodeRequestForeachData* data;
  InfcNodeRequest* node_request;
  guint node_id;

  data = (InfcBrowserIterGetNodeRequestForeachData*)user_data;
  g_assert(INFC_IS_NODE_REQUEST(request));

  node_request = INFC_NODE_REQUEST(request);
  g_object_get(G_OBJECT(node_request), "node-id", &node_id, NULL);

  /* TODO: Stop foreach when we found the request. Requires changes in
   * InfcRequestManager. */
  if(node_id == data->iter->node_id)
    data->result = node_request;
}

static void
infc_browser_iter_get_sync_in_requests_foreach_func(InfcRequest* request,
                                                    gpointer user_data)
{
  InfcBrowserIterGetSyncInRequestsForeachData* data;
  InfcNodeRequest* node_request;
  InfSession* session;
  guint node_id;

  data = (InfcBrowserIterGetNodeRequestForeachData*)user_data;
  g_assert(INFC_IS_NODE_REQUEST(request));

  /* This is only a sync-in request if we assigned a session to sync with */
  node_request = INFC_NODE_REQUEST(request);
  session = g_object_get_qdata(
    G_OBJECT(node_request),
    infc_browser_sync_in_session_quark
  );

  if(session)
  {
    g_object_get(G_OBJECT(node_request), "node-id", &node_id, NULL);
    if(node_id == data->iter->node_id)
      data->result = g_slist_prepend(data->result, node_request);
  }
}

/*
 * Tree handling
 */

static void
infc_browser_node_link(InfcBrowserNode* node,
                       InfcBrowserNode* parent)
{
  g_assert(parent != NULL);
  g_assert(parent->type == INFC_BROWSER_NODE_SUBDIRECTORY);

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
infc_browser_node_unlink(InfcBrowserNode* node)
{
  g_assert(node->parent != NULL);
  g_assert(node->parent->type == INFC_BROWSER_NODE_SUBDIRECTORY);

  if(node->prev != NULL)
    node->prev->next = node->next;
  else
    node->parent->shared.subdir.child = node->next;

  if(node->next != NULL)
    node->next->prev = node->prev;
}

static InfcBrowserNode*
infc_browser_node_new_common(InfcBrowser* browser,
                             InfcBrowserNode* parent,
                             guint id,
                             InfcBrowserNodeType type,
                             const gchar* name)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  priv = INFC_BROWSER_PRIVATE(browser);

  node = g_slice_new(InfcBrowserNode);
  node->parent = parent;
  node->type = type;
  node->id = id;
  node->name = g_strdup(name);

  if(parent != NULL)
  {
    node->status = INFC_BROWSER_NODE_INHERIT;
    infc_browser_node_link(node, parent);
  }
  else
  {
    node->status = INFC_BROWSER_NODE_SYNC;
    node->prev = NULL;
    node->next = NULL;
  }

  g_assert(
    g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node->id)) == NULL
  );

  g_hash_table_insert(priv->nodes, GUINT_TO_POINTER(node->id), node);
  return node;
}

static InfcBrowserNode*
infc_browser_node_new_subdirectory(InfcBrowser* browser,
                                   InfcBrowserNode* parent,
                                   guint id,
                                   const gchar* name)
{
  InfcBrowserNode* node;
  node = infc_browser_node_new_common(
    browser,
    parent,
    id,
    INFC_BROWSER_NODE_SUBDIRECTORY,
    name
  );

  node->shared.subdir.explored = FALSE;
  node->shared.subdir.child = NULL;

  return node;
}

static InfcBrowserNode*
infc_browser_node_new_note(InfcBrowser* browser,
                           InfcBrowserNode* parent,
                           guint id,
                           const gchar* name,
                           const gchar* type)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcNotePlugin* plugin;

  priv = INFC_BROWSER_PRIVATE(browser);
  plugin = g_hash_table_lookup(priv->plugins, type);

  node = infc_browser_node_new_common(
    browser,
    parent,
    id,
    (plugin != NULL) ? INFC_BROWSER_NODE_NOTE_KNOWN :
      INFC_BROWSER_NODE_NOTE_UNKNOWN,
    name
  );

  if(plugin != NULL)
  {
    node->shared.known.plugin = plugin;
    node->shared.known.session = NULL;
  }
  else
  {
    node->shared.unknown.type = g_strdup(type);
  }

  return node;
}

/* Required by infc_browser_session_remove_session */
static void
infc_browser_session_close_cb(InfSession* session,
                              gpointer user_data);

static void
infc_browser_session_remove_session(InfcBrowser* browser,
                                    InfcBrowserNode* node)
{
  InfSession* session;

  g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);
  g_assert(node->shared.known.session != NULL);

  session = infc_session_proxy_get_session(node->shared.known.session);

  g_signal_handlers_disconnect_by_func(
    session,
    G_CALLBACK(infc_browser_session_close_cb),
    browser
  );

  g_object_set_qdata(
    G_OBJECT(session),
    infc_browser_session_proxy_quark,
    NULL
  );

  g_object_unref(node->shared.known.session);
  node->shared.known.session = NULL;
}

/* Required by infc_browser_node_free */
static void
infc_browser_remove_sync_in(InfcBrowser* browser,
                            InfcBrowserSyncIn* sync_in);

static void
infc_browser_node_free(InfcBrowser* browser,
                       InfcBrowserNode* node)
{
  InfcBrowserPrivate* priv;
  gboolean removed;

  GSList* item;
  InfcBrowserSyncIn* sync_in;

  priv = INFC_BROWSER_PRIVATE(browser);

  switch(node->type)
  {
  case INFC_BROWSER_NODE_SUBDIRECTORY:
    if(node->shared.subdir.explored == TRUE)
      while(node->shared.subdir.child != NULL)
        infc_browser_node_free(browser, node->shared.subdir.child);

    break;
  case INFC_BROWSER_NODE_NOTE_KNOWN:
    if(node->shared.known.session != NULL)
      infc_browser_session_remove_session(browser, node);

    break;
  case INFC_BROWSER_NODE_NOTE_UNKNOWN:
    g_free(node->shared.unknown.type);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  /* Remove sync-ins that sync into this node */
  for(item = priv->sync_ins; item != NULL; )
  {
    sync_in = (InfcBrowserSyncIn*)item->data;
    item = item->next;

    if(sync_in->node == node)
      infc_browser_remove_sync_in(browser, sync_in);
  }

  if(node->parent != NULL)
    infc_browser_node_unlink(node);

  removed = g_hash_table_remove(priv->nodes, GUINT_TO_POINTER(node->id));
  g_assert(removed == TRUE);

  g_free(node->name);
  g_slice_free(InfcBrowserNode, node);
}

/*
 * Signal handlers
 */

static void
infc_browser_session_close_cb(InfSession* session,
                              gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserIter* iter;
  InfcBrowserNode* node;

  browser = INFC_BROWSER(user_data);
  iter = (InfcBrowserIter*)g_object_get_qdata(
    G_OBJECT(session),
    infc_browser_session_proxy_quark
  );

  g_assert(iter != NULL);
  g_assert(
    g_hash_table_lookup(
      INFC_BROWSER_PRIVATE(browser)->nodes, GUINT_TO_POINTER(iter->node_id)
    ) == iter->node
  );

  node = (InfcBrowserNode*)iter->node;

  g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);
  g_assert(node->shared.known.session != NULL);
  g_assert(
    infc_session_proxy_get_session(node->shared.known.session) == session
  );

  infc_browser_session_remove_session(browser, node);
}

/* Required by infc_browser_release_connection() */
static void
infc_browser_connection_notify_status_cb(InfXmlConnection* connection,
                                         GParamSpec* pspec,
                                         gpointer user_data);

static void
infc_browser_release_connection(InfcBrowser* browser)
{
  InfcBrowserPrivate* priv;
  priv = INFC_BROWSER_PRIVATE(browser);

  /* Note that we do not remove the corresponding node that we sync in. We
   * lost the connection to the server anyway, so we do not care whether that
   * node exists on the server or not. */
  while(priv->sync_ins != NULL)
    infc_browser_remove_sync_in(browser, priv->sync_ins->data);

  /* TODO: Emit failed signal with some "canceled" error? */
  infc_request_manager_clear(priv->request_manager);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->connection),
    G_CALLBACK(infc_browser_connection_notify_status_cb),
    browser
  );

  if(priv->group != NULL)
  {
    /* Reset group since the browser's connection
     * is always the publisher. */
    inf_connection_manager_group_unref(priv->group);
    priv->group = NULL;
  }

  /* Keep tree so it is still accessible, however, we cannot explore
   * anything anymore. */

  g_object_unref(G_OBJECT(priv->connection));
  priv->connection = NULL;
}

static void
infc_browser_connection_notify_status_cb(InfXmlConnection* connection,
                                         GParamSpec* pspec,
                                         gpointer user_data)
{
  InfcBrowser* browser;
  InfXmlConnectionStatus status;

  browser = INFC_BROWSER(user_data);
  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSED ||
     status == INF_XML_CONNECTION_CLOSING)
  {
    /* Reset connection in case of closure */
    infc_browser_release_connection(browser);
  }
}

/*
 * Helper functions
 */

static xmlNodePtr
infc_browser_request_to_xml(InfcRequest* request)
{
  xmlNodePtr xml;
  gchar seq_buffer[16];

  xml = xmlNewNode(NULL, (const xmlChar*)infc_request_get_name(request));
  sprintf(seq_buffer, "%u", infc_request_get_seq(request));

  xmlNewProp(xml, (const xmlChar*)"seq", (const xmlChar*)seq_buffer);
  return xml;
}

const InfConnectionManagerMethodDesc*
infc_browser_lookup_named_method(InfcBrowser* browser,
                                 InfXmlConnection* connection,
                                 const gchar* method_name,
                                 GError** error)
{
  InfcBrowserPrivate* priv;
  gchar* network;
  const InfConnectionManagerMethodDesc* method;

  priv = INFC_BROWSER_PRIVATE(browser);
  g_object_get(G_OBJECT(connection), "network", &network, NULL);

  method = inf_method_manager_lookup_method(
    priv->method_manager,
    network,
    method_name
  );

  if(method == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_METHOD_UNSUPPORTED,
      "This session requires communication method `%s' which is not "
      "installed for network `%s'",
      method_name,
      network
    );

    g_free(network);
    return NULL;
  }

  g_free(network);
  return method;
}

/*
 * GObject overrides.
 */

static void
infc_browser_init(GTypeInstance* instance,
                  gpointer g_class)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  browser = INFC_BROWSER(instance);
  priv = INFC_BROWSER_PRIVATE(browser);

  priv->io = NULL;
  priv->connection_manager = NULL;
  priv->method_manager = NULL;
  priv->connection = NULL;
  priv->request_manager = infc_request_manager_new();

  priv->plugins = g_hash_table_new(g_str_hash, g_str_equal);
  priv->nodes = g_hash_table_new(NULL, NULL);
  priv->root = infc_browser_node_new_subdirectory(browser, NULL, 0, NULL);

  priv->sync_ins = NULL;
}

static GObject*
infc_browser_constructor(GType type,
                         guint n_construct_properties,
                         GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  gchar* network;
  const InfConnectionManagerMethodDesc* method;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);

  if(priv->method_manager == NULL)
    priv->method_manager = inf_method_manager_get_default();

  g_assert(priv->connection_manager != NULL);
  g_assert(priv->connection != NULL);

  g_object_get(priv->connection, "network", &network, NULL);

  method = inf_method_manager_lookup_method(
    priv->method_manager,
    network,
    "central"
  );

  if(method != NULL)
  {
    /* Join directory group of server */
    priv->group = inf_connection_manager_join_group(
      priv->connection_manager,
      "InfDirectory",
      priv->connection,
      INF_NET_OBJECT(browser),
      method
    );
  }
  else
  {
    infc_browser_release_connection(browser);

    g_warning(
      "Cannot connect to server since the \"central\" method could not be "
      "found for network \"%s\"",
      network
    );
  }

  g_free(network);
  return object;
}

static void
infc_browser_dispose(GObject* object)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);

  infc_browser_node_free(browser, priv->root);
  priv->root = NULL;

  g_hash_table_destroy(priv->nodes);
  priv->nodes = NULL;

  if(priv->connection != NULL)
    infc_browser_release_connection(browser);

  /* Should have been freed by infc_browser_release_connection */
  g_assert(priv->sync_ins == NULL);

  g_object_unref(priv->connection_manager);
  priv->connection_manager = NULL;

  g_object_unref(priv->method_manager);
  priv->method_manager = NULL;

  g_hash_table_destroy(priv->plugins);
  priv->plugins = NULL;

  g_object_unref(G_OBJECT(priv->request_manager));
  priv->request_manager = NULL;

  if(priv->io != NULL)
  {
    g_object_unref(G_OBJECT(priv->io));
    priv->io = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
infc_browser_set_property(GObject* object,
                          guint prop_id,
                          const GValue* value,
                          GParamSpec* pspec)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct only */
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_CONNECTION_MANAGER:
    g_assert(priv->connection_manager == NULL); /* construct/only */
    priv->connection_manager =
      INF_CONNECTION_MANAGER(g_value_dup_object(value));

    break;
  case PROP_CONNECTION:
    g_assert(priv->connection == NULL); /* construct/only */
    priv->connection = INF_XML_CONNECTION(g_value_dup_object(value));

    g_signal_connect(
      G_OBJECT(priv->connection),
      "notify::status",
      G_CALLBACK(infc_browser_connection_notify_status_cb),
      browser
    );

    break;
  case PROP_METHOD_MANAGER:
    g_assert(priv->method_manager == NULL);
    priv->method_manager = INF_METHOD_MANAGER(g_value_dup_object(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_browser_get_property(GObject* object,
                          guint prop_id,
                          GValue* value,
                          GParamSpec* pspec)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_CONNECTION_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->connection_manager));
    break;
  case PROP_CONNECTION:
    g_value_set_object(value, G_OBJECT(priv->connection));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * Directory tree operations.
 */
static void
infc_browser_node_register(InfcBrowser* browser,
                           InfcBrowserNode* node)
{
  InfcBrowserIter iter;
  iter.node_id = node->id;
  iter.node = node;

  g_signal_emit(G_OBJECT(browser), browser_signals[NODE_ADDED], 0, &iter);
}

static void
infc_browser_node_unregister(InfcBrowser* browser,
                             InfcBrowserNode* node)
{
  InfcBrowserIter iter;
  iter.node_id = node->id;
  iter.node = node;

  g_signal_emit(G_OBJECT(browser), browser_signals[NODE_REMOVED], 0, &iter);
}

static InfcBrowserNode*
infc_browser_node_add_subdirectory(InfcBrowser* browser,
                                   InfcBrowserNode* parent,
                                   guint id,
                                   const gchar* name,
                                   GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_assert(parent->type == INFC_BROWSER_NODE_SUBDIRECTORY);
  g_assert(parent->shared.subdir.explored == TRUE);

  priv = INFC_BROWSER_PRIVATE(browser);
  if(g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(id)) != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NODE_EXISTS,
      "Node with ID '%u' exists already",
      id
    );

    return NULL;
  }
  else
  {
    node = infc_browser_node_new_subdirectory(browser, parent, id, name);
    infc_browser_node_register(browser, node);
    return node;
  }
}

static InfcBrowserNode*
infc_browser_node_add_note(InfcBrowser* browser,
                           InfcBrowserNode* parent,
                           guint id,
                           const gchar* name,
                           const gchar* type,
                           GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_assert(parent->type == INFC_BROWSER_NODE_SUBDIRECTORY);
  g_assert(parent->shared.subdir.explored == TRUE);

  priv = INFC_BROWSER_PRIVATE(browser);
  if(g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(id)) != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NODE_EXISTS,
      "Node with ID '%u' exists already",
      id
    );

    return NULL;
  }
  else
  {
    node = infc_browser_node_new_note(browser, parent, id, name, type);
    infc_browser_node_register(browser, node);
    return node;
  }
}

static gboolean
infc_browser_node_remove(InfcBrowser* browser,
                         InfcBrowserNode* node,
                         GError** error)
{
  infc_browser_node_unregister(browser, node);
  infc_browser_node_free(browser, node);
  return TRUE;
}

/*
 * Sync-In
 */

static void
infc_browser_sync_in_synchronization_failed_cb(InfSession* session,
                                               InfXmlConnection* connection,
                                               GError* error,
                                               gpointer user_data)
{
  InfcBrowserSyncIn* sync_in;
  sync_in = (InfcBrowserSyncIn*)user_data;

  /* Ignore if this affects the synchronization to another connection */
  if(connection != sync_in->connection) return;

  infc_browser_node_remove(sync_in->browser, sync_in->node, NULL);
  infc_browser_remove_sync_in(sync_in->browser, sync_in);
}

static void
infc_browser_sync_in_synchronization_complete_cb(InfSession* session,
                                                 InfXmlConnection* connection,
                                                 gpointer user_data)
{
  InfcBrowserSyncIn* sync_in;

  sync_in = (InfcBrowserSyncIn*)user_data;

  /* Ignore if this affects the synchronization to another connection */
  if(connection != sync_in->connection) return;
  infc_browser_remove_sync_in(sync_in->browser, sync_in);
}

static InfcBrowserSyncIn*
infc_browser_add_sync_in(InfcBrowser* browser,
                         InfcBrowserNode* node,
                         InfXmlConnection* connection,
                         InfcSessionProxy* proxy)
{
  InfcBrowserPrivate* priv;
  InfcBrowserSyncIn* sync_in;

  priv = INFC_BROWSER_PRIVATE(browser);
  sync_in = g_slice_new(InfcBrowserSyncIn);

  sync_in->browser = browser;
  sync_in->node = node;
  /* Actually the same as browser's connection: */
  sync_in->connection = connection;
  sync_in->proxy = proxy;
  g_object_ref(proxy);

  g_signal_connect(
    G_OBJECT(infc_session_proxy_get_session(proxy)),
    "synchronization-failed",
    G_CALLBACK(infc_browser_sync_in_synchronization_failed_cb),
    sync_in
  );

  g_signal_connect(
    G_OBJECT(infc_session_proxy_get_session(proxy)),
    "synchronization-complete",
    G_CALLBACK(infc_browser_sync_in_synchronization_complete_cb),
    sync_in
  );

  priv->sync_ins = g_slist_prepend(priv->sync_ins, sync_in);
  return sync_in;
}

static void
infc_browser_remove_sync_in(InfcBrowser* browser,
                            InfcBrowserSyncIn* sync_in)
{
  InfcBrowserPrivate* priv;
  priv = INFC_BROWSER_PRIVATE(browser);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(infc_session_proxy_get_session(sync_in->proxy)),
    G_CALLBACK(infc_browser_sync_in_synchronization_complete_cb),
    sync_in
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(infc_session_proxy_get_session(sync_in->proxy)),
    G_CALLBACK(infc_browser_sync_in_synchronization_failed_cb),
    sync_in
  );

  g_object_unref(sync_in->proxy);
  g_slice_free(InfcBrowserSyncIn, sync_in);

  priv->sync_ins = g_slist_remove(priv->sync_ins, sync_in);
}

/*
 * Network command handling.
 */

static InfcBrowserNode*
infc_browser_get_node_from_xml(InfcBrowser* browser,
                               xmlNodePtr xml,
                               const gchar* attrib,
                               GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  guint node_id;
  gboolean has_node;

  priv = INFC_BROWSER_PRIVATE(browser);
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
  else
  {
    return node;
  }
}

static InfcBrowserNode*
infc_browser_get_node_from_xml_typed(InfcBrowser* browser,
                                     xmlNodePtr xml,
                                     const gchar* attrib,
                                     InfcBrowserNodeType mask,
                                     GError** error)
{
  InfcBrowserNode* node;

  g_assert(mask != 0);
  node = infc_browser_get_node_from_xml(browser, xml, attrib, error);

  if(node != NULL && (node->type & mask) == 0)
  {
    if(mask & INFC_BROWSER_NODE_SUBDIRECTORY)
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY,
        "%s",
        inf_directory_strerror(INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY)
      );
    }
    else
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NOT_A_NOTE,
        "%s",
        inf_directory_strerror(INF_DIRECTORY_ERROR_NOT_A_NOTE)
      );
    }

    return NULL;
  }
  else
  {
    return node;
  }
}

/* If initial_sync is TRUE, then the session is initially synchronized in the 
 * subscription group. Otherwise, an empty session is used. */
static gboolean
infc_browser_subscribe_session(InfcBrowser* browser,
                               InfcBrowserNode* node,
                               InfXmlConnection* connection,
                               xmlNodePtr xml,
                               gboolean initial_sync,
                               GError** error)
{
  InfcBrowserPrivate* priv;
  xmlChar* method_name;
  const InfConnectionManagerMethodDesc* method;
  xmlChar* group_name;
  InfConnectionManagerGroup* group;
  InfcSessionProxy* proxy;
  InfcBrowserIter iter;
  InfSession* session;

  priv = INFC_BROWSER_PRIVATE(browser);

  g_assert(node->shared.known.plugin != NULL);
  g_assert(node->shared.known.session == NULL);

  method_name = inf_xml_util_get_attribute_required(xml, "method", error);
  if(method_name == NULL) return FALSE;

  method = infc_browser_lookup_named_method(
    browser,
    connection,
    (const gchar*)method_name,
    error
  );

  xmlFree(method_name);
  if(method == NULL) return FALSE;

  group_name = inf_xml_util_get_attribute_required(xml, "group", error);
  if(group_name == NULL) return FALSE;

  /* Server is publisher */
  group = inf_connection_manager_join_group(
    priv->connection_manager,
    (const gchar*)group_name,
    connection,
    NULL,
    method
  );

  xmlFree(group_name);

  if(initial_sync)
  {
    session = node->shared.known.plugin->session_new(
      priv->io,
      priv->connection_manager,
      group,
      connection
    );
  }
  else
  {
    session = node->shared.known.plugin->session_new(
      priv->io,
      priv->connection_manager,
      NULL,
      NULL
    );
  }

  proxy = g_object_new(INFC_TYPE_SESSION_PROXY, "session", session, NULL);
  inf_connection_manager_group_set_object(group, INF_NET_OBJECT(proxy));
  infc_session_proxy_set_connection(proxy, group, connection);

  g_object_unref(session);
  inf_connection_manager_group_unref(group);

  iter.node_id = node->id;
  iter.node = node;

  g_signal_emit(
    G_OBJECT(browser),
    browser_signals[SUBSCRIBE_SESSION],
    0,
    &iter,
    proxy
  );

  /* The default handler refs the proxy */
  g_object_unref(G_OBJECT(proxy));
  return TRUE;
}

static gboolean
infc_browser_handle_explore_begin(InfcBrowser* browser,
                                  InfXmlConnection* connection,
                                  xmlNodePtr xml,
                                  GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  xmlChar* total_attr;
  guint total;

  guint node_id;
  InfcBrowserNode* node;

  priv = INFC_BROWSER_PRIVATE(browser);
  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    "explore-node",
    xml,
    error
  );

  if(request == NULL) return FALSE;
  g_assert(INFC_IS_EXPLORE_REQUEST(request));

  /* TODO: Consider non-given total attribute as an error */
  total_attr = xmlGetProp(xml, (const xmlChar*)"total");
  total = 0;
  if(total_attr != NULL)
  {
    total = strtoul((const char*)total_attr, NULL, 10);
    xmlFree(total_attr);
  }

  node_id = infc_explore_request_get_node_id(INFC_EXPLORE_REQUEST(request));
  node = g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id));

  if(node == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NO_SUCH_NODE,
      "Node to explore does no longer exist"
    );

    return FALSE;
  }
  else if(node->type != INFC_BROWSER_NODE_SUBDIRECTORY)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY,
      "Node to explore is not a subdirectory"
    );

    return FALSE;
  }
  else if(node->shared.subdir.explored == TRUE)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ALREADY_EXPLORED,
      "Node to explore is already explored"
    );

    return FALSE;
  }
  else
  {
    node->shared.subdir.explored = TRUE;
    infc_explore_request_initiated(INFC_EXPLORE_REQUEST(request), total);
    return TRUE;
  }
}

static gboolean
infc_browser_handle_explore_end(InfcBrowser* browser,
                                InfXmlConnection* connection,
                                xmlNodePtr xml,
                                GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  gboolean result;

  priv = INFC_BROWSER_PRIVATE(browser);
  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    "explore-node",
    xml,
    error
  );

  if(request == NULL) return FALSE;
  g_assert(INFC_IS_EXPLORE_REQUEST(request));

  result = infc_explore_request_finished(
    INFC_EXPLORE_REQUEST(request),
    error
  );

  infc_request_manager_remove_request(priv->request_manager, request);
  return result;
}

static gboolean
infc_browser_handle_add_node(InfcBrowser* browser,
                             InfXmlConnection* connection,
                             xmlNodePtr xml,
                             GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* parent;
  InfcBrowserNode* node;
  InfcBrowserIter iter;
  guint id;
  xmlChar* name;
  xmlChar* type;
  InfcRequest* request;
  GError* local_error;

  xmlNodePtr child;
  gboolean result;

  priv = INFC_BROWSER_PRIVATE(browser);

  if(inf_xml_util_get_attribute_uint_required(xml, "id", &id, error) == FALSE)
    return FALSE;

  parent = infc_browser_get_node_from_xml_typed(
    browser,
    xml,
    "parent",
    INFC_BROWSER_NODE_SUBDIRECTORY,
    error
  );

  if(parent == NULL) return FALSE;

  type = inf_xml_util_get_attribute_required(xml, "type", error);
  if(type == NULL) return FALSE;

  name = inf_xml_util_get_attribute_required(xml, "name", error);
  if(name == NULL)
  {
    xmlFree(type);
    return FALSE;
  }

  if(strcmp((const gchar*)type, "InfSubdirectory") == 0)
  {
    node = infc_browser_node_add_subdirectory(
      browser,
      parent,
      id,
      (const gchar*)name,
      error
    );

    xmlFree(type);
    xmlFree(name);

    if(node == NULL)
      return FALSE;
  }
  else
  {
    for(child = xml->children; child != NULL; child = child->next)
      if(strcmp((const char*)child->name, "subscribe"))
        break;

    node = infc_browser_node_add_note(
      browser,
      parent,
      id,
      (const gchar*)name,
      (const gchar*)type,
      error
    );

    xmlFree(type);
    xmlFree(name);
    if(node == NULL) return FALSE;

    if(child != NULL)
    {
      /* TODO: Currently we ignore if an error during subscription occurs.
       * However, we should somehow report that error, but still not abort
       * the node creation. */

      /* <subscribe/> in <add-node> is normally only used for newly created
       * nodes that require no synchronization which is why we pass %FALSE
       * for initial_sync here. */ 
      result = infc_browser_subscribe_session(
        browser,
        node,
        connection,
        child,
        FALSE,
        NULL
      );
    }
  }

  local_error = NULL;
  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    NULL,
    xml,
    &local_error
  );

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  if(request != NULL)
  {
    /* when seq was set, then we issued that add-node. We might
     * either do this implicitly by exploring a folder or explicitely by
     * sending an add-node request. */
    if(INFC_IS_EXPLORE_REQUEST(request))
    {
      return infc_explore_request_progress(
        INFC_EXPLORE_REQUEST(request),
        error
      );
    }
    else if(INFC_IS_NODE_REQUEST(request))
    {
      iter.node_id = node->id;
      iter.node = node;
      infc_node_request_finished(INFC_NODE_REQUEST(request), &iter);
      infc_request_manager_remove_request(priv->request_manager, request);
      return TRUE;
    }
    else
    {
      g_set_error(
        error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_INVALID_SEQ,
        "The request contains a sequence number refering to a request of "
        "type '%s', but a request of either 'explore' or 'add-node' was "
        "expected.",
        infc_request_get_name(request)
      );

      return FALSE;
    }
  }
  else
  {
    /* no seq was set, so this is add-note request was not issued by us */
    return TRUE;
  }
}

static gboolean
infc_browser_handle_sync_in(InfcBrowser* browser,
                            InfXmlConnection* connection,
                            xmlNodePtr xml,
                            GError** error)
{
  InfcBrowserPrivate* priv;
  guint id;
  InfcBrowserNode* parent;
  InfcRequest* request;
  InfSession* session;
  const InfcNotePlugin* plugin;
  InfConnectionManagerGroup* sync_group;
  InfcSessionProxy* proxy;
  const InfConnectionManagerMethodDesc* method;
  InfcBrowserNode* node;
  InfcBrowserIter iter;

  xmlChar* type;
  xmlChar* name;
  xmlChar* method_name;
  xmlChar* group_name;
  xmlNodePtr child;

  priv = INFC_BROWSER_PRIVATE(browser); 

  if(inf_xml_util_get_attribute_uint_required(xml, "id", &id, error) == FALSE)
    return FALSE;

  parent = infc_browser_get_node_from_xml_typed(
    browser,
    xml,
    "parent",
    INFC_BROWSER_NODE_SUBDIRECTORY,
    error
  );

  if(parent == NULL) return FALSE;

  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    "add-note",
    xml,
    error
  );

  /* Note that such a request MUST exist. We cannot sync something in without
   * knowing where to get the data to sync from. */
  if(!request) return FALSE;
  g_assert(INFC_IS_NODE_REQUEST(request));

  session = g_object_steal_qdata(
    G_OBJECT(request),
    infc_browser_sync_in_session_quark
  );

  plugin = g_object_steal_qdata(
    G_OBJECT(request),
    infc_browser_sync_in_plugin_quark
  );

  if(session == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_UNEXPECTED_SYNC_IN,
      "Received sync-in without having requested one"
    );

    return FALSE;
  }

  /* We always set both session and plugin quark: */
  g_assert(plugin != NULL);

  type = inf_xml_util_get_attribute_required(xml, "type", error);
  if(type == NULL) goto error_session;

  if(strcmp((const char*)type, plugin->note_type) != 0)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_UNEXPECTED_SYNC_IN,
      "Expected note type `%s' for sync-in, but received `%s'",
      plugin->note_type,
      (const gchar*)type
    );

    xmlFree(type);
    goto error_session;
  }
  xmlFree(type);

  method_name = inf_xml_util_get_attribute_required(xml, "method", error);
  if(method_name == NULL) { g_object_unref(session); return FALSE; }

  method = infc_browser_lookup_named_method(
    browser,
    connection,
    (const gchar*)method_name,
    error
  );

  xmlFree(method_name);
  if(method == NULL) goto error_session;

  name = inf_xml_util_get_attribute_required(xml, "name", error);
  if(name == NULL) goto error_session;

  group_name = inf_xml_util_get_attribute_required(xml, "group", error);
  if(group_name == NULL) { xmlFree(name); goto error_session; }

  /* TODO: The server might specify a different subscription group & method */
  for(child = xml->children; child != NULL; child = child->next)
    if(strcmp((const char*)child->name, "subscribe") == 0)
      break;

  /* Server is publisher */
  sync_group = inf_connection_manager_join_group(
    priv->connection_manager,
    (const gchar*)group_name,
    connection,
    NULL,
    method
  );

  xmlFree(group_name);

  proxy = g_object_new(INFC_TYPE_SESSION_PROXY, "session", session, NULL);
  inf_connection_manager_group_set_object(sync_group, INF_NET_OBJECT(proxy));

  inf_session_synchronize_to(session, sync_group, connection);
  g_object_unref(session);

  infc_browser_add_sync_in(browser, node, connection, proxy);

  node = infc_browser_node_add_note(
    browser,
    parent,
    id,
    (const gchar*)name,
    (const gchar*)type,
    error
  );

  xmlFree(name);

  iter.node_id = node->id;
  iter.node = node;

  if(child != NULL)
  {
    /* Make sure the session is not yet subscribed */
    g_assert(inf_session_get_subscription_group(session) == NULL);

    /* Subscribe to the newly created node. We don't need to do all the
     * stuff infc_browser_subscribe_session() does since we already created
     * the session, proxy and group. */
    infc_session_proxy_set_connection(proxy, sync_group, connection);

    g_signal_emit(
      G_OBJECT(browser),
      browser_signals[SUBSCRIBE_SESSION],
      0,
      &iter,
      proxy
    );
  }

  g_object_unref(proxy);

  /* TODO: Emit a signal, so that others are notified that a sync-in begins
   * and can show progress or something. */

  /* Don't finish request yet, wait for successful synchronization */
  infc_node_request_finished(INFC_NODE_REQUEST(request), &iter);
  infc_request_manager_remove_request(priv->request_manager, request);

  return TRUE;
error_session:
  g_object_unref(session);
  return FALSE;
}

static gboolean
infc_browser_handle_remove_node(InfcBrowser* browser,
                                InfXmlConnection* connection,
                                xmlNodePtr xml,
                                GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  InfcBrowserIter iter;

  priv = INFC_BROWSER_PRIVATE(browser);
  node = infc_browser_get_node_from_xml(browser, xml, "id", error);
  if(node == NULL) return FALSE;

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "remove-node",
    xml,
    error
  );

  if(request != NULL)
  {
    g_assert(INFC_IS_NODE_REQUEST(request));
    iter.node_id = node->id;
    iter.node = node;
    infc_node_request_finished(INFC_NODE_REQUEST(request), &iter);
    infc_request_manager_remove_request(priv->request_manager, request);
  }

  /* TODO: Make sure to not finish the request successfully before being
   * sure this returned TRUE. However, when the node is removed we no longer
   * can have an iter to it. */
  if(infc_browser_node_remove(browser, node, error) == FALSE)
    return FALSE;

  return TRUE;
}

static gboolean
infc_browser_handle_subscribe_session(InfcBrowser* browser,
                                      InfXmlConnection* connection,
                                      xmlNodePtr xml,
                                      GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  InfcBrowserIter iter;
  gboolean result;

  priv = INFC_BROWSER_PRIVATE(browser);

  node = infc_browser_get_node_from_xml_typed(
    browser,
    xml,
    "id",
    INFC_BROWSER_NODE_NOTE_KNOWN | INFC_BROWSER_NODE_NOTE_UNKNOWN,
    error
  );

  if(node == NULL) return FALSE;

  if(node->type == INFC_BROWSER_NODE_NOTE_UNKNOWN)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_TYPE_UNKNOWN,
      "Note type '%s' is not supported",
      node->shared.unknown.type
    );

    return FALSE;
  }

  if(node->shared.known.session != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED,
      "Already subscribed to this session"
    );

    return FALSE;
  }

  result = infc_browser_subscribe_session(
    browser,
    node,
    connection,
    xml,
    TRUE,
    error
  );
  if(!result) return FALSE;

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "subscribe-session",
    xml,
    NULL
  );

  /* We do this after having emitted the "subscribe-session" signal so that
   * the handlers of the InfcNodeRequest::finished signal can access the
   * new session via infc_browser_iter_get_session(), which is set by
   * the default signal handler of InfcBrowser::subscribe-session. */
  if(request != NULL)
  {
    g_assert(INFC_IS_NODE_REQUEST(request));
    infc_node_request_finished(INFC_NODE_REQUEST(request), &iter);
    infc_request_manager_remove_request(priv->request_manager, request);
  }

  return TRUE;
}

static gboolean
infc_browser_handle_save_session_in_progress(InfcBrowser* browser,
                                             InfXmlConnection* connection,
                                             xmlNodePtr xml,
                                             GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;

  priv = INFC_BROWSER_PRIVATE(browser);

  node = infc_browser_get_node_from_xml_typed(
    browser,
    xml,
    "id",
    INFC_BROWSER_NODE_NOTE_KNOWN,
    error
  );

  if(node == NULL) return FALSE;

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "save-session",
    xml,
    NULL
  );

  if(request != NULL)
  {
    g_assert(INFC_IS_NODE_REQUEST(request));
    /* TODO: Make a special save request that could now emit
     * an in-progress signal */
  }

  return TRUE;
}

static gboolean
infc_browser_handle_saved_session(InfcBrowser* browser,
                                  InfXmlConnection* connection,
                                  xmlNodePtr xml,
                                  GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  InfcBrowserIter iter;

  priv = INFC_BROWSER_PRIVATE(browser);

  node = infc_browser_get_node_from_xml_typed(
    browser,
    xml,
    "id",
    INFC_BROWSER_NODE_NOTE_KNOWN,
    error
  );

  if(node == NULL) return FALSE;

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "save-session",
    xml,
    NULL
  );

  if(request != NULL)
  {
    g_assert(INFC_IS_NODE_REQUEST(request));
    iter.node_id = node->id;
    iter.node = node;
    infc_node_request_finished(INFC_NODE_REQUEST(request), &iter);
    infc_request_manager_remove_request(priv->request_manager, request);
  }

  return TRUE;
}

static gboolean
infc_browser_handle_request_failed(InfcBrowser* browser,
                                   InfXmlConnection* connection,
                                   xmlNodePtr xml,
                                   GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserClass* browserc_class;
  InfcRequest* request;

  xmlChar* domain;
  gboolean has_code;
  guint code;
  GError* req_error;

  priv = INFC_BROWSER_PRIVATE(browser);
  browserc_class = INFC_BROWSER_GET_CLASS(browser);

  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    NULL,
    xml,
    error
  );

  if(request == NULL) return FALSE;

  has_code = inf_xml_util_get_attribute_uint_required(
    xml,
    "code",
    &code,
    error
  );

  if(has_code == FALSE) return FALSE;

  domain = inf_xml_util_get_attribute_required(xml, "domain", error);
  if(domain == NULL) return FALSE;

  req_error = NULL;

  if(g_quark_from_string((gchar*)domain) == inf_directory_error_quark())
  {
    g_set_error(
      &req_error,
      inf_directory_error_quark(),
      code,
      "%s",
      inf_directory_strerror(code)
    );
  }
  /* TODO: Can errors from the inf_request_error_quark() domain occur? */
  else
  {
    /* TODO: Look whether server has sent a human-readable error message
     * (which we cannot localize in that case, of course, but it is
     * probably better than nothing). */
    g_set_error(
      &req_error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_UNKNOWN_DOMAIN,
      "Error comes from unknown error domain '%s' (code %u)",
      (const gchar*)domain,
      code
    );
  }

  xmlFree(domain);

  infc_request_manager_fail_request(
    priv->request_manager,
    request,
    req_error
  );

  g_error_free(req_error);
  return TRUE;
}

/*
 * InfNetObject implementation
 */

static gboolean
infc_browser_net_object_received(InfNetObject* net_object,
                                 InfXmlConnection* connection,
                                 xmlNodePtr node,
                                 GError** error)
{
  if(strcmp((const gchar*)node->name, "request-failed") == 0)
  {
    infc_browser_handle_request_failed(
      INFC_BROWSER(net_object),
      connection,
      node,
      error
    );
  }
  else if(strcmp((const gchar*)node->name, "explore-begin") == 0)
  {
    infc_browser_handle_explore_begin(
      INFC_BROWSER(net_object),
      connection,
      node,
      error
    );
  }
  else if(strcmp((const gchar*)node->name, "explore-end") == 0)
  {
    infc_browser_handle_explore_end(
      INFC_BROWSER(net_object),
      connection,
      node,
      error
    );
  }
  else if(strcmp((const gchar*)node->name, "add-node") == 0)
  {
    infc_browser_handle_add_node(
      INFC_BROWSER(net_object),
      connection,
      node,
      error
    );
  }
  else if(strcmp((const gchar*)node->name, "sync-in") == 0)
  {
    infc_browser_handle_sync_in(
      INFC_BROWSER(net_object),
      connection,
      node,
      error
    );
  }
  else if(strcmp((const gchar*)node->name, "remove-node") == 0)
  {
    infc_browser_handle_remove_node(
      INFC_BROWSER(net_object),
      connection,
      node,
      error
    );
  }
  else if(strcmp((const gchar*)node->name, "subscribe-session") == 0)
  {
    infc_browser_handle_subscribe_session(
      INFC_BROWSER(net_object),
      connection,
      node,
      error
    );
  }
  else if(strcmp((const gchar*)node->name, "save-session-in-progress") == 0)
  {
    infc_browser_handle_save_session_in_progress(
      INFC_BROWSER(net_object),
      connection,
      node,
      error
    );
  }
  else if(strcmp((const gchar*)node->name, "saved-session") == 0)
  {
    infc_browser_handle_saved_session(
      INFC_BROWSER(net_object),
      connection,
      node,
      error
    );
  }
  else
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE,
      "%s",
      inf_directory_strerror(INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE)
    );
  }

  /* Browser is client-side anyway, so we should not even need to forward
   * anything. */
  return FALSE;
}

/*
 * Default signal handlers
 */

static void
infc_browser_subscribe_session_impl(InfcBrowser* browser,
                                    InfcBrowserIter* iter,
                                    InfcSessionProxy* proxy)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfSession* session;

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;
  
  g_assert(
    g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(iter->node_id)) ==
    node
  );

  g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);
  g_assert(node->shared.known.session == NULL);

  node->shared.known.session = proxy;

  g_object_ref(G_OBJECT(proxy));

  session = infc_session_proxy_get_session(proxy);

  /* Associate the iter to the session so that we can remove the proxy
   * from that item when the session is closed. */
  g_object_set_qdata_full(
    G_OBJECT(session),
    infc_browser_session_proxy_quark,
    infc_browser_iter_copy(iter),
    (GDestroyNotify)infc_browser_iter_free
  );

  /* connect_after so that we release the reference to the object after it
   * was closed. Otherwise, we would trigger another close signal when
   * disposing the session before the default handler of the "close" signal
   * ran. */
  g_signal_connect_after(
    session,
    "close",
    G_CALLBACK(infc_browser_session_close_cb),
    browser
  );
}

/*
 * GType registration.
 */

static void
infc_browser_class_init(gpointer g_class,
                        gpointer class_data)
{
  GObjectClass* object_class;
  InfcBrowserClass* browser_class;

  object_class = G_OBJECT_CLASS(g_class);
  browser_class = INFC_BROWSER_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcBrowserPrivate));

  object_class->constructor = infc_browser_constructor;
  object_class->dispose = infc_browser_dispose;
  object_class->set_property = infc_browser_set_property;
  object_class->get_property = infc_browser_get_property;

  browser_class->node_added = NULL;
  browser_class->node_removed = NULL;
  browser_class->begin_explore = NULL;
  browser_class->begin_subscribe = NULL;
  browser_class->subscribe_session = infc_browser_subscribe_session_impl;

  infc_browser_session_proxy_quark = g_quark_from_static_string(
    "infc-browser-session-proxy-quark"
  );
  
  infc_browser_sync_in_session_quark = g_quark_from_static_string(
    "infc-browser-sync-in-session-quark"
  );
  
  infc_browser_sync_in_plugin_quark = g_quark_from_static_string(
    "infc-browser-sync-in-plugin-quark"
  );

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "The InfIo to schedule timeouts",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CONNECTION_MANAGER,
    g_param_spec_object(
      "connection-manager",
      "Connection manager",
      "The connection manager for the browser",
      INF_TYPE_CONNECTION_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CONNECTION,
    g_param_spec_object(
      "connection",
      "Server connection",
      "Connection to the server exposing the directory to browse",
      INF_TYPE_XML_CONNECTION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_METHOD_MANAGER,
    g_param_spec_object(
      "method-manager",
      "Method manager",
      "Method manager to load communication methods from",
      INF_TYPE_METHOD_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  /**
   * InfcBrowser::node-added:
   * @browser: The #InfcBrowser emitting the siganl.
   * @iter: A #InfcBrowserIter pointing to the created node.
   *
   * Emitted when a new node was added in the browser. This can happen either
   * while exploring a subdirectory, or when a new node was added on the
   * server. In the latter case the signal is only emitted when the
   * parent directory of the newly created node is already explored.
   **/
  browser_signals[NODE_ADDED] = g_signal_new(
    "node-added",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcBrowserClass, node_added),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INFC_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
  );

  /**
   * InfcBrowser::node-removed:
   * @browser: The #InfcBrowser emitting the siganl.
   * @iter: A #InfcBrowserIter pointing to the removed node.
   *
   * This signal is emitted every time a node is removed from the browser.
   * This happens when the corresponding node is removed at the server. The
   * signal is emitted only when the parent directory of the removed node
   * is already explored. The signal can also be emitted on non-empty
   * subdirectory nodes in which case all children are also removed.
   **/
  browser_signals[NODE_REMOVED] = g_signal_new(
    "node-removed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcBrowserClass, node_removed),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INFC_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE
  );


  /**
   * InfcBrowser::subscribe-session:
   * @browser: The #InfcBrowser emitting the siganl.
   * @iter: A #InfcBrowserIter pointing to the subscribed node.
   * @proxy: A #InfcSessionProxy for the subscribed session.
   *
   * Emitted when subscribed to a session. The subscription was successful,
   * but the synchronization (the server sending the initial session state)
   * might still fail. Use #InfSession::synchronization-complete and
   * #InfSession::synchronization-failed to be notified.
   **/
  browser_signals[SUBSCRIBE_SESSION] = g_signal_new(
    "subscribe-session",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcBrowserClass, subscribe_session),
    NULL, NULL,
    inf_marshal_VOID__BOXED_OBJECT,
    G_TYPE_NONE,
    2,
    INFC_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INFC_TYPE_SESSION_PROXY
  );

  /**
   * InfcBrowser::begin-explore:
   * @browser: The #InfcBrowser emitting the siganl.
   * @iter: A #InfcBrowserIter pointing to the node being explored.
   * @request: A #InfcExploreRequest for the operation.
   *
   * This signal is emitted when a subdirectory is started to be explored.
   * @request can be used to get notified when the exploration was finished.
   **/
  browser_signals[BEGIN_EXPLORE] = g_signal_new(
    "begin-explore",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcBrowserClass, begin_explore),
    NULL, NULL,
    inf_marshal_VOID__BOXED_OBJECT,
    G_TYPE_NONE,
    2,
    INFC_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INFC_TYPE_EXPLORE_REQUEST
  );

  /**
   * InfcBrowser::begin-subscribe:
   * @browser: The #InfcBrowser emitting the signal.
   * @iter: A #InfcBrowserIter pointing to the node to which the subscription
   * starts.
   * @request: A #InfcNodeRequest for the operation.
   *
   * This signal is emitted whenever a subscription request for a
   * (non-subdirectory) node is made. Note that the subscription may still
   * fail (connect to #InfcNodeRequest::finished and #InfcRequest::failed
   * to be notified).
   **/
  browser_signals[BEGIN_SUBSCRIBE] = g_signal_new(
    "begin-subscribe",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcBrowserClass, begin_subscribe),
    NULL, NULL,
    inf_marshal_VOID__BOXED_OBJECT,
    G_TYPE_NONE,
    2,
    INFC_TYPE_BROWSER_ITER | G_SIGNAL_TYPE_STATIC_SCOPE,
    INFC_TYPE_NODE_REQUEST
  );
}

static void
infc_browser_net_object_init(gpointer g_iface,
                             gpointer iface_data)
{
  InfNetObjectIface* iface;
  iface = (InfNetObjectIface*)g_iface;

  iface->received = infc_browser_net_object_received;
}

GType
infc_browser_get_type(void)
{
  static GType browser_type = 0;

  if(!browser_type)
  {
    static const GTypeInfo browser_type_info = {
      sizeof(InfcBrowserClass),    /* class_size */
      NULL,                        /* base_init */
      NULL,                        /* base_finalize */
      infc_browser_class_init,     /* class_init */
      NULL,                        /* class_finalize */
      NULL,                        /* class_data */
      sizeof(InfcBrowser),         /* instance_size */
      0,                           /* n_preallocs */
      infc_browser_init,           /* instance_init */
      NULL                         /* value_table */
    };

    static const GInterfaceInfo net_object_info = {
      infc_browser_net_object_init,
      NULL,
      NULL
    };

    browser_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcBrowser",
      &browser_type_info,
      0
    );

    g_type_add_interface_static(
      browser_type,
      INF_TYPE_NET_OBJECT,
      &net_object_info
    );
  }

  return browser_type;
}

/*
 * Public API.
 */

/**
 * infc_browser_new:
 * @io: A #InfIo object used to schedule timeouts.
 * @connection_manager: A #InfConnectionManager to register the server
 * connection and which forwards incoming data to the blowser or running
 * sessions.
 * @method_manager: A #InfMethodManager to lookup required communication
 * methods.
 * @connection: Connection to the server.
 *
 * Creates a new #InfcBrowser.
 *
 * Return Value: A new #InfcBrowser.
 **/
InfcBrowser*
infc_browser_new(InfIo* io,
                 InfConnectionManager* manager,
                 InfMethodManager* method_manager,
                 InfXmlConnection* connection)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(method_manager == NULL ||
                       INF_IS_METHOD_MANAGER(method_manager), NULL);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), NULL);

  object = g_object_new(
    INFC_TYPE_BROWSER,
    "io", io,
    "connection-manager", manager,
    "method-manager", method_manager,
    "connection", connection,
    NULL
  );

  return INFC_BROWSER(object);
}

/**
 * infc_browser_get_connection_manager:
 * @browser: A #InfcBrowser.
 *
 * Returns the connection manager of this browser.
 *
 * Return Value: A #InfConnectionManager.
 **/
InfConnectionManager*
infc_browser_get_connection_manager(InfcBrowser* browser)
{
  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  return INFC_BROWSER_PRIVATE(browser)->connection_manager;
}

/**
 * infc_browser_get_connection:
 * @browser: A #InfcBrowser.
 *
 * Returns the connection to the server.
 *
 * Return Value: A #InfXmlConnection.
 **/
InfXmlConnection*
infc_browser_get_connection(InfcBrowser* browser)
{
  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  return INFC_BROWSER_PRIVATE(browser)->connection;
}

/**
 * infc_browser_add_plugin:
 * @browser: A #InfcBrowser.
 * @plugin: A #InfcNotePlugin.
 *
 * Adds @plugin to @browser. This allows the browser to create sessions of
 * the plugin's type. Only one plugin of each type can be added to the
 * directory.
 *
 * Return Value: Whether the plugin was added successfully.
 **/
gboolean
infc_browser_add_plugin(InfcBrowser* browser,
                        const InfcNotePlugin* plugin)
{
  InfcBrowserPrivate* priv;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(plugin != NULL, FALSE);

  priv = INFC_BROWSER_PRIVATE(browser);

  if(g_hash_table_lookup(priv->plugins, plugin->note_type) != NULL)
    return FALSE;

  g_hash_table_insert(
    priv->plugins,
    (gpointer)plugin->note_type,
    (gpointer)plugin
  );

  /* TODO: Check for yet unknown note types and make them known if they
   * match this plugin. */

  return TRUE;
}

/**
 * infc_browser_lookup_plugin:
 * @browser: A #InfcBrowser.
 * @note_type: A note type, such as "InfText".
 *
 * Returns a previously registered plugin (see infc_browser_add_plugin()) for
 * the given note type, or %NULL if there is no such plugin.
 *
 * Return Value: A #InfcNotePlugin, or %NULL.
 **/
const InfcNotePlugin*
infc_browser_lookup_plugin(InfcBrowser* browser,
                           const gchar* note_type)
{
  InfcBrowserPrivate* priv;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(note_type != NULL, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  return (const InfcNotePlugin*)g_hash_table_lookup(priv->plugins, note_type);
}

/**
 * infc_browser_iter_get_root:
 * @browser: A #InfcBrowser.
 * @iter: An uninitialized #InfcBrowserIter.
 *
 * Sets @iter to point to the root node of the browser tree.
 **/
void
infc_browser_iter_get_root(InfcBrowser* browser,
                           InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;

  g_return_if_fail(INFC_IS_BROWSER(browser));
  g_return_if_fail(iter != NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_assert(priv->root != NULL);

  iter->node_id = priv->root->id;
  iter->node = priv->root;
}

/**
 * infc_browser_iter_get_next:
 * @browser: A #InfcBrowser
 * @iter: A #InfcBrowserIter pointing to a node in @browser.
 *
 * Sets @iter to point to the next node within the same subdirectory. If
 * @iter already points to the last node, @iter is left untouched and
 * %FALSE is returned.
 *
 * Return Value: %TRUE if iter was set, %FALSE otherwise.
 **/
gboolean
infc_browser_iter_get_next(InfcBrowser* browser,
                           InfcBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(browser, iter, FALSE);

  node = (InfcBrowserNode*)iter->node;

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
 * infc_browser_iter_get_prev:
 * @browser: A #InfcBrowser
 * @iter: A #InfcBrowserIter pointing to a node in @browser.
 *
 * Sets @iter to point to the provious node within the same subdirectory. If
 * @iter already points to the first node, @iter is left untouched and
 * %FALSE is returned.
 *
 * Return Value: %TRUE if iter was set, %FALSE otherwise.
 **/
gboolean
infc_browser_iter_get_prev(InfcBrowser* browser,
                           InfcBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(browser, iter, FALSE);

  node = (InfcBrowserNode*)iter->node;

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
 * infc_browser_iter_get_parent:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a node in @browser.
 *
 * Sets @iter to point to the parent node. If @iter already points to the
 * root node, @iter is left untouched and the function returns %FALSE.
 *
 * Return Value: %TRUE if iter was set, %FALSE otherwise.
 **/
gboolean
infc_browser_iter_get_parent(InfcBrowser* browser,
                             InfcBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(browser, iter, FALSE);

  node = (InfcBrowserNode*)iter->node;

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
 * infc_browser_iter_get_explored:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a subdirectory node in @browser.
 *
 * Returns whether the subdirectory node @iter points to has been explored.
 *
 * Return Value: %TRUE if the subdirectory has been explored and %FALSE
 * otherwise.
 **/
gboolean
infc_browser_iter_get_explored(InfcBrowser* browser,
                               InfcBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(browser, iter, FALSE);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_return_val_if_subdir_fail(node, FALSE);

  return node->shared.subdir.explored;
}

/**
 * infc_browser_iter_get_child:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a subdirectory node in @brwoser that
 * has already been explored.
 *
 * Sets @iter to point to the first child of the subdirectory it is currently
 * pointing to. The subdirectory must already have been explored. If the
 * subdirectory has no children, @iter is left untouched and %FALSE is
 * returned.
 *
 * @Return Value: %TRUE if @iter was set, %FALSE otherwise.
 **/
gboolean
infc_browser_iter_get_child(InfcBrowser* browser,
                            InfcBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(browser, iter, FALSE);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_return_val_if_subdir_fail(node, FALSE);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, FALSE);

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
 * infc_browser_iter_explore:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a subdirectory node in @browser that
 * has not yet been explored.
 *
 * Explores the given subdirectory node. The returned request may be used
 * to get informed about exploration progress and completion. When the
 * exploration has been initiated, infc_browser_iter_get_child() may be called
 * to get the content that has so-far been explored. When the request has
 * been finished, all content is available.
 *
 * Return Value: A #InfcExploreRequest to watch the exploration process.
 **/
InfcExploreRequest*
infc_browser_iter_explore(InfcBrowser* browser,
                          InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_return_val_if_subdir_fail(node, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == FALSE, NULL);
  g_assert(infc_browser_iter_get_explore_request(browser, iter) == NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_EXPLORE_REQUEST,
    "explore-node",
    "node_id", node->id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_connection_manager_group_send_to_connection(
    priv->group,
    priv->connection,
    xml
  );

  g_signal_emit(
    G_OBJECT(browser),
    browser_signals[BEGIN_EXPLORE],
    0,
    iter,
    request
  );

  return INFC_EXPLORE_REQUEST(request);
}

/**
 * infc_browser_iter_get_name:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a node in @browser.
 *
 * Returns the name of the node @iter points to.
 *
 * Return Value: The node's name. The returned string must not be freed.
 **/
const gchar*
infc_browser_iter_get_name(InfcBrowser* browser,
                           InfcBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  return node->name;
}

/**
 * infc_browser_iter_is_subdirectory:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a node in @browser.
 *
 * Returns whether @iter points to a subdirectory node or not.
 *
 * Return Value: Whether the node @iter points to is a subdirectory node.
 **/
gboolean
infc_browser_iter_is_subdirectory(InfcBrowser* browser,
                                  InfcBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(browser, iter, FALSE);

  node = (InfcBrowserNode*)iter->node;
  if(node->type == INFC_BROWSER_NODE_SUBDIRECTORY)
    return TRUE;

  return FALSE;
}

/**
 * infc_browser_add_subdirectory:
 * @browser: A #InfcBrowser.
 * @parent: A #InfcBrowserIter pointing to an explored subdirectory in which
 * to create the new subdirectory.
 * @name: The name for the new subdirectory.
 *
 * Creates a new request that asks the server to create a new subdirectory.
 * Note that the parent subdirectory must already have been explored.
 *
 * Return Value: A #InfcNodeRequest to be notified when the request
 * finishes or fails.
 **/
InfcNodeRequest*
infc_browser_add_subdirectory(InfcBrowser* browser,
                              InfcBrowserIter* parent,
                              const gchar* name)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, parent, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  node = (InfcBrowserNode*)parent->node;
  infc_browser_return_val_if_subdir_fail(node, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "add-node",
    "node-id", parent->node_id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "parent", node->id);
  inf_xml_util_set_attribute(xml, "type", "InfSubdirectory");
  inf_xml_util_set_attribute(xml, "name", name);

  inf_connection_manager_group_send_to_connection(
    priv->group,
    priv->connection,
    xml
  );

  return INFC_NODE_REQUEST(request);
}

/**
 * infc_browser_add_note:
 * @browser: A #InfcBrowser.
 * @parent: A #InfcBrowserIter pointing to an explored subdirectory.
 * @name: Name for the new node.
 * @plugin: Type of the new node.
 * @initial_subscribe: Whether to automatically subscribe to the newly created
 * node.
 *
 * Asks the server to create a new note with the given type. The returned
 * request may be used to be notified when the request finishes or fails.
 *
 * If @initial_subscribe is set, then, when the returned request finishes,
 * you might call infc_browser_iter_get_session() on the resulting
 * #InfcBrowserIter. However, that function is not guaranteed to return
 * non-%NULL in this case since the node might have been created, but the
 * subscription could have failed.
 *
 * Return Value: A #InfcNodeRequest.
 **/
InfcNodeRequest*
infc_browser_add_note(InfcBrowser* browser,
                      InfcBrowserIter* parent,
                      const gchar* name,
                      const InfcNotePlugin* plugin,
                      gboolean initial_subscribe)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, parent, NULL);
  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail(plugin != NULL, NULL);

  node = (InfcBrowserNode*)parent->node;
  infc_browser_return_val_if_subdir_fail(node, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "add-node",
    "node-id", parent->node_id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "parent", node->id);
  inf_xml_util_set_attribute(xml, "type", plugin->note_type);
  inf_xml_util_set_attribute(xml, "name", name);

  if(initial_subscribe != FALSE)
    xmlNewChild(xml, NULL, (const xmlChar*)"subscribe", NULL);

  inf_connection_manager_group_send_to_connection(
    priv->group,
    priv->connection,
    xml
  );

  return INFC_NODE_REQUEST(request);
}

/**
 * infc_browser_add_note_with_content:
 * @browser: A #InfcBrowser.
 * @parent: A #InfcBrowserIter pointing to an explored subdirectory.
 * @name: Name for the new node.
 * @plugin: Type of the new node.
 * @session: A session that is copied to the server and used as initial
 * content for the new node.
 * @initial_subscribe: Whether to automatically subscribe to the newly created
 * node.
 *
 * Asks the server to create a new note with the given type. The returned
 * request may be used to be notified when the request finishes or fails.
 *
 * The returned request finishes as soon as the server acknowledges the
 * creation of the node, which is before the content is transmitted. If,
 * during transmission, an error occurs, then the node is removed again. To
 * get notified when the transmission fails, finishes or changes in progress,
 * you can connect to the InfSession::synchronization-failed,
 * InfSession::synchronization-complete and
 * InfSession::synchronization-progress signals. Note that a single session
 * might be synchronized to multiple servers at the same time, you will have
 * to check the connection parameter in the signal hander to find out to
 * which server the session is synchronized.
 *
 * You can safely unref session after having called this function. If the
 * request or the synchronization fails, the session will be discarded in
 * that case. When the returned request finishes, you can use
 * infc_browser_iter_get_sync_in() to get the session again.
 *
 * If @initial_subscribe is set, then, when the returned request finishes,
 * you might call infc_browser_iter_get_session() on the resulting
 * #InfcBrowserIter. However, that function is not guaranteed to return
 * non-%NULL in this case since the node might have been created, but the
 * subscription could have failed.
 *
 * Return Value: A #InfcNodeRequest.
 **/
InfcNodeRequest*
infc_browser_add_note_with_content(InfcBrowser* browser,
                                   InfcBrowserIter* parent,
                                   const gchar* name,
                                   const InfcNotePlugin* plugin,
                                   InfSession* session,
                                   gboolean initial_subscribe)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, parent, NULL);
  g_return_val_if_fail(name != NULL, NULL);
  g_return_val_if_fail(plugin != NULL, NULL);
  g_return_val_if_fail(INF_IS_SESSION(session), NULL);

  g_return_val_if_fail(
    inf_session_get_status(session) == INF_SESSION_RUNNING,
    NULL
  );

  /* Can only subscribe if that session is not already subscribed elsewhere */
  g_return_val_if_fail(
    !initial_subscribe ||
    inf_session_get_subscription_group(session) == NULL,
    NULL
  );

  node = (InfcBrowserNode*)parent->node;
  infc_browser_return_val_if_subdir_fail(node, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);

  /* TODO: Add a InfcSyncInRequest, deriving from InfcNodeRequest that
   * carries session and plugin, so we don't need g_object_set_qdata for the
   * session and plugin. */
  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "add-node",
    "node-id", parent->node_id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "parent", node->id);
  inf_xml_util_set_attribute(xml, "type", plugin->note_type);
  inf_xml_util_set_attribute(xml, "name", name);

  if(initial_subscribe != FALSE)
    xmlNewChild(xml, NULL, (const xmlChar*)"subscribe", NULL);
  xmlNewChild(xml, NULL, (const xmlChar*)"sync-in", NULL);

  inf_connection_manager_group_send_to_connection(
    priv->group,
    priv->connection,
    xml
  );

  g_object_ref(session);
  g_object_set_qdata_full(
    G_OBJECT(request),
    infc_browser_sync_in_session_quark,
    session,
    g_object_unref
  );

  g_object_set_qdata(
    G_OBJECT(request),
    infc_browser_sync_in_plugin_quark,
    (gpointer)plugin
  );

  return INFC_NODE_REQUEST(request);
}

/**
 * infc_browser_remove_node:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a node in @browser.
 *
 * Asks the server to remove the node @iter points to.
 *
 * Return Value: A #InfcNodeRequest that may be used to get notified when
 * the request finishes or fails.
 **/
InfcNodeRequest*
infc_browser_remove_node(InfcBrowser* browser,
                         InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  /* TODO: Check that there is not a remove-node request already enqueued. */

  g_return_val_if_fail(priv->connection != NULL, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "remove-node",
    "node-id", iter->node_id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_connection_manager_group_send_to_connection(
    priv->group,
    priv->connection,
    xml
  );

  return INFC_NODE_REQUEST(request);
}

/**
 * infc_browser_iter_get_note_type:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note inside @browser.
 *
 * Returns the  type of the note @iter points to. This must not be a
 * subdirectory node.
 *
 * Return Value: The note's type.
 **/
const gchar*
infc_browser_iter_get_note_type(InfcBrowser* browser,
                                InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  switch(node->type)
  {
  case INFC_BROWSER_NODE_SUBDIRECTORY:
    g_return_val_if_reached(NULL);
    return NULL;
  case INFC_BROWSER_NODE_NOTE_KNOWN:
    return node->shared.known.plugin->note_type;
  case INFC_BROWSER_NODE_NOTE_UNKNOWN:
    return node->shared.unknown.type;
  default:
    g_assert_not_reached();
    return NULL;
  }
}

/**
 * infc_browser_iter_get_plugin:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note inside @browser.
 *
 * Returns the #InfcNodePlugin that is used for subscriptions to the note
 * @iter points to, or %NULL if no plugin for the note's type has been
 * registered.
 *
 * Return Value: A #InfcNotePlugin, or %NULL.
 **/
const InfcNotePlugin*
infc_browser_iter_get_plugin(InfcBrowser* browser,
                             InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  switch(node->type)
  {
  case INFC_BROWSER_NODE_SUBDIRECTORY:
    g_return_val_if_reached(NULL);
    return NULL;
  case INFC_BROWSER_NODE_NOTE_KNOWN:
    return node->shared.known.plugin;
  case INFC_BROWSER_NODE_NOTE_UNKNOWN:
    return NULL;
  default:
    g_assert_not_reached();
    return NULL;
  }
}

/**
 * infc_browser_iter_subscribe_session:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note inside @browser.
 *
 * Subscribes to the given note. When the request has finished (which does
 * not mean that the subscription has finished, but the server is ready to
 * perform the subscription), infc_browser_iter_get_session() can be used
 * to access the #InfcSessionProxy object representing the subscription.
 *
 * Return Value: A #InfcNodeRequest that may be used to get notified when
 * the request finishes or fails.
 **/
InfcNodeRequest*
infc_browser_iter_subscribe_session(InfcBrowser* browser,
                                    InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(node->type == INFC_BROWSER_NODE_NOTE_KNOWN, NULL);
  g_return_val_if_fail(node->shared.known.session == NULL, NULL);

  g_return_val_if_fail(
    infc_browser_iter_get_subscribe_request(browser, iter) == NULL,
    NULL
  );

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "subscribe-session",
    "node-id", iter->node_id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_connection_manager_group_send_to_connection(
    priv->group,
    priv->connection,
    xml
  );

  g_signal_emit(
    G_OBJECT(browser),
    browser_signals[BEGIN_SUBSCRIBE],
    0,
    iter,
    request
  );

  return INFC_NODE_REQUEST(request);
}

/**
 * infc_browser_iter_save_session:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note in @browser.
 *
 * Requests that the server saves the note pointed to by @iter into its
 * background storage. Normally, the server only does this when it is either
 * shut down or when the there are no more subscriptions to the note. Note that
 * this is merely a request and the server might decide not to save the
 * session for whatever reason.
 *
 * Return Value: A #InfcNodeRequest that may be used to get notified when
 * the request finishes or fails.
 **/
InfcNodeRequest*
infc_browser_iter_save_session(InfcBrowser* browser,
                               InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(node->type == INFC_BROWSER_NODE_NOTE_KNOWN, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "save-session",
    "node-id", iter->node_id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_connection_manager_group_send_to_connection(
    priv->group,
    priv->connection,
    xml
  );

  return INFC_NODE_REQUEST(request);
}

/**
 * infc_browser_iter_get_session:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note in @browser.
 *
 * Returns the #InfcSessionProxy representing the subscription to the given
 * note, if the client is subscribed, and %NULL otherwise.
 *
 * Return Value: A #InfcSessionProxy, or %NULL if not subscribed.
 **/
InfcSessionProxy*
infc_browser_iter_get_session(InfcBrowser* browser,
                              InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  if(node->type != INFC_BROWSER_NODE_NOTE_KNOWN) return NULL;
  return node->shared.known.session;
}

/**
 * infc_browser_iter_get_sync_in:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note in @browser.
 *
 * Returns the #InfcSessionProxy that is used to synchronize the note @iter
 * points to to the server. Such a node is created by
 * infc_browser_iter_add_note_with_content(). If the client is subscribed to
 * this note, then this returns the same session as
 * infc_browser_iter_get_session(). However, it is possible that we
 * synchronize this node to the server without being subscribed to it. In
 * this case, this function returns the session that does the synchronization,
 * while infc_browser_iter_get_session() would return %NULL.
 *
 * Return Value: A #InfcSessionProxy, or %NULL if we are currently not
 * synchronizing this node to the server.
 **/
InfcSessionProxy*
infc_browser_iter_get_sync_in(InfcBrowser* browser,
                              InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  GSList* item;
  InfcBrowserSyncIn* sync_in;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  for(item = priv->sync_ins; item != NULL; item = item->next)
  {
    sync_in = (InfcBrowserSyncIn*)item->data;
    if(sync_in->node == iter->node)
      return sync_in->proxy;
  }

  return NULL;
}

/**
 * infc_browser_iter_get_subscribe_request:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note in @browser.
 *
 * Returns the #InfcNodeRequest that represents the subscription request sent
 * for the note @iter points to. Returns %NULL if we are already subscribed
 * to that node, or no subscription request has been sent. In the former
 * case infc_browser_iter_get_session() will return the #InfcSessionProxy for
 * the subscription.
 *
 * Return Value: A #InfcNodeRequest, or %NULL.
 **/
InfcNodeRequest*
infc_browser_iter_get_subscribe_request(InfcBrowser* browser,
                                        InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcBrowserIterGetNodeRequestForeachData data;

  data.iter = iter;
  data.result = NULL;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  priv = INFC_BROWSER_PRIVATE(browser);

  infc_request_manager_foreach_named_request(
    priv->request_manager,
    "subscribe-session",
    infc_browser_iter_get_node_request_foreach_func,
    &data
  );

  return data.result;
}

/**
 * infc_browser_iter_get_explore_request:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a subdirectory node in @browser.
 *
 * Returns the #InfcExploreRequest with which the node @iter points to is
 * currenty explored. Returns %NULL if that node is already explored or is
 * not currently explored.
 *
 * Return Value: A #InfcExploreRequest, or %NULL.
 **/
InfcExploreRequest*
infc_browser_iter_get_explore_request(InfcBrowser* browser,
                                      InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcBrowserIterGetExploreRequestForeachData data;

  data.iter = iter;
  data.result = NULL;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_return_val_if_subdir_fail(node, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);

  infc_request_manager_foreach_named_request(
    priv->request_manager,
    "explore-node",
    infc_browser_iter_get_explore_request_foreach_func,
    &data
  );

  return data.result;
}

/**
 * infc_browser_iter_get_sync_in_requests:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a subdirectory node in @browser.
 *
 * Returns a list of all #InfcNodeRequest created with
 * infc_browser_add_note_with_content() with the node @iter points to as
 * parent. Such requests begin a synchronization to the server when they
 * have finished.
 *
 * Return Value: A list of #InfcNodeRequest<!-- -->s. Free with g_slist_free()
 * when done.
 **/
GSList*
infc_browser_iter_get_sync_in_requests(InfcBrowser* browser,
                                       InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcBrowserIterGetSyncInRequestsForeachData data;

  data.iter = iter;
  data.result = NULL;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_return_val_if_subdir_fail(node, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);

  infc_request_manager_foreach_named_request(
    priv->request_manager,
    "add-node",
    infc_browser_iter_get_sync_in_requests_foreach_func,
    &data
  );

  return data.result;
}

/**
 * infc_browser_iter_from_node_request:
 * @browser: A #InfcBrowser.
 * @request: A #InfcNodeRequest issued by @browser.
 * @iter: A #InfcBrowserIter.
 *
 * Sets @iter to point to the node @request is related to. If there is no such
 * node (someone could have deleted it while the request is still running),
 * the function returns %FALSE and @iter is unchanged.
 *
 * Return Value: %TRUE if @iter was set, %FALSE otherwise.
 **/
gboolean
infc_browser_iter_from_node_request(InfcBrowser* browser,
                                    InfcNodeRequest* request,
                                    InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  guint node_id;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(INFC_IS_NODE_REQUEST(request), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_object_get(G_OBJECT(request), "node-id", &node_id, NULL);

  node = g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id));
  if(node == NULL) return FALSE;

  iter->node_id = node_id;
  iter->node = node;
  return TRUE;
}

/**
 * infc_browser_iter_from_explore_request:
 * @browser: A #InfcBrowser.
 * @request: A #InfcExploreRequest exploring a node in @browser.
 * @iter: A #InfcBrowserIter.
 *
 * Sets @iter to the node @request is currently exploring. If there is no such
 * node (someone could have deleted it while exploring), the function returns
 * %FALSE and lets @iter untouched.
 *
 * Return Value: %TRUE if @iter was set, %FALSE otherwise.
 **/
gboolean
infc_browser_iter_from_explore_request(InfcBrowser* browser,
                                       InfcExploreRequest* request,
                                       InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  guint node_id;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(INFC_IS_EXPLORE_REQUEST(request), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_object_get(G_OBJECT(request), "node-id", &node_id, NULL);

  node = g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id));
  if(node == NULL) return FALSE;

  iter->node_id = node_id;
  iter->node = node;
  return TRUE;
}

/* vim:set et sw=2 ts=2: */
