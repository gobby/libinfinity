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

#include <libinfinity/common/inf-chat-session.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-protocol.h>
#include <libinfinity/common/inf-error.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-signals.h>

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
  const InfcBrowserIter* iter;
  InfcExploreRequest* result;
};

typedef struct _InfcBrowserIterGetNodeRequestForeachData
  InfcBrowserIterGetNodeRequestForeachData;
struct _InfcBrowserIterGetNodeRequestForeachData {
  const InfcBrowserIter* iter;
  InfcNodeRequest* result;
};

typedef struct _InfcBrowserIterGetSyncInRequestsForeachData
  InfcBrowserIterGetSyncInRequestsForeachData;
struct _InfcBrowserIterGetSyncInRequestsForeachData {
  const InfcBrowserIter* iter;
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

typedef enum _InfcBrowserSubreqType {
  INFC_BROWSER_SUBREQ_CHAT,
  INFC_BROWSER_SUBREQ_SESSION,
  INFC_BROWSER_SUBREQ_ADD_NODE,
  INFC_BROWSER_SUBREQ_SYNC_IN
} InfcBrowserSubreqType;

typedef struct _InfcBrowserSubreq InfcBrowserSubreq;
struct _InfcBrowserSubreq {
  InfcBrowserSubreqType type;
  /* TODO: This should maybe go to shared, as not required for chat: */
  guint node_id;

  union {
    struct {
      InfcNodeRequest* request;
      InfCommunicationJoinedGroup* subscription_group;
    } chat;

    struct {
      InfcBrowserNode* node;
      InfcNodeRequest* request;
      InfCommunicationJoinedGroup* subscription_group;
    } session;

    struct {
      InfcBrowserNode* parent;
      const InfcNotePlugin* plugin;
      gchar* name;
      InfcNodeRequest* request;
      InfCommunicationJoinedGroup* subscription_group;
    } add_node;

    struct {
      InfcBrowserNode* parent;
      const InfcNotePlugin* plugin;
      gchar* name;
      InfcNodeRequest* request;
      InfCommunicationJoinedGroup* synchronization_group;
      InfCommunicationJoinedGroup* subscription_group; /* can be NULL */
      InfSession* session;
    } sync_in;
  } shared;
};

typedef struct _InfcBrowserPrivate InfcBrowserPrivate;
struct _InfcBrowserPrivate {
  InfIo* io;
  InfCommunicationManager* communication_manager;
  InfCommunicationJoinedGroup* group;
  InfXmlConnection* connection;

  guint seq_id;
  gpointer welcome_timeout;
  InfcRequestManager* request_manager;

  GHashTable* plugins; /* Registered plugins */

  InfcBrowserStatus status;
  GHashTable* nodes; /* Mapping from id to node */
  InfcBrowserNode* root;

  GSList* sync_ins;
  GSList* subscription_requests;

  InfcSessionProxy* chat_session;
};

#define INFC_BROWSER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_BROWSER, InfcBrowserPrivate))

enum {
  PROP_0,

  PROP_IO,
  PROP_COMMUNICATION_MANAGER,
  PROP_CONNECTION,

