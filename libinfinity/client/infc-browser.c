/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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
 * to subscribe to sessions. #InfcBrowser implements the #InfBrowser
 * interface, through which most operations are performed.
 **/

#include <libinfinity/client/infc-browser.h>
#include <libinfinity/client/infc-request-manager.h>
#include <libinfinity/client/infc-explore-request.h>
#include <libinfinity/client/infc-acl-account-list-request.h>

#include <libinfinity/common/inf-chat-session.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-protocol.h>
#include <libinfinity/common/inf-error.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-signals.h>

#include <string.h>

/* Some Windows header #defines error for no good */
#ifdef G_OS_WIN32
# ifdef ERROR
#  undef ERROR
# endif
#endif

typedef enum _InfcBrowserNodeType {
  INFC_BROWSER_NODE_SUBDIRECTORY = 1 << 0,
  /* We found a plugin to handle the note type */
  INFC_BROWSER_NODE_NOTE_KNOWN   = 1 << 1,
  /* There was no plugin registered to handle the note's type */
  INFC_BROWSER_NODE_NOTE_UNKNOWN = 1 << 2
} InfcBrowserNodeType;

typedef struct _InfcBrowserGetAclAccountListData
  InfcBrowserGetAclAccountListData;
struct _InfcBrowserGetAclAccountListData {
  const InfAclAccount** accounts;
  guint index;
};

typedef struct _InfcBrowserListPendingRequestsForeachData
  InfcBrowserListPendingRequestsForeachData;
struct _InfcBrowserListPendingRequestsForeachData {
  const InfBrowserIter* iter;
  GSList* result;
};

typedef struct _InfcBrowserIterGetChatRequestForeachData
  InfcBrowserIterGetChatRequestForeachData;
struct _InfcBrowserIterGetChatRequestForeachData {
  const InfBrowserIter* iter;
  InfcChatRequest* result;
};

typedef struct _InfcBrowserIterGetSyncInRequestsForeachData
  InfcBrowserIterGetSyncInRequestsForeachData;
struct _InfcBrowserIterGetSyncInRequestsForeachData {
  const InfBrowserIter* iter;
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
  InfAclSheetSet* acl;
  gboolean acl_queried;

  /*InfcBrowserNodeStatus status;*/

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
      InfcChatRequest* request;
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
      InfAclSheetSet* sheet_set;
      InfcNodeRequest* request;
      InfCommunicationJoinedGroup* subscription_group;
    } add_node;

    struct {
      InfcBrowserNode* parent;
      const InfcNotePlugin* plugin;
      gchar* name;
      InfAclSheetSet* sheet_set;
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
  InfIoTimeout* welcome_timeout;
  InfcRequestManager* request_manager;

  GHashTable* plugins; /* Registered plugins */

  InfBrowserStatus status;
  GHashTable* nodes; /* Mapping from id to node */
  InfcBrowserNode* root;

  GHashTable* accounts; /* known accounts, id -> InfAclAccount* */
  const InfAclAccount* local_account;
  gboolean account_list_queried;

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
static GQuark infc_browser_session_proxy_quark;
static GQuark infc_browser_sync_in_session_quark;
static GQuark infc_browser_sync_in_plugin_quark;

/*
 * Callbacks
 */

static void
infc_browser_get_acl_account_list_foreach_func(gpointer key,
                                               gpointer value,
                                               gpointer user_data)
{
  InfcBrowserGetAclAccountListData* data;
  data = (InfcBrowserGetAclAccountListData*)user_data;

  data->accounts[data->index++] = (const InfAclAccount*)value;
}

static const InfAclAccount*
infc_browser_acl_account_lookup_func(const gchar* id,
                                     gpointer user_data)
{
  GHashTable* account_table;
  account_table = (GHashTable*)user_data;
  return (InfAclAccount*)g_hash_table_lookup(account_table, id);
}

static void
infc_browser_browser_list_pending_requests_foreach_func(InfcRequest* request,
                                                        gpointer user_data)
{
  InfcBrowserListPendingRequestsForeachData* data;
  InfcNodeRequest* node_request;
  guint node_id;

  data = (InfcBrowserListPendingRequestsForeachData*)user_data;
  if(INFC_IS_NODE_REQUEST(request))
  {
    node_request = INFC_NODE_REQUEST(request);
    g_object_get(G_OBJECT(node_request), "node-id", &node_id, NULL);

    if(data->iter != NULL && node_id == data->iter->node_id)
      data->result = g_slist_prepend(data->result, node_request);
  }
  else
  {
    if(data->iter == NULL)
      data->result = g_slist_prepend(data->result, node_request);
  }
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
  InfcBrowserIterGetChatRequestForeachData* data;

  data = (InfcBrowserIterGetChatRequestForeachData*)user_data;
  g_assert(INFC_IS_CHAT_REQUEST(request));

  /* There can only be one such request: */
  g_assert(data->result == NULL);

  /* TODO: Stop foreach when we found the request. Requires changes in
   * InfcRequestManager. */
  data->result = INFC_CHAT_REQUEST(request);
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
                             const gchar* name,
                             const InfAclSheetSet* sheet_set)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfBrowserIter iter;

  priv = INFC_BROWSER_PRIVATE(browser);

  node = g_slice_new(InfcBrowserNode);
  node->parent = parent;
  node->id = id;
  node->name = g_strdup(name);
  node->type = type;
  if(sheet_set != NULL)
    node->acl = inf_acl_sheet_set_copy(sheet_set);
  else
    node->acl = NULL;
  node->acl_queried = FALSE;

  if(parent != NULL)
  {
    /*node->status = INFC_BROWSER_NODE_INHERIT;*/
    infc_browser_node_link(node, parent);
  }
  else
  {
    /*node->status = INFC_BROWSER_NODE_SYNC;*/
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
                                   const gchar* name,
                                   const InfAclSheetSet* sheet_set)
{
  InfcBrowserNode* node;
  node = infc_browser_node_new_common(
    browser,
    parent,
    id,
    INFC_BROWSER_NODE_SUBDIRECTORY,
    name,
    sheet_set
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
                           const gchar* type,
                           const InfAclSheetSet* sheet_set)
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
    name,
    sheet_set
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
  InfcSessionProxy* proxy;
  InfBrowserIter iter;

  g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);
  g_assert(node->shared.known.session != NULL);

  proxy = node->shared.known.session;
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

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

  node->shared.known.session = NULL;

  inf_browser_unsubscribe_session(
    INF_BROWSER(browser),
    &iter,
    INF_SESSION_PROXY(proxy)
  );

  g_object_unref(session);
  g_object_unref(proxy);
}

static void
infc_browser_node_register(InfcBrowser* browser,
                           InfcBrowserNode* node)
{
  InfBrowserIter iter;
  iter.node_id = node->id;
  iter.node = node;

  inf_browser_node_added(INF_BROWSER(browser), &iter);
}

