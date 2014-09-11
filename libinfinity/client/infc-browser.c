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
#include <libinfinity/client/infc-progress-request.h>
#include <libinfinity/client/infc-request-manager.h>

#include <libinfinity/common/inf-request-result.h>
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

typedef struct _InfcBrowserMakeAclAccountListData
  InfcBrowserMakeAclAccountListData;
struct _InfcBrowserMakeAclAccountListData {
  InfAclAccount* accounts;
  guint index;
};

typedef struct _InfcBrowserLookupAclAccountByByNameData
  InfcBrowserLookupAclAccountByByNameData;
struct _InfcBrowserLookupAclAccountByByNameData {
  const gchar* name;
  GArray* accounts;
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
  InfcRequest* result;
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
      InfcRequest* request;
      InfCommunicationJoinedGroup* subscription_group;
    } chat;

    struct {
      InfcBrowserNode* node;
      InfcRequest* request;
      InfCommunicationJoinedGroup* subscription_group;
    } session;

    /* TODO: It would simplify some code if we merge the add_node
     * and sync_in blocks. */
    struct {
      InfcBrowserNode* parent;
      const InfcNotePlugin* plugin;
      gchar* name;
      InfAclSheetSet* sheet_set;
      InfcRequest* request;
      InfCommunicationJoinedGroup* subscription_group;
    } add_node;

    struct {
      InfcBrowserNode* parent;
      const InfcNotePlugin* plugin;
      gchar* name;
      InfAclSheetSet* sheet_set;
      InfcRequest* request;
      InfCommunicationJoinedGroup* synchronization_group;
      InfCommunicationJoinedGroup* subscription_group; /* can be NULL */
      InfSession* session;
    } sync_in;
  } shared;
};

typedef enum _InfcBrowserAccountListStatus {
  INFC_BROWSER_ACCOUNT_LIST_NOT_QUERIED,
  INFC_BROWSER_ACCOUNT_LIST_NO_NOTIFICATIONS,
  INFC_BROWSER_ACCOUNT_LIST_NOTIFICATIONS
} InfcBrowserAccountListStatus;

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
  InfcBrowserAccountListStatus account_list_status;

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

static GQuark infc_browser_session_proxy_quark;
static GQuark infc_browser_sync_in_session_quark;
static GQuark infc_browser_sync_in_plugin_quark;
static GQuark infc_browser_lookup_acl_accounts_ids_quark;
static GQuark infc_browser_lookup_acl_accounts_n_ids_quark;
static GQuark infc_browser_lookup_acl_accounts_name_quark;
static GQuark infc_browser_query_acl_account_list_accounts_quark;

static void infc_browser_communication_object_iface_init(InfCommunicationObjectInterface* iface);
static void infc_browser_browser_iface_init(InfBrowserInterface* iface);
G_DEFINE_TYPE_WITH_CODE(InfcBrowser, infc_browser, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfcBrowser)
  G_IMPLEMENT_INTERFACE(INF_COMMUNICATION_TYPE_OBJECT, infc_browser_communication_object_iface_init)
  G_IMPLEMENT_INTERFACE(INF_TYPE_BROWSER, infc_browser_browser_iface_init))

/*
 * Callbacks
 */

static void
infc_browser_make_acl_account_list_foreach_func(gpointer key,
                                                gpointer value,
                                                gpointer user_data)
{
  InfcBrowserMakeAclAccountListData* data;
  data = (InfcBrowserMakeAclAccountListData*)user_data;

  data->accounts[data->index++] = *(const InfAclAccount*)value;
}

static void
infc_browser_browser_lookup_acl_account_by_name_find_func(gpointer key,
                                                          gpointer value,
                                                          gpointer user_data)
{
  const InfcBrowserLookupAclAccountByByNameData* data;
  const InfAclAccount* acc;

  data = (InfcBrowserLookupAclAccountByByNameData*)user_data;
  acc = (const InfAclAccount*)value;

  if(acc->name != NULL)
   if(strcmp(acc->name, data->name) == 0)
     g_array_append_val(data->accounts, *acc);
}

InfAclAccount*
infc_browser_make_acl_account_list(InfcBrowser* browser,
                                   guint* n_accounts)
{
  InfcBrowserPrivate* priv;
  InfcBrowserMakeAclAccountListData data;
  guint n;

  priv = INFC_BROWSER_PRIVATE(browser);
  n = g_hash_table_size(priv->accounts);

  data.index = 0;
  data.accounts = g_malloc(sizeof(InfAclAccount) * n);

  g_hash_table_foreach(
    priv->accounts,
    infc_browser_make_acl_account_list_foreach_func,
    &data
  );

  g_assert(data.index == n);
  if(n_accounts != NULL) *n_accounts = n;
  return data.accounts;
}

static void
infc_browser_browser_list_pending_requests_foreach_func(InfcRequest* request,
                                                        gpointer user_data)
{
  InfcBrowserListPendingRequestsForeachData* data;
  guint node_id;

  data = (InfcBrowserListPendingRequestsForeachData*)user_data;
  g_object_get(G_OBJECT(request), "node-id", &node_id, NULL);

  if(node_id == G_MAXUINT)
  {
    if(data->iter == NULL)
      data->result = g_slist_prepend(data->result, request);
  }
  else
  {
    if(data->iter != NULL && node_id == data->iter->node_id)
      data->result = g_slist_prepend(data->result, request);
  }
}

static void
infc_browser_iter_get_sync_in_requests_foreach_func(InfcRequest* request,
                                                    gpointer user_data)
{
  InfcBrowserIterGetSyncInRequestsForeachData* data;
  InfSession* session;
  guint node_id;

  data = (InfcBrowserIterGetSyncInRequestsForeachData*)user_data;

  /* This is only a sync-in request if we assigned a session to sync with */
  session = g_object_get_qdata(
    G_OBJECT(request),
    infc_browser_sync_in_session_quark
  );

  if(session != NULL)
  {
    g_object_get(G_OBJECT(request), "node-id", &node_id, NULL);
    if(node_id != G_MAXUINT && node_id == data->iter->node_id)
      data->result = g_slist_prepend(data->result, request);
  }
}

static void
infc_browser_get_chat_request_foreach_func(InfcRequest* request,
                                           gpointer user_data)
{
  InfcBrowserIterGetChatRequestForeachData* data;

  data = (InfcBrowserIterGetChatRequestForeachData*)user_data;

  /* There can only be one such request: */
  g_assert(data->result == NULL);

  /* TODO: Stop foreach when we found the request. Requires changes in
   * InfcRequestManager. */
  data->result = request;
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
infc_browser_session_notify_subscription_group_cb(InfSession* session,
                                                  const GParamSpec* pspec,
                                                  gpointer user_data);

static void
infc_browser_session_remove_session(InfcBrowser* browser,
                                    InfcBrowserNode* node,
                                    InfcRequest* request)
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
    G_CALLBACK(infc_browser_session_notify_subscription_group_cb),
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
    INF_SESSION_PROXY(proxy),
    INF_REQUEST(request)
  );

  g_object_unref(session);
  g_object_unref(proxy);
}

static void
infc_browser_session_remove_child_sessions(InfcBrowser* browser,
                                           InfcBrowserNode* node,
                                           InfcRequest* request)
{
  InfcBrowserNode* child;

  switch(node->type)
  {
  case INFC_BROWSER_NODE_SUBDIRECTORY:
    if(node->shared.subdir.explored == TRUE)
    {
      for(child = node->shared.subdir.child;
          child != NULL;
          child = child->next)
      {
        infc_browser_session_remove_child_sessions(browser, child, request);
      }
    }

    break;
  case INFC_BROWSER_NODE_NOTE_KNOWN:
    if(node->shared.known.session != NULL)
      infc_browser_session_remove_session(browser, node, request);
    break;
  case INFC_BROWSER_NODE_NOTE_UNKNOWN:
    /* nothing to do */
    break;
  }
}

static void
infc_browser_node_register(InfcBrowser* browser,
                           InfcBrowserNode* node,
                           InfcRequest* request)
{
  InfBrowserIter iter;
  iter.node_id = node->id;
  iter.node = node;

  inf_browser_node_added(INF_BROWSER(browser), &iter, INF_REQUEST(request));
}