  /* read only */
  PROP_STATUS,
  PROP_CHAT_SESSION
};

enum {
  ERROR,

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
    iter != NULL && \
    g_hash_table_lookup( \
      INFC_BROWSER_PRIVATE(INFC_BROWSER(browser))->nodes, \
      GUINT_TO_POINTER((iter)->node_id) \
    ) == (iter)->node \
  )

#define infc_browser_return_val_if_iter_fail(browser, iter, val) \
  g_return_val_if_fail( \
    iter != NULL && \
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

  data = (InfcBrowserIterGetSyncInRequestsForeachData*)user_data;
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

static void
infc_browser_get_chat_request_foreach_func(InfcRequest* request,
                                           gpointer user_data)
{
  InfcBrowserIterGetNodeRequestForeachData* data;

  data = (InfcBrowserIterGetNodeRequestForeachData*)user_data;
  g_assert(INFC_IS_NODE_REQUEST(request));

  /* There can only be one such request: */
  g_assert(data->result == NULL);

  /* TODO: Stop foreach when we found the request. Requires changes in
   * InfcRequestManager. */
  data->result = INFC_NODE_REQUEST(request);
}

/*
 * Path handling
 */

/* Returns the complete path to this node in the given GString */
static void
infc_browser_node_get_path_string(InfcBrowserNode* node,
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
      infc_browser_node_get_path_string(node->parent, string);
 
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
infc_browser_node_get_path(InfcBrowserNode* node,
                           gchar** path,
                           gsize* len)
{
  GString* str;

  g_return_if_fail(node != NULL);
  g_return_if_fail(path != NULL);

  str = g_string_sized_new(128);

  infc_browser_node_get_path_string(node, str);
  *path = str->str;

  if(len != NULL)
    *len = str->len;

  g_string_free(str, FALSE);
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

  inf_signal_handlers_disconnect_by_func(
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

/* Required by infc_browser_node_free */
static void
infc_browser_remove_sync_in(InfcBrowser* browser,
                            InfcBrowserSyncIn* sync_in);

static void
infc_browser_remove_subreq(InfcBrowser* browser,
                           InfcBrowserSubreq* request);

static void
infc_browser_node_free(InfcBrowser* browser,
                       InfcBrowserNode* node)
{
  InfcBrowserPrivate* priv;
  gboolean removed;

  GSList* item;
  InfcBrowserSyncIn* sync_in;
  InfcBrowserSubreq* request;

  GError* error;

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

  /* Check subscription requests for this node */
  for(item = priv->subscription_requests; item != NULL; )
  {
    request = (InfcBrowserSubreq*)item->data;
    item = item->next;

    /* TODO: Can't we remove the subscription requests here already, and
     * assert that a request exists in
     * infc_browser_communication_object_sent()? Or is there something that
     * needs to be done in infc_browser_communication_object_sent() even if
     * the corresponding node has been removed? */
    switch(request->type)
    {
    case INFC_BROWSER_SUBREQ_SESSION:
      if(request->shared.session.node == node)
      {
        request->shared.session.node = NULL;

        if(request->shared.session.request != NULL)
        {
          error = g_error_new_literal(
            inf_directory_error_quark(),
            INF_DIRECTORY_ERROR_NO_SUCH_NODE,
            _("The node to subscribe to has been removed")
          );

          infc_request_manager_fail_request(
            priv->request_manager,
            INFC_REQUEST(request->shared.session.request),
            error
          );

          g_error_free(error);

          request->shared.session.request = NULL;
        }
      }

      break;
    case INFC_BROWSER_SUBREQ_ADD_NODE:
      if(request->shared.add_node.parent == node)
      {
        request->shared.add_node.parent = NULL;

        if(request->shared.add_node.request != NULL)
        {
          error = g_error_new_literal(
            inf_directory_error_quark(),
            INF_DIRECTORY_ERROR_NO_SUCH_NODE,
            _("The subdirectory into which the new node should have been "
              "created has been removed")
          );

          infc_request_manager_fail_request(
            priv->request_manager,
            INFC_REQUEST(request->shared.add_node.request),
            error
          );

          g_error_free(error);

          request->shared.add_node.request = NULL;
        }
      }

      break;
    case INFC_BROWSER_SUBREQ_SYNC_IN:
      if(request->shared.sync_in.parent == node)
      {
        request->shared.sync_in.parent = NULL;

        g_assert(request->shared.sync_in.request != NULL);

        error = g_error_new_literal(
          inf_directory_error_quark(),
          INF_DIRECTORY_ERROR_NO_SUCH_NODE,
          _("The subdirectory into which the new node should have been "
            "created has been removed")
        );

        infc_request_manager_fail_request(
          priv->request_manager,
          INFC_REQUEST(request->shared.sync_in.request),
          error
        );

        g_error_free(error);

        request->shared.sync_in.request = NULL;
      }

      break;
    default:
      g_assert_not_reached();
      break;
    }
  }

  if(node->parent != NULL)
    infc_browser_node_unlink(node);

  removed = g_hash_table_remove(priv->nodes, GUINT_TO_POINTER(node->id));
  g_assert(removed == TRUE);

  g_free(node->name);
  g_slice_free(InfcBrowserNode, node);
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
 * Signal handlers
 */

static void
infc_browser_session_close_cb(InfSession* session,
                              gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfcBrowserIter* iter;
  InfcBrowserNode* node;

  browser = INFC_BROWSER(user_data);
  priv = INFC_BROWSER_PRIVATE(browser);

  iter = (InfcBrowserIter*)g_object_get_qdata(
    G_OBJECT(session),
    infc_browser_session_proxy_quark
  );

  if(iter != NULL)
  {
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
  else
  {
    g_assert(priv->chat_session != NULL);
    g_assert(infc_session_proxy_get_session(priv->chat_session) == session);

    g_object_unref(priv->chat_session);
    priv->chat_session = NULL;

    g_object_notify(G_OBJECT(browser), "chat-session");
  }
}

static void
infc_browser_welcome_timeout_func(gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  GError* error;

  browser = INFC_BROWSER(user_data);
  priv = INFC_BROWSER_PRIVATE(browser);

  priv->welcome_timeout = NULL;

  error = NULL;
  g_set_error(
    &error,
    inf_directory_error_quark(),
    INF_DIRECTORY_ERROR_NO_WELCOME_MESSAGE,
    "%s",
    _("The server did not send an initial welcome message. This means that "
      "the server is running a lower version of the software which is "
      "incompatible to the client. Consider downgrading the client, or ask "
      "the server maintainers to upgrade their software.")
  );

  g_signal_emit(G_OBJECT(browser), browser_signals[ERROR], 0, error);
  g_error_free(error);

  g_assert(priv->status != INFC_BROWSER_DISCONNECTED);
  priv->status = INFC_BROWSER_DISCONNECTED;
  g_object_notify(G_OBJECT(browser), "status");
}

/* Required by infc_browser_disconnected */
static void
infc_browser_member_removed_cb(InfCommunicationGroup* group,
                               InfXmlConnection* connection,
                               gpointer user_data);

static void
infc_browser_connected(InfcBrowser* browser)
{
  InfcBrowserPrivate* priv;
  priv = INFC_BROWSER_PRIVATE(browser);

  g_assert(priv->group == NULL);

  /* Join directory group of server */
  /* directory group always uses central method */
  priv->group = inf_communication_manager_join_group(
    priv->communication_manager,
    "InfDirectory",
    priv->connection,
    "central"
  );

  /* "central" method should always be available */
  g_assert(priv->group != NULL);

  inf_communication_group_set_target(
    INF_COMMUNICATION_GROUP(priv->group),
    INF_COMMUNICATION_OBJECT(browser)
  );

  g_signal_connect(
    priv->group,
    "member-removed",
    G_CALLBACK(infc_browser_member_removed_cb),
    browser
  );

  /* Wait for welcome message */
  if(priv->status != INFC_BROWSER_CONNECTING)
  {
    priv->status = INFC_BROWSER_CONNECTING;
    g_object_notify(G_OBJECT(browser), "status");
  }

  /* TODO: We have a relatively low timeout here to easily recognize when
   * we try to connect to a server which does not yet send a welcome
   * message. We can set it somewhat higher later when there are fewer
   * old servers around. */
  g_assert(priv->welcome_timeout == NULL);
  priv->welcome_timeout = inf_io_add_timeout(
    priv->io,
    5*1000,
    infc_browser_welcome_timeout_func,
    browser,
    NULL
  );
}

static void
infc_browser_disconnected(InfcBrowser* browser)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* child;
  InfcBrowserNode* next;

  priv = INFC_BROWSER_PRIVATE(browser);

  /* Note that we do not remove the corresponding node that we sync in. We
   * lost the connection to the server anyway, so we do not care whether
   * that node exists on the server or not. */
  while(priv->sync_ins != NULL)
    infc_browser_remove_sync_in(browser, priv->sync_ins->data);

  while(priv->subscription_requests != NULL)
    infc_browser_remove_subreq(browser, priv->subscription_requests->data);

  /* TODO: Emit failed signal with some "disconnected" error */
  if(priv->request_manager)
  {
    infc_request_manager_clear(priv->request_manager);
    g_object_unref(priv->request_manager);
    priv->request_manager = NULL;
  }

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->group),
    G_CALLBACK(infc_browser_member_removed_cb),
    browser
  );

  /* Reset group since the browser's connection is always the publisher. */
  g_object_unref(priv->group);
  priv->group = NULL;

  /* Remove tree */
  for(child = priv->root->shared.subdir.child; child != NULL; child = next)
  {
    next = child->next;

    infc_browser_node_unregister(browser, child);
    infc_browser_node_free(browser, child);
  }

  g_assert(priv->root->shared.subdir.child == NULL);
  priv->root->shared.subdir.explored = FALSE;

  priv->status = INFC_BROWSER_DISCONNECTED;
  g_object_notify(G_OBJECT(browser), "status");
}

static void
infc_browser_member_removed_cb(InfCommunicationGroup* group,
                               InfXmlConnection* connection,
                               gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  browser = INFC_BROWSER(user_data);
  priv = INFC_BROWSER_PRIVATE(browser);

  g_assert(INF_COMMUNICATION_GROUP(priv->group) == group);

  if(connection == priv->connection)
    infc_browser_disconnected(browser);
}

static void
infc_browser_connection_notify_status_cb(GObject* object,
                                         GParamSpec* pspec,
                                         gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfXmlConnectionStatus status;

  browser = INFC_BROWSER(user_data);
  priv = INFC_BROWSER_PRIVATE(browser);

  g_object_get(object, "status", &status, NULL);

  switch(status)
  {
  case INF_XML_CONNECTION_OPENING:
    if(priv->status != INFC_BROWSER_CONNECTING)
    {
      priv->status = INFC_BROWSER_CONNECTING;
      g_object_notify(G_OBJECT(browser), "status");
    }

    break;
  case INF_XML_CONNECTION_OPEN:
    infc_browser_connected(browser);
    break;
  case INF_XML_CONNECTION_CLOSING:
  case INF_XML_CONNECTION_CLOSED:
    /* The group will emit ::member-removed in this case in which we
     * do some cleanup. If we got here from INF_XML_CONNECTION_OPENING then
     * that cleanup is not required, but remember to reset status. */
    if(priv->group == NULL && priv->status != INFC_BROWSER_DISCONNECTED)
    {
      g_assert(priv->request_manager == NULL);

      priv->status = INFC_BROWSER_DISCONNECTED;
      g_object_notify(G_OBJECT(browser), "status");
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static void
infc_browser_connection_error_cb(InfXmlConnection* connection,
                                 const GError* error,
                                 gpointer user_data)
{
  InfcBrowser* browser;
  browser = INFC_BROWSER(user_data);

  /* Just relay to save others some work */
  g_signal_emit(G_OBJECT(browser), browser_signals[ERROR], 0, error);
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

static GError*
infc_browser_method_unsupported_error(const gchar* method_name,
                                      InfXmlConnection* connection)
{
  GError* error;
  gchar* network;

  g_object_get(G_OBJECT(connection), "network", &network, NULL);

  error = g_error_new(
    inf_directory_error_quark(),
    INF_DIRECTORY_ERROR_METHOD_UNSUPPORTED,
    _("This session requires communication method `%s' which is not "
      "installed for network `%s'"),
    method_name,
    network
  );

  g_free(network);
  return error;
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
  priv->communication_manager = NULL;
  priv->group = NULL;
  priv->connection = NULL;
  priv->seq_id = 0;
  priv->welcome_timeout = NULL;
  priv->request_manager = NULL;

  priv->plugins = g_hash_table_new(g_str_hash, g_str_equal);
  priv->status = INFC_BROWSER_DISCONNECTED;
  priv->nodes = g_hash_table_new(NULL, NULL);
  priv->root = infc_browser_node_new_subdirectory(browser, NULL, 0, NULL);

  priv->sync_ins = NULL;
  priv->subscription_requests = NULL;
  priv->chat_session = NULL;
}

static GObject*
infc_browser_constructor(GType type,
                         guint n_construct_properties,
                         GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);

  g_assert(priv->communication_manager != NULL);
  g_assert(priv->io != NULL);

  return object;
}

static void
infc_browser_dispose(GObject* object)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);

  /* TODO: Use infc_browser_set_connection() as soon as it is available -
   * should be trivial to implement now. Remember to make the "connection"
   * property writable also. */
#if 0
  infc_browser_set_connection(browser, NULL);
#else
  if(priv->group != NULL)
    infc_browser_disconnected(browser);
  g_assert(priv->group == NULL);

  /* Should have been freed by infc_browser_disconnected */
  g_assert(priv->sync_ins == NULL);
  g_assert(priv->subscription_requests == NULL);

  if(priv->connection != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      priv->connection,
      G_CALLBACK(infc_browser_connection_notify_status_cb),
      browser
    );

    inf_signal_handlers_disconnect_by_func(
      priv->connection,
      G_CALLBACK(infc_browser_connection_error_cb),
      browser
    );

    g_object_unref(priv->connection);
    priv->connection = NULL;
  }
#endif

  /* Close chat session if it is open */
  if(priv->chat_session != NULL)
    inf_session_close(infc_session_proxy_get_session(priv->chat_session));
  g_assert(priv->chat_session == NULL);

  g_object_unref(priv->communication_manager);
  priv->communication_manager = NULL;

  g_hash_table_destroy(priv->plugins);
  priv->plugins = NULL;

  g_assert(priv->request_manager == NULL);

  if(priv->welcome_timeout != NULL)
  {
    inf_io_remove_timeout(priv->io, priv->welcome_timeout);
    priv->welcome_timeout = NULL;
  }

  if(priv->io != NULL)
  {
    g_object_unref(G_OBJECT(priv->io));
    priv->io = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
infc_browser_finalize(GObject* object)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);

  infc_browser_node_free(browser, priv->root);
  priv->root = NULL;

  g_hash_table_destroy(priv->nodes);
  priv->nodes = NULL;

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_browser_set_property(GObject* object,
                          guint prop_id,
                          const GValue* value,
                          GParamSpec* pspec)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfXmlConnectionStatus status;

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct only */
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_COMMUNICATION_MANAGER:
    g_assert(priv->communication_manager == NULL); /* construct/only */
    priv->communication_manager =
      INF_COMMUNICATION_MANAGER(g_value_dup_object(value));

    break;
  case PROP_CONNECTION:
    if(priv->connection != NULL)
    {
      if(priv->group != NULL)
        infc_browser_disconnected(browser);

      inf_signal_handlers_disconnect_by_func(
        priv->connection,
        G_CALLBACK(infc_browser_connection_notify_status_cb),
        browser
      );

      inf_signal_handlers_disconnect_by_func(
        priv->connection,
        G_CALLBACK(infc_browser_connection_error_cb),
        browser
      );

      g_object_unref(priv->connection);
    }

    priv->connection = INF_XML_CONNECTION(g_value_get_object(value));

    if(priv->connection)
    {
      g_object_ref(priv->connection);
      g_object_get(G_OBJECT(priv->connection), "status", &status, NULL);

      g_signal_connect(
        G_OBJECT(priv->connection),
        "notify::status",
        G_CALLBACK(infc_browser_connection_notify_status_cb),
        browser
      );

      g_signal_connect(
        G_OBJECT(priv->connection),
        "error",
        G_CALLBACK(infc_browser_connection_error_cb),
        browser
      );

      switch(status)
      {
      case INF_XML_CONNECTION_OPENING:
        if(priv->status != INFC_BROWSER_CONNECTING)
        {
          priv->status = INFC_BROWSER_CONNECTING;
          g_object_notify(G_OBJECT(browser), "status");
        }

        break;
      case INF_XML_CONNECTION_OPEN:
        infc_browser_connected(browser);
        break;
      case INF_XML_CONNECTION_CLOSING:
      case INF_XML_CONNECTION_CLOSED:
        if(priv->status != INFC_BROWSER_DISCONNECTED)
        {
          priv->status = INFC_BROWSER_DISCONNECTED;
          g_object_notify(G_OBJECT(browser), "status");
        }

        break;
      }
    }
    else
    {
      if(priv->status != INFC_BROWSER_DISCONNECTED)
      {
        priv->status = INFC_BROWSER_DISCONNECTED;
        g_object_notify(G_OBJECT(browser), "status");
      }
    }

    break;
  case PROP_STATUS:
  case PROP_CHAT_SESSION:
    /* read only */
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
  case PROP_COMMUNICATION_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->communication_manager));
    break;
  case PROP_CONNECTION:
    g_value_set_object(value, G_OBJECT(priv->connection));
    break;
  case PROP_STATUS:
    g_value_set_enum(value, priv->status);
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
 * Subscription requests
 */

static InfcBrowserSubreq*
infc_browser_add_subreq_common(InfcBrowser* browser,
                               InfcBrowserSubreqType type,
                               guint node_id)
{
  InfcBrowserPrivate* priv;
  InfcBrowserSubreq* request;

  priv = INFC_BROWSER_PRIVATE(browser);

  request = g_slice_new(InfcBrowserSubreq);
  request->type = type;
  request->node_id = node_id;

  priv->subscription_requests =
    g_slist_prepend(priv->subscription_requests, request);

  return request;
}

static InfcBrowserSubreq*
infc_browser_add_subreq_chat(InfcBrowser* browser,
                             InfcNodeRequest* request,
                             InfCommunicationJoinedGroup* group)
{
  InfcBrowserSubreq* subreq;

  subreq = infc_browser_add_subreq_common(
    browser,
    INFC_BROWSER_SUBREQ_CHAT,
    0
  );

  subreq->shared.chat.request = request;
  subreq->shared.chat.subscription_group = group;

  /* Request can be NULL if the server subscribed us to the chat without
   * us asking about it. */
  if(request != NULL)
    g_object_ref(request);
  g_object_ref(group);

  return subreq;
}

static InfcBrowserSubreq*
infc_browser_add_subreq_session(InfcBrowser* browser,
                                InfcBrowserNode* node,
                                InfcNodeRequest* request,
                                InfCommunicationJoinedGroup* group)
{
  InfcBrowserSubreq* subreq;

  subreq = infc_browser_add_subreq_common(
    browser,
    INFC_BROWSER_SUBREQ_SESSION,
    node->id
  );

  subreq->shared.session.node = node;
  subreq->shared.session.request = request;
  subreq->shared.session.subscription_group = group;

  /* TODO: Document in what case request can be NULL, or assert if it can't */
  if(request != NULL)
    g_object_ref(request);
  g_object_ref(group);

  return subreq;
}

static InfcBrowserSubreq*
infc_browser_add_subreq_add_node(InfcBrowser* browser,
                                 guint node_id,
                                 InfcBrowserNode* parent,
                                 const InfcNotePlugin* plugin,
                                 const gchar* name,
                                 InfcNodeRequest* request,
                                 InfCommunicationJoinedGroup* group)
{
  InfcBrowserSubreq* subreq;

  subreq = infc_browser_add_subreq_common(
    browser,
    INFC_BROWSER_SUBREQ_ADD_NODE,
    node_id
  );

  subreq->shared.add_node.parent = parent;
  subreq->shared.add_node.plugin = plugin;
  subreq->shared.add_node.name = g_strdup(name);
  subreq->shared.add_node.request = request;
  subreq->shared.add_node.subscription_group = group;

  if(request != NULL)
    g_object_ref(request);
  g_object_ref(group);

  return subreq;
}

static InfcBrowserSubreq*
infc_browser_add_subreq_sync_in(InfcBrowser* browser,
                                guint node_id,
                                InfcBrowserNode* parent,
                                const InfcNotePlugin* plugin,
                                const gchar* name,
                                InfcNodeRequest* request,
                                InfSession* session,
                                InfCommunicationJoinedGroup* sync_group,
                                InfCommunicationJoinedGroup* sub_group)
{
  InfcBrowserSubreq* subreq;

  /* Request must be available for sync-in: We can't sync-in something we
   * didn't request, because we don't have any data to sync. */
  g_assert(request != NULL);

  g_assert(sync_group != NULL);
  g_assert(session != NULL);

  subreq = infc_browser_add_subreq_common(
    browser,
    INFC_BROWSER_SUBREQ_SYNC_IN,
    node_id
  );

  subreq->shared.sync_in.parent = parent;
  subreq->shared.sync_in.plugin = plugin;
  subreq->shared.sync_in.name = g_strdup(name);
  subreq->shared.sync_in.request = request;
  subreq->shared.sync_in.session = session;
  subreq->shared.sync_in.synchronization_group = sync_group;
  subreq->shared.sync_in.subscription_group = sub_group;

  g_object_ref(request);
  g_object_ref(session);
  g_object_ref(sync_group);
  if(sub_group != NULL)
    g_object_ref(sub_group);

  return subreq;
}

static void
infc_browser_free_subreq(InfcBrowserSubreq* request)
{
  switch(request->type)
  {
  case INFC_BROWSER_SUBREQ_CHAT:
    g_object_unref(request->shared.chat.subscription_group);

    if(request->shared.chat.request != NULL)
      g_object_unref(request->shared.chat.request);

    break;
  case INFC_BROWSER_SUBREQ_SESSION:
    g_object_unref(request->shared.session.subscription_group);

    if(request->shared.session.request != NULL)
      g_object_unref(request->shared.session.request);

    break;
  case INFC_BROWSER_SUBREQ_ADD_NODE:
    g_object_unref(request->shared.add_node.subscription_group);

    if(request->shared.add_node.request != NULL)
      g_object_unref(request->shared.add_node.request);

    g_free(request->shared.add_node.name);
    break;
  case INFC_BROWSER_SUBREQ_SYNC_IN:
    if(request->shared.sync_in.subscription_group != NULL)
      g_object_unref(request->shared.sync_in.subscription_group);
    g_object_unref(request->shared.sync_in.synchronization_group);
    g_object_unref(request->shared.sync_in.session);

    /* Can be NULL if the corresponding node (parent) has been freed */
    if(request->shared.sync_in.request != NULL)
      g_object_unref(request->shared.sync_in.request);

    g_free(request->shared.sync_in.name);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_slice_free(InfcBrowserSubreq, request);
}

static void
infc_browser_unlink_subreq(InfcBrowser* browser,
                           InfcBrowserSubreq* request)
{
  InfcBrowserPrivate* priv;

  priv = INFC_BROWSER_PRIVATE(browser);

  priv->subscription_requests =
    g_slist_remove(priv->subscription_requests, request);
}

static void
infc_browser_remove_subreq(InfcBrowser* browser,
                           InfcBrowserSubreq* request)
{
  infc_browser_unlink_subreq(browser, request);
  infc_browser_free_subreq(request);
}

/* Find a subscription request which will create a node with the given ID
 * upon completion. */
static InfcBrowserSubreq*
infc_browser_find_subreq(InfcBrowser* browser,
                         guint node_id)
{
  InfcBrowserPrivate* priv;
  GSList* item;
  InfcBrowserSubreq* subreq;

  priv = INFC_BROWSER_PRIVATE(browser);

  for(item = priv->subscription_requests; item != NULL; item = item->next)
  {
    subreq = (InfcBrowserSubreq*)item->data;
    switch(subreq->type)
    {
    case INFC_BROWSER_SUBREQ_CHAT:
    case INFC_BROWSER_SUBREQ_SESSION:
      /* These do not create a note */
      break;
    case INFC_BROWSER_SUBREQ_ADD_NODE:
      if(subreq->node_id == node_id)
        return subreq;
      break;
    case INFC_BROWSER_SUBREQ_SYNC_IN:
      if(subreq->node_id == node_id)
        return subreq;
      break;
    default:
      g_assert_not_reached();
      break;
    }
  }

  return NULL;
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

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(infc_session_proxy_get_session(sync_in->proxy)),
    G_CALLBACK(infc_browser_sync_in_synchronization_complete_cb),
    sync_in
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(infc_session_proxy_get_session(sync_in->proxy)),
    G_CALLBACK(infc_browser_sync_in_synchronization_failed_cb),
    sync_in
  );

  g_object_unref(sync_in->proxy);
  g_slice_free(InfcBrowserSyncIn, sync_in);

  priv->sync_ins = g_slist_remove(priv->sync_ins, sync_in);
}

/*
 * Node creation
 */

static InfcBrowserNode*
infc_browser_node_add_subdirectory(InfcBrowser* browser,
                                   InfcBrowserNode* parent,
                                   guint id,
                                   const gchar* name)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_assert(parent->type == INFC_BROWSER_NODE_SUBDIRECTORY);
  g_assert(parent->shared.subdir.explored == TRUE);

  priv = INFC_BROWSER_PRIVATE(browser);

  node = infc_browser_node_new_subdirectory(browser, parent, id, name);
  infc_browser_node_register(browser, node);

  return node;
}

static InfcBrowserNode*
infc_browser_node_add_note(InfcBrowser* browser,
                           InfcBrowserNode* parent,
                           guint id,
                           const gchar* name,
                           const gchar* type,
                           InfcSessionProxy* sync_in_session)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_assert(parent->type == INFC_BROWSER_NODE_SUBDIRECTORY);
  g_assert(parent->shared.subdir.explored == TRUE);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_assert(priv->connection != NULL);

  node = infc_browser_node_new_note(browser, parent, id, name, type);

  if(sync_in_session != NULL)
  {
    infc_browser_add_sync_in(
      browser,
      node,
      priv->connection,
      sync_in_session
    );
  }

  infc_browser_node_register(browser, node);
  return node;
}

/*
 * Network command handling.
 */

static void
infc_browser_subscribe_ack(InfcBrowser* browser,
                           InfXmlConnection* connection,
                           InfcBrowserSubreq* request)
{
  InfcBrowserPrivate* priv;
  xmlNodePtr xml;

  priv = INFC_BROWSER_PRIVATE(browser);

  xml = xmlNewNode(NULL, (const xmlChar*)"subscribe-ack");
  if(request->type != INFC_BROWSER_SUBREQ_CHAT)
    inf_xml_util_set_attribute_uint(xml, "id", request->node_id);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    xml
  );
}

