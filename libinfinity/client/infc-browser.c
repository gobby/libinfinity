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
      InfcSession* session;
      InfcNotePlugin* plugin;
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

typedef struct _InfcBrowserPrivate InfcBrowserPrivate;
struct _InfcBrowserPrivate {
  InfConnectionManager* connection_manager;
  InfConnectionManagerGroup* group; /* TODO: This should be a property */
  InfXmlConnection* connection;

  InfcRequestManager* request_manager;

  GHashTable* plugins; /* Registered plugins */

  GHashTable* nodes; /* Mapping from id to node */
  InfcBrowserNode* root;
};

#define INFC_BROWSER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_BROWSER, InfcBrowserPrivate))

enum {
  PROP_0,

  PROP_CONNECTION_MANAGER,
  PROP_CONNECTION
};

enum {
  NODE_ADDED,
  NODE_REMOVED,

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

  if(plugin == NULL)
    node->shared.known.plugin = plugin;
  else
    node->shared.unknown.type = g_strdup(type);

  return node;
}

static void
infc_browser_node_free(InfcBrowser* browser,
                       InfcBrowserNode* node)
{
  InfcBrowserPrivate* priv;
  gboolean removed;

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
      g_object_unref(G_OBJECT(node->shared.known.session));

    break;
  case INFC_BROWSER_NODE_NOTE_UNKNOWN:
    g_free(node->shared.unknown.type);
    break;
  default:
    g_assert_not_reached();
    break;
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
infc_browser_set_connection(InfcBrowser* browser,
                            InfXmlConnection* connection);

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
    infc_browser_set_connection(browser, NULL);
  }
}

/*
 * Helper functions
 */

static void
infc_browser_set_connection_manager(InfcBrowser* browser,
                                    InfConnectionManager* manager)
{
  InfcBrowserPrivate* priv;
  priv = INFC_BROWSER_PRIVATE(browser);

  if(priv->connection_manager != NULL)
  {
    if(priv->connection != NULL)
    {
      inf_connection_manager_unref_connection(
        priv->connection_manager,
        priv->group,
        priv->connection
      );
    }
    
    inf_connection_manager_unref_group(
      priv->connection_manager,
      priv->group
    );

    g_object_unref(G_OBJECT(priv->connection_manager));
  }

  priv->connection_manager = manager;

  if(manager != NULL)
  {
    g_object_ref(G_OBJECT(manager));

    priv->group = inf_connection_manager_create_group(
      priv->connection_manager,
      "InfDirectory",
      INF_NET_OBJECT(browser)
    );

    if(priv->connection != NULL)
    {
      inf_connection_manager_ref_connection(
        priv->connection_manager,
        priv->group,
        priv->connection
      );
    }
  }
  else
  {
    priv->group = NULL;
  }

  g_object_notify(G_OBJECT(browser), "connection-manager");
}