static void
infc_browser_node_unregister(InfcBrowser* browser,
                             InfcBrowserNode* node)
{
  InfBrowserIter iter;
  iter.node_id = node->id;
  iter.node = node;

  inf_browser_node_removed(INF_BROWSER(browser), &iter);
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

  InfBrowserIter iter;
  GError* error;

  priv = INFC_BROWSER_PRIVATE(browser);

  switch(node->type)
  {
  case INFC_BROWSER_NODE_SUBDIRECTORY:
    /* Also do this for nodes that don't have the explored flag to TRUE, to
     * delete nodes in case we are still in the middle of an exploration */
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

  if(node->acl != NULL)
    inf_acl_sheet_set_free(node->acl);

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
  InfBrowserIter* iter;
  InfcBrowserNode* node;
  InfcSessionProxy* proxy;
  InfSession* proxy_session;

  browser = INFC_BROWSER(user_data);
  priv = INFC_BROWSER_PRIVATE(browser);

  iter = (InfBrowserIter*)g_object_get_qdata(
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
    
    g_object_get(
      G_OBJECT(node->shared.known.session),
      "session", &proxy_session,
      NULL
    );

    g_assert(proxy_session == session);
    g_object_unref(proxy_session);

    infc_browser_session_remove_session(browser, node);
  }
  else
  {
    g_assert(priv->chat_session != NULL);
    g_object_get(
      G_OBJECT(priv->chat_session),
      "session", &proxy_session,
      NULL
    );
    g_assert(proxy_session == session);

    inf_signal_handlers_disconnect_by_func(
      proxy_session,
      G_CALLBACK(infc_browser_session_close_cb),
      browser
    );

    g_object_unref(proxy_session);

    proxy = priv->chat_session;
    priv->chat_session = NULL;
    g_object_notify(G_OBJECT(browser), "chat-session");

    inf_browser_unsubscribe_session(
      INF_BROWSER(browser),
      NULL,
      INF_SESSION_PROXY(proxy)
    );

    g_object_unref(proxy);
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
      "the server administrators to upgrade their software.")
  );

  inf_browser_error(INF_BROWSER(browser), error);
  g_error_free(error);

  g_assert(priv->status != INF_BROWSER_CLOSED);
  priv->status = INF_BROWSER_CLOSED;
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
  if(priv->status != INF_BROWSER_OPENING)
  {
    priv->status = INF_BROWSER_OPENING;
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
  InfSession* session;
  InfBrowserIter iter;

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

#if 0
  /* Remove tree */
  for(child = priv->root->shared.subdir.child; child != NULL; child = next)
  {
    next = child->next;

    infc_browser_node_unregister(browser, child);
    infc_browser_node_free(browser, child);
  }
#endif

  if(priv->root != NULL)
  {
    infc_browser_node_unregister(browser, priv->root);
    infc_browser_node_free(browser, priv->root);
    priv->root = NULL;
  }

  priv->local_account = NULL;

  if(priv->accounts != NULL)
  {
    g_hash_table_destroy(priv->accounts);
    priv->accounts = NULL;
  }

  g_object_freeze_notify(G_OBJECT(browser));

  if(priv->chat_session != NULL)
  {
    g_object_get(G_OBJECT(priv->chat_session), "session", &session, NULL);

    inf_signal_handlers_disconnect_by_func(
      session,
      G_CALLBACK(infc_browser_session_close_cb),
      browser
    );

    g_object_unref(session);

    g_object_unref(priv->chat_session);
    priv->chat_session = NULL;
    
    g_object_notify(G_OBJECT(browser), "chat-session");
  }

  priv->status = INF_BROWSER_CLOSED;
  g_object_notify(G_OBJECT(browser), "status");
  g_object_thaw_notify(G_OBJECT(browser));
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
    if(priv->status != INF_BROWSER_OPENING)
    {
      priv->status = INF_BROWSER_OPENING;
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
    if(priv->group == NULL && priv->status != INF_BROWSER_CLOSED)
    {
      g_assert(priv->request_manager == NULL);

      priv->status = INF_BROWSER_CLOSED;
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
  inf_browser_error(INF_BROWSER(browser), error);
}

/*
 * Helper functions
 */

/* TODO: This function should be moved to InfcRequest */
static xmlNodePtr
infc_browser_request_to_xml(InfcRequest* request)
{
  xmlNodePtr xml;
  gchar* type;
  guint seq;
  gchar seq_buffer[16];
  
  g_object_get(G_OBJECT(request), "type", &type, "seq", &seq, NULL);
  sprintf(seq_buffer, "%u", seq);

  xml = xmlNewNode(NULL, (const xmlChar*)type);
  xmlNewProp(xml, (const xmlChar*)"seq", (const xmlChar*)seq_buffer);

  g_free(type);
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
  priv->status = INF_BROWSER_CLOSED;
  priv->nodes = g_hash_table_new(NULL, NULL);
  priv->root = NULL;

  priv->accounts = NULL;
  priv->local_account = NULL;
  priv->account_list_queried = FALSE;

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
  InfSession* session;

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
  g_assert(priv->accounts == NULL);
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
  {
    g_object_get(G_OBJECT(priv->chat_session), "session", &session, NULL);
    inf_session_close(session);
    g_object_unref(session);
  }

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
        if(priv->status != INF_BROWSER_OPENING)
        {
          priv->status = INF_BROWSER_OPENING;
          g_object_notify(G_OBJECT(browser), "status");
        }

        break;
      case INF_XML_CONNECTION_OPEN:
        infc_browser_connected(browser);
        break;
      case INF_XML_CONNECTION_CLOSING:
      case INF_XML_CONNECTION_CLOSED:
        if(priv->status != INF_BROWSER_CLOSED)
        {
          priv->status = INF_BROWSER_CLOSED;
          g_object_notify(G_OBJECT(browser), "status");
        }

        break;
      }
    }
    else
    {
      if(priv->status != INF_BROWSER_CLOSED)
      {
        priv->status = INF_BROWSER_CLOSED;
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
                             InfcChatRequest* request,
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
                                 const InfAclSheetSet* sheet_set,
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
  if(sheet_set != NULL)
    subreq->shared.add_node.sheet_set = inf_acl_sheet_set_copy(sheet_set);
  else
    subreq->shared.add_node.sheet_set = NULL;
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
                                const InfAclSheetSet* sheet_set,
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
  if(sheet_set != NULL)
    subreq->shared.sync_in.sheet_set = inf_acl_sheet_set_copy(sheet_set);
  else
    subreq->shared.sync_in.sheet_set = NULL;
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

    if(request->shared.add_node.sheet_set != NULL)
      inf_acl_sheet_set_free(request->shared.add_node.sheet_set);
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

    if(request->shared.sync_in.sheet_set != NULL)
      inf_acl_sheet_set_free(request->shared.sync_in.sheet_set);
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
  InfSession* session;

  priv = INFC_BROWSER_PRIVATE(browser);
  sync_in = g_slice_new(InfcBrowserSyncIn);

  sync_in->browser = browser;
  sync_in->node = node;
  /* Actually the same as browser's connection: */
  sync_in->connection = connection;
  sync_in->proxy = proxy;
  g_object_ref(proxy);

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  g_signal_connect(
    session,
    "synchronization-failed",
    G_CALLBACK(infc_browser_sync_in_synchronization_failed_cb),
    sync_in
  );

  g_signal_connect(
    session,
    "synchronization-complete",
    G_CALLBACK(infc_browser_sync_in_synchronization_complete_cb),
    sync_in
  );

  g_object_unref(session);

  priv->sync_ins = g_slist_prepend(priv->sync_ins, sync_in);
  return sync_in;
}

static void
infc_browser_remove_sync_in(InfcBrowser* browser,
                            InfcBrowserSyncIn* sync_in)
{
  InfcBrowserPrivate* priv;
  InfSession* session;

  priv = INFC_BROWSER_PRIVATE(browser);
  
  g_object_get(G_OBJECT(sync_in->proxy), "session", &session, NULL);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(infc_browser_sync_in_synchronization_complete_cb),
    sync_in
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(infc_browser_sync_in_synchronization_failed_cb),
    sync_in
  );

  g_object_unref(session);
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
                                   const gchar* name,
                                   const InfAclSheetSet* sheet_set)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_assert(parent->type == INFC_BROWSER_NODE_SUBDIRECTORY);
  g_assert(parent->shared.subdir.explored == TRUE);

  priv = INFC_BROWSER_PRIVATE(browser);

  node = infc_browser_node_new_subdirectory(
    browser,
    parent,
    id,
    name,
    sheet_set
  );

  infc_browser_node_register(browser, node);

  return node;
}

static InfcBrowserNode*
infc_browser_node_add_note(InfcBrowser* browser,
                           InfcBrowserNode* parent,
                           guint id,
                           const gchar* name,
                           const gchar* type,
                           const InfAclSheetSet* sheet_set,
                           InfcSessionProxy* sync_in_session)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_assert(parent->type == INFC_BROWSER_NODE_SUBDIRECTORY);
  g_assert(parent->shared.subdir.explored == TRUE);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_assert(priv->connection != NULL);

  node = infc_browser_node_new_note(
    browser,
    parent,
    id,
    name,
    type,
    sheet_set
  );

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

static InfcNodeRequest*
infc_browser_get_add_node_request_from_xml(InfcBrowser* browser,
                                           xmlNodePtr xml,
                                           GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  guint current;
  guint total;
  gchar* type;

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
    g_object_get(G_OBJECT(request), "type", &type, NULL);

    /* when seq was set, then we issued that add-node. We might
     * either do this implicitly by exploring a folder or explicitely by
     * sending an add-node request. */
    if(strcmp(type, "add-node") != 0 && strcmp(type, "explore-node") != 0)
    {
      g_set_error(
        error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_INVALID_SEQ,
        _("The request contains a sequence number refering to a request of "
          "type '%s', but a request of either 'explore' or 'add-node' was "
          "expected."),
        type
      );

      g_free(type);
      return NULL;
    }

    g_assert(INFC_IS_NODE_REQUEST(request));

    /* For explore request, we do some basic sanity checking here */
    if(strcmp(type, "explore-node") == 0)
    {
      /* TODO: Make a InfProgressRequest interface, and then move this code
       * into a separate function, sharing it with the add-user checks. */
      g_assert(INFC_IS_EXPLORE_REQUEST(request));

      g_object_get(
        G_OBJECT(request),
        "current", &current,
        "total", &total,
        NULL
      );

      if(infc_explore_request_get_initiated(INFC_EXPLORE_REQUEST(request)) ==
         FALSE)
      {
        g_set_error(
          error,
          inf_directory_error_quark(),
          INF_DIRECTORY_ERROR_NOT_INITIATED,
          "%s",
          inf_directory_strerror(INF_DIRECTORY_ERROR_NOT_INITIATED)
        );

        g_free(type);
        return NULL;
      }
      else if(current >= total)
      {
        g_set_error(
          error,
          inf_directory_error_quark(),
          INF_DIRECTORY_ERROR_TOO_MANY_CHILDREN,
          "%s",
          inf_directory_strerror(INF_DIRECTORY_ERROR_TOO_MANY_CHILDREN)
        );

        g_free(type);
        return NULL;
      }
    }

    g_free(type);
  }

  return INFC_NODE_REQUEST(request);
}