static void
infc_browser_subscribe_nack(InfcBrowser* browser,
                            InfXmlConnection* connection,
                            guint node_id)
{
  InfcBrowserPrivate* priv;
  xmlNodePtr xml;

  priv = INFC_BROWSER_PRIVATE(browser);

  xml = xmlNewNode(NULL, (const xmlChar*)"subscribe-nack");
  if(node_id > 0) inf_xml_util_set_attribute_uint(xml, "id", node_id);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    xml
  );
}

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

/* TODO: Change to InfcNodeRequest as soon as InfcExploreRequest inherits
 * from InfcNodeRequest */
static InfcRequest*
infc_browser_get_add_node_request_from_xml(InfcBrowser* browser,
                                           xmlNodePtr xml,
                                           GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;

  priv = INFC_BROWSER_PRIVATE(browser);

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    NULL,
    xml,
    NULL
  );

  /* If no seq was set, then this add-node request was not issued by us */
  if(request != NULL)
  {
    /* when seq was set, then we issued that add-node. We might
     * either do this implicitly by exploring a folder or explicitely by
     * sending an add-node request. */
    if(!INFC_IS_EXPLORE_REQUEST(request) && !INFC_IS_NODE_REQUEST(request))
    {
      g_set_error(
        error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_INVALID_SEQ,
        _("The request contains a sequence number refering to a request of "
          "type '%s', but a request of either 'explore' or 'add-node' was "
          "expected."),
        infc_request_get_name(request)
      );

      return NULL;
    }

    /* TODO: If EXPLORE_REQUEST, then check whether we can add a node at this
     * point. Means: initiated && current < total. Remove error from
     * infc_explore_request_progress() and _finished(), check in browser
     * instead. */
  }

  return request;
}