static void
infc_browser_node_unregister(InfcBrowser* browser,
                             InfcBrowserNode* node,
                             InfcRequest* request)
{
  InfBrowserIter iter;
  iter.node_id = node->id;
  iter.node = node;

  inf_browser_node_removed(INF_BROWSER(browser), &iter, INF_REQUEST(request));
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
    /* Is first unlinked with remove_child_sessions */
    g_assert(node->shared.known.session == NULL);
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

          g_object_unref(request->shared.session.request);
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

          g_object_unref(request->shared.add_node.request);
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

        g_object_unref(request->shared.sync_in.request);
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

static void
infc_browser_remove_acl_sheet_from_sheet_set(InfAclSheetSet* sheet_set,
                                             InfAclAccountId account)
{
  InfAclSheet* sheet;

  if(sheet_set != NULL)
  {
    sheet = inf_acl_sheet_set_find_sheet(sheet_set, account);
    if(sheet != NULL)
      inf_acl_sheet_set_remove_sheet(sheet_set, sheet);
  }
}

static void
infc_browser_remove_account_from_sheets(InfcBrowser* browser,
                                        InfcBrowserNode* node,
                                        InfAclAccountId account)
{
  InfcBrowserNode* child;
  InfAclSheet* sheet;
  InfAclSheet announce_sheet;
  InfAclSheetSet sheet_set;
  InfBrowserIter iter;

  if(node->type == INFC_BROWSER_NODE_SUBDIRECTORY &&
     node->shared.subdir.explored)
  {
    for(child = node->shared.subdir.child; child != NULL; child = child->next)
    {
      infc_browser_remove_account_from_sheets(browser, child, account);
    }
  }

  if(node->acl != NULL)
  {
    sheet = inf_acl_sheet_set_find_sheet(node->acl, account);
    if(sheet != NULL)
    {
      announce_sheet = *sheet;
      inf_acl_sheet_set_remove_sheet(node->acl, sheet);

      /* Clear the mask, to announce that all permissions
       * have been reset to default */
      inf_acl_mask_clear(&announce_sheet.mask);

      sheet_set.own_sheets = NULL;
      sheet_set.sheets = &announce_sheet;
      sheet_set.n_sheets = 1;

      iter.node = node;
      iter.node_id = node->id;

      inf_browser_acl_changed(INF_BROWSER(browser), &iter, &sheet_set, NULL);
    }
  }
}

/* Enforce the ACL for the current user. If the server changes ACLs or we are
 * switching accounts, then we need to update our internal state to the new
 * permissions. new_sheet contains a ACL sheet for the current user which we
 * add to the node's ACL. This is used when before enforcing the new ACL the
 * ACL for this node was not known. */
static void
infc_browser_enforce_single_acl(InfcBrowser* browser,
                                InfcBrowserNode* node,
                                InfcRequest* request,
                                const InfAclSheet* new_sheet)
{
  InfcBrowserPrivate* priv;
  InfBrowser* ibrowser;
  InfBrowserIter iter;
  InfAclAccountId account;
  InfAclMask mask;
  InfcRequest* req;
  GError* error;
  InfcBrowserNode* child;
  GSList* item;
  InfcBrowserSubreq* subreq;
  InfAclAccountId default_id;
  InfAclSheetSet* sheet_set;
  InfAclSheet* sheet;
  InfAclSheet* added_sheet;

  priv = INFC_BROWSER_PRIVATE(browser);
  ibrowser = INF_BROWSER(browser);
  iter.node = node;
  iter.node_id = node->id;
  account = priv->local_account->id;

  g_assert(new_sheet == NULL || new_sheet->account == account);

  if(node->type == INFC_BROWSER_NODE_SUBDIRECTORY)
  {
    if(node->shared.subdir.explored == TRUE)
    {
      inf_acl_mask_set1(&mask, INF_ACL_CAN_EXPLORE_NODE);
      if(!inf_browser_check_acl(ibrowser, &iter, account, &mask, NULL))
      {
        req = INFC_REQUEST(
          inf_browser_get_pending_request(
            ibrowser,
            &iter,
            "explore-node"
          )
        );

        if(req != NULL)
        {
          error = NULL;

          g_set_error(
            &error,
            inf_request_error_quark(),
            INF_REQUEST_ERROR_NOT_AUTHORIZED,
            "%s",
            _("Permissions to explore this node have been revoked")
          );

          infc_request_manager_fail_request(priv->request_manager, req, error);
          g_error_free(error);
        }

        while(node->shared.subdir.child != NULL)
        {
          child = node->shared.subdir.child;
          infc_browser_node_unregister(browser, child, request);
          infc_browser_node_free(browser, child);
        }

        /* Cancel any subscription request that wants to create a node in this
         * node. It is important to this here and not in
         * communication_object_sent(), so that there are no inconsistencies
         * when the server re-explores the node for us. */
        for(item = priv->subscription_requests; item != NULL; )
        {
          subreq = (InfcBrowserSubreq*)item->data;
          item = item->next;

          switch(subreq->type)
          {
          case INFC_BROWSER_SUBREQ_CHAT:
          case INFC_BROWSER_SUBREQ_SESSION:
            break;
          case INFC_BROWSER_SUBREQ_ADD_NODE:
            if(subreq->shared.add_node.parent == node)
            {
              subreq->shared.add_node.parent = NULL;
              if(subreq->shared.add_node.request != NULL)
              {
                error = NULL;

                g_set_error(
                  &error,
                  inf_request_error_quark(),
                  INF_REQUEST_ERROR_NOT_AUTHORIZED,
                  "%s",
                  _("Permissions to explore the parent node "
                    "have been revoked")
                );

                infc_request_manager_fail_request(
                  priv->request_manager,
                  subreq->shared.add_node.request,
                  error
                );

                g_error_free(error);

                g_object_unref(subreq->shared.add_node.request);
                subreq->shared.add_node.request = NULL;
              }
            }
            break;
          case INFC_BROWSER_SUBREQ_SYNC_IN:
            if(subreq->shared.sync_in.parent == node)
            {
              subreq->shared.sync_in.parent = NULL;
              if(subreq->shared.sync_in.request != NULL)
              {
                error = NULL;

                g_set_error(
                  &error,
                  inf_request_error_quark(),
                  INF_REQUEST_ERROR_NOT_AUTHORIZED,
                  "%s",
                  _("Permissions to explore the parent node "
                    "have been revoked")
                );

                infc_request_manager_fail_request(
                  priv->request_manager,
                  subreq->shared.sync_in.request,
                  error
                );

                g_error_free(error);

                g_object_unref(subreq->shared.sync_in.request);
                subreq->shared.sync_in.request = NULL;
              }
            }

            break;
          default:
            g_assert_not_reached();
            break;
          }
        }

        node->shared.subdir.explored = FALSE;
      }
    }
  }
  else
  {
    /* Don't handle subscriptions explicitely, they will be ended by the
     * server on session level (or, if the server allows them to live on,
     * even better). */
  }

  /* Check if query-acl was revoked, and if yes, reset the acl_queried flag
   * for the node. */
  if(node->acl_queried == TRUE)
  {
    inf_acl_mask_set1(&mask, INF_ACL_CAN_QUERY_ACL);
    if(inf_browser_check_acl(ibrowser, &iter, account, &mask, NULL) == FALSE)
      node->acl_queried = FALSE;
  }

  /* If query-acl was revoked, then update the sheet set by removing all
   * sheets other than the default one and the one for our account. */
  if(node->acl_queried == FALSE)
  {
    if(node->acl != NULL)
      sheet_set = inf_acl_sheet_set_get_clear_sheets(node->acl);
    else
      sheet_set = inf_acl_sheet_set_new();

    default_id = inf_acl_account_id_from_string("default");

    sheet = inf_acl_sheet_set_find_sheet(sheet_set, default_id);
    if(sheet != NULL) inf_acl_sheet_set_remove_sheet(sheet_set, sheet);

    if(priv->local_account->id != default_id)
    {
      sheet =
        inf_acl_sheet_set_find_sheet(sheet_set, priv->local_account->id);

      if(sheet != NULL)
      {
        inf_acl_sheet_set_remove_sheet(sheet_set, sheet);
        g_assert(new_sheet == NULL);
      }
      else if(new_sheet != NULL)
      {
        added_sheet =
          inf_acl_sheet_set_add_sheet(sheet_set, new_sheet->account);
        *added_sheet = *new_sheet;
      }
    }

    if(sheet_set != NULL && sheet_set->n_sheets > 0)
    {
      node->acl = inf_acl_sheet_set_merge_sheets(node->acl, sheet_set);

      /* Check subscription requests for this node, and adapt the sheet set,
       * so that the sheet set is correct when the node is
       * eventually created. */
      for(item = priv->subscription_requests; item != NULL; )
      {
        subreq = (InfcBrowserSubreq*)item->data;
        item = item->next;

        switch(subreq->type)
        {
        case INFC_BROWSER_SUBREQ_CHAT:
        case INFC_BROWSER_SUBREQ_SESSION:
          break;
        case INFC_BROWSER_SUBREQ_ADD_NODE:
          if(subreq->shared.add_node.parent == node)
          {
            subreq->shared.add_node.sheet_set =
              inf_acl_sheet_set_merge_sheets(
                subreq->shared.add_node.sheet_set,
                sheet_set
              );
          }

          break;
        case INFC_BROWSER_SUBREQ_SYNC_IN:
          if(subreq->shared.sync_in.parent == node)
          {
            subreq->shared.sync_in.sheet_set = inf_acl_sheet_set_merge_sheets(
              subreq->shared.sync_in.sheet_set,
              sheet_set
            );
          }

          break;
        default:
          g_assert_not_reached();
          break;
        }
      }

      inf_browser_acl_changed(
        INF_BROWSER(browser),
        &iter,
        sheet_set,
        INF_REQUEST(request)
      );
    }

    inf_acl_sheet_set_free(sheet_set);
  }
}

static void
infc_browser_enforce_acl(InfcBrowser* browser,
                         InfcBrowserNode* node,
                         InfcRequest* request,
                         GHashTable* new_acls)
{
  InfBrowser* ibrowser;
  InfcBrowserPrivate* priv;
  InfcBrowserNode* child;
  InfBrowserIter iter;
  InfAclMask mask;
  const InfAclSheet* sheet;
  InfAclAccountId account;

  ibrowser = INF_BROWSER(browser);
  priv = INFC_BROWSER_PRIVATE(browser);

  /* First, check whether the account_list_queried flag needs to be reset
   * because we lost the can-query-account-list permission. Technically we
   * still have the account list queried, but we do not receive notifications
   * anymore from the server if this permission has been revoked. */
  if(node == priv->root &&
     priv->account_list_status == INFC_BROWSER_ACCOUNT_LIST_NOTIFICATIONS)
  {
    iter.node = node;
    iter.node_id = node->id;
    inf_acl_mask_set1(&mask, INF_ACL_CAN_QUERY_ACCOUNT_LIST);
    account = priv->local_account->id;

    if(inf_browser_check_acl(ibrowser, &iter, account, &mask, NULL) == FALSE)
    {
      priv->account_list_status = INFC_BROWSER_ACCOUNT_LIST_NO_NOTIFICATIONS;
    }
  }

  /* Enforce ACL for this node */
  sheet = NULL;
  if(new_acls != NULL)
    sheet = g_hash_table_lookup(new_acls, GUINT_TO_POINTER(node->id));
  infc_browser_enforce_single_acl(browser, node, request, sheet);

  /* Enforce it for all children. Note that if the explore-node permission was
   * revoked, then the above call has already removed all child nodes. */
  if(node->type == INFC_BROWSER_NODE_SUBDIRECTORY)
  {
    for(child = node->shared.subdir.child; child != NULL; child = child->next)
    {
      infc_browser_enforce_acl(browser, child, request, new_acls);
    }
  }

  /* Note that even if are no longer allowed to query the account list, we
   * can still keep around our cache of accounts, it just will not be updated
   * anymore from now on. */
}

/*
 * Signal handlers
 */

static void
infc_browser_session_notify_subscription_group_cb(InfSession* session,
                                                  const GParamSpec* spec,
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

  /* When the session loses its subscription group, we detach it from the
   * browser. It means that we are not subscribed to the session anymore. The
   * session is now in a "floating" state. It can still be used, for example
   * to sync-in the session into another node, but it is not connected
   * anymore to any other host. */
  if(inf_session_get_subscription_group(session) == NULL)
  {
    iter = (InfBrowserIter*)g_object_get_qdata(
      G_OBJECT(session),
      infc_browser_session_proxy_quark
    );

    if(iter != NULL)
    {
      g_assert(
        g_hash_table_lookup(
          INFC_BROWSER_PRIVATE(browser)->nodes,
          GUINT_TO_POINTER(iter->node_id)
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

      infc_browser_session_remove_session(browser, node, NULL);
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
        G_CALLBACK(infc_browser_session_notify_subscription_group_cb),
        browser
      );

      g_object_unref(proxy_session);

      proxy = priv->chat_session;
      priv->chat_session = NULL;
      g_object_notify(G_OBJECT(browser), "chat-session");

      inf_browser_unsubscribe_session(
        INF_BROWSER(browser),
        NULL,
        INF_SESSION_PROXY(proxy),
        NULL
      );

      g_object_unref(proxy);
    }
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
    infc_browser_session_remove_child_sessions(browser, priv->root, NULL);
    infc_browser_node_unregister(browser, priv->root, NULL);
    infc_browser_node_free(browser, priv->root);
    priv->root = NULL;
  }

  priv->account_list_status = INFC_BROWSER_ACCOUNT_LIST_NOT_QUERIED;
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
      G_CALLBACK(infc_browser_session_notify_subscription_group_cb),
      browser
    );

    g_object_unref(session);

    g_object_unref(priv->chat_session);
    priv->chat_session = NULL;
    
    g_object_notify(G_OBJECT(browser), "chat-session");
  }

  if(priv->welcome_timeout != NULL)
  {
    inf_io_remove_timeout(priv->io, priv->welcome_timeout);
    priv->welcome_timeout = NULL;
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
infc_browser_init(InfcBrowser* browser)
{
  InfcBrowserPrivate* priv;
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
  priv->account_list_status = INFC_BROWSER_ACCOUNT_LIST_NOT_QUERIED;

  priv->sync_ins = NULL;
  priv->subscription_requests = NULL;
  priv->chat_session = NULL;
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

  G_OBJECT_CLASS(infc_browser_parent_class)->dispose(object);
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

  G_OBJECT_CLASS(infc_browser_parent_class)->finalize(object);
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
                             InfcRequest* request,
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
                                InfcRequest* request,
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
                                 InfcRequest* request,
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
                                InfcRequest* request,
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
  InfcBrowser* browser;
  InfcBrowserNode* node;

  sync_in = (InfcBrowserSyncIn*)user_data;

  /* Ignore if this affects the synchronization to another connection */
  if(connection != sync_in->connection) return;

  browser = sync_in->browser;
  node = sync_in->node;

  g_object_ref(browser);

  /* The request for the sync-in was already considered successful, now that
   * the synchronization failed we remove the node again without request */
  infc_browser_remove_sync_in(browser, sync_in);
  infc_browser_node_unregister(browser, node, NULL);
  infc_browser_node_free(browser, node);

  g_object_unref(browser);
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
                                   InfcRequest* request,
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

  infc_browser_node_register(browser, node, request);

  return node;
}

static InfcBrowserNode*
infc_browser_node_add_note(InfcBrowser* browser,
                           InfcBrowserNode* parent,
                           InfcRequest* request,
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

  infc_browser_node_register(browser, node, request);
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

static gboolean
infc_browser_validate_progress_request(InfcBrowser* browser,
                                       InfcProgressRequest* request,
                                       GError** error)
{
  guint current;
  guint total;

  if(infc_progress_request_get_initiated(request) == FALSE)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NOT_INITIATED,
      "%s",
      inf_directory_strerror(INF_DIRECTORY_ERROR_NOT_INITIATED)
    );

    return FALSE;
  }
  else
  {
    g_object_get(
      G_OBJECT(request),
      "current", &current,
      "total", &total,
      NULL
    );

    if(current >= total)
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_TOO_MANY_CHILDREN,
        "%s",
        inf_directory_strerror(INF_DIRECTORY_ERROR_TOO_MANY_CHILDREN)
      );

      return FALSE;
    }
    else
    {
      return TRUE;
    }
  }
}