static void
infc_browser_process_add_node_request(InfcBrowser* browser,
                                      InfcNodeRequest* request,
                                      InfcBrowserNode* node)
{
  InfcBrowserPrivate* priv;
  InfBrowserIter iter;

  priv = INFC_BROWSER_PRIVATE(browser);

  if(INFC_IS_EXPLORE_REQUEST(request))
  {
    infc_explore_request_progress(
      INFC_EXPLORE_REQUEST(request)
    );
  }
  else
  {
    iter.node_id = node->id;
    iter.node = node;
    inf_node_request_finished(INF_NODE_REQUEST(request), &iter, NULL);

    infc_request_manager_remove_request(
      priv->request_manager,
      INFC_REQUEST(request)
    );
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
  InfBrowserIter iter;
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

  inf_browser_subscribe_session(
    INF_BROWSER(browser),
    &iter,
    INF_SESSION_PROXY(proxy)
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
  InfAclSheetSet* sheet_set;

  xmlNodePtr node;
  InfAclAccount* default_account;
  GError* local_error;
  InfAclSheet* sheet;
  InfAclMask default_mask;

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

  if(server_major < own_major)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_VERSION_MISMATCH,
      "%s",
      _("The server uses an older version of the protocol which is no longer "
        "supported by this client. Consider using an earlier version of it, "
        "or ask the server administrators to upgrade their software.")
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
      _("The server uses a newer version of the protocol which is not "
        "supported by this client. Consider upgrading your client.")
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

  /* Load ACL accounts */
  g_assert(priv->accounts == NULL);
  g_assert(priv->local_account == NULL);

  priv->accounts = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    NULL,
    (GDestroyNotify)inf_acl_account_free
  );

  default_account = inf_acl_account_new("default", NULL);
  g_hash_table_insert(priv->accounts, default_account->id, default_account);

  priv->local_account = default_account;
  for(node = xml->children; node != NULL; node = node->next)
  {
    if(node->type != XML_ELEMENT_NODE) continue;

    if(strcmp(node->name, "account") == 0)
    {
      priv->local_account = inf_acl_account_from_xml(node, error);
      if(priv->local_account == NULL)
      {
        g_hash_table_destroy(priv->accounts);
        priv->accounts = NULL;
        return FALSE;
      }

      g_hash_table_insert(
        priv->accounts,
        priv->local_account->id,
        (gpointer)priv->local_account
      );

      break;
    }
  }

  /* Load the root ACL sheet set */
  local_error = NULL;

  sheet_set = inf_acl_sheet_set_from_xml(
    xml,
    infc_browser_acl_account_lookup_func,
    priv->accounts,
    &local_error
  );

  if(local_error != NULL)
  {
    g_hash_table_destroy(priv->accounts);
    priv->accounts = NULL;
    priv->local_account = NULL;

    g_propagate_error(error, local_error);
    return FALSE;
  }

  /* Assume default permissions for the root node if they are not sent
   * explicitely by the server. */
  if(sheet_set == NULL)
    sheet_set = inf_acl_sheet_set_new();

  sheet = inf_acl_sheet_set_add_sheet(sheet_set, default_account);
  default_mask = sheet->mask;

  inf_acl_mask_neg(&default_mask, &default_mask);
  inf_acl_mask_and(&default_mask, &INF_ACL_MASK_DEFAULT, &default_mask);
  inf_acl_mask_or(&sheet->perms, &default_mask, &sheet->perms);

  sheet->mask = INF_ACL_MASK_ALL;

  g_assert(priv->root == NULL);
  priv->root = infc_browser_node_new_subdirectory(
    browser,
    NULL,
    0,
    NULL,
    sheet_set
  );

  inf_acl_sheet_set_free(sheet_set);

  g_assert(priv->request_manager == NULL);
  priv->request_manager = infc_request_manager_new(priv->seq_id);

  priv->status = INF_BROWSER_OPEN;
  g_object_notify(G_OBJECT(browser), "status");

  inf_browser_acl_account_added(INF_BROWSER(browser), default_account);
  if(priv->local_account != default_account)
    inf_browser_acl_account_added(INF_BROWSER(browser), priv->local_account);

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
  guint total;

  guint node_id;
  InfcBrowserNode* node;

  priv = INFC_BROWSER_PRIVATE(browser);

  /* TODO: Allow exploration without us asking. We should then create a
   * request here, and correctly handle the case when there is already one. */
  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    "explore-node",
    xml,
    error
  );

  if(request == NULL) return FALSE;
  g_assert(INFC_IS_EXPLORE_REQUEST(request));

  if(!inf_xml_util_get_attribute_uint_required(xml, "total", &total, error))
    return FALSE;

  g_object_get(G_OBJECT(request), "node-id", &node_id, NULL);
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
  guint current;
  guint total;
  InfBrowserIter iter;

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

  g_object_get(G_OBJECT(request), "current", &current, "total", &total, NULL);
  if(current < total)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_TOO_FEW_CHILDREN,
      "%s",
      _("Not all nodes were received before explore-end was received")
    );

    return FALSE;
  }
  else
  {
    g_object_get(G_OBJECT(request), "node-id", &iter.node_id, NULL);

    iter.node = g_hash_table_lookup(
      priv->nodes,
      GUINT_TO_POINTER(iter.node_id)
    );

    /* The node being explored must exist, or the request would have been
     * cancelled before. */
    g_assert(iter.node != NULL);

    /*((InfcBrowserNode*)iter.node)->shared.subdir.explored = TRUE;*/

    inf_node_request_finished(INF_NODE_REQUEST(request), &iter, NULL);
    if(priv->status == INF_BROWSER_OPEN)
      infc_request_manager_remove_request(priv->request_manager, request);
    return TRUE;
  }
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
  InfAclSheetSet* sheet_set;
  InfcNodeRequest* request;
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

  sheet_set = inf_acl_sheet_set_from_xml(
    xml,
    infc_browser_acl_account_lookup_func,
    priv->accounts,
    &local_error
  );

  if(local_error != NULL)
  {
    xmlFree(type);
    xmlFree(name);
    g_propagate_error(error, local_error);
    return FALSE;
  }

  if(strcmp((const gchar*)type, "InfSubdirectory") == 0)
  {
    node = infc_browser_node_add_subdirectory(
      browser,
      parent,
      id,
      (const gchar*)name,
      sheet_set
    );

    if(request != NULL)
    {
      infc_browser_process_add_node_request(
        browser,
        request,
        node
      );
    }

    result = TRUE;
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
              sheet_set,
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
        sheet_set,
        NULL
      );

      if(request != NULL)
      {
        infc_browser_process_add_node_request(
          browser,
          request,
          node
        );
      }

      result = TRUE;
    }
  }

  if(sheet_set != NULL)
    inf_acl_sheet_set_free(sheet_set);

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
  GError* local_error;
  InfAclSheetSet* sheet_set;
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
        local_error = NULL;

        sheet_set = inf_acl_sheet_set_from_xml(
          xml,
          infc_browser_acl_account_lookup_func,
          priv->accounts,
          &local_error
        );

        if(local_error != NULL)
        {
          g_propagate_error(error, local_error);
        }
        else
        {
          /* Note that all the errors which could have occured up to this
           * point are the server's fault. If one of those occured, then the
           * server sent us crap and could have known better. We therefore
           * don't reply with subscribe-nack on these errors. */

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
              sheet_set,
              INFC_NODE_REQUEST(request),
              session,
              sync_group,
              child != NULL ? sync_group : NULL
            );

            g_object_unref(sync_group);

            infc_browser_subscribe_ack(browser, connection, subreq);
            result = TRUE;
          }

          if(sheet_set != NULL)
            inf_acl_sheet_set_free(sheet_set);
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
  InfBrowserIter iter;

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
    inf_node_request_finished(INF_NODE_REQUEST(request), &iter, NULL);
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

  g_assert(request == NULL || INFC_IS_CHAT_REQUEST(request));

  subreq = infc_browser_add_subreq_chat(
    browser,
    INFC_CHAT_REQUEST(request),
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
  InfBrowserIter iter;

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
    inf_node_request_finished(INF_NODE_REQUEST(request), &iter, NULL);
    infc_request_manager_remove_request(priv->request_manager, request);
  }

  return TRUE;
}