/* TODO: Remove error from this function as soon as
 * infc_explore_request_progress can't fail anymore (it should assert
 * instead). */
static gboolean
infc_browser_process_add_node_request(InfcBrowser* browser,
                                      InfcRequest* request,
                                      InfcBrowserNode* node,
                                      GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserIter iter;

  priv = INFC_BROWSER_PRIVATE(browser);

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
    g_assert_not_reached();
    return FALSE;
  }
}

static InfCommunicationJoinedGroup*
infc_browser_create_group_from_xml(InfcBrowser* browser,
                                   InfXmlConnection* connection,
                                   xmlNodePtr xml,
                                   GError** error)
{
  InfcBrowserPrivate* priv;
  xmlChar* method_name;
  xmlChar* group_name;
  InfCommunicationJoinedGroup* group;

  priv = INFC_BROWSER_PRIVATE(browser);
  
  method_name = inf_xml_util_get_attribute_required(xml, "method", error);
  if(method_name == NULL) return FALSE;

  group_name = inf_xml_util_get_attribute_required(xml, "group", error);
  if(group_name == NULL) { xmlFree(method_name); return FALSE; }

  /* Server is publisher */
  group = inf_communication_manager_join_group(
    priv->communication_manager,
    (const gchar*)group_name,
    connection,
    (const gchar*)method_name
  );

  if(!group)
  {
    g_propagate_error(
      error,
      infc_browser_method_unsupported_error(
        (const gchar*)method_name,
        connection
      )
    );
  }

  xmlFree(group_name);
  xmlFree(method_name);

  return group;
}

/* If initial_sync is TRUE, then the session is initially synchronized in the 
 * subscription group. Otherwise, an empty session is used. */
static gboolean
infc_browser_subscribe_session(InfcBrowser* browser,
                               InfcBrowserNode* node,
                               InfCommunicationJoinedGroup* group,
                               InfXmlConnection* connection,
                               gboolean initial_sync)
{
  InfcBrowserPrivate* priv;
  InfcSessionProxy* proxy;
  InfcBrowserIter iter;
  InfSession* session;

  priv = INFC_BROWSER_PRIVATE(browser);

  g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);
  g_assert(node->shared.known.plugin != NULL);
  g_assert(node->shared.known.session == NULL);

  if(initial_sync)
  {
    session = node->shared.known.plugin->session_new(
      priv->io,
      priv->communication_manager,
      INF_SESSION_SYNCHRONIZING,
      group,
      connection,
      node->shared.known.plugin->user_data
    );
  }
  else
  {
    session = node->shared.known.plugin->session_new(
      priv->io,
      priv->communication_manager,
      INF_SESSION_RUNNING,
      NULL,
      NULL,
      node->shared.known.plugin->user_data
    );
  }

  proxy = g_object_new(INFC_TYPE_SESSION_PROXY, "session", session, NULL);

  inf_communication_group_set_target(
    INF_COMMUNICATION_GROUP(group),
    INF_COMMUNICATION_OBJECT(proxy)
  );

  infc_session_proxy_set_connection(proxy, group, connection, priv->seq_id);

  g_object_unref(session);

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
  g_object_unref(proxy);
  return TRUE;
}