static InfcRequest*
infc_browser_get_add_node_request_from_xml(InfcBrowser* browser,
                                           xmlNodePtr xml,
                                           GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  gchar* type;
  gboolean result;

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
          "type '%s', but a request of either 'explore-node' or 'add-node' "
          "was expected."),
        type
      );

      g_free(type);
      return NULL;
    }

    /* For explore request, we do some basic sanity checking here */
    if(strcmp(type, "explore-node") == 0)
    {
      g_assert(INFC_IS_PROGRESS_REQUEST(request));

      result = infc_browser_validate_progress_request(
        browser,
        INFC_PROGRESS_REQUEST(request),
        error
      );

      if(result == FALSE)
      {
        g_free(type);
        return NULL;
      }
    }

    g_free(type);
  }

  return request;
}

static void
infc_browser_process_add_node_request(InfcBrowser* browser,
                                      InfcRequest* request,
                                      InfcBrowserNode* node)
{
  InfcBrowserPrivate* priv;
  InfBrowserIter parent_iter;
  InfBrowserIter iter;

  priv = INFC_BROWSER_PRIVATE(browser);

  if(INFC_IS_PROGRESS_REQUEST(request))
  {
    infc_progress_request_progress(INFC_PROGRESS_REQUEST(request));
  }
  else
  {
    g_object_get(G_OBJECT(request), "node-id", &parent_iter.node_id, NULL);
    g_assert(parent_iter.node_id != G_MAXUINT);

    parent_iter.node = g_hash_table_lookup(
      priv->nodes,
      GUINT_TO_POINTER(parent_iter.node_id)
    );

    g_assert(parent_iter.node != NULL);

    iter.node_id = node->id;
    iter.node = node;
    g_assert(node->parent == parent_iter.node);

    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_add_node(
        INF_BROWSER(browser),
        &parent_iter,
        &iter
      )
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
static void
infc_browser_subscribe_session(InfcBrowser* browser,
                               InfcBrowserNode* node,
                               InfcRequest* request,
                               InfCommunicationJoinedGroup* group,
                               InfXmlConnection* connection,
                               gboolean initial_sync)
{
  InfcBrowserPrivate* priv;
  InfcSessionProxy* proxy;
  InfBrowserIter iter;
  GString* path;
  InfSession* session;

  priv = INFC_BROWSER_PRIVATE(browser);

  g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);
  g_assert(node->shared.known.plugin != NULL);
  g_assert(node->shared.known.session == NULL);

  path = g_string_sized_new(128);
  infc_browser_node_get_path_string(node, path);

  if(initial_sync)
  {
    session = node->shared.known.plugin->session_new(
      priv->io,
      priv->communication_manager,
      INF_SESSION_SYNCHRONIZING,
      INF_COMMUNICATION_GROUP(group),
      connection,
      path->str,
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
      path->str,
      node->shared.known.plugin->user_data
    );
  }

  g_string_free(path, TRUE);

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
    INF_SESSION_PROXY(proxy),
    INF_REQUEST(request)
  );

  /* The default handler refs the proxy */
  g_object_unref(proxy);
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
  InfAclAccountId default_id;
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
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)inf_acl_account_free
  );

  default_id = inf_acl_account_id_from_string("default");
  default_account = inf_acl_account_new(default_id, NULL);

  g_hash_table_insert(
    priv->accounts, 
    INF_ACL_ACCOUNT_ID_TO_POINTER(default_account->id),
    default_account
  );

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
        INF_ACL_ACCOUNT_ID_TO_POINTER(priv->local_account->id),
        (gpointer)priv->local_account
      );

      break;
    }
  }

  /* Load the root ACL sheet set */
  local_error = NULL;

  sheet_set = inf_acl_sheet_set_from_xml(xml, &local_error);

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

  sheet = inf_acl_sheet_set_add_sheet(sheet_set, default_id);
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

  inf_browser_acl_account_added(INF_BROWSER(browser), default_account, NULL);
  if(priv->local_account != default_account)
  {
    inf_browser_acl_account_added(
      INF_BROWSER(browser),
      priv->local_account,
      NULL
    );
  }

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
  g_assert(INFC_IS_PROGRESS_REQUEST(request));

  if(!inf_xml_util_get_attribute_uint_required(xml, "total", &total, error))
    return FALSE;

  g_object_get(G_OBJECT(request), "node-id", &node_id, NULL);
  g_assert(node_id != G_MAXUINT);

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
    infc_progress_request_initiated(INFC_PROGRESS_REQUEST(request), total);
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
  g_assert(INFC_IS_PROGRESS_REQUEST(request));

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

    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_explore_node(INF_BROWSER(browser), &iter)
    );

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

  sheet_set = inf_acl_sheet_set_from_xml(xml, &local_error);

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
      request,
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
      if(request != NULL && INFC_IS_PROGRESS_REQUEST(request))
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
              request,
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
        request,
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

        sheet_set = inf_acl_sheet_set_from_xml(xml, &local_error);

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
              request,
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
    g_object_ref(request);

    iter.node_id = node->id;
    iter.node = node;

    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_remove_node(INF_BROWSER(browser), &iter)
    );
  }

  infc_browser_session_remove_child_sessions(browser, node, request);
  infc_browser_node_unregister(browser, node, request);
  infc_browser_node_free(browser, node);

  if(request != NULL)
    g_object_unref(request);

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

  subreq = infc_browser_add_subreq_session(browser, node, request, group);
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

  subreq = infc_browser_add_subreq_chat(browser, request, group);
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
    iter.node_id = node->id;
    iter.node = node;

    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_save_session(INF_BROWSER(browser), &iter)
    );
  }

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
  guint notifications_enabled;
  gboolean result;

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
  g_assert(INFC_IS_PROGRESS_REQUEST(request));

  if(!inf_xml_util_get_attribute_uint_required(xml, "total", &total, error))
    return FALSE;

  result = inf_xml_util_get_attribute_uint_required(
    xml,
    "notifications-enabled",
    &notifications_enabled,
    error
  );

  if(result == FALSE)
    return FALSE;

  g_object_set_qdata_full(
    G_OBJECT(request),
    infc_browser_query_acl_account_list_accounts_quark,
    g_hash_table_new(NULL, NULL),
    (GDestroyNotify)g_hash_table_destroy
  );

  if(notifications_enabled)
    priv->account_list_status = INFC_BROWSER_ACCOUNT_LIST_NOTIFICATIONS;
  else
    priv->account_list_status = INFC_BROWSER_ACCOUNT_LIST_NO_NOTIFICATIONS;
  infc_progress_request_initiated(INFC_PROGRESS_REQUEST(request), total);
  return TRUE;
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

  InfAclAccountId default_id;
  GHashTable* table;
  GHashTableIter hash_iter;
  gpointer value;
  InfAclAccount* account;
  GSList* to_be_removed;
  GSList* item;

  InfAclAccount* accounts;
  guint n_accounts;
  gboolean does_notifications;

  priv = INFC_BROWSER_PRIVATE(browser);

  /* TODO: Allow user list query without us asking. */
  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    "query-acl-account-list",
    xml,
    error
  );

  if(request == NULL) return FALSE;
  g_assert(INFC_IS_PROGRESS_REQUEST(request));

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
    /* Remove all accounts that we have in our account list but has not been
     * listed by the server now. These seem to have been removed since the
     * last time we queried the account list. */
    table = g_object_get_qdata(
      G_OBJECT(request),
      infc_browser_query_acl_account_list_accounts_quark
    );

    to_be_removed = NULL;
    default_id = inf_acl_account_id_from_string("default");
    g_hash_table_iter_init(&hash_iter, priv->accounts);
    while(g_hash_table_iter_next(&hash_iter, NULL, &value))
    {
      /* Server does not explicitly send default and local account */
      account = (InfAclAccount*)value;
      if(account != priv->local_account &&
         account->id != default_id &&
         g_hash_table_lookup(table, account) == NULL)
      {
        /* We cannot remove the item directly, since we are currently
         * iterating over the hash table. */
        to_be_removed = g_slist_prepend(to_be_removed, account);
      }
    }

    for(item = to_be_removed; item != NULL; item = item->next)
    {
      account = (InfAclAccount*)item->data;

      g_hash_table_steal(
        priv->accounts,
        INF_ACL_ACCOUNT_ID_TO_POINTER(account->id)
      );

      inf_browser_acl_account_removed(INF_BROWSER(browser), account, NULL);
      inf_acl_account_free(account);
    }

    /* Now with the updated account list, we can report back to the caller */
    accounts = infc_browser_make_acl_account_list(browser, &n_accounts);

    switch(priv->account_list_status)
    {
    case INFC_BROWSER_ACCOUNT_LIST_NOT_QUERIED:
      g_assert_not_reached();
      break;
    case INFC_BROWSER_ACCOUNT_LIST_NO_NOTIFICATIONS:
      does_notifications = FALSE;
      break;
    case INFC_BROWSER_ACCOUNT_LIST_NOTIFICATIONS:
      does_notifications = TRUE;
      break;
    default:
      g_assert_not_reached();
      break;
    }

    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_query_acl_account_list(
        INF_BROWSER(browser),
        accounts,
        n_accounts,
        does_notifications
      )
    );

    g_free(accounts);
    return TRUE;
  }
}