static gboolean
infc_browser_handle_certificate_generated(InfcBrowser* browser,
                                          InfXmlConnection* connection,
                                          xmlNodePtr xml,
                                          GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  xmlNodePtr child;
  gnutls_datum_t cert_text;

  int res;
  gnutls_x509_crt_t cert;
  InfCertificateChain* chain;
  gnutls_x509_crt_t root_cert;

  int verify_result;
  guint n_certs;
  guint i;
  gnutls_x509_crt_t* all_certs;
  InfCertificateChain* new_chain;

  priv = INFC_BROWSER_PRIVATE(browser);

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "request-certificate",
    xml,
    NULL
  );

  if(request == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE,
      "%s",
      _("No certificate request has been made")
    );
  }

  cert_text.data = NULL;
  for(child = xml->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;

    if(strcmp((const char*)child->name, "certificate") == 0)
    {
      if(child->children != NULL && child->children->type == XML_TEXT_NODE)
      {
        cert_text.data = (char*)child->children->content;
        cert_text.size = strlen(cert_text.data);
      }
    }
  }

  if(cert_text.data == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
      "%s",
      _("No certificate provided")
    );

    return FALSE;
  }

  res = gnutls_x509_crt_init(&cert);
  if(res != GNUTLS_E_SUCCESS)
  {
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  res = gnutls_x509_crt_import(cert, &cert_text, GNUTLS_X509_FMT_PEM);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  g_object_get(G_OBJECT(connection), "remote-certificate", &chain, NULL);
  if(chain == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED,
      "%s",
      _("Cannot verify the certificate without server certificate")
    );

    gnutls_x509_crt_deinit(cert);
    return FALSE;
  }

  /* Verify that it is signed
   * a) correctly
   * b) by the server itself */
  root_cert = inf_certificate_chain_get_root_certificate(chain);

  /* TODO: Validate the whole chain after it was constructed,
   * using gnutls_x509_crt_list_verify(). */
  res = gnutls_x509_crt_verify(
    cert,
    &root_cert,
    1,
    GNUTLS_VERIFY_DO_NOT_ALLOW_X509_V1_CA_CRT,
    &verify_result
  );

  inf_certificate_chain_unref(chain);
  if(res != GNUTLS_E_SUCCESS || (verify_result & GNUTLS_CERT_INVALID) != 0)
  {
    if(res != GNUTLS_E_SUCCESS)
    {
      inf_gnutls_set_error(error, res);
    }
    else
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_INVALID_CERTIFICATE,
        _("Server sent an invalid certificate (%d)"),
        verify_result
      );
    }

    gnutls_x509_crt_deinit(cert);
    return FALSE;
  }

  n_certs = inf_certificate_chain_get_n_certificates(chain);
  all_certs = g_malloc(sizeof(gnutls_x509_crt_t) * (n_certs + 1));
  all_certs[0] = cert;
  for(i = 0; i < n_certs; ++i)
  {
    all_certs[i+1] = inf_cert_util_copy_certificate(
      inf_certificate_chain_get_nth_certificate(chain, i),
      error
    );

    if(all_certs[i+1] == NULL)
    {
      for(; i > 0; --i)
        gnutls_x509_crt_deinit(all_certs[i]);
      gnutls_x509_crt_deinit(cert);
      return FALSE;
    }
  }

  new_chain = inf_certificate_chain_new(all_certs, n_certs + 1);

  infc_certificate_request_finished(
    INFC_CERTIFICATE_REQUEST(request),
    new_chain,
    NULL
  );

  infc_request_manager_remove_request(priv->request_manager, request);

  inf_certificate_chain_unref(new_chain);
  return TRUE;
}