static gboolean
infc_browser_handle_welcome(InfcBrowser* browser,
                            InfXmlConnection* connection,
                            xmlNodePtr xml,
                            GError** error)
{
  InfcBrowserPrivate* priv;
  xmlChar* version;
  guint server_major;
  guint server_minor;
  guint own_major;
  guint own_minor;
  gboolean result;

  priv = INFC_BROWSER_PRIVATE(browser);

  version = inf_xml_util_get_attribute_required(
    xml,
    "protocol-version",
    error);
  if(!version) return FALSE;

  result = inf_protocol_parse_version(
    (const gchar*)version,
    &server_major, &server_minor,
    error
  );

  xmlFree(version);
  if(!result) return FALSE;

  result = inf_protocol_parse_version(
    inf_protocol_get_version(),
    &own_major, &own_minor,
    NULL
  );

  g_assert(result == TRUE);

  if(server_major < own_major || server_minor < own_minor)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_VERSION_MISMATCH,
      "%s",
      _("The server uses an older version of the protocol which is no longer "
        "supported by this client. Consider using an earlier version of it, "
        "or ask the server maintainers to upgrade their software.")
    );
    return FALSE;
  }

  if(server_major > own_major)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_VERSION_MISMATCH,
      "%s",
      _("The server uses a newer version. Consider upgrading your client.")
    );

    return FALSE;
  }

  result = inf_xml_util_get_attribute_uint_required(
    xml,
    "sequence-id",
    &priv->seq_id,
    error
  );

  if(!result) return FALSE;

  g_assert(priv->request_manager == NULL);
  priv->request_manager = infc_request_manager_new(priv->seq_id);

  priv->status = INFC_BROWSER_CONNECTED;
  g_object_notify(G_OBJECT(browser), "status");

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

  /* TODO: Allow exploration without us asking */
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
      "%s",
      _("Node to explore does no longer exist")
    );

    return FALSE;
  }
  else if(node->type != INFC_BROWSER_NODE_SUBDIRECTORY)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY,
      "%s",
      _("Node to explore is not a subdirectory")
    );

    return FALSE;
  }
  else if(node->shared.subdir.explored == TRUE)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ALREADY_EXPLORED,
      "%s",
      _("Node to explore is already explored")
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

  /* TODO: Allow exploration without us asking */
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
  guint id;
  xmlChar* name;
  xmlChar* type;
  InfcRequest* request;
  GError* local_error;

  xmlNodePtr child;
  const InfcNotePlugin* plugin;
  InfCommunicationJoinedGroup* group;
  InfcBrowserSubreq* subreq;
  gboolean result;

  priv = INFC_BROWSER_PRIVATE(browser);

  if(inf_xml_util_get_attribute_uint_required(xml, "id", &id, error) == FALSE)
    return FALSE;

  if(g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(id)) != NULL ||
     infc_browser_find_subreq(browser, id) != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NODE_EXISTS,
      _("Node with ID \"%u\" exists already"),
      id
    );

    return FALSE;
  }

  parent = infc_browser_get_node_from_xml_typed(
    browser,
    xml,
    "parent",
    INFC_BROWSER_NODE_SUBDIRECTORY,
    error
  );

  if(parent == NULL) return FALSE;

  local_error = NULL;
  request =
    infc_browser_get_add_node_request_from_xml(browser, xml, &local_error);
  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

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
      (const gchar*)name
    );

    if(request != NULL)
    {
      result = infc_browser_process_add_node_request(
        browser,
        request,
        node,
        error
      );
    }
    else
    {
      result = TRUE;
    }
  }
  else
  {
    for(child = xml->children; child != NULL; child = child->next)
      if(strcmp((const char*)child->name, "subscribe") == 0)
        break;

    if(child != NULL)
    {
      /* Subscribe to the newly added node */

      /* Request must be a node-request at this point. <add-node> messages
       * resulting from a node exploration cannot yield subscription. */
      if(request != NULL && !INFC_IS_NODE_REQUEST(request))
      {
        g_set_error(
          error,
          inf_request_error_quark(),
          INF_REQUEST_ERROR_INVALID_SEQ,
          "%s",
          _("Explored nodes cannot be initially be subscribed to")
        );

        result = FALSE;
      }
      else
      {
        /* Can't subscribe if we don't know the note type */
        plugin = g_hash_table_lookup(priv->plugins, type);
        if(plugin == NULL)
        {
          g_set_error(
            error,
            inf_directory_error_quark(),
            INF_DIRECTORY_ERROR_TYPE_UNKNOWN,
            _("Note type \"%s\" not known"),
            (const gchar*)type
          );

          infc_browser_subscribe_nack(browser, connection, id);
          result = FALSE;
        }
        else
        {
          group = infc_browser_create_group_from_xml(
            browser,
            connection,
            child,
            error
          );

          if(group == NULL)
          {
            infc_browser_subscribe_nack(browser, connection, id);
            result = FALSE;
          }
          else
          {
            subreq = infc_browser_add_subreq_add_node(
              browser,
              id,
              parent,
              plugin,
              (const gchar*)name,
              INFC_NODE_REQUEST(request),
              group
            );

            g_object_unref(group);

            infc_browser_subscribe_ack(browser, connection, subreq);
            result = TRUE;
          }
        }
      }
    }
    else
    {
      node = infc_browser_node_add_note(
        browser,
        parent,
        id,
        (const gchar*)name,
        (const gchar*)type,
        NULL
      );

      if(request != NULL)
      {
        result = infc_browser_process_add_node_request(
          browser,
          request,
          node,
          error
        );
      }
      else
      {
        result = TRUE;
      }
    }
  }

  xmlFree(type);
  xmlFree(name);

  return result;
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
  InfCommunicationJoinedGroup* sync_group;

  xmlChar* type;
  xmlChar* name;
  xmlNodePtr child;
  InfcBrowserSubreq* subreq;
  gboolean result;

  priv = INFC_BROWSER_PRIVATE(browser); 

  if(inf_xml_util_get_attribute_uint_required(xml, "id", &id, error) == FALSE)
    return FALSE;

  if(g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(id)) != NULL ||
     infc_browser_find_subreq(browser, id) != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NODE_EXISTS,
      _("Node with ID \"%u\" exists already"),
      id
    );

    return FALSE;
  }

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
    "add-node",
    xml,
    error
  );

  /* Note that such a request MUST exist. We cannot sync something in without
   * knowing where to get the data to sync from. */
  if(!request) return FALSE;

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
      "%s",
      _("Received sync-in without having requested one")
    );

    return FALSE;
  }

  /* We always set both session and plugin quark: */
  g_assert(plugin != NULL);
  /* We only set this on node requests */
  g_assert(INFC_IS_NODE_REQUEST(request));

  result = FALSE;

  type = inf_xml_util_get_attribute_required(xml, "type", error);
  if(type != NULL)
  {
    if(strcmp((const char*)type, plugin->note_type) != 0)
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_UNEXPECTED_SYNC_IN,
        _("Expected note type \"%s\" for sync-in, but received \"%s\""),
        plugin->note_type,
        (const gchar*)type
      );
    }
    else
    {
      name = inf_xml_util_get_attribute_required(xml, "name", error);
      if(name != NULL)
      {
        /* Note that all the errors which could have occured up to this point
         * are the server's fault. If one of those occured, then the server
         * sent us crap and could have known better. We therefore don't reply
         * with subscribe-nack on these errors. */

        sync_group = infc_browser_create_group_from_xml(
          browser,
          connection,
          xml,
          error
        );

        if(sync_group == NULL)
        {
          infc_browser_subscribe_nack(browser, connection, id);
        }
        else
        {
          /* TODO: The server might specify a different
           * subscription group */
          for(child = xml->children; child != NULL; child = child->next)
            if(strcmp((const char*)child->name, "subscribe") == 0)
              break;

          subreq = infc_browser_add_subreq_sync_in(
            browser,
            id,
            parent,
            plugin,
            (const gchar*)name,
            INFC_NODE_REQUEST(request),
            session,
            sync_group,
            child != NULL ? sync_group : NULL
          );

          g_object_unref(sync_group);

          infc_browser_subscribe_ack(browser, connection, subreq);
          result = TRUE;
        }

        xmlFree(name);
      }
    }

    xmlFree(type);
  }

  g_object_unref(session);
  return result;
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
  InfCommunicationJoinedGroup* group;
  InfcBrowserSubreq* subreq;

  priv = INFC_BROWSER_PRIVATE(browser);

  node = infc_browser_get_node_from_xml_typed(
    browser,
    xml,
    "id",
    INFC_BROWSER_NODE_NOTE_KNOWN | INFC_BROWSER_NODE_NOTE_UNKNOWN,
    error
  );

  /* TODO: Send subscribe-nack with the id the server sent, even if we could
   * not find the node. */
  if(node == NULL) return FALSE;

  if(node->type == INFC_BROWSER_NODE_NOTE_UNKNOWN)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_TYPE_UNKNOWN,
      _("Note type '%s' is not supported"),
      node->shared.unknown.type
    );

    infc_browser_subscribe_nack(browser, connection, node->id);
    return FALSE;
  }

  if(node->shared.known.session != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED,
      "%s",
      _("Already subscribed to this session")
    );

    /* Don't send subscribe-nack here, it is the server's job not to send us
     * subscribe-session if we are already subscribed. */

    return FALSE;
  }

  group = infc_browser_create_group_from_xml(
    browser,
    connection,
    xml,
    error
  );

  if(!group)
  {
    infc_browser_subscribe_nack(browser, connection, node->id);
    return FALSE;
  }

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "subscribe-session",
    xml,
    NULL
  );

  g_assert(INFC_IS_NODE_REQUEST(request));

  subreq = infc_browser_add_subreq_session(
    browser,
    node,
    INFC_NODE_REQUEST(request),
    group
  );

  g_object_unref(group);

  infc_browser_subscribe_ack(browser, connection, subreq);

  return TRUE;
}