static void
infc_browser_handle_add_acl_account_foreach_func(InfcRequest* request,
                                                 gpointer user_data)
{
  GHashTable* table;

  table = g_object_get_qdata(
    G_OBJECT(request),
    infc_browser_query_acl_account_list_accounts_quark
  );

  /* Only insert a pointer to the account object
   * in the permanent hash table. */
  g_hash_table_insert(table, user_data, user_data);
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
  gboolean result;

  InfAclAccount* account;
  InfAclAccount* cache_account;
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

  if(request != NULL)
  {
    g_assert(INFC_IS_PROGRESS_REQUEST(request));

    result = infc_browser_validate_progress_request(
      browser,
      INFC_PROGRESS_REQUEST(request),
      error
    );

    if(result == FALSE)
      return FALSE;
  }

  account = inf_acl_account_from_xml(xml, error);
  if(account == NULL) return FALSE;

  cache_account = g_hash_table_lookup(
    priv->accounts,
    INF_ACL_ACCOUNT_ID_TO_POINTER(account->id)
  );

  if(cache_account != NULL)
  {
    /* Update account name, if it has changed */
    if(strcmp(cache_account->name, account->name) != 0)
    {
      g_free(cache_account->name);
      cache_account->name = g_strdup(account->name);
    }

    inf_acl_account_free(account);
  }
  else
  {
    g_hash_table_insert(
      priv->accounts,
      INF_ACL_ACCOUNT_ID_TO_POINTER(account->id),
      account
    );

    inf_browser_acl_account_added(
      INF_BROWSER(browser),
      account,
      INF_REQUEST(request)
    );

    cache_account = account;
  }

  if(request != NULL)
    infc_progress_request_progress(INFC_PROGRESS_REQUEST(request));

  infc_request_manager_foreach_named_request(
    priv->request_manager,
    "query-acl-account-list",
    infc_browser_handle_add_acl_account_foreach_func,
    cache_account
  );

  return TRUE;
}

static gboolean
infc_browser_handle_lookup_acl_accounts(InfcBrowser* browser,
                                        InfXmlConnection* connection,
                                        xmlNodePtr xml,
                                        GError** error)
{
  InfcBrowserPrivate* priv;
  GError* local_error;
  InfcRequest* request;
  xmlNodePtr child;

  GPtrArray* accounts;
  InfAclAccount* account;
  InfAclAccount* existing_account;
  InfAclAccount req_account;
  InfcBrowserLookupAclAccountByByNameData lookup_data;
  GArray* req_accounts;
  guint i;

  guint len;
  gchar* name;
  const InfAclAccountId* ids;
  guint n_ids;

  priv = INFC_BROWSER_PRIVATE(browser);

  local_error = NULL;
  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "lookup-acl-accounts",
    xml,
    error
  );

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  /* Read accounts from XML structure */
  accounts = g_ptr_array_new();
  for(child = xml->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;
    if(strcmp((const gchar*)child->name, "account") == 0)
    {
      account = inf_acl_account_from_xml(child, error);
      if(account == NULL)
      {
        for(i = 0; i < accounts->len; ++i)
          inf_acl_account_free(accounts->pdata[i]);
        g_ptr_array_free(accounts, TRUE);
        return FALSE;
      }

      g_ptr_array_add(accounts, account);
    }
  }

  /* Update local account table */
  for(i = 0; i < accounts->len; ++i)
  {
    account = (InfAclAccount*)accounts->pdata[i];

    existing_account = g_hash_table_lookup(
      priv->accounts,
      INF_ACL_ACCOUNT_ID_TO_POINTER(account->id)
    );

    if(existing_account != NULL)
    {
      /* Update account name, if it has changed */
      if(strcmp(existing_account->name, account->name) != 0)
      {
        g_free(existing_account->name);
        existing_account->name = g_strdup(account->name);
      }

      inf_acl_account_free(account);
    }
    else
    {
      /* Should not happen, as we should have served this from our cache. If
       * this happens, the server lied to us. */
      if(priv->account_list_status == INFC_BROWSER_ACCOUNT_LIST_NOTIFICATIONS)
      {
        g_warning(
          _("Unknown account ID \"%s\" in server reply of "
            "\"%s\". Typically, this means the server claimed it notified us "
            "about new connections as soon as they are available, but it "
            "did not do so."),
          inf_acl_account_id_to_string(account->id),
          "lookup-acl-accounts"
        );
      }

      g_hash_table_insert(
        priv->accounts,
        INF_ACL_ACCOUNT_ID_TO_POINTER(account->id),
        account
      );

      inf_browser_acl_account_added(
        INF_BROWSER(browser),
        account,
        INF_REQUEST(request)
      );
    }
  }

  g_ptr_array_free(accounts, TRUE);

  /* Lookup all requested IDs and names from local cache */
  if(request != NULL)
  {
    ids = g_object_get_qdata(
      G_OBJECT(request), 
      infc_browser_lookup_acl_accounts_ids_quark
    );

    n_ids = GPOINTER_TO_UINT(
      g_object_get_qdata(
        G_OBJECT(request),
        infc_browser_lookup_acl_accounts_n_ids_quark
      )
    );

    name = g_object_get_qdata(
      G_OBJECT(request),
      infc_browser_lookup_acl_accounts_name_quark
    );

    req_accounts = g_array_new(FALSE, FALSE, sizeof(InfAclAccount));

    if(ids != NULL)
    {
      g_assert(n_ids > 0);
      for(i = 0; i < n_ids; ++i)
      {
        account = g_hash_table_lookup(
          priv->accounts,
          INF_ACL_ACCOUNT_ID_TO_POINTER(ids[i])
        );

        if(account != NULL)
        {
          g_array_append_val(req_accounts, *account);
        }
        else
        {
          /* The server has not sent us the name for this account. We suppose
           * this means that this account does not exist. */
          req_account.id = ids[i];
          req_account.name = NULL;
          g_array_append_val(req_accounts, req_account);
        }
      }
    }

    if(name != NULL)
    {
      len = req_accounts->len;

      lookup_data.name = name;
      lookup_data.accounts = req_accounts;

      g_hash_table_foreach(
        priv->accounts,
        infc_browser_browser_lookup_acl_account_by_name_find_func,
        &lookup_data
      );

      if(req_accounts->len == len)
      {
        /* No account with this name available; create a new result entry
         * with 0 ID to indicate this to the caller. */
        req_account.id = 0;
        req_account.name = name;
        g_array_append_val(req_accounts, req_account);
      }
    }

    /* TODO: Here, remove duplicates, or make sure from the start that there
     * will be no duplicates. */

    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_lookup_acl_accounts(
        INF_BROWSER(browser),
        (InfAclAccount*)req_accounts->data,
        req_accounts->len
      )
    );

    g_array_free(req_accounts, TRUE);
  }

  return TRUE;
}