static gboolean
infc_browser_handle_acl_account_list_begin(InfcBrowser* browser,
                                           InfXmlConnection* connection,
                                           xmlNodePtr xml,
                                           GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  guint total;

  guint node_id;
  InfcBrowserNode* node;
  InfBrowserIter iter;

  priv = INFC_BROWSER_PRIVATE(browser);

  /* TODO: Allow account list query without us asking. We should then create a
   * request here, and correctly handle the case that there is already one. */
  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    "query-acl-account-list",
    xml,
    error
  );

  if(request == NULL) return FALSE;
  g_assert(INFC_IS_ACL_ACCOUNT_LIST_REQUEST(request));

  if(!inf_xml_util_get_attribute_uint_required(xml, "total", &total, error))
    return FALSE;

  if(priv->account_list_queried == TRUE)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ACCOUNT_LIST_ALREADY_QUERIED,
      "%s",
      _("The account list has already been queried")
    );

    return FALSE;
  }
  else
  {
    priv->account_list_queried = TRUE;

    infc_acl_account_list_request_initiated(
      INFC_ACL_ACCOUNT_LIST_REQUEST(request),
      total
    );

    return TRUE;
  }
}

static gboolean
infc_browser_handle_acl_account_list_end(InfcBrowser* browser,
                                         InfXmlConnection* connection,
                                         xmlNodePtr xml,
                                         GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  guint current;
  guint total;
  InfBrowserIter iter;

  priv = INFC_BROWSER_PRIVATE(browser);

  /* TODO: Allow user list query without us asking. */
  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    "query-acl-account-list",
    xml,
    error
  );

  if(request == NULL) return FALSE;
  g_assert(INFC_IS_ACL_ACCOUNT_LIST_REQUEST(request));

  g_object_get(G_OBJECT(request), "current", &current, "total", &total, NULL);
  if(current < total)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_TOO_FEW_CHILDREN,
      "%s",
      _("Not all users have been transmitted before "
        "user-list-end was received")
    );

    return FALSE;
  }
  else
  {
    inf_acl_account_list_request_finished(
      INF_ACL_ACCOUNT_LIST_REQUEST(request),
      NULL
    );

    if(priv->status == INF_BROWSER_OPEN)
      infc_request_manager_remove_request(priv->request_manager, request);

    return TRUE;
  }
}

static gboolean
infc_browser_handle_add_acl_account(InfcBrowser* browser,
                                    InfXmlConnection* connection,
                                    xmlNodePtr xml,
                                    GError** error)
{
  InfcBrowserPrivate* priv;
  GError* local_error;
  InfcRequest* request;
  InfcAclAccountListRequest* acl_request;
  guint current;
  guint total;
  InfAclAccount* account;
  gchar* id;

  priv = INFC_BROWSER_PRIVATE(browser);

  local_error = NULL;
  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "query-acl-account-list",
    xml,
    &local_error
  );

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  acl_request = NULL;
  if(request != NULL)
  {
    g_assert(INFC_IS_ACL_ACCOUNT_LIST_REQUEST(request));
    acl_request = INFC_ACL_ACCOUNT_LIST_REQUEST(request);

    g_object_get(
      G_OBJECT(request),
      "current", &current,
      "total", &total,
      NULL
    );

    if(infc_acl_account_list_request_get_initiated(acl_request) == FALSE)
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NOT_INITIATED,
        "%s",
        _("\"query-acl-account-list\" request was not initiated yet, but "
          "\"add-acl-account\" was already received")
      );

      return FALSE;
    }
    else if(current >= total)
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_TOO_MANY_CHILDREN,
        "%s",
        _("More users were added in response to \"query-acl-account-list\" "
          "than initially proclaimed")
      );

      return FALSE;
    }
  }

  account = inf_acl_account_from_xml(xml, error);
  if(account == NULL) return FALSE;

  if(g_hash_table_lookup(priv->accounts, account->id) != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_DUPLICATE_ACCOUNT,
      "%s",
      _("Server sent a duplicate account in \"query-acl-account-list\"")
    );

    inf_acl_account_free(account);
    return FALSE;
  }

  g_hash_table_insert(priv->accounts, account->id, account);

  if(acl_request != NULL)
    infc_acl_account_list_request_progress(acl_request);

  inf_browser_acl_account_added(INF_BROWSER(browser), account);
  return TRUE;
}

static gboolean
infc_browser_handle_set_acl(InfcBrowser* browser,
                            InfXmlConnection* connection,
                            xmlNodePtr xml,
                            GError** error)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfAclSheetSet* sheet_set;
  GError* local_error;
  InfcRequest* request;
  gchar* request_type;
  InfBrowserIter iter;

  priv = INFC_BROWSER_PRIVATE(browser);

  node = infc_browser_get_node_from_xml(browser, xml, "id", error);
  if(node == NULL) return FALSE;

  local_error = NULL;

  sheet_set = inf_acl_sheet_set_from_xml(
    xml,
    infc_browser_acl_account_lookup_func,
    priv->accounts,
    &local_error
  );

  if(local_error != NULL)
    return FALSE;

  /* request can either by query-acl or set-acl */
  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    NULL,
    xml,
    error
  );

  g_assert(request == NULL || INF_IS_NODE_REQUEST(request));

  /* Remember that we have queried this ACL */
  if(request != NULL)
  {
    g_object_get(G_OBJECT(request), "type", &request_type, NULL);
    if(strcmp(request_type, "query-acl") == 0)
    {
      g_assert(node->acl_queried == FALSE);
      node->acl_queried = TRUE;
    }

    g_free(request_type);
  }

  iter.node_id = node->id;
  iter.node = node;

  /* sheet_set can be NULL, for example when querying the ACL for a node and
   * the ACL is empty. */
  if(sheet_set != NULL)
  {
    if(sheet_set->n_sheets > 0)
    {
      node->acl = inf_acl_sheet_set_merge_sheets(node->acl, sheet_set);
      inf_browser_acl_changed(INF_BROWSER(browser), &iter, sheet_set);
    }

    inf_acl_sheet_set_free(sheet_set);
  }

  if(request != NULL)
    inf_node_request_finished(INF_NODE_REQUEST(request), &iter, NULL);

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
  else if(g_quark_from_string((gchar*)domain) == inf_request_error_quark())
  {
    g_set_error(
      &req_error,
      inf_request_error_quark(),
      code,
      "%s",
      inf_request_strerror(code)
    );
  }
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
 * InfCommunicationObject implementation
 */