static gboolean
infc_browser_handle_subscribe_chat(InfcBrowser* browser,
                                   InfXmlConnection* connection,
                                   xmlNodePtr xml,
                                   GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  InfCommunicationJoinedGroup* group;
  InfcBrowserSubreq* subreq;

  priv = INFC_BROWSER_PRIVATE(browser);

  if(priv->chat_session != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED,
      "%s",
      _("Already subscribed to the chat session")
    );

    /* Don't send subscribe-nack here, it is the server's job not to send us
     * subscribe-session if we are already subscribed. */

    return FALSE;
  }

  group = infc_browser_create_group_from_xml(
    browser,
    connection,
    xml,
    error
  );

  if(!group)
  {
    infc_browser_subscribe_nack(browser, connection, 0);
    return FALSE;
  }

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "subscribe-chat",
    xml,
    NULL
  );

  g_assert(request == NULL || INFC_IS_NODE_REQUEST(request));

  subreq = infc_browser_add_subreq_chat(
    browser,
    INFC_NODE_REQUEST(request),
    group
  );

  g_object_unref(group);

  infc_browser_subscribe_ack(browser, connection, subreq);

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
      _("Error comes from unknown error domain '%s' (code %u)"),
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

static InfCommunicationScope
infc_browser_communication_object_received(InfCommunicationObject* object,
                                           InfXmlConnection* connection,
                                           xmlNodePtr node,
                                           GError** error)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  GError* local_error;
  GError* seq_error;
  InfcRequest* request;

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);
  local_error = NULL;

  if(priv->status == INFC_BROWSER_CONNECTING &&
     strcmp((const gchar*)node->name, "welcome") == 0)
  {
    if(priv->welcome_timeout)
    {
      inf_io_remove_timeout(priv->io, priv->welcome_timeout);
      priv->welcome_timeout = NULL;
    }

    if(!infc_browser_handle_welcome(browser, connection, node, &local_error))
    {
      g_signal_emit(browser, browser_signals[ERROR], 0, local_error);
      g_error_free(local_error);
      local_error = NULL;

      priv->status = INFC_BROWSER_DISCONNECTED;
      g_object_notify(G_OBJECT(browser), "status");
    }
  }
  else if(strcmp((const gchar*)node->name, "request-failed") == 0)
  {
    infc_browser_handle_request_failed(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "explore-begin") == 0)
  {
    infc_browser_handle_explore_begin(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "explore-end") == 0)
  {
    infc_browser_handle_explore_end(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "add-node") == 0)
  {
    infc_browser_handle_add_node(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "sync-in") == 0)
  {
    infc_browser_handle_sync_in(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "remove-node") == 0)
  {
    infc_browser_handle_remove_node(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "subscribe-session") == 0)
  {
    infc_browser_handle_subscribe_session(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "subscribe-chat") == 0)
  {
    infc_browser_handle_subscribe_chat(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "save-session-in-progress") == 0)
  {
    infc_browser_handle_save_session_in_progress(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "saved-session") == 0)
  {
    infc_browser_handle_saved_session(
      browser,
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
    /* If the request had a (valid) seq set, we cancel the corresponding
     * request because the reply could not be processed. */
    request = infc_request_manager_get_request_by_xml(
      priv->request_manager,
      NULL,
      node,
      NULL
    );

    if(request != NULL)
    {
      seq_error = NULL;
      g_set_error(
        &seq_error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_REPLY_UNPROCESSED,
        _("Server reply could not be processed: %s"),
        local_error->message
      );

      infc_request_manager_fail_request(
        priv->request_manager,
        request,
        seq_error
      );

      g_error_free(seq_error);
    }

    /* TODO: We might just want to emit error instead... */
    g_propagate_error(error, local_error);
  }

  /* Browser is client-side anyway, so we should not even need to forward
   * anything. */
  return INF_COMMUNICATION_SCOPE_PTP;
}