static gboolean
infc_browser_handle_change_acl_account(InfcBrowser* browser,
                                       InfXmlConnection* connection,
                                       xmlNodePtr xml,
                                       GError** error)
{
  InfcBrowserPrivate* priv;
  InfAclAccount* account;
  InfAclAccount* existing_account;
  GHashTable* new_acls;
  xmlNodePtr child;
  gboolean res;
  InfAclSheet* sheet;
  guint node_id;

  priv = INFC_BROWSER_PRIVATE(browser);

  account = inf_acl_account_from_xml(xml, error);
  if(account == NULL) return FALSE;

  existing_account = g_hash_table_lookup(
    priv->accounts,
    INF_ACL_ACCOUNT_ID_TO_POINTER(account->id)
  );

  if(existing_account != NULL)
  {
    if(strcmp(account->name, existing_account->name) != 0)
    {
      /* Update account name, if it has changed */
      g_free(existing_account->name);
      existing_account->name = g_strdup(account->name);
    }

    inf_acl_account_free(account);
    account = existing_account;
  }

  new_acls = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)inf_acl_sheet_free
  );

  for(child = xml->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;
    if(strcmp((const gchar*)child->name, "acl") == 0)
    {
      res = inf_xml_util_get_attribute_uint_required(
        child,
        "node-id",
        &node_id,
        error
      );

      if(res == FALSE)
      {
        if(account != existing_account)
          inf_acl_account_free(account);
        g_hash_table_destroy(new_acls);
        return FALSE;
      }

      sheet = inf_acl_sheet_new(account->id);

      res = inf_acl_sheet_perms_from_xml(
        child,
        &sheet->mask,
        &sheet->perms,
        error
      );

      if(res == FALSE)
      {
        inf_acl_sheet_free(sheet);
        if(account != existing_account)
          inf_acl_account_free(account);
        g_hash_table_destroy(new_acls);
        return FALSE;
      }

      g_hash_table_insert(new_acls, GUINT_TO_POINTER(node_id), sheet);
    }
  }

  if(account != existing_account)
  {
    g_hash_table_insert(
      priv->accounts,
      INF_ACL_ACCOUNT_ID_TO_POINTER(account->id),
      account
    );

    inf_browser_acl_account_added(
      INF_BROWSER(browser),
      account,
      NULL
    );
  }

  priv->local_account = account;
  infc_browser_enforce_acl(browser, priv->root, NULL, new_acls);
  g_hash_table_destroy(new_acls);

  inf_browser_acl_local_account_changed(INF_BROWSER(browser), account, NULL);
  return TRUE;
}

static gboolean
infc_browser_handle_create_acl_account(InfcBrowser* browser,
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

  gnutls_x509_crt_t* certs;
  guint n_certs;
  GError* local_error;
  int verify_result;
  InfCertificateChain* new_chain;
  InfAclAccount* account;
  InfAclAccount* existing_account;

  priv = INFC_BROWSER_PRIVATE(browser);

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "create-acl-account",
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

    return FALSE;
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

  n_certs = 10;
  certs = g_malloc(n_certs * sizeof(gnutls_x509_crt_t));

  res = gnutls_x509_crt_list_import(
    certs,
    &n_certs,
    &cert_text,
    GNUTLS_X509_FMT_PEM,
    GNUTLS_X509_CRT_LIST_FAIL_IF_UNSORTED |
    GNUTLS_X509_CRT_LIST_IMPORT_FAIL_IF_EXCEED
  );

  if(res < 0)
  {
    g_free(certs);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }
  else if(res == 0)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
      "%s",
      _("No certificate provided")
    );

    g_free(certs);
    return FALSE;
  }

  certs = g_realloc(certs, n_certs * sizeof(gnutls_x509_crt_t));
  new_chain = inf_certificate_chain_new(certs, n_certs);

#if 0
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

  root_cert = inf_certificate_chain_get_root_certificate(chain);
#endif

  /* Verify that it is signed correctly. Note that we do not check who signed
   * the certificate. This is debatable, but it makes things a bit easier for
   * us. This allows the server to directly use its certificate to sign our
   * client certificate, and it does not need an extra (self-signed) CA
   * certificate for that. In the latter case it would be harder for us to
   * verify that we trust that CA. The certificate we obtained this way can
   * only be used for identifying ourselves with the server, since for other
   * purposes the server would make sure that our certificate was signed by
   * an actual CA. And we have to trust the server anyway, or otherwise it
   * could compromise all our data. */
  res = gnutls_x509_crt_list_verify(
    certs,
    n_certs,
    &certs[n_certs - 1],
    1,
    NULL,
    0,
    GNUTLS_VERIFY_DO_NOT_ALLOW_X509_V1_CA_CRT | GNUTLS_VERIFY_DISABLE_CA_SIGN,
    &verify_result
  );

  if(res != GNUTLS_E_SUCCESS || (verify_result & GNUTLS_CERT_INVALID) != 0)
  {
    if(res != GNUTLS_E_SUCCESS)
    {
      inf_gnutls_set_error(error, res);
    }
    else
    {
      local_error = NULL;

      inf_gnutls_certificate_verification_set_error(
        &local_error,
        verify_result
      );

      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_INVALID_CERTIFICATE,
        _("Server sent an invalid certificate (%s)"),
        local_error->message
      );

      g_error_free(local_error);
    }

    inf_certificate_chain_unref(new_chain);
    return FALSE;
  }

  account = inf_acl_account_from_xml(xml, error);
  if(account == NULL)
  {
    inf_certificate_chain_unref(new_chain);
    return FALSE;
  }

  /* Note that it is allowed that the account already exists, in which
   * case we have created a new certificate for that account. */
  existing_account = g_hash_table_lookup(
    priv->accounts,
    INF_ACL_ACCOUNT_ID_TO_POINTER(account->id)
  );

  if(existing_account != NULL)
  {
    inf_acl_account_free(account);
    account = existing_account;
  }
  else
  {
    g_hash_table_insert(
      priv->accounts,
      INF_ACL_ACCOUNT_ID_TO_POINTER(account->id),
      account
    );

    inf_browser_acl_account_added(
      INF_BROWSER(browser),
      account,
      INF_REQUEST(request)
    );
  }

  infc_request_manager_finish_request(
    priv->request_manager,
    request,
    inf_request_result_make_create_acl_account(
      INF_BROWSER(browser),
      account,
      new_chain
    )
  );

  inf_certificate_chain_unref(new_chain);
  return TRUE;
}