static InfCommunicationScope
infc_browser_communication_object_received(InfCommunicationObject* object,
                                           InfXmlConnection* connection,
                                           xmlNodePtr node)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  GError* local_error;
  GError* seq_error;
  InfcRequest* request;

  browser = INFC_BROWSER(object);
  priv = INFC_BROWSER_PRIVATE(browser);
  local_error = NULL;

  if(priv->status == INF_BROWSER_OPENING &&
     strcmp((const gchar*)node->name, "welcome") == 0)
  {
    if(priv->welcome_timeout != NULL)
    {
      inf_io_remove_timeout(priv->io, priv->welcome_timeout);
      priv->welcome_timeout = NULL;
    }

    if(!infc_browser_handle_welcome(browser, connection, node, &local_error))
    {
      inf_browser_error(INF_BROWSER(browser), local_error);
      g_error_free(local_error);
      local_error = NULL;

      priv->status = INF_BROWSER_CLOSED;
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
  else if(strcmp((const gchar*)node->name, "certificate-generated") == 0)
  {
    infc_browser_handle_certificate_generated(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "acl-account-list-begin") == 0)
  {
    infc_browser_handle_acl_account_list_begin(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "acl-account-list-end") == 0)
  {
    infc_browser_handle_acl_account_list_end(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "add-acl-account") == 0)
  {
    infc_browser_handle_add_acl_account(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "set-acl") == 0)
  {
    infc_browser_handle_set_acl(
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

    inf_browser_error(INF_BROWSER(browser), local_error);
    g_error_free(local_error);
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
  InfBrowserIter iter;

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

      inf_browser_subscribe_session(
        INF_BROWSER(browser),
        NULL,
        INF_SESSION_PROXY(proxy)
      );

      /* The default handler refs the proxy */
      g_object_unref(proxy);

      if(subreq->shared.chat.request != NULL)
      {
        infc_chat_request_finished(subreq->shared.chat.request, NULL);

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

          inf_node_request_finished(
            INF_NODE_REQUEST(subreq->shared.session.request),
            &iter,
            NULL
          );

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
          subreq->shared.add_node.sheet_set,
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

          inf_node_request_finished(
            INF_NODE_REQUEST(subreq->shared.add_node.request),
            &iter,
            NULL
          );

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
          subreq->shared.sync_in.sheet_set,
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

          inf_browser_subscribe_session(
            INF_BROWSER(browser),
            &iter,
            INF_SESSION_PROXY(proxy)
          );
        }

        g_object_unref(proxy);

        /* TODO: Emit a signal, so that others are notified that a sync-in
         * begins and can show progress or something. */

        g_assert(subreq->shared.sync_in.request != NULL);

        inf_node_request_finished(
          INF_NODE_REQUEST(subreq->shared.sync_in.request),
          &iter,
          NULL
        );

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
 * InfBrowser implementation
 */

static void
infc_browser_browser_subscribe_session(InfBrowser* browser,
                                       const InfBrowserIter* iter,
                                       InfSessionProxy* proxy)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfSession* session;

  priv = INFC_BROWSER_PRIVATE(browser);

  g_assert(INFC_IS_SESSION_PROXY(proxy));
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  if(iter != NULL)
  {
    node = (InfcBrowserNode*)iter->node;

    g_assert(
      g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(iter->node_id)) ==
      node
    );

    g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);
    g_assert(node->shared.known.session == NULL);

    node->shared.known.session = INFC_SESSION_PROXY(proxy);

    g_object_ref(proxy);

    /* Associate the iter to the session so that we can remove the proxy
     * from that item when the session is closed. */
    g_object_set_qdata_full(
      G_OBJECT(session),
      infc_browser_session_proxy_quark,
      inf_browser_iter_copy(iter),
      (GDestroyNotify)inf_browser_iter_free
    );
  }
  else
  {
    g_assert(priv->chat_session == NULL);

    g_object_ref(proxy);
    priv->chat_session = INFC_SESSION_PROXY(proxy);

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

  g_object_unref(session);
}

static gboolean
infc_browser_browser_get_root(InfBrowser* browser,
                              InfBrowserIter* iter)
{
  InfcBrowserPrivate* priv;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, FALSE);

  g_assert(priv->root != NULL);
  iter->node_id = priv->root->id;
  iter->node = priv->root;
  return TRUE;
}

static gboolean
infc_browser_browser_get_next(InfBrowser* browser,
                              InfBrowserIter* iter)
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

static gboolean
infc_browser_browser_get_prev(InfBrowser* browser,
                              InfBrowserIter* iter)
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

static gboolean
infc_browser_browser_get_parent(InfBrowser* browser,
                                InfBrowserIter* iter)
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

static gboolean
infc_browser_browser_get_child(InfBrowser* browser,
                               InfBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(browser, iter, FALSE);

  node = (InfcBrowserNode*)iter->node;

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

static InfExploreRequest*
infc_browser_browser_explore(InfBrowser* browser,
                             const InfBrowserIter* iter)
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
  g_return_val_if_fail(
    inf_browser_get_pending_request(browser, iter, "explore-node") == NULL,
    NULL
  );

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_EXPLORE_REQUEST,
    "explore-node",
    "node_id", node->id,
    NULL
  );

  inf_browser_begin_request(browser, iter, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_EXPLORE_REQUEST(request);
}

static gboolean
infc_browser_browser_get_explored(InfBrowser* browser,
                                  const InfBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(browser, iter, FALSE);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_return_val_if_subdir_fail(node, FALSE);

  return node->shared.subdir.explored;
}

static gboolean
infc_browser_browser_is_subdirectory(InfBrowser* browser,
                                     const InfBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(browser, iter, FALSE);

  node = (InfcBrowserNode*)iter->node;
  if(node->type == INFC_BROWSER_NODE_SUBDIRECTORY)
    return TRUE;
  return FALSE;
}

static InfNodeRequest*
infc_browser_browser_add_note(InfBrowser* infbrowser,
                              const InfBrowserIter* iter,
                              const char* name,
                              const char* type,
                              const InfAclSheetSet* sheet_set,
                              InfSession* session,
                              gboolean initial_subscribe)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  const InfcNotePlugin* plugin;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(infbrowser), NULL);
  browser = INFC_BROWSER(infbrowser);

  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_return_val_if_subdir_fail(node, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);

  plugin = infc_browser_lookup_plugin(browser, type);
  g_return_val_if_fail(plugin != NULL, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "add-node",
    "node-id", iter->node_id,
    NULL
  );

  if(session != NULL)
  {
    /* TODO: Add a InfcSyncInRequest, deriving from InfcNodeRequest that
     * carries session and plugin, so we don't need g_object_set_qdata for the
     * session and plugin. */
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
  }

  inf_browser_begin_request(INF_BROWSER(browser), iter, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "parent", node->id);
  inf_xml_util_set_attribute(xml, "type", type);
  inf_xml_util_set_attribute(xml, "name", name);

  if(sheet_set != NULL)
    inf_acl_sheet_set_to_xml(sheet_set, xml);

  if(initial_subscribe != FALSE)
    xmlNewChild(xml, NULL, (const xmlChar*)"subscribe", NULL);
  if(session != NULL)
    xmlNewChild(xml, NULL, (const xmlChar*)"sync-in", NULL);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_NODE_REQUEST(request);
}

static InfNodeRequest*
infc_browser_browser_add_subdirectory(InfBrowser* infbrowser,
                                      const InfBrowserIter* iter,
                                      const char* name,
                                      const InfAclSheetSet* sheet_set)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(infbrowser), NULL);
  browser = INFC_BROWSER(infbrowser);

  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  infc_browser_return_val_if_subdir_fail(node, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "add-node",
    "node-id", iter->node_id,
    NULL
  );

  inf_browser_begin_request(INF_BROWSER(browser), iter, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "parent", node->id);
  inf_xml_util_set_attribute(xml, "type", "InfSubdirectory");
  inf_xml_util_set_attribute(xml, "name", name);

  if(sheet_set != NULL)
    inf_acl_sheet_set_to_xml(sheet_set, xml);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_NODE_REQUEST(request);
}

static InfNodeRequest*
infc_browser_browser_remove_node(InfBrowser* infbrowser,
                                 const InfBrowserIter* iter)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(infbrowser), NULL);
  browser = INFC_BROWSER(infbrowser);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  /* The root node cannot be removed */
  g_return_val_if_fail(node->parent != NULL, NULL);

  /* TODO: Check that there is not a remove-node request already enqueued. */

  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "remove-node",
    "node-id", iter->node_id,
    NULL
  );

  inf_browser_begin_request(INF_BROWSER(browser), iter, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_NODE_REQUEST(request);
}

static const gchar*
infc_browser_browser_get_node_name(InfBrowser* browser,
                                   const InfBrowserIter* iter)
{
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  return node->name;
}