static void
infc_browser_set_connection(InfcBrowser* browser,
                            InfXmlConnection* connection)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* child;

  priv = INFC_BROWSER_PRIVATE(browser);

  if(priv->connection != NULL)
  {
    /* TODO: Emit failed signal with some "canceled" error? */
    infc_request_manager_clear(priv->request_manager);

    /* Clear tree, priv->root may be NULL if called from dispose */
    if(priv->root != NULL && priv->root->shared.subdir.explored == TRUE)
    {
      while((child = priv->root->shared.subdir.child) != NULL)
      {
        /* TODO: Unregister node (signal) */
        infc_browser_node_free(browser, child);
      }
    }

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->connection),
      G_CALLBACK(infc_browser_connection_notify_status_cb),
      browser
    );

    if(priv->connection_manager != NULL)
    {
      inf_connection_manager_unref_connection(
        priv->connection_manager,
        priv->group,
        priv->connection
      );
    }

    g_object_unref(G_OBJECT(priv->connection));
  }

  priv->connection = connection;

  if(connection != NULL)
  {
    g_object_ref(G_OBJECT(connection));

    g_signal_connect(
      G_OBJECT(connection),
      "notify::status",
      G_CALLBACK(infc_browser_connection_notify_status_cb),
      browser
    );

    if(priv->connection_manager != NULL)
    {
      inf_connection_manager_ref_connection(
        priv->connection_manager,
        priv->group,
        priv->connection
      );
    }
  }

  g_object_notify(G_OBJECT(browser), "connection");
}

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

  priv->connection_manager = NULL;
  priv->connection = NULL;
  priv->request_manager = infc_request_manager_new();

  priv->plugins = g_hash_table_new(g_str_hash, g_str_equal);
  priv->nodes = g_hash_table_new(NULL, NULL);
  priv->root = infc_browser_node_new_subdirectory(browser, NULL, 0, NULL);
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
  case PROP_CONNECTION_MANAGER:
    infc_browser_set_connection_manager(
      browser,
      INF_CONNECTION_MANAGER(g_value_get_object(value))
    );
    break;
  case PROP_CONNECTION:
    infc_browser_set_connection(
      browser,
      INF_XML_CONNECTION(g_value_get_object(value))
    );
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

  infc_browser_set_connection(browser, NULL);
  infc_browser_set_connection_manager(browser, NULL);

  g_hash_table_destroy(priv->plugins);
  priv->plugins = NULL;

  g_object_unref(G_OBJECT(priv->request_manager));
  priv->request_manager = NULL;

  G_OBJECT_CLASS(parent_class)->dispose(object);
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

  priv = INFC_BROWSER_PRIVATE(browser);

  /* TODO: If we requested to do an initial sync, then the server should
   * have sent the identifier and we can initiate the sync now. */

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
  }
  else
  {
    node = infc_browser_node_add_note(
      browser,
      parent,
      id,
      (const gchar*)name,
      (const gchar*)type,
      error
    );
  }

  xmlFree(type);
  xmlFree(name);

  if(node == NULL) return FALSE;

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
  InfcSession* session;
  InfcRequest* request;
  InfcBrowserIter iter;
  xmlChar* identifier;

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

  identifier = inf_xml_util_get_attribute_required(xml, "identifier", error);
  if(identifier == NULL) return FALSE;

  g_assert(node->shared.known.plugin != NULL);
  session = node->shared.known.plugin->session_new(
    priv->connection_manager,
    connection,
    (const gchar*)identifier
  );

  /* TODO: Unref session when it drops its connection (possibly due to
   * unsubscription). */

  xmlFree(identifier);

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "subscribe-session",
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

static void
infc_browser_net_object_received(InfNetObject* net_object,
                                 InfXmlConnection* connection,
                                 xmlNodePtr node)
{
  GError* error;

  error = NULL;

  if(strcmp((const gchar*)node->name, "request-failed") == 0)
  {
    infc_browser_handle_request_failed(
      INFC_BROWSER(net_object),
      connection,
      node,
      &error
    );
  }
  else if(strcmp((const gchar*)node->name, "explore-begin") == 0)
  {
    infc_browser_handle_explore_begin(
      INFC_BROWSER(net_object),
      connection,
      node,
      &error
    );
  }
  else if(strcmp((const gchar*)node->name, "explore-end") == 0)
  {
    infc_browser_handle_explore_end(
      INFC_BROWSER(net_object),
      connection,
      node,
      &error
    );
  }
  else if(strcmp((const gchar*)node->name, "add-node") == 0)
  {
    infc_browser_handle_add_node(
      INFC_BROWSER(net_object),
      connection,
      node,
      &error
    );
  }
  else if(strcmp((const gchar*)node->name, "remove-node") == 0)
  {
    infc_browser_handle_remove_node(
      INFC_BROWSER(net_object),
      connection,
      node,
      &error
    );
  }
  else if(strcmp((const gchar*)node->name, "subscribe-session") == 0)
  {
    infc_browser_handle_subscribe_session(
      INFC_BROWSER(net_object),
      connection,
      node,
      &error
    );
  }
  else
  {
    g_set_error(
      &error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE,
      "%s",
      inf_directory_strerror(INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE)
    );
  }

  if(error != NULL)
  {
    g_warning(
      "Failed to process %s request from server: %s\n",
      (const gchar*)node->name,
      error->message
    );

    g_error_free(error);
  }
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

  object_class->dispose = infc_browser_dispose;
  object_class->set_property = infc_browser_set_property;
  object_class->get_property = infc_browser_get_property;

  browser_class->node_added = NULL;
  browser_class->node_removed = NULL;

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

/** infc_browser_new:
 *
 * @connection_manager: A #InfConnectionManager to register the server
 * connection and which forwards incoming data to the blowser or running
 * sessions.
 * @connection: Connection to the server.
 *
 * Creates a new #InfcBrowser.
 *
 * Return Value: A new #InfcBrowser.
 **/
InfcBrowser*
infc_browser_new(InfConnectionManager* manager,
                 InfXmlConnection* connection)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), NULL);

  object = g_object_new(
    INFC_TYPE_BROWSER,
    "connection-manager", manager,
    "connection", connection,
    NULL
  );

  return INFC_BROWSER(object);
}