static gboolean
infc_browser_handle_remove_acl_account(InfcBrowser* browser,
                                       InfXmlConnection* connection,
                                       xmlNodePtr xml,
                                       GError** error)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  xmlChar* account_id;
  InfAclAccountId account;
  InfAclAccount* acc;
  InfAclAccount report_acc;
  InfAclAccountId default_id;
  GSList* item;
  InfcBrowserSubreq* subreq;

  priv = INFC_BROWSER_PRIVATE(browser);

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "remove-acl-account",
    xml,
    NULL
  );

  account_id = inf_xml_util_get_attribute_required(xml, "id", error);
  if(account_id == NULL) return FALSE;

  account = inf_acl_account_id_from_string((const gchar*)account_id);
  if(account == 0)
  {
    /* The server has notified us about the removal of an account that we
     * never heard about. This can happen if the server has notifications
     * enabled but we have never queried the account list. */
    xmlFree(account_id);
    return TRUE;
  }

  xmlFree(account_id);

  default_id = inf_acl_account_id_from_string("default");

  if(account == default_id)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NO_SUCH_ACCOUNT,
      "%s",
      _("The default account cannot be removed")
    );

    return FALSE;
  }

  if(priv->local_account->id == account)
  {
    /* Own account was removed: Demote to default account */
    priv->local_account = g_hash_table_lookup(
      priv->accounts,
      INF_ACL_ACCOUNT_ID_TO_POINTER(default_id)
    );

    g_assert(priv->local_account != NULL);
    infc_browser_enforce_acl(browser, priv->root, NULL, NULL);

    inf_browser_acl_local_account_changed(
      INF_BROWSER(browser),
      priv->local_account,
      NULL
    );
  }

  /* Remove sheet from all nodes and subreqs */
  infc_browser_remove_account_from_sheets(browser, priv->root, account);

  for(item = priv->subscription_requests; item != NULL; item = item->next)
  {
    subreq = (InfcBrowserSubreq*)item->data;

    switch(subreq->type)
    {
    case INFC_BROWSER_SUBREQ_CHAT:
    case INFC_BROWSER_SUBREQ_SESSION:
      break;
    case INFC_BROWSER_SUBREQ_ADD_NODE:
      infc_browser_remove_acl_sheet_from_sheet_set(
        subreq->shared.add_node.sheet_set,
        account
      );

      break;
    case INFC_BROWSER_SUBREQ_SYNC_IN:
      infc_browser_remove_acl_sheet_from_sheet_set(
        subreq->shared.sync_in.sheet_set,
        account
      );

      break;
    }
  }

  /* remove account */
  acc = g_hash_table_lookup(
    priv->accounts,
    INF_ACL_ACCOUNT_ID_TO_POINTER(account)
  );

  if(acc != NULL)
  {
    g_hash_table_steal(
      priv->accounts,
      INF_ACL_ACCOUNT_ID_TO_POINTER(account)
    );
  }
  else
  {
    report_acc.id = account;
    report_acc.name = NULL; /* unknown */
    acc = &report_acc;
  }

  if(request != NULL)
  {
    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_remove_acl_account(
        INF_BROWSER(browser),
        acc
      )
    );
  }

  inf_browser_acl_account_removed(INF_BROWSER(browser), acc, NULL);
  if(acc != &report_acc)
    inf_acl_account_free(acc);

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

  InfAclAccountId default_id;
  InfAclSheet* default_sheet;
  InfAclMask default_mask;

  priv = INFC_BROWSER_PRIVATE(browser);

  node = infc_browser_get_node_from_xml(browser, xml, "id", error);
  if(node == NULL) return FALSE;

  local_error = NULL;

  sheet_set = inf_acl_sheet_set_from_xml(xml, &local_error);
  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  /* request can either by query-acl or set-acl */
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

  /* Remember that we have queried this ACL */
  if(request != NULL)
  {
    g_object_get(G_OBJECT(request), "type", &request_type, NULL);
    if(strcmp(request_type, "query-acl") == 0)
    {
      g_assert(node->acl_queried == FALSE);
      node->acl_queried = TRUE;
    }
    else if(strcmp(request_type, "set-acl") != 0)
    {
      g_set_error(
        error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_INVALID_SEQ,
        _("The request contains a sequence number refering to a request of "
          "type '%s', but a request of either 'query-acl' or 'set-acl' "
          "was expected."),
        request_type
      );

      g_free(request_type);
      return FALSE;
    }
  }

  iter.node_id = node->id;
  iter.node = node;

  /* sheet_set can be NULL, for example when querying the ACL for a node and
   * the ACL is empty. */
  if(sheet_set != NULL)
  {
    if(sheet_set->n_sheets > 0)
    {
      /* If the root node permissions are changed, make sure that none are
       * masked out and reset them to the default values instead. This can
       * happen if we are running against an older server which does not
       * support all permissions that we do support. */
      if(node == priv->root)
      {
        default_id = inf_acl_account_id_from_string("default");

        default_sheet =
          inf_acl_sheet_set_find_sheet(sheet_set, default_id);
        if(default_sheet != NULL)
        {
          default_mask = default_sheet->mask;

          /* Make sure that all masked-out bits
           * in permissions are set to zero */
          inf_acl_mask_and(
            &default_sheet->perms,
            &default_sheet->mask,
            &default_sheet->perms
          );

          /* Next, set the masked out bits to their default values */
          inf_acl_mask_neg(&default_mask, &default_mask);

          inf_acl_mask_and(
            &default_mask,
            &INF_ACL_MASK_DEFAULT,
            &default_mask
          );

          inf_acl_mask_or(
            &default_sheet->perms,
            &default_mask,
            &default_sheet->perms
          );

          default_sheet->mask = INF_ACL_MASK_ALL;
        }
      }

      node->acl = inf_acl_sheet_set_merge_sheets(node->acl, sheet_set);
      infc_browser_enforce_acl(browser, node, request, NULL);

      inf_browser_acl_changed(
        INF_BROWSER(browser),
        &iter,
        sheet_set,
        INF_REQUEST(request)
      );
    }

    inf_acl_sheet_set_free(sheet_set);
  }

  if(request != NULL)
  {
    g_object_get(G_OBJECT(request), "type", &request_type, NULL);
    if(strcmp(request_type, "query-acl") == 0)
    {
      infc_request_manager_finish_request(
        priv->request_manager,
        request,
        inf_request_result_make_query_acl(
          INF_BROWSER(browser),
          &iter,
          node->acl
        )
      );
    }
    else
    {
      infc_request_manager_finish_request(
        priv->request_manager,
        request,
        inf_request_result_make_set_acl(INF_BROWSER(browser), &iter)
      );
    }

    g_free(request_type);
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
  else if(strcmp((const gchar*)node->name, "lookup-acl-accounts") == 0)
  {
    infc_browser_handle_lookup_acl_accounts(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "change-acl-account") == 0)
  {
    infc_browser_handle_change_acl_account(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "create-acl-account") == 0)
  {
    infc_browser_handle_create_acl_account(
      browser,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const gchar*)node->name, "remove-acl-account") == 0)
  {
    infc_browser_handle_remove_acl_account(
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
  InfChatBuffer* buffer;
  InfChatSession* session;
  InfcSessionProxy* proxy;

  InfBrowserIter parent_iter;
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

      buffer = inf_chat_buffer_new(256);

      /* Synchronize in subscription group: */
      session = inf_chat_session_new(
        priv->communication_manager,
        buffer,
        INF_SESSION_SYNCHRONIZING,
        INF_COMMUNICATION_GROUP(subreq->shared.chat.subscription_group),
        connection
      );

      g_object_unref(buffer);

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
        INF_SESSION_PROXY(proxy),
        INF_REQUEST(subreq->shared.chat.request)
      );

      if(subreq->shared.chat.request != NULL)
      {
        infc_request_manager_finish_request(
          priv->request_manager,
          subreq->shared.chat.request,
          inf_request_result_make_subscribe_chat(
            INF_BROWSER(browser),
            INF_SESSION_PROXY(proxy)
          )
        );
      }

      /* The default handler of the subscribe-session signal refs the proxy */
      g_object_unref(proxy);

      break;
    case INFC_BROWSER_SUBREQ_SESSION:
      g_assert(has_id == TRUE);
      if(subreq->shared.session.node != NULL)
      {
        g_assert(subreq->shared.session.node->id == node_id);

        infc_browser_subscribe_session(
          browser,
          subreq->shared.session.node,
          subreq->shared.session.request,
          subreq->shared.session.subscription_group,
          connection,
          TRUE
        );

        if(subreq->shared.session.request != NULL)
        {
          iter.node = subreq->shared.session.node;
          iter.node_id = node_id;

          g_assert(
            subreq->shared.session.node->type == INFC_BROWSER_NODE_NOTE_KNOWN
          );

          proxy = subreq->shared.session.node->shared.known.session;
          g_assert(proxy != NULL);

          infc_request_manager_finish_request(
            priv->request_manager,
            subreq->shared.session.request,
            inf_request_result_make_subscribe_session(
              INF_BROWSER(browser),
              &iter,
              INF_SESSION_PROXY(proxy)
            )
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
          subreq->shared.add_node.request,
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
          subreq->shared.add_node.request,
          subreq->shared.add_node.subscription_group,
          connection,
          FALSE
        );

        /* Finish request */
        if(subreq->shared.add_node.request != NULL)
        {
          parent_iter.node_id = subreq->shared.sync_in.parent->id;
          parent_iter.node = subreq->shared.sync_in.parent;
          iter.node = node;
          iter.node_id = node_id;

          infc_request_manager_finish_request(
            priv->request_manager,
            subreq->shared.add_node.request,
            inf_request_result_make_add_node(
              INF_BROWSER(browser),
              &parent_iter,
              &iter
            )
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
          subreq->shared.sync_in.request,
          node_id,
          subreq->shared.sync_in.name,
          subreq->shared.sync_in.plugin->note_type,
          subreq->shared.sync_in.sheet_set,
          proxy
        );

        g_assert(node->type == INFC_BROWSER_NODE_NOTE_KNOWN);

        parent_iter.node_id = subreq->shared.sync_in.parent->id;
        parent_iter.node = subreq->shared.sync_in.parent;
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
            INF_SESSION_PROXY(proxy),
            INF_REQUEST(subreq->shared.sync_in.request)
          );
        }

        g_object_unref(proxy);

        /* TODO: Emit a signal, so that others are notified that a sync-in
         * begins and can show progress or something. */

        g_assert(subreq->shared.sync_in.request != NULL);

        infc_request_manager_finish_request(
          priv->request_manager,
          subreq->shared.sync_in.request,
          inf_request_result_make_add_node(
            INF_BROWSER(browser),
            &parent_iter,
            &iter
          )
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
                                       InfSessionProxy* proxy,
                                       InfRequest* request)
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

  g_signal_connect(
    session,
    "notify::subscription-group",
    G_CALLBACK(infc_browser_session_notify_subscription_group_cb),
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

static InfRequest*
infc_browser_browser_explore(InfBrowser* browser,
                             const InfBrowserIter* iter,
                             InfRequestFunc func,
                             gpointer user_data)
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
    INFC_TYPE_PROGRESS_REQUEST,
    "explore-node",
    G_CALLBACK(func),
    user_data,
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

  return INF_REQUEST(request);
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

static InfRequest*
infc_browser_browser_add_note(InfBrowser* infbrowser,
                              const InfBrowserIter* iter,
                              const char* name,
                              const char* type,
                              const InfAclSheetSet* sheet_set,
                              InfSession* session,
                              gboolean initial_subscribe,
                              InfRequestFunc func,
                              gpointer user_data)
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
    INFC_TYPE_REQUEST,
    "add-node",
    G_CALLBACK(func),
    user_data,
    "node-id", iter->node_id,
    NULL
  );

  if(session != NULL)
  {
    /* TODO: Add a InfcSyncInRequest, deriving from InfcRequest that
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

  return INF_REQUEST(request);
}

static InfRequest*
infc_browser_browser_add_subdirectory(InfBrowser* infbrowser,
                                      const InfBrowserIter* iter,
                                      const char* name,
                                      const InfAclSheetSet* sheet_set,
                                      InfRequestFunc func,
                                      gpointer user_data)
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
    INFC_TYPE_REQUEST,
    "add-node",
    G_CALLBACK(func), user_data,
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

  return INF_REQUEST(request);
}

static InfRequest*
infc_browser_browser_remove_node(InfBrowser* infbrowser,
                                 const InfBrowserIter* iter,
                                 InfRequestFunc func,
                                 gpointer user_data)
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
    INFC_TYPE_REQUEST,
    "remove-node",
    G_CALLBACK(func),
    user_data,
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

  return INF_REQUEST(request);
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

static InfRequest*
infc_browser_browser_subscribe(InfBrowser* browser,
                               const InfBrowserIter* iter,
                               InfRequestFunc func,
                               gpointer user_data)
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
    INFC_TYPE_REQUEST,
    "subscribe-session",
    G_CALLBACK(func),
    user_data,
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

  return INF_REQUEST(request);
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
                                       InfRequest* request,
                                       InfBrowserIter* iter)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  guint node_id;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  g_return_val_if_fail(INFC_IS_REQUEST(request), FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  priv = INFC_BROWSER_PRIVATE(browser);
  g_object_get(G_OBJECT(request), "node-id", &node_id, NULL);
  if(node_id == G_MAXUINT) return FALSE;

  node = g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id));
  if(node == NULL) return FALSE;

  iter->node_id = node_id;
  iter->node = node;
  return TRUE;
}

static InfRequest*
infc_browser_browser_query_acl_account_list(InfBrowser* browser,
                                            InfRequestFunc func,
                                            gpointer user_data)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  xmlNodePtr xml;

  InfAclAccount* accounts;
  guint n_accounts;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  priv = INFC_BROWSER_PRIVATE(browser);

  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);

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
    INFC_TYPE_PROGRESS_REQUEST,
    "query-acl-account-list",
    G_CALLBACK(func),
    user_data,
    NULL
  );

  inf_browser_begin_request(browser, NULL, INF_REQUEST(request));

  switch(priv->account_list_status)
  {
  case INFC_BROWSER_ACCOUNT_LIST_NOT_QUERIED:
  case INFC_BROWSER_ACCOUNT_LIST_NO_NOTIFICATIONS:
    xml = infc_browser_request_to_xml(request);

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      priv->connection,
      xml
    );

    break;
  case INFC_BROWSER_ACCOUNT_LIST_NOTIFICATIONS:
    /* Our cache is up to date, i.e. serve from cache */
    accounts = infc_browser_make_acl_account_list(
      INFC_BROWSER(browser),
      &n_accounts
    );

    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_query_acl_account_list(
        INF_BROWSER(browser),
        accounts,
        n_accounts,
        TRUE
      )
    );

    g_free(accounts);
    request = NULL;
    break;
  }

  return INF_REQUEST(request);
}