static const gchar*
infc_browser_browser_get_node_type(InfBrowser* infbrowser,
                                   const InfBrowserIter* iter)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(infbrowser), NULL);
  browser = INFC_BROWSER(infbrowser);
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

static InfNodeRequest*
infc_browser_browser_subscribe(InfBrowser* browser,
                               const InfBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);

  infc_browser_return_val_if_iter_fail(INFC_BROWSER(browser), iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);
  g_return_val_if_fail(node->type == INFC_BROWSER_NODE_NOTE_KNOWN, NULL);
  g_return_val_if_fail(node->shared.known.session == NULL, NULL);

  g_return_val_if_fail(
    inf_browser_get_pending_request(
      browser,
      iter,
      "subscribe-session"
    ) == NULL,
    NULL
  );

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "subscribe-session",
    "node-id", iter->node_id,
    NULL
  );

  inf_browser_begin_request(browser, iter, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_NODE_REQUEST(request);
}

static InfSessionProxy*
infc_browser_browser_get_session(InfBrowser* browser,
                                 const InfBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(browser, iter, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  if(node->type != INFC_BROWSER_NODE_NOTE_KNOWN) return NULL;
  return INF_SESSION_PROXY(node->shared.known.session);
}

static GSList*
infc_browser_browser_list_pending_requests(InfBrowser* browser,
                                           const InfBrowserIter* iter,
                                           const gchar* request_type)
{
  InfcBrowserPrivate* priv;
  InfcBrowserListPendingRequestsForeachData data;

  data.iter = iter;
  data.result = NULL;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);

  if(iter != NULL)
  {
    infc_browser_return_val_if_iter_fail(browser, iter, NULL);
  }

  priv = INFC_BROWSER_PRIVATE(browser);

  if(priv->request_manager != NULL)
  {
    if(request_type == NULL)
    {
      infc_request_manager_foreach_request(
        priv->request_manager,
        infc_browser_browser_list_pending_requests_foreach_func,
        &data
      );
    }
    else
    {
      infc_request_manager_foreach_named_request(
        priv->request_manager,
        request_type,
        infc_browser_browser_list_pending_requests_foreach_func,
        &data
      );
    }
  }

  return data.result;
}

static gboolean
infc_browser_browser_iter_from_request(InfBrowser* browser,
                                       InfNodeRequest* request,
                                       InfBrowserIter* iter)
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

static InfAclAccountListRequest*
infc_browser_browser_query_acl_account_list(InfBrowser* browser)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  priv = INFC_BROWSER_PRIVATE(browser);

  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);
  g_return_val_if_fail(priv->account_list_queried == FALSE, NULL);

  g_return_val_if_fail(
    inf_browser_get_pending_request(
      INF_BROWSER(browser),
      NULL,
      "query-acl-account-list"
    ) == NULL,
    NULL
  );

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_ACL_ACCOUNT_LIST_REQUEST,
    "query-acl-account-list",
    NULL
  );

  inf_browser_begin_request(browser, NULL, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_ACL_ACCOUNT_LIST_REQUEST(request);
}

static const InfAclAccount**
infc_browser_browser_get_acl_account_list(InfBrowser* infbrowser,
                                          guint* n_accounts)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfcBrowserGetAclAccountListData data;

  browser = INFC_BROWSER(infbrowser);
  priv = INFC_BROWSER_PRIVATE(browser);

  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);

  if(priv->account_list_queried == FALSE)
    return NULL;

  *n_accounts = g_hash_table_size(priv->accounts);
  if(*n_accounts == 0)
    return NULL;

  data.accounts = g_malloc(sizeof(InfAclAccount*) * (*n_accounts));
  data.index = 0;

  g_hash_table_foreach(
    priv->accounts,
    infc_browser_get_acl_account_list_foreach_func,
    &data
  );

  return data.accounts;
}

static const InfAclAccount*
infc_browser_browser_get_acl_local_account(InfBrowser* infbrowser)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;

  browser = INFC_BROWSER(infbrowser);
  priv = INFC_BROWSER_PRIVATE(browser);

  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);
  g_assert(priv->local_account != NULL);

  return priv->local_account;
}

static const InfAclAccount*
infc_browser_browser_lookup_acl_account(InfBrowser* browser,
                                        const gchar* id)
{
  InfcBrowserPrivate* priv;
  priv = INFC_BROWSER_PRIVATE(browser);
  return (const InfAclAccount*)g_hash_table_lookup(priv->accounts, id);
}

static InfNodeRequest*
infc_browser_browser_query_acl(InfBrowser* browser,
                               const InfBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(INFC_BROWSER(browser), iter, NULL);
  node = (InfcBrowserNode*)iter->node;

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);
  g_return_val_if_fail(node->acl_queried == FALSE, NULL);

  g_return_val_if_fail(
    inf_browser_get_pending_request(browser, iter, "query-acl") == NULL,
    NULL
  );

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "query-acl",
    "node-id", node->id,
    NULL
  );

  inf_browser_begin_request(browser, iter, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_NODE_REQUEST(request);
}

static gboolean
infc_browser_browser_has_acl(InfBrowser* browser,
                             const InfBrowserIter* iter,
                             const InfAclAccount* account)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(INFC_BROWSER(browser), iter, FALSE);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  /* If we have queried the full ACL, everything is available */
  if(node->acl_queried == TRUE)
    return TRUE;

  /* Otherwise we only have the local user and the default user sheets */
  if(account != NULL)
  {
    if(account == priv->local_account || strcmp(account->id, "default") == 0)
      return TRUE;
  }

  /* So if that's not the case we don't have it */
  return FALSE;
}

static const InfAclSheetSet*
infc_browser_browser_get_acl(InfBrowser* browser,
                             const InfBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(INFC_BROWSER(browser), iter, NULL);

  node = (InfcBrowserNode*)iter->node;
  return node->acl;
}

static InfNodeRequest*
infc_browser_browser_set_acl(InfBrowser* browser,
                             const InfBrowserIter* iter,
                             const InfAclSheetSet* sheet_set)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  infc_browser_return_val_if_iter_fail(INFC_BROWSER(browser), iter, NULL);
  node = (InfcBrowserNode*)iter->node;

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);
  g_return_val_if_fail(node->acl_queried == TRUE, NULL);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_NODE_REQUEST,
    "set-acl",
    "node-id", node->id,
    NULL
  );

  inf_browser_begin_request(browser, iter, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);
  inf_xml_util_set_attribute_uint(xml, "id", node->id);

  inf_acl_sheet_set_to_xml(sheet_set, xml);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_NODE_REQUEST(request);
}

/*
 * GType registration.
 */

static void
infc_browser_class_init(gpointer g_class,
                        gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcBrowserPrivate));

  object_class->constructor = infc_browser_constructor;
  object_class->dispose = infc_browser_dispose;
  object_class->finalize = infc_browser_finalize;
  object_class->set_property = infc_browser_set_property;
  object_class->get_property = infc_browser_get_property;

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
    PROP_CHAT_SESSION,
    g_param_spec_object(
      "chat-session",
      "Chat session",
      "Active chat session",
      INFC_TYPE_SESSION_PROXY,
      G_PARAM_READABLE
    )
  );

  g_object_class_override_property(object_class, PROP_STATUS, "status");
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