/** infc_browser_get_connection_manager:
 *
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

/** infc_browser_get_connection:
 *
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

/** infc_browser_add_plugin:
 *
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
                        InfcNotePlugin* plugin)
{
  InfcBrowserPrivate* priv;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(plugin != NULL, FALSE);

  if(g_hash_table_lookup(priv->plugins, plugin->identifier) != NULL)
    return FALSE;

  g_hash_table_insert(priv->plugins, (gpointer)plugin->identifier, plugin);
  /* TODO: Check for yet unknown note types and make them known if they
   * match this plugin. */
  return TRUE;
}

/** infc_browser_iter_get_root:
 *
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

/** infc_browser_iter_get_next:
 *
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

/** infc_browser_iter_get_prev:
 *
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

/** infc_browser_iter_get_parent:
 *
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

/** infc_browser_iter_get_explored:
 *
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a subdirectory node in @browser.
 *
 * TODO: Let this return an enum that also specifies whether the node
 * is currently being explored.
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

/** infc_browser_iter_get_child:
 *
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

/** infc_browser_iter_explore:
 *
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

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);

  /* TODO: Verify that there is not already an exploration request for
   * that node in enqueued. */

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_EXPLORE_REQUEST,
    "explore-node",
    "node_id", node->id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_connection_manager_send_to(
    priv->connection_manager,
    priv->group,
    priv->connection,
    xml
  );

  return INFC_EXPLORE_REQUEST(request);
}

/** infc_browser_iter_get_name:
 *
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

/** infc_browser_add_subdirectory:
 *
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
                              InfcBrowserIter* iter,
                              const gchar* name)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_return_val_if_subdir_fail(node, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "add-node",
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "parent", node->id);
  inf_xml_util_set_attribute(xml, "type", "InfSubdirectory");
  inf_xml_util_set_attribute(xml, "name", name);

  inf_connection_manager_send_to(
    priv->connection_manager,
    priv->group,
    priv->connection,
    xml
  );

  return INFC_NODE_REQUEST(request);
}

/** infc_browser_add_note:
 *
 * @browser: A #InfcBrowser.
 * @parent: A #InfcBrowserIter pointing to an explored subdirectory.
 * @name: Name for the new node.
 * @plugin: Type of the new node.
 *
 * Asks the server to create a new note with the given type. The returned
 * request may be used to be notified when the request finishes or fails.
 *
 * Return Value: A #InfcNodeRequest.
 **/
InfcNodeRequest*
infc_browser_add_note(InfcBrowser* browser,
                      InfcBrowserIter* parent,
                      const gchar* name,
                      InfcNotePlugin* plugin)
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
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "parent", node->id);
  inf_xml_util_set_attribute(xml, "type", plugin->identifier);
  inf_xml_util_set_attribute(xml, "name", name);

  inf_connection_manager_send_to(
    priv->connection_manager,
    priv->group,
    priv->connection,
    xml
  );

  return INFC_NODE_REQUEST(request);
}

/** infc_browser_remove_node:
 *
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
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_connection_manager_send_to(
    priv->connection_manager,
    priv->group,
    priv->connection,
    xml
  );

  return INFC_NODE_REQUEST(request);
}

/** infc_browser_subscribe_session:
 *
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note inside @browser.
 *
 * Subscribes to the given note. When the request has finished (which does
 * not mean that the subscription has finished, but the server is ready to
 * perform the subscription), infc_browser_iter_get_session() can be used
 * to access the #InfcSession object representing the subscription.
 *
 * Return Value: A #InfcNodeRequest that may be used to get notified when
 * the request finishes or fails.
 **/
InfcNodeRequest*
infc_browser_subscribe_session(InfcBrowser* browser,
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
  g_return_val_if_fail(node->shared.known.session != NULL, NULL);

  /* TODO: Check that there is not a subscription request already enqueued. */

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "subscribe-session",
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_connection_manager_send_to(
    priv->connection_manager,
    priv->group,
    priv->connection,
    xml
  );

  return INFC_NODE_REQUEST(request);
}

/** infc_browser_iter_get_session:
 *
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note in @browser.
 *
 * Returns the #InfcSession representing the subscription to the given note,
 * if the client is subscribed, and %NULL otherwise.
 *
 * Return Value: A #InfcSession, or %NULL if not subscribed.
 **/
InfcSession*
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

/* vim:set et sw=2 ts=2: */