static void
infc_browser_communication_object_sent(InfCommunicationObject* object,
                                       InfXmlConnection* connection,
                                       xmlNodePtr xml)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  gboolean has_id;
  guint node_id;
  GSList* item;
  InfCommunicationJoinedGroup* sync_group;
  InfcBrowserSubreq* subreq;
  InfChatSession* session;
  InfcSessionProxy* proxy;
  InfcBrowserIter iter;

  /* TODO: Can we do this in enqueued already? */
  if(strcmp((const char*)xml->name, "subscribe-ack") == 0)
  {
    browser = INFC_BROWSER(object);
    priv = INFC_BROWSER_PRIVATE(browser);

    has_id = inf_xml_util_get_attribute_uint(xml, "id", &node_id, NULL);

    for(item = priv->subscription_requests; item != NULL; item = item->next)
    {
      subreq = (InfcBrowserSubreq*)item->data;
      if( (has_id  && subreq->node_id == node_id) ||
          (!has_id && subreq->node_id == 0))
      {
        break;
      }
    }

    /* TODO: Only Assert here, and remove the subreq in
     * infc_browser_node_free() */
    if(item == NULL) return;
    /*g_assert(item != NULL);*/

    infc_browser_unlink_subreq(browser, subreq);

    switch(subreq->type)
    {
    case INFC_BROWSER_SUBREQ_CHAT:
      g_assert(has_id == FALSE);
      /* OK, do the subscription */
      g_assert(priv->chat_session == NULL);

      /* Synchronize in subscription group: */
      session = inf_chat_session_new(
        priv->communication_manager,
        256,
        INF_SESSION_SYNCHRONIZING,
        INF_COMMUNICATION_GROUP(subreq->shared.chat.subscription_group),
        connection
      );

      proxy = g_object_new(INFC_TYPE_SESSION_PROXY, "session", session, NULL);

      inf_communication_group_set_target(
        INF_COMMUNICATION_GROUP(subreq->shared.chat.subscription_group),
        INF_COMMUNICATION_OBJECT(proxy)
      );

      infc_session_proxy_set_connection(
        proxy,
        subreq->shared.chat.subscription_group,
        connection,
        priv->seq_id
      );

      g_object_unref(session);

      g_signal_emit(
        G_OBJECT(browser),
        browser_signals[SUBSCRIBE_SESSION],
        0,
        NULL,
        proxy
      );

      /* The default handler refs the proxy */
      g_object_unref(proxy);

      if(subreq->shared.chat.request != NULL)
      {
        infc_node_request_finished(subreq->shared.chat.request, NULL);

        infc_request_manager_remove_request(
          priv->request_manager,
          INFC_REQUEST(subreq->shared.chat.request)
        );
      }

      break;
    case INFC_BROWSER_SUBREQ_SESSION:
      g_assert(has_id == TRUE);
      if(subreq->shared.session.node != NULL)
      {
        g_assert(subreq->shared.session.node->id == node_id);

        infc_browser_subscribe_session(
          browser,
          subreq->shared.session.node,
          subreq->shared.session.subscription_group,
          connection,
          TRUE
        );

        if(subreq->shared.session.request != NULL)
        {
          iter.node = subreq->shared.session.node;
          iter.node_id = node_id;
          infc_node_request_finished(subreq->shared.session.request, &iter);

          infc_request_manager_remove_request(
            priv->request_manager,
            INFC_REQUEST(subreq->shared.session.request)
          );
        }
      }

      break;
    case INFC_BROWSER_SUBREQ_ADD_NODE:
      g_assert(has_id == TRUE);
      if(subreq->shared.add_node.parent != NULL)
      {
        /* Any other attempt at creating a node with this ID should have
         * failed already. */
        g_assert(
          g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id)) == NULL
        );

        g_assert(infc_browser_find_subreq(browser, node_id) == NULL);

        node = infc_browser_node_add_note(
          browser,
          subreq->shared.add_node.parent,
          node_id,
          subreq->shared.add_node.name,
          subreq->shared.add_node.plugin->note_type,
          NULL
        );

        g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);

        /* Newly added nodes are normally newly created, so don't sync */
        infc_browser_subscribe_session(
          browser,
          node,
          subreq->shared.add_node.subscription_group,
          connection,
          FALSE
        );

        /* Finish request */
        if(subreq->shared.add_node.request != NULL)
        {
          iter.node = node;
          iter.node_id = node_id;
          infc_node_request_finished(subreq->shared.add_node.request, &iter);

          infc_request_manager_remove_request(
            priv->request_manager,
            INFC_REQUEST(subreq->shared.add_node.request)
          );
        }
      }

      break;
    case INFC_BROWSER_SUBREQ_SYNC_IN:
      g_assert(has_id == TRUE);
      if(subreq->shared.sync_in.parent != NULL)
      {
        /* Any other attempt at creating a node with this ID should have
         * failed already. */
        g_assert(
          g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id)) == NULL
        );

        g_assert(infc_browser_find_subreq(browser, node_id) == NULL);

        proxy = g_object_new(
          INFC_TYPE_SESSION_PROXY,
          "session", subreq->shared.sync_in.session,
          NULL
        );

        sync_group = subreq->shared.sync_in.synchronization_group;

        inf_communication_group_set_target(
          INF_COMMUNICATION_GROUP(sync_group),
          INF_COMMUNICATION_OBJECT(proxy)
        );

        inf_session_synchronize_to(
          subreq->shared.sync_in.session,
          INF_COMMUNICATION_GROUP(sync_group),
          connection
        );

        node = infc_browser_node_add_note(
          browser,
          subreq->shared.sync_in.parent,
          node_id,
          subreq->shared.sync_in.name,
          subreq->shared.sync_in.plugin->note_type,
          proxy
        );

        g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);

        iter.node_id = node->id;
        iter.node = node;

        if(subreq->shared.sync_in.subscription_group != NULL)
        {
          /* Make sure the session is not yet subscribed */
          g_assert(
            inf_session_get_subscription_group(
              subreq->shared.sync_in.session
            ) == NULL
          );

          if(subreq->shared.sync_in.subscription_group != sync_group)
          {
            inf_communication_group_set_target(
              INF_COMMUNICATION_GROUP(
                subreq->shared.sync_in.subscription_group
              ),
              INF_COMMUNICATION_OBJECT(proxy)
            );
          }

          /* Subscribe to the newly created node. We don't need to do all the
           * stuff infc_browser_subscribe_session() does since we already
           * created the session and proxy. */
          infc_session_proxy_set_connection(
            proxy,
            subreq->shared.sync_in.subscription_group,
            connection,
            priv->seq_id
          );

          g_signal_emit(
            G_OBJECT(browser),
            browser_signals[SUBSCRIBE_SESSION],
            0,
            &iter,
            proxy
          );
        }

        g_object_unref(proxy);

        /* TODO: Emit a signal, so that others are notified that a sync-in
         * begins and can show progress or something. */

        g_assert(subreq->shared.sync_in.request != NULL);
        infc_node_request_finished(subreq->shared.sync_in.request, &iter);

        infc_request_manager_remove_request(
          priv->request_manager,
          INFC_REQUEST(subreq->shared.sync_in.request)
        );
      }

      break;
    default:
      g_assert_not_reached();
      break;
    }

    infc_browser_free_subreq(subreq);
  }
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
  session = infc_session_proxy_get_session(proxy);

  if(iter != NULL)
  {
    node = (InfcBrowserNode*)iter->node;

    g_assert(
      g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(iter->node_id)) ==
      node
    );

    g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);
    g_assert(node->shared.known.session == NULL);

    node->shared.known.session = proxy;

    g_object_ref(proxy);

    /* Associate the iter to the session so that we can remove the proxy
     * from that item when the session is closed. */
    g_object_set_qdata_full(
      G_OBJECT(session),
      infc_browser_session_proxy_quark,
      infc_browser_iter_copy(iter),
      (GDestroyNotify)infc_browser_iter_free
    );
  }
  else
  {
    g_assert(priv->chat_session == NULL);

    g_object_ref(proxy);
    priv->chat_session = proxy;

    g_object_notify(G_OBJECT(browser), "chat-session");
  }

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
  object_class->finalize = infc_browser_finalize;
  object_class->set_property = infc_browser_set_property;
  object_class->get_property = infc_browser_get_property;

  browser_class->error = NULL;
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
    PROP_COMMUNICATION_MANAGER,
    g_param_spec_object(
      "communication-manager",
      "Communication manager",
      "The communication manager for the browser",
      INF_COMMUNICATION_TYPE_MANAGER,
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
    PROP_STATUS,
    g_param_spec_enum(
      "status",
      "Status",
      "The current connectivity status of the browser",
      INFC_TYPE_BROWSER_STATUS,
      INFC_BROWSER_DISCONNECTED,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CHAT_SESSION,
    g_param_spec_object(
      "chat-session",
      "Chat session",
      "Active chat session",
      INFC_TYPE_SESSION_PROXY,
      G_PARAM_READABLE
    )
  );

  /**
   * InfcBrowser::error:
   * @browser: The #InfcBrowser emitting the signal.
   * @error: A @GError* saying what's wrong.
   *
   * This signal is emitted whenever an error occured. If the browser's
   * underlying #InfXmlConnection produces emits #InfXmlConnection::error,
   * then this signal will be emitted with the corresponding error as well.
   * Also, if another error occurs on the browser level this signal is
   * emitted. It may or may not be fatal. If it is fatal then the browser's
   * status will change to %INFC_BROWSER_DISCONNECTED.
   */
  browser_signals[ERROR] = g_signal_new(
    "error",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcBrowserClass, error),
    NULL, NULL,
    inf_marshal_VOID__POINTER,
    G_TYPE_NONE,
    1,
    G_TYPE_POINTER /* actually a GError* */
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
   * @iter: A #InfcBrowserIter pointing to the subscribed node, or %NULL.
   * @proxy: A #InfcSessionProxy for the subscribed session.
   *
   * Emitted when subscribed to a session. The subscription was successful,
   * but the synchronization (the server sending the initial session state)
   * might still fail. Use #InfSession::synchronization-complete and
   * #InfSession::synchronization-failed to be notified.
   *
   * If @iter is %NULL this is a subscription to the chat. This guarantees
   * @proxy's session to be a #InfChatSession. If @iter is non-%NULL this is a
   * subscription to the session of the node pointed to by @iter.
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
   * starts, or %NULL.
   * @request: A #InfcNodeRequest for the operation.
   *
   * This signal is emitted whenever a subscription request for a
   * (non-subdirectory) node is made. Note that the subscription may still
   * fail (connect to #InfcNodeRequest::finished and #InfcRequest::failed
   * to be notified).
   *
   * If @iter is %NULL the signal refers to the chat session, otherwise it
   * points to the node to whose session the client requested a subscription.
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
infc_browser_communication_object_init(gpointer g_iface,
                                       gpointer iface_data)
{
  InfCommunicationObjectIface* iface;
  iface = (InfCommunicationObjectIface*)g_iface;

  iface->received = infc_browser_communication_object_received;
  iface->sent = infc_browser_communication_object_sent;
}

GType
infc_browser_status_get_type(void)
{
  static GType browser_status_type = 0;

  if(!browser_status_type)
  {
    static const GEnumValue browser_status_values[] = {
      {
        INFC_BROWSER_DISCONNECTED,
        "INFC_BROWSER_DISCONNECTED",
        "disconnected"
      }, {
        INFC_BROWSER_CONNECTING,
        "INFC_BROWSER_CONNECTING",
        "connecting"
      }, {
        INFC_BROWSER_CONNECTED,
        "INFC_BROWSER_CONNECTED",
        "connected"
      }
    };

    browser_status_type = g_enum_register_static(
      "InfcBrowserStatus",
      browser_status_values
    );
  }

  return browser_status_type;
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

    static const GInterfaceInfo communication_object_info = {
      infc_browser_communication_object_init,
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
      INF_COMMUNICATION_TYPE_OBJECT,
      &communication_object_info
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
 * @comm_manager: A #InfCommunicationManager to register the server connection
 * and which forwards incoming data to the browser or running sessions.
 * @connection: Connection to the server.
 *
 * Creates a new #InfcBrowser.
 *
 * Return Value: A new #InfcBrowser.
 **/
InfcBrowser*
infc_browser_new(InfIo* io,
                 InfCommunicationManager* comm_manager,
                 InfXmlConnection* connection)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(INF_COMMUNICATION_IS_MANAGER(comm_manager), NULL);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), NULL);

  object = g_object_new(
    INFC_TYPE_BROWSER,
    "io", io,
    "communication-manager", comm_manager,
    "connection", connection,
    NULL
  );

  return INFC_BROWSER(object);
}