static void
infc_browser_browser_init(gpointer g_iface,
                          gpointer iface_data)
{
  InfBrowserIface* iface;
  iface = (InfBrowserIface*)g_iface;

  iface->error = NULL;
  iface->node_added = NULL;
  iface->node_removed = NULL;
  iface->subscribe_session = infc_browser_browser_subscribe_session;
  iface->unsubscribe_session = NULL;
  iface->begin_request = NULL;
  iface->acl_account_added = NULL;
  iface->acl_changed = NULL;

  iface->get_root = infc_browser_browser_get_root;
  iface->get_next = infc_browser_browser_get_next;
  iface->get_prev = infc_browser_browser_get_prev;
  iface->get_parent = infc_browser_browser_get_parent;
  iface->get_child = infc_browser_browser_get_child;
  iface->explore = infc_browser_browser_explore;
  iface->get_explored = infc_browser_browser_get_explored;
  iface->is_subdirectory = infc_browser_browser_is_subdirectory;
  iface->add_note = infc_browser_browser_add_note;
  iface->add_subdirectory = infc_browser_browser_add_subdirectory;
  iface->remove_node = infc_browser_browser_remove_node;
  iface->get_node_name = infc_browser_browser_get_node_name;
  iface->get_node_type = infc_browser_browser_get_node_type;
  iface->subscribe = infc_browser_browser_subscribe;
  iface->get_session = infc_browser_browser_get_session;
  iface->list_pending_requests = infc_browser_browser_list_pending_requests;
  iface->iter_from_request = infc_browser_browser_iter_from_request;

  iface->query_acl_account_list = infc_browser_browser_query_acl_account_list;
  iface->get_acl_account_list = infc_browser_browser_get_acl_account_list;
  iface->get_acl_local_account = infc_browser_browser_get_acl_local_account;
  iface->lookup_acl_account = infc_browser_browser_lookup_acl_account;
  iface->query_acl = infc_browser_browser_query_acl;
  iface->has_acl = infc_browser_browser_has_acl;
  iface->get_acl = infc_browser_browser_get_acl;
  iface->set_acl = infc_browser_browser_set_acl;
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
    
    static const GInterfaceInfo browser_info = {
      infc_browser_browser_init,
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
    
    g_type_add_interface_static(
      browser_type,
      INF_TYPE_BROWSER,
      &browser_info
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
                               const InfBrowserIter* iter)
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

  inf_browser_begin_request(INF_BROWSER(browser), iter, INF_REQUEST(request));

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
 * infc_browser_iter_get_sync_in:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a note in @browser.
 *
 * Returns the #InfcSessionProxy that is used to synchronize the note @iter
 * points to to the server. Such a node is created by
 * inf_browser_add_note() with non-%NULL @session parameter. If the client is
 * subscribed to this note, then this returns the same session as
 * inf_browser_get_session(). However, it is possible that we
 * synchronize this node to the server without being subscribed to it. In
 * this case, this function returns the session that does the synchronization,
 * while inf_browser_get_session() would return %NULL.
 *
 * Return Value: A #InfcSessionProxy, or %NULL if we are currently not
 * synchronizing this node to the server.
 **/
InfcSessionProxy*
infc_browser_iter_get_sync_in(InfcBrowser* browser,
                              const InfBrowserIter* iter)
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
 * infc_browser_iter_get_sync_in_requests:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter pointing to a subdirectory node in @browser.
 *
 * Returns a list of all #InfcNodeRequest created with
 * inf_browser_add_note() with the node @iter points to as
 * parent. Such requests begin a synchronization to the server when they
 * have finished.
 *
 * Return Value: A list of #InfcNodeRequest<!-- -->s. Free with g_slist_free()
 * when done.
 **/
GSList*
infc_browser_iter_get_sync_in_requests(InfcBrowser* browser,
                                       const InfBrowserIter* iter)
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
 * infc_browser_iter_is_valid:
 * @browser: A #InfcBrowser.
 * @iter: A #InfcBrowserIter.
 *
 * Returns whether @iter points to a valid node in @browser. This is
 * generally the case for any #InfcBrowserIter returned by one of the
 * InfcBrowser functions, it gets invalid however as soon as the node it
 * points to is removed.
 *
 * Returns: Whether @iter points to a node in @browser.
 */
gboolean
infc_browser_iter_is_valid(InfcBrowser* browser,
                           const InfBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  gpointer node;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  if(!iter) return FALSE;

  priv = INFC_BROWSER_PRIVATE(browser);
  node = g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(iter->node_id));
  return node != NULL && node == iter->node;
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
 * Returns: A #InfcChatRequest that may be used to get notified when
 * the request finishes or fails.
 */
InfcChatRequest*
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
    INFC_TYPE_CHAT_REQUEST,
    "subscribe-chat",
    NULL
  );

  inf_browser_begin_request(INF_BROWSER(browser), NULL, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INFC_CHAT_REQUEST(request);
}

/**
 * infc_browser_get_subscribe_chat_request:
 * @browser: A #InfcBrowser.
 *
 * Returns the #InfcChatRequest that represests the request sent to the server
 * which attempts to subscribe to its chat. If there is no such request
 * running, then the function returns %NULL. After such a request finishes,
 * call infc_browser_get_chat_session() to get the #InfcSessionProxy for the
 * chat session. To initiate the request, call infc_browser_subscribe_chat().
 *
 * Returns: A #InfcChatRequest, or %NULL.
 */
InfcChatRequest*
infc_browser_get_subscribe_chat_request(InfcBrowser* browser)
{
  InfcBrowserPrivate* priv;
  InfcBrowserIterGetChatRequestForeachData data;

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

/**
 * infc_browser_request_certificate:
 * @browser: A #InfcBrowser.
 * @crt: A .X509 certificate request.
 * @extra_data: Extra data to be associated to the entity.
 * @error: Location to store error information, if any.
 *
 * Requests a new certificate at the server. The server is expected to sign
 * it with its certificate and send us the signed certificate back.
 * @extra_data is some extra data to be associated to the new certificate.
 * However, this is not stored within the certificate but centrally on the
 * server. This allows to change the @extra_data later without having to
 * issue a new certificate.
 *
 * The function may fail in case there is a problem serializing the
 * certificate request. In that case the function returns %NULL and @error
 * is set.
 *
 * Returns: A #InfcCertificateRequest that can be used to get notified when
 * the request finishes, or %NULL on error.
 */
InfcCertificateRequest*
infc_browser_request_certificate(InfcBrowser* browser,
                                 gnutls_x509_crq_t crq,
                                 const gchar* extra_data,
                                 GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  xmlNodePtr xml;
  xmlNodePtr extra;
  xmlNodePtr crqNode;

  gchar* crq_text;
  size_t size;
  int res;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  g_return_val_if_fail(crq != NULL, NULL);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);

  size = 0;
  res = gnutls_x509_crq_export(crq, GNUTLS_X509_FMT_PEM, NULL, &size);
  if(res != GNUTLS_E_SHORT_MEMORY_BUFFER)
  {
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  crq_text = g_malloc(size);
  res = gnutls_x509_crq_export(crq, GNUTLS_X509_FMT_PEM, crq_text, &size);
  if(res != GNUTLS_E_SUCCESS)
  {
    g_free(crq_text);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_CERTIFICATE_REQUEST,
    "request-certificate",
    NULL
  );

  inf_browser_begin_request(INF_BROWSER(browser), NULL, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);
  extra = xmlNewChild(xml, NULL, (const xmlChar*)"extra", NULL);
  xmlNodeAddContent(extra, (const xmlChar*)extra_data);
  crqNode = xmlNewChild(xml, NULL, (const xmlChar*)"crq", NULL);
  xmlNodeAddContentLen(crqNode, (const xmlChar*)crq_text, size);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INFC_CERTIFICATE_REQUEST(request);
}

/* vim:set et sw=2 ts=2: */