static const InfAclAccount*
infc_browser_browser_get_acl_default_account(InfBrowser* infbrowser)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfAclAccountId default_id;
  InfAclAccount* default_account;

  browser = INFC_BROWSER(infbrowser);
  priv = INFC_BROWSER_PRIVATE(browser);

  g_return_val_if_fail(priv->status == INF_BROWSER_OPEN, NULL);

  default_id = inf_acl_account_id_from_string("default");

  default_account = g_hash_table_lookup(
    priv->accounts,
    INF_ACL_ACCOUNT_ID_TO_POINTER(default_id)
  );

  g_assert(default_account != NULL);
  return default_account;
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

static InfRequest*
infc_browser_browser_lookup_acl_accounts(InfBrowser* infbrowser,
                                         const InfAclAccountId* ids,
                                         guint n_ids,
                                         InfRequestFunc func,
                                         gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  guint i;
  const InfAclAccount* account;
  InfAclAccount* accounts;
  guint n_accounts;
  xmlNodePtr xml;

  InfAclAccountId* ids_req;

  browser = INFC_BROWSER(infbrowser);
  priv = INFC_BROWSER_PRIVATE(browser);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_REQUEST,
    "lookup-acl-accounts",
    G_CALLBACK(func),
    user_data,
    NULL
  );

  if(priv->account_list_status != INFC_BROWSER_ACCOUNT_LIST_NOTIFICATIONS)
    xml = infc_browser_request_to_xml(request);
  else
    xml = NULL;

  accounts = g_malloc(sizeof(InfAclAccount) * n_ids);
  n_accounts = 0;

  /* Lookup each account from server that we don't have in our cache */
  for(i = 0; i < n_ids; ++i)
  {
    account = g_hash_table_lookup(
      priv->accounts,
      INF_ACL_ACCOUNT_ID_TO_POINTER(ids[i])
    );

    if(account != NULL)
    {
      accounts[i] = *account;
      ++n_accounts;
    }
    else
    {
      accounts[i].id = ids[i];
      accounts[i].name = NULL;

      if(xml != NULL)
      {
        xmlNewChild(
          xml,
          NULL,
          (const xmlChar*)"id",
          (const xmlChar*)inf_acl_account_id_to_string(ids[i])
        );
      }
    }
  }

  if(xml == NULL || n_accounts == n_ids)
  {
    /* All lookup IDs are available from cache */
    if(xml != NULL) xmlFreeNode(xml);

    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_lookup_acl_accounts(
        INF_BROWSER(browser),
        accounts,
        n_ids
      )
    );

    request = NULL;
  }
  else
  {
    /* TODO: Add a InfcLookupAclAccountsRequest, deriving from InfcRequest
     * that carries ids and n_ids, so we don't need
     * g_object_set_qdata for the session and plugin. */

    /* Remember all IDs that we were asked to look up, also the ones that are
     * available already, so that we can finish the request in one go when
     * the server reply is available. */
    ids_req = g_malloc(sizeof(InfAclAccountId) * n_ids);
    for(i = 0; i < n_ids; ++i)
      ids_req[i] = ids[i];

    g_object_set_qdata_full(
      G_OBJECT(request),
      infc_browser_lookup_acl_accounts_ids_quark,
      ids_req,
      g_free
    );

    g_object_set_qdata(
      G_OBJECT(request),
      infc_browser_lookup_acl_accounts_n_ids_quark,
      GUINT_TO_POINTER(n_ids)
    );

    /* Need to send a request */
    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      priv->connection,
      xml
    );
  }

  g_free(accounts);
  return INF_REQUEST(request);
}

static InfRequest*
infc_browser_browser_lookup_acl_account_by_name(InfBrowser* infbrowser,
                                                const gchar* name,
                                                InfRequestFunc func,
                                                gpointer user_data)
{
  InfcBrowser* browser;
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  GArray* cached_accounts;
  InfcBrowserLookupAclAccountByByNameData lookup_data;
  InfAclAccount result_acc;

  xmlNodePtr xml;

  browser = INFC_BROWSER(infbrowser);
  priv = INFC_BROWSER_PRIVATE(browser);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_REQUEST,
    "lookup-acl-accounts",
    G_CALLBACK(func),
    user_data,
    NULL
  );

  lookup_data.name = name;
  lookup_data.accounts = g_array_new(FALSE, FALSE, sizeof(InfAclAccount));

  g_hash_table_foreach(
    priv->accounts,
    infc_browser_browser_lookup_acl_account_by_name_find_func,
    &lookup_data
  );

  if(lookup_data.accounts->len == 0)
  {
    if(priv->account_list_status == INFC_BROWSER_ACCOUNT_LIST_NOTIFICATIONS)
    {
      /* We know that the entry does not exist */
      result_acc.id = 0;
      result_acc.name = (gchar*)name;

      infc_request_manager_finish_request(
        priv->request_manager,
        request,
        inf_request_result_make_lookup_acl_accounts(
          INF_BROWSER(browser),
          &result_acc,
          1
        )
      );

      request = NULL;
    }
    else
    {
      /* Make a server request */
      xml = infc_browser_request_to_xml(request);
      xmlNewChild(xml, NULL, (const xmlChar*)"name", (const xmlChar*)name);

      g_object_set_qdata_full(
        G_OBJECT(request),
        infc_browser_lookup_acl_accounts_name_quark,
        g_strdup(name),
        g_free
      );

      inf_communication_group_send_message(
        INF_COMMUNICATION_GROUP(priv->group),
        priv->connection,
        xml
      );
    }
  }
  else
  {
    /* Use cache value */
    infc_request_manager_finish_request(
      priv->request_manager,
      request,
      inf_request_result_make_lookup_acl_accounts(
        INF_BROWSER(browser),
        (InfAclAccount*)lookup_data.accounts->data,
        lookup_data.accounts->len
      )
    );

    request = NULL;
  }

  g_array_free(lookup_data.accounts, TRUE);

  /* Note that cached account names point to the hash table entries
   * and have not been copied, so do not need to be freed here. */
  return INF_REQUEST(request);
}

static InfRequest*
infc_browser_browser_create_acl_account(InfBrowser* browser,
                                        gnutls_x509_crq_t crq,
                                        InfRequestFunc func,
                                        gpointer user_data)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  xmlNodePtr xml;
  xmlNodePtr crqNode;

  GError* error;
  gchar* crq_text;
  size_t size;
  int res;

  priv = INFC_BROWSER_PRIVATE(browser);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_REQUEST,
    "create-acl-account",
    G_CALLBACK(func),
    user_data,
    NULL
  );

  inf_browser_begin_request(INF_BROWSER(browser), NULL, INF_REQUEST(request));

  size = 0;
  res = gnutls_x509_crq_export(crq, GNUTLS_X509_FMT_PEM, NULL, &size);
  if(res != GNUTLS_E_SHORT_MEMORY_BUFFER)
  {
    error = NULL;
    inf_gnutls_set_error(&error, res);

    infc_request_manager_fail_request(priv->request_manager, request, error);
    g_error_free(error);

    g_object_unref(request);
    return NULL;
  }

  crq_text = g_malloc(size);
  res = gnutls_x509_crq_export(crq, GNUTLS_X509_FMT_PEM, crq_text, &size);
  if(res != GNUTLS_E_SUCCESS)
  {
    g_free(crq_text);

    error = NULL;
    inf_gnutls_set_error(&error, res);

    infc_request_manager_fail_request(priv->request_manager, request, error);
    g_error_free(error);

    g_object_unref(request);
    return NULL;
  }

  xml = infc_browser_request_to_xml(request);
  crqNode = xmlNewChild(xml, NULL, (const xmlChar*)"crq", NULL);
  xmlNodeAddContentLen(crqNode, (const xmlChar*)crq_text, size);
  g_free(crq_text);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_REQUEST(request);
}