/**
 * infc_browser_get_communication_manager:
 * @browser: A #InfcBrowser.
 *
 * Returns the communication manager of this browser.
 *
 * Return Value: A #InfCommunicationManager.
 **/
InfCommunicationManager*
infc_browser_get_communication_manager(InfcBrowser* browser)
{
  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  return INFC_BROWSER_PRIVATE(browser)->communication_manager;
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
 * infc_browser_get_status:
 * @browser: A #InfcBrowser.
 *
 * Returns the browser's status. Note that the browser status can be
 * %INFC_BROWSER_DISCONNECTED even if browser's connection is still open. This
 * can happen if a fatal error on the browser layer happens, for example when
 * it does not understand the server's messages.
 *
 * Returns: The browser's status.
 */
InfcBrowserStatus
infc_browser_get_status(InfcBrowser* browser)
{
  g_return_val_if_fail(INFC_IS_BROWSER(browser), INFC_BROWSER_DISCONNECTED);
  return INFC_BROWSER_PRIVATE(browser)->status;
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
    /* cast const away without warning */
    /* take addr -> char const * const *
     *     cast  -> void         const *
     *     cast  -> void       * const *
     *     deref -> void * const
     */
    *(const gpointer*)(gconstpointer)&plugin->note_type,
    *(gpointer*)(gpointer)&plugin
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
                               const InfcBrowserIter* iter)
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
 * Return Value: %TRUE if @iter was set, %FALSE otherwise.
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
                          const InfcBrowserIter* iter)
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
  g_return_val_if_fail(priv->status == INFC_BROWSER_CONNECTED, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_EXPLORE_REQUEST,
    "explore-node",
    "node_id", node->id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  g_signal_emit(
    G_OBJECT(browser),
    browser_signals[BEGIN_EXPLORE],
    0,
    iter,
    request
  );

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
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
                           const InfcBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  return node->name;
}

/**
 * infc_browser_iter_get_path:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a node in @browser.
 *
 * Returns the complete path to the node @iter points to. The path to a node
 * is the name of the node and the name of all parent nodes separated by '/',
 * as a filesystem path on Unix.
 *
 * Return Value: The node's path. Free with g_free() when no longer in use.
 **/
gchar*
infc_browser_iter_get_path(InfcBrowser* browser,
                           const InfcBrowserIter* iter)
{
  InfcBrowserNode* node;
  gchar* path;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_node_get_path(node, &path, NULL);
  return path;
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
                                  const InfcBrowserIter* iter)
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
                              const InfcBrowserIter* parent,
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
  g_return_val_if_fail(priv->status == INFC_BROWSER_CONNECTED, NULL);

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

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
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
                      const InfcBrowserIter* parent,
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
  g_return_val_if_fail(priv->status == INFC_BROWSER_CONNECTED, NULL);

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

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
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
                                   const InfcBrowserIter* parent,
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
  g_return_val_if_fail(priv->status == INFC_BROWSER_CONNECTED, NULL);

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
    /* cast away const without warning */
    *(gpointer*)(gpointer)&plugin
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "parent", node->id);
  inf_xml_util_set_attribute(xml, "type", plugin->note_type);
  inf_xml_util_set_attribute(xml, "name", name);

  if(initial_subscribe != FALSE)
    xmlNewChild(xml, NULL, (const xmlChar*)"subscribe", NULL);
  xmlNewChild(xml, NULL, (const xmlChar*)"sync-in", NULL);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
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
                         const InfcBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  /* The root node cannot be removed */
  g_return_val_if_fail(node->parent != NULL, NULL);

  /* TODO: Check that there is not a remove-node request already enqueued. */

  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->status == INFC_BROWSER_CONNECTED, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "remove-node",
    "node-id", iter->node_id,
    NULL
  );

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
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
                                const InfcBrowserIter* iter)
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
                             const InfcBrowserIter* iter)
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
                                    const InfcBrowserIter* iter)
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
  g_return_val_if_fail(priv->status == INFC_BROWSER_CONNECTED, NULL);
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

  g_signal_emit(
    G_OBJECT(browser),
    browser_signals[BEGIN_SUBSCRIBE],
    0,
    iter,
    request
  );

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
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
                               const InfcBrowserIter* iter)
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

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
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
                              const InfcBrowserIter* iter)
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
                              const InfcBrowserIter* iter)
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
                                        const InfcBrowserIter* iter)
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

  if(priv->request_manager != NULL)
  {
    infc_request_manager_foreach_named_request(
      priv->request_manager,
      "subscribe-session",
      infc_browser_iter_get_node_request_foreach_func,
      &data
    );
  }

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
                                      const InfcBrowserIter* iter)
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

  if(priv->request_manager != NULL)
  {
    infc_request_manager_foreach_named_request(
      priv->request_manager,
      "explore-node",
      infc_browser_iter_get_explore_request_foreach_func,
      &data
    );
  }

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
                                       const InfcBrowserIter* iter)
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

  if(priv->request_manager != NULL)
  {
    infc_request_manager_foreach_named_request(
      priv->request_manager,
      "add-node",
      infc_browser_iter_get_sync_in_requests_foreach_func,
      &data
    );
  }

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

/**
 * infc_browser_subscribe_chat:
 * @browser: A #InfcBrowser.
 *
 * Attempts to subscribe to the server's chat. When the operation finishes
 * infc_browser_get_chat_session() will return a #InfcSessionProxy
 * representing the chat session. It can be used to read the chat's content.
 * The request can fail in case the server chat is disabled.
 *
 * Returns: A #InfcNodeRequest that may be used to get notified when
 * the request finishes or fails.
 */
InfcNodeRequest*
infc_browser_subscribe_chat(InfcBrowser* browser)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(infc_browser_get_chat_session(browser) == NULL, NULL);

  g_return_val_if_fail(
    infc_browser_get_subscribe_chat_request(browser) == NULL,
    NULL
  );

  priv = INFC_BROWSER_PRIVATE(browser);

  /* This should really be a separate request type */
  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "subscribe-chat",
    "node-id", 0,
    NULL
  );

  xml = infc_browser_request_to_xml(request);

  g_signal_emit(
    G_OBJECT(browser),
    browser_signals[BEGIN_SUBSCRIBE],
    0,
    NULL,
    request
  );

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INFC_NODE_REQUEST(request);
}

/**
 * infc_browser_get_subscribe_chat_request:
 * @browser: A #InfcBrowser.
 *
 * Returns the #InfcNodeRequest that represests the request sent to the server
 * which attempts to subscribe to its chat. If there is no such request
 * running, then the function returns %NULL. After such a request finishes,
 * call infc_browser_get_chat_session() to get the #InfcSessionProxy for the
 * chat session. To initiate the request, call infc_browser_subscribe_chat().
 *
 * Returns: A #InfcNodeRequest, or %NULL.
 */
InfcNodeRequest*
infc_browser_get_subscribe_chat_request(InfcBrowser* browser)
{
  InfcBrowserPrivate* priv;
  InfcBrowserIterGetNodeRequestForeachData data;

  data.result = NULL;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  priv = INFC_BROWSER_PRIVATE(browser);

  if(priv->request_manager != NULL)
  {
    infc_request_manager_foreach_named_request(
      priv->request_manager,
      "subscribe-chat",
      infc_browser_get_chat_request_foreach_func,
      &data
    );
  }

  return data.result;
}

/**
 * infc_browser_get_chat_session:
 * @browser: A #InfcBrowser.
 *
 * Returns the #InfcSessionProxy representing the running chat session if the
 * local client is subscribed to it, or %NULL otherwise.
 *
 * Returns: A #InfcSessionProxy for the chat, or %NULL.
 */
InfcSessionProxy*
infc_browser_get_chat_session(InfcBrowser* browser)
{
  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  return INFC_BROWSER_PRIVATE(browser)->chat_session;
}

/* vim:set et sw=2 ts=2: */