static InfRequest*
infc_browser_browser_remove_acl_account(InfBrowser* browser,
                                        InfAclAccountId account,
                                        InfRequestFunc func,
                                        gpointer user_data)
{
  InfcBrowserPrivate* priv;
  InfcRequest* request;
  xmlNodePtr xml;

  priv = INFC_BROWSER_PRIVATE(browser);

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_REQUEST,
    "remove-acl-account",
    G_CALLBACK(func),
    user_data,
    NULL
  );

  inf_browser_begin_request(INF_BROWSER(browser), NULL, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);

  inf_xml_util_set_attribute(
    xml,
    "id",
    inf_acl_account_id_to_string(account)
  );

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_REQUEST(request);
}

static InfRequest*
infc_browser_browser_query_acl(InfBrowser* browser,
                               const InfBrowserIter* iter,
                               InfRequestFunc func,
                               gpointer user_data)
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
    INFC_TYPE_REQUEST,
    "query-acl",
    G_CALLBACK(func),
    user_data,
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

  return INF_REQUEST(request);
}

static gboolean
infc_browser_browser_has_acl(InfBrowser* browser,
                             const InfBrowserIter* iter,
                             InfAclAccountId account)
{
  InfcBrowserPrivate* priv;
  InfcBrowserNode* node;
  InfAclAccountId default_id;

  g_return_val_if_fail(INFC_IS_BROWSER(browser), FALSE);
  infc_browser_return_val_if_iter_fail(INFC_BROWSER(browser), iter, FALSE);

  priv = INFC_BROWSER_PRIVATE(browser);
  node = (InfcBrowserNode*)iter->node;

  /* If we have queried the full ACL, everything is available */
  if(node->acl_queried == TRUE)
    return TRUE;

  /* Otherwise we only have the local user and the default user sheets */
  if(account != 0)
  {
    if(account == priv->local_account->id)
      return TRUE;

    default_id = inf_acl_account_id_from_string("default");
    if(account == default_id)
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

static InfRequest*
infc_browser_browser_set_acl(InfBrowser* browser,
                             const InfBrowserIter* iter,
                             const InfAclSheetSet* sheet_set,
                             InfRequestFunc func,
                             gpointer user_data)
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
    INFC_TYPE_REQUEST,
    "set-acl",
    G_CALLBACK(func),
    user_data,
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

  return INF_REQUEST(request);
}

/*
 * GType registration.
 */

static void
infc_browser_class_init(InfcBrowserClass* browser_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(browser_class);

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

  infc_browser_lookup_acl_accounts_ids_quark = g_quark_from_static_string(
    "infc-browser-lookup-acl-accounts-ids-quark"
  );

  infc_browser_lookup_acl_accounts_n_ids_quark = g_quark_from_static_string(
    "infc-browser-lookup-acl-accounts-n-ids-quark"
  );

  infc_browser_lookup_acl_accounts_name_quark = g_quark_from_static_string(
    "infc-browser-lookup-acl-accounts-name-quark"
  );

  infc_browser_query_acl_account_list_accounts_quark =
    g_quark_from_static_string(
      "infc-browser-query-acl-account-list-accounts-quark"
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
infc_browser_communication_object_iface_init(
  InfCommunicationObjectInterface* iface)
{
  iface->received = infc_browser_communication_object_received;
  iface->sent = infc_browser_communication_object_sent;
}

static void
infc_browser_browser_iface_init(InfBrowserInterface* iface)
{
  iface->error = NULL;
  iface->node_added = NULL;
  iface->node_removed = NULL;
  iface->subscribe_session = infc_browser_browser_subscribe_session;
  iface->unsubscribe_session = NULL;
  iface->begin_request = NULL;
  iface->acl_account_added = NULL;
  iface->acl_account_removed = NULL;
  iface->acl_local_account_changed = NULL;
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

  iface->get_acl_default_account = infc_browser_browser_get_acl_default_account;
  iface->get_acl_local_account = infc_browser_browser_get_acl_local_account;
  iface->query_acl_account_list = infc_browser_browser_query_acl_account_list;
  iface->lookup_acl_accounts = infc_browser_browser_lookup_acl_accounts;
  iface->lookup_acl_account_by_name =
    infc_browser_browser_lookup_acl_account_by_name;
  iface->create_acl_account = infc_browser_browser_create_acl_account;
  iface->remove_acl_account = infc_browser_browser_remove_acl_account;
  iface->query_acl = infc_browser_browser_query_acl;
  iface->has_acl = infc_browser_browser_has_acl;
  iface->get_acl = infc_browser_browser_get_acl;
  iface->set_acl = infc_browser_browser_set_acl;
}

/*
 * Public API.
 */

/**
 * infc_browser_new: (constructor)
 * @io: A #InfIo object used to schedule timeouts.
 * @comm_manager: A #InfCommunicationManager to register the server connection
 * and which forwards incoming data to the browser or running sessions.
 * @connection: Connection to the server.
 *
 * Creates a new #InfcBrowser.
 *
 * Returns: (transfer full): A new #InfcBrowser.
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
 * Returns: (transfer none): A #InfCommunicationManager.
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
 * Returns: (transfer none): A #InfXmlConnection.
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
 * Returns: (transfer none): Whether the plugin was added successfully.
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
 * Returns: (transfer none): A #InfcNotePlugin, or %NULL.
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
 * @iter: A #InfBrowserIter pointing to a note in @browser.
 * @func: (scope async): The function to be called when the request finishes,
 * or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Requests that the server saves the note pointed to by @iter into its
 * background storage. Normally, the server only does this when it is either
 * shut down or when the there are no more subscriptions to the note. Note that
 * this is merely a request and the server might decide not to save the
 * session for whatever reason.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest that may be used to
 * get notified when the request finishes or fails.
 **/
InfRequest*
infc_browser_iter_save_session(InfcBrowser* browser,
                               const InfBrowserIter* iter,
                               InfRequestFunc func,
                               gpointer user_data)
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
    INFC_TYPE_REQUEST,
    "save-session",
    G_CALLBACK(func),
    user_data,
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

  return INF_REQUEST(request);
}

/**
 * infc_browser_iter_get_sync_in:
 * @browser: A #InfcBrowser.
 * @iter: A #InfBrowserIter pointing to a note in @browser.
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
 * Returns: (transfer none) (allow-none): A #InfcSessionProxy, or %NULL if we
 * are currently not synchronizing this node to the server.
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
 * @iter: A #InfBrowserIter pointing to a subdirectory node in @browser.
 *
 * Returns a list of all #InfRequest<!-- -->s created with
 * inf_browser_add_note() with the node @iter points to as
 * parent. Such requests begin a synchronization to the server when they
 * have finished.
 *
 * Returns: (transfer container) (element-type InfRequest): A list of
 * #InfRequest<!-- -->s. Free with g_slist_free() when done.
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
 * @iter: A #InfBrowserIter.
 *
 * Returns whether @iter points to a valid node in @browser. This is
 * generally the case for any #InfBrowserIter returned by one of the
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
 * @func: (scope async): The function to be called when the request
 * finishes, or %NULL.
 * @user_data: Additional data to pass to @func.
 *
 * Attempts to subscribe to the server's chat. When the operation finishes
 * infc_browser_get_chat_session() will return a #InfcSessionProxy
 * representing the chat session. It can be used to read the chat's content.
 * The request can fail in case the server chat is disabled.
 *
 * The request might either finish during the call to this function, in which
 * case @func will be called and %NULL being returned. If the request does not
 * finish within the function call, a #InfRequest object is returned,
 * where @func has been installed for the #InfRequest::finished signal,
 * so that it is called as soon as the request finishes.
 *
 * Returns: (transfer none) (allow-none): A #InfRequest that may be used to
 * get notified when the request finishes or fails.
 */
InfRequest*
infc_browser_subscribe_chat(InfcBrowser* browser,
                            InfRequestFunc func,
                            gpointer user_data)
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
    INFC_TYPE_REQUEST,
    "subscribe-chat",
    G_CALLBACK(func),
    user_data,
    NULL
  );

  inf_browser_begin_request(INF_BROWSER(browser), NULL, INF_REQUEST(request));

  xml = infc_browser_request_to_xml(request);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    priv->connection,
    xml
  );

  return INF_REQUEST(request);
}

/**
 * infc_browser_get_subscribe_chat_request:
 * @browser: A #InfcBrowser.
 *
 * Returns the #InfRequest that represests the request sent to the server
 * which attempts to subscribe to its chat. If there is no such request
 * running, then the function returns %NULL. After such a request finishes,
 * call infc_browser_get_chat_session() to get the #InfcSessionProxy for the
 * chat session. To initiate the request, call infc_browser_subscribe_chat().
 *
 * Returns: (transfer none) (allow-none): A #InfRequest, or %NULL.
 */
InfRequest*
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

  return INF_REQUEST(data.result);
}

/**
 * infc_browser_get_chat_session:
 * @browser: A #InfcBrowser.
 *
 * Returns the #InfcSessionProxy representing the running chat session if the
 * local client is subscribed to it, or %NULL otherwise.
 *
 * Returns: (transfer none) (allow-none): A #InfcSessionProxy for the chat,
 * or %NULL.
 */
InfcSessionProxy*
infc_browser_get_chat_session(InfcBrowser* browser)
{
  g_return_val_if_fail(INFC_IS_BROWSER(browser), NULL);
  return INFC_BROWSER_PRIVATE(browser)->chat_session;
}

/* vim:set et sw=2 ts=2: */
