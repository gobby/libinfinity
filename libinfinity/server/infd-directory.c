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
#include <libinfinity/server/infd-request.h>
#include <libinfinity/server/infd-progress-request.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-chat-session.h>
#include <libinfinity/common/inf-request-result.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/common/inf-protocol.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-cert-util.h>
#include <libinfinity/communication/inf-communication-object.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <gnutls/gnutls.h>

#include <string.h>

typedef struct _InfdDirectoryWriteAccountListForeachData
  InfdDirectoryWriteAccountListForeachData;
struct _InfdDirectoryWriteAccountListForeachData {
  const InfdAclAccountInfo** accounts;
  guint index;
};

typedef struct _InfdDirectoryGetAclAccountListData
  InfdDirectoryGetAclAccountListData;
struct _InfdDirectoryGetAclAccountListData {
  const InfAclAccount** accounts;
  guint index;
};

typedef struct _InfdDirectoryNode InfdDirectoryNode;
struct _InfdDirectoryNode {
  InfdDirectoryNode* parent;
  InfdDirectoryNode* prev;
  InfdDirectoryNode* next;

  InfAclSheetSet* acl;
  GSList* acl_connections;

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
      InfIoTimeout* save_timeout;
      /* Whether we hold a weak reference or a strong reference on session */
      gboolean weakref;
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
  InfAclSheetSet* sheet_set;
  const InfdNotePlugin* plugin;
  InfdSessionProxy* proxy;
  InfdRequest* request;
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
      InfdRequest* request;
    } session;

    struct {
      InfdDirectoryNode* parent;
      InfCommunicationHostedGroup* group;
      const InfdNotePlugin* plugin;
      gchar* name;
      InfAclSheetSet* sheet_set;
      /* TODO: Isn't group already present in proxy? */
      InfdSessionProxy* proxy;
      InfdRequest* request;
    } add_node;

    struct {
      InfdDirectoryNode* parent;
      InfCommunicationHostedGroup* synchronization_group;
      InfCommunicationHostedGroup* subscription_group;
      const InfdNotePlugin* plugin;
      gchar* name;
      InfAclSheetSet* sheet_set;
      /* TODO: Aren't the groups already present in proxy? */
      InfdSessionProxy* proxy;
      InfdRequest* request;
    } sync_in;
  } shared;
};

typedef struct _InfdDirectoryConnectionInfo InfdDirectoryConnectionInfo;
struct _InfdDirectoryConnectionInfo {
  guint seq_id;
  const InfdAclAccountInfo* account;
};

typedef struct _InfdDirectoryPrivate InfdDirectoryPrivate;
struct _InfdDirectoryPrivate {
  InfIo* io;
  InfdStorage* storage;
  InfCommunicationManager* communication_manager;
  InfCommunicationHostedGroup* group;

  gnutls_x509_privkey_t private_key;
  InfCertificateChain* certificate;

  GHashTable* plugins; /* Registered plugins */
  GHashTable* connections; /* Connection infos */

  GHashTable* accounts; /* All accounts */
  GHashTable* accounts_by_certificate; /* For lookup by certificate */
  GSList* account_list_connections;

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

  PROP_PRIVATE_KEY,
  PROP_CERTIFICATE,

  /* read only */
  PROP_CHAT_SESSION,
  PROP_STATUS
};

enum {
  CONNECTION_ADDED,
  CONNECTION_REMOVED,

  LAST_SIGNAL
};

static const unsigned int DAYS = 24 * 60 * 60;

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
                                   InfdDirectoryNode* node,
                                   InfdRequest* request);

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
  InfSession* session;

  timeout_data = (InfdDirectorySessionSaveTimeoutData*)user_data;

  g_assert(timeout_data->node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(timeout_data->node->shared.note.save_timeout != NULL);
  priv = INFD_DIRECTORY_PRIVATE(timeout_data->directory);
  error = NULL;

  infd_directory_node_get_path(timeout_data->node, &path, NULL);

  g_object_get(
    G_OBJECT(timeout_data->node->shared.note.session),
    "session", &session,
    NULL
  );

  /* TODO: Only write if the buffer modified-flag is set */

  result = timeout_data->node->shared.note.plugin->session_write(
    priv->storage,
    session,
    path,
    timeout_data->node->shared.note.plugin->user_data,
    &error
  );

  g_object_unref(session);

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
      timeout_data->node,
      NULL
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

  if(priv->storage != NULL)
  {
    node->shared.note.save_timeout = inf_io_add_timeout(
      priv->io,
      INFD_DIRECTORY_SAVE_TIMEOUT,
      infd_directory_session_save_timeout_func,
      timeout_data,
      infd_directory_session_save_timeout_data_free
    );
  }
}

static void
infd_directory_session_weak_ref_cb(gpointer data,
                                   GObject* where_the_object_was)
{
  InfdDirectoryNode* node;
  node = (InfdDirectoryNode*)data;

  g_assert(G_OBJECT(node->shared.note.session) == where_the_object_was);
  g_assert(node->shared.note.weakref == TRUE);
  g_assert(node->shared.note.save_timeout == NULL);

  node->shared.note.session = NULL;
  node->shared.note.weakref = FALSE;
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
    if(node->shared.note.weakref == FALSE &&
       node->shared.note.save_timeout == NULL)
    {
      infd_directory_start_session_save_timeout(directory, node);
    }
  }
  else
  {
    /* If a session becomes non-idle again then strong-ref it */
    if(node->shared.note.weakref == TRUE)
    {
      g_object_ref(node->shared.note.session);
      g_assert(node->shared.note.save_timeout == NULL);
      node->shared.note.weakref = FALSE;

      g_object_weak_unref(
        G_OBJECT(node->shared.note.session),
        infd_directory_session_weak_ref_cb,
        node
      );
    }
    else if(node->shared.note.save_timeout != NULL)
    {
      inf_io_remove_timeout(priv->io, node->shared.note.save_timeout);
      node->shared.note.save_timeout = NULL;
    }
  }
}

static gboolean
infd_directory_session_reject_user_join_cb(InfdSessionProxy* proxy,
                                           InfXmlConnection* connection,
                                           const GArray* user_properties,
                                           InfUser* rejoin_user,
                                           gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  gpointer node_id;
  InfdDirectoryNode* node;

  InfdDirectoryConnectionInfo* info;
  InfBrowserIter iter;
  InfAclMask check_mask;
  gboolean result;

  directory = INFD_DIRECTORY(user_data);
  priv = INFD_DIRECTORY_PRIVATE(directory);
  node_id = g_object_get_qdata(G_OBJECT(proxy), infd_directory_node_id_quark);
  node = g_hash_table_lookup(priv->nodes, node_id);
  g_assert(node != NULL);

  /* ACL cannot prevent local users from joining */
  if(connection != NULL)
  {
    info = g_hash_table_lookup(priv->connections, connection);
    g_assert(info != NULL);

    iter.node_id = node->id;
    iter.node = node;

    inf_acl_mask_set1(&check_mask, INF_ACL_CAN_JOIN_USER);

    result = inf_browser_check_acl(
      INF_BROWSER(directory),
      &iter,
      &info->account->account,
      &check_mask,
      NULL
    );

    /* Reject the user join if the permission is not set. */
    if(result == FALSE)
      return TRUE;
  }

  return FALSE;
}

/* Releases a session fully from the directory. This is not normally needed
 * because if we don't need a session anymore we still keep a weak reference
 * to it around, in case we need to recover it later. When nobody is holding
 * a strong reference to it anymore we clear the pointer in
 * infd_directory_session_weak_ref_cb(). The only point where this function
 * is being called is when a node with an active session is removed from the
 * directory. */
static void
infd_directory_release_session(InfdDirectory* directory,
                               InfdDirectoryNode* node,
                               InfdSessionProxy* session)
{
  InfdDirectoryPrivate* priv;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(node->shared.note.session == session);

  if(node->shared.note.save_timeout != NULL)
  {
    inf_io_remove_timeout(priv->io, node->shared.note.save_timeout);
    node->shared.note.save_timeout = NULL;
  }

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(infd_directory_session_idle_notify_cb),
    directory
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(infd_directory_session_reject_user_join_cb),
    directory
  );

  g_object_set_qdata(
    G_OBJECT(session),
    infd_directory_node_id_quark,
    NULL
  );

  if(node->shared.note.weakref == TRUE)
  {
    g_object_weak_unref(
      G_OBJECT(session),
      infd_directory_session_weak_ref_cb,
      node
    );
  }
  else
  {
    g_object_unref(session);
  }

  node->shared.note.session = NULL;
}

/*
 * ACLs
 */

const InfAclAccount*
infd_directory_acl_account_lookup_func(const gchar* id,
                                       gpointer user_data)
{
  GHashTable* account_table;
  InfdAclAccountInfo* info;

  account_table = (GHashTable*)user_data;
  info = (InfdAclAccountInfo*)g_hash_table_lookup(account_table, id);

  return &info->account;
}

static void
infd_directory_get_acl_account_list_foreach_func(gpointer key,
                                                 gpointer value,
                                                 gpointer user_data)
{
  InfdDirectoryGetAclAccountListData* data;
  const InfdAclAccountInfo* info;

  data = (InfdDirectoryGetAclAccountListData*)user_data;
  info = (const InfdAclAccountInfo*)value;

  data->accounts[data->index++] = &info->account;
}

static void
infd_directory_write_account_list_foreach_func(gpointer key,
                                               gpointer value,
                                               gpointer user_data)
{
  InfdDirectoryWriteAccountListForeachData* data;
  const InfdAclAccountInfo* info;

  data = (InfdDirectoryWriteAccountListForeachData*)user_data;
  info = (const InfdAclAccountInfo*)value;

  /* This is for writing the account list to storage. We omit writing the
   * default account, and all transient accounts. */
  if(strcmp(info->account.id, "default") != 0)
    if(info->transient == FALSE)
      data->accounts[data->index++] = info;
}

static void
infd_directory_write_account_list(InfdDirectory* directory)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryWriteAccountListForeachData data;
  GError* error;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  if(priv->storage != NULL)
  {
    data.index = 0;
    data.accounts = g_malloc(
      sizeof(InfdAclAccountInfo*) * (g_hash_table_size(priv->accounts) - 1)
    );

    g_hash_table_foreach(
      priv->accounts,
      infd_directory_write_account_list_foreach_func,
      &data
    );

    error = NULL;
    infd_storage_write_account_list(
      priv->storage,
      data.accounts,
      data.index,
      &error
    );

    if(error != NULL)
    {
      g_warning(
        _("Failed to write account list to storage: %s\n"
          "Keeping it in memory for now, but it will not be available "
          "after server restart"),
        error->message
      );

      g_error_free(error);
      error = NULL;
    }

    g_free(data.accounts);
  }
}

static void
infd_directory_announce_acl_account(InfdDirectory* directory,
                                    const InfdAclAccountInfo* info,
                                    InfXmlConnection* except)
{
  InfdDirectoryPrivate* priv;
  GSList* item;
  xmlNodePtr xml;
  InfXmlConnection* connection;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  xml = xmlNewNode(NULL, (const xmlChar*)"add-acl-account");
  inf_acl_account_to_xml(&info->account, xml);

  for(item = priv->account_list_connections;
      item != NULL;
      item = g_slist_next(item))
  {
    connection = INF_XML_CONNECTION(item->data);
    if(connection != except)
    {
      inf_communication_group_send_message(
        INF_COMMUNICATION_GROUP(priv->group),
        connection,
        xmlCopyNode(xml, 1)
      );
    }
  }

  inf_browser_acl_account_added(INF_BROWSER(directory), &info->account, NULL);

  xmlFreeNode(xml);
}

/* acl_connections is a list of connections which have queried the full ACL.
 * It can be NULL in which case only the default sheet and the sheet for that
 * particular connection are sent. */
static gboolean
infd_directory_acl_sheets_to_xml_for_connection(InfdDirectory* directory,
                                                GSList* acl_connections,
                                                const InfAclSheetSet* sheets,
                                                InfXmlConnection* connection,
                                                xmlNodePtr xml)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryConnectionInfo* info;
  guint written_sheets;
  InfAclSheet selected_sheets[2];
  InfAclSheetSet set;
  guint i;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(g_slist_find(acl_connections, connection) != NULL)
  {
    if(sheets->n_sheets > 0)
      inf_acl_sheet_set_to_xml(sheets, xml);
    written_sheets = sheets->n_sheets;
  }
  else
  {
    /* Otherwise, add only the sheets for the user itself and the default
     * sheet. */
    info = g_hash_table_lookup(priv->connections, connection);
    g_assert(info != NULL);

    written_sheets = 0;
    for(i = 0; i < sheets->n_sheets && written_sheets < 2; ++i)
    {
      if(strcmp(sheets->sheets[i].account->id, "default") == 0 ||
         sheets->sheets[i].account == &info->account->account)
      {
        selected_sheets[written_sheets] = sheets->sheets[i];
        ++written_sheets;
      }
    }

    if(written_sheets > 0)
    {
      set.own_sheets = NULL;
      set.sheets = selected_sheets;
      set.n_sheets = written_sheets;
      inf_acl_sheet_set_to_xml(&set, xml);
    }
  }

  if(written_sheets == 0)
    return FALSE;
  return TRUE;
}

static void
infd_directory_announce_acl_sheets_for_connection(InfdDirectory* directory,
                                                  const InfdDirectoryNode* nd,
                                                  const InfAclSheetSet* shts,
                                                  InfXmlConnection* conn)
{
  InfdDirectoryPrivate* priv;
  gboolean has_sheets;
  xmlNodePtr xml;
  gboolean any_sheets;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  xml = xmlNewNode(NULL, (const xmlChar*)"set-acl");

  any_sheets = infd_directory_acl_sheets_to_xml_for_connection(
    directory,
    nd->acl_connections,
    shts,
    conn,
    xml
  );

  if(any_sheets == TRUE)
  {
    inf_xml_util_set_attribute_uint(xml, "id", nd->id);

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      conn,
      xml
    );
  }
  else
  {
    xmlFreeNode(xml);
  }
}

static void
infd_directory_announce_acl_sheets(InfdDirectory* directory,
                                   InfdDirectoryNode* node,
                                   InfdRequest* request,
                                   const InfAclSheetSet* sheet_set,
                                   InfXmlConnection* except)
{
  InfdDirectoryPrivate* priv;
  xmlNodePtr xml;
  GList* connection_list;
  GSList* local_connection_list;
  GList* item;
  GSList* local_item;
  InfBrowserIter iter;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* Go through all connections that see this node, i.e. have explored the
   * parent node. To those connections we need to send an ACL update. */
  if(node->parent == NULL)
  {
    connection_list = g_hash_table_get_keys(priv->connections);

    for(item = connection_list; item != NULL; item = g_list_next(item))
    {
      if(item->data != except)
      {
        infd_directory_announce_acl_sheets_for_connection(
          directory,
          node,
          sheet_set,
          INF_XML_CONNECTION(item->data)
        );
      }
    }

    g_list_free(connection_list);
  }
  else
  {
    local_connection_list = node->parent->shared.subdir.connections;

    for(local_item = local_connection_list;
        local_item != NULL;
        local_item = g_slist_next(local_item))
    {
      if(local_item->data != except)
      {
        infd_directory_announce_acl_sheets_for_connection(
          directory,
          node,
          sheet_set,
          INF_XML_CONNECTION(local_item->data)
        );
      }
    }
  }

  iter.node_id = node->id;
  iter.node = node;

  inf_browser_acl_changed(
    INF_BROWSER(directory),
    &iter,
    sheet_set,
    INF_REQUEST(request)
  );
}

static InfdAclAccountInfo*
infd_directory_get_account_for_certificate(InfdDirectory* directory,
                                           gnutls_x509_crt_t cert)
{
  InfdDirectoryPrivate* priv;
  InfdAclAccountInfo* info;
  gchar* fingerprint;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  fingerprint = inf_cert_util_get_fingerprint(cert, GNUTLS_DIG_SHA256);
  info = g_hash_table_lookup(priv->accounts_by_certificate, fingerprint);
  g_free(fingerprint);

  return info;
}

static InfdAclAccountInfo*
infd_directory_login_by_certificate(InfdDirectory* directory,
                                    InfXmlConnection* connection)
{
  InfdDirectoryPrivate* priv;
  InfCertificateChain* chain;
  InfdAclAccountInfo* info;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_object_get(G_OBJECT(connection), "remote-certificate", &chain, NULL);

  info = NULL;
  if(chain != NULL)
  {
    info = infd_directory_get_account_for_certificate(
      directory,
      inf_certificate_chain_get_own_certificate(chain)
    );

    inf_certificate_chain_unref(chain);

    if(info != NULL)
    {
      /* Update last seen time */
      infd_acl_account_info_update_time(info);
      infd_directory_write_account_list(directory);
    }
  }

  /* No client certificate provided, or certificate not registered with any
   * account: Fall back to default user. */
  if(info == NULL)
  {
    info = g_hash_table_lookup(priv->accounts, "default");
  }

  g_assert(info != NULL);
  return info;
}

/* node can be NULL. If node is not NULL, additional sheets are returned
 * which correspond to erasure of the current ACL for the node. This allows
 * the ACL change to be performed atomically on the node. */
static InfAclSheetSet*
infd_directory_read_acl(InfdDirectory* directory,
                        const gchar* path,
                        InfdDirectoryNode* node,
                        GError** error)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;
  GError* local_error;
  GSList* acl;
  GSList* item;
  InfdStorageAcl* storage_acl;
  InfAclSheetSet* sheet_set;
  InfAclSheet* sheet;
  const InfdAclAccountInfo* info;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(priv->storage != NULL);

  local_error = NULL;
  acl = infd_storage_read_acl(priv->storage, path, &local_error);

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  /* If there are any ACLs set already for this node, then clear them. This
   * should usually not happen because we only call this function for new
   * nodes, but it can happen when the storage is changed on the fly and the
   * root node changes. */
  if(node != NULL && node->acl != NULL)
  {
    sheet_set = inf_acl_sheet_set_get_clear_sheets(node->acl);
  }
  else
  {
    sheet_set = inf_acl_sheet_set_new();
  }

  for(item = acl; item != NULL; item = g_slist_next(item))
  {
    storage_acl = (InfdStorageAcl*)item->data;
    info = g_hash_table_lookup(priv->accounts, storage_acl->account_id);

    /* It is possible that we do not find this user in the user table. For
     * example this can happen if reading the initial user table failed, or if
     * the account was removed. */
    if(info != NULL)
    {
      sheet = inf_acl_sheet_set_add_sheet(sheet_set, &info->account);
      sheet->mask = storage_acl->mask;
      sheet->perms = storage_acl->perms;
    }
  }

  infd_storage_acl_list_free(acl);

  return sheet_set;
}

static void
infd_directory_write_acl(InfdDirectory* directory,
                         InfdDirectoryNode* node)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;
  gchar* path;
  GError* error;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* Write the changed ACL into the storage */
  if(priv->storage != NULL)
  {
    infd_directory_node_get_path(node, &path, NULL);
    error = NULL;

    /* TODO: Don't write sheets for transient accounts. It does not make much
     * difference, because the transient user account is not stored, so the
     * sheet will be rejected anyway when it is read from disk next time. */
    infd_storage_write_acl(priv->storage, path, node->acl, &error);

    if(error != NULL)
    {
      g_warning(
        _("Failed to write ACL for node \"%s\": %s\nThe new ACL is applied "
          "but will be lost after a server re-start. This is a possible "
          "security problem. Please fix the problem with the storage!"),
        path,
        error->message
      );

      g_error_free(error);
    }

    g_free(path);
  }
}

static void
infd_directory_read_root_acl(InfdDirectory* directory)
{
  InfdDirectoryPrivate* priv;
  InfdAclAccountInfo* default_account;
  InfdAclAccountInfo** accounts;
  guint n_accounts;
  guint i, j;
  InfdAclAccountInfo* info;

  GHashTableIter hash_iter;
  gpointer key;
  gpointer value;
  InfdDirectoryConnectionInfo* conn_info;

  InfAclSheetSet* sheet_set;
  InfAclSheet* default_sheet;
  InfAclMask default_mask;
  InfAclMask tmp_mask;
  GError* error;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  error = NULL;

  /* Remove existing user accounts, we are going to lead new ones
   * from storage. */
  /* TODO: Emit an account-removed signal,
   * and send it to account_list_connections */
  g_hash_table_remove_all(priv->accounts);
  g_hash_table_remove_all(priv->accounts_by_certificate);

  default_account = infd_acl_account_info_new("default", NULL, FALSE);

  g_hash_table_insert(
    priv->accounts,
    default_account->account.id,
    default_account
  );

  infd_directory_announce_acl_account(directory, default_account, NULL);

  accounts = infd_storage_read_account_list(
    priv->storage,
    &n_accounts,
    &error
  );

  if(error != NULL)
  {
    g_assert(accounts == NULL);

    g_warning(
      _("Failed to read account list from storage: %s\n"
        "Will start with an empty account list."),
      error->message
    );

    g_error_free(error);
    error = NULL;
  }
  else
  {
    /* TODO: If any error occurs below, we should also fall back to an
     * empty account list. Would make sense to put this code into another
     * function then. */
    for(i = 0; i < n_accounts; ++i)
    {
      info = g_hash_table_lookup(priv->accounts, accounts[i]->account.id);
      if(info != NULL)
      {
        g_warning(
          _("Account ID \"%s\" occurs more than once in storage. Ignoring "
            "all but the first one."),
          accounts[i]->account.id
        );

        infd_acl_account_info_free(accounts[i]);
      }
      else
      {
        g_hash_table_insert(
          priv->accounts,
          accounts[i]->account.id,
          accounts[i]
        );

        for(j = 0; j < accounts[i]->n_certificates; ++j)
        {
          info = g_hash_table_lookup(
            priv->accounts_by_certificate,
            accounts[i]->certificates[j]
          );

          if(info != NULL)
          {
            g_warning(
              "Certificate \"%s\" is mapped to more than one account. "
              "Removing it from all accounts.",
              accounts[i]->certificates[j]
            );

            g_hash_table_remove(
              priv->accounts_by_certificate,
              accounts[i]->certificates[j]
            );

            infd_acl_account_info_remove_certificate(
              info,
              accounts[i]->certificates[j]
            );

            infd_acl_account_info_remove_certificate(
              accounts[i],
              accounts[i]->certificates[j]
            );
          }
          else
          {
            g_hash_table_insert(
              priv->accounts_by_certificate,
              accounts[i]->certificates[j],
              accounts[i]
            );
          }
        }

        infd_directory_announce_acl_account(directory, accounts[i], NULL);
      }
    }

    g_free(accounts);
  }

  g_hash_table_iter_init(&hash_iter, priv->connections);
  while(g_hash_table_iter_next(&hash_iter, &key, &value))
  {
    conn_info = (InfdDirectoryConnectionInfo*)value;

    conn_info->account = infd_directory_login_by_certificate(
      directory,
      INF_XML_CONNECTION(key)
    );

    /* TODO: We should send each user its new logined account. */
  }

  sheet_set = infd_directory_read_acl(
    directory,
    "/",
    priv->root,
    &error
  );

  default_account = g_hash_table_lookup(priv->accounts, "default");
  g_assert(default_account != NULL);

  if(error != NULL)
  {
    g_assert(sheet_set == NULL);

    g_warning(
      _("Failed to read the ACL for the root node: %s\n"
        "In order not to compromise security all permissions have been "
        "revoked for all users. The infinote server is likely not very "
        "usable in this configuration, so please check the storage "
        "system, fix the problem and re-start the server."),
      error->message
    );

    g_error_free(error);

    /* Clear all existing sheets and add one sheet for the default user which
     * prohibits everything. */
    if(priv->root->acl != NULL)
      sheet_set = inf_acl_sheet_set_get_clear_sheets(priv->root->acl);
    else
      sheet_set = inf_acl_sheet_set_new();

    default_sheet =
      inf_acl_sheet_set_add_sheet(sheet_set, &default_account->account);
    default_sheet->mask = INF_ACL_MASK_ALL;
    inf_acl_mask_clear(&default_sheet->perms);

    /* Make sure this "revoke-everything" sheet is not written to disk. */
    priv->root->acl = inf_acl_sheet_set_merge_sheets(
      priv->root->acl,
      sheet_set
    );

    infd_directory_announce_acl_sheets(
      directory,
      priv->root,
      NULL, /* TODO: make a request for this? */
      sheet_set,
      NULL
    );
  }
  else
  {
    /* Make sure there are permissions for the default user defined. If there
     * are not, use sensible default permissions. */
    default_sheet =
      inf_acl_sheet_set_add_sheet(sheet_set, &default_account->account);

    default_mask = default_sheet->mask;
    inf_acl_mask_neg(&default_mask, &tmp_mask);
    inf_acl_mask_and(&tmp_mask, &INF_ACL_MASK_DEFAULT, &tmp_mask);

    inf_acl_mask_or(&default_sheet->perms, &tmp_mask, &default_sheet->perms);

    default_sheet->mask = INF_ACL_MASK_ALL;

    /* Note that the sheets array already includes sheets that clear the
     * existing ACL. */
    priv->root->acl = inf_acl_sheet_set_merge_sheets(
      priv->root->acl,
      sheet_set
    );

    infd_directory_announce_acl_sheets(
      directory,
      priv->root,
      NULL, /* TODO: make a request for this? */
      sheet_set,
      NULL
    );

    inf_acl_mask_and(&default_mask, &INF_ACL_MASK_ALL, &default_mask);
    if(!inf_acl_mask_equal(&default_mask, &INF_ACL_MASK_ALL))
      infd_directory_write_acl(directory, priv->root);
  }

  inf_acl_sheet_set_free(sheet_set);
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
  InfdDirectoryPrivate* priv;
  InfdSessionProxy* proxy;
  
  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(
    inf_communication_group_get_target(INF_COMMUNICATION_GROUP(g)) == NULL
  );

  proxy = INFD_SESSION_PROXY(
    g_object_new(
      INFD_TYPE_SESSION_PROXY,
      "io", priv->io,
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
    INF_COMMUNICATION_GROUP(sync_g),
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

/* Called after a session proxy has been created for a newly added node.
 * It attempts to store the node in the storage. If it cannot be stored, the
 * function fails and unrefs the proxy. */
static gboolean
infd_directory_session_proxy_ensure(InfdDirectory* directory,
                                    InfdDirectoryNode* parent,
                                    const gchar* name,
                                    const InfdNotePlugin* plugin,
                                    InfdSessionProxy* proxy,
                                    GError** error)
{
  InfdDirectoryPrivate* priv;
  gchar* path;
  InfSession* session;
  gboolean ret;
  InfCommunicationGroup* sub_group;
  InfCommunicationGroup* sync_group;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  if(priv->storage != NULL)
  {
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

    /* Unset modified flag since we just wrote the buffer contents into
     * storage. */
    if(ret == TRUE)
      inf_buffer_set_modified(inf_session_get_buffer(session), FALSE);
  }
  else
  {
    ret = TRUE;
  }

  if(ret == FALSE)
  {
    /* Reset communication groups for the proxy, to avoid a warning at
     * final unref. Due do this failing the groups are very likely going to be
     * unrefed as well any time soon. */
    sub_group = inf_session_get_subscription_group(session);
    inf_communication_group_set_target(sub_group, NULL);

    g_object_get(G_OBJECT(session), "sync-group", &sync_group, NULL);
    if(sync_group != NULL && sync_group != sub_group)
      inf_communication_group_set_target(sync_group, NULL);
    g_object_unref(sync_group);

    g_object_unref(proxy);
  }

  g_object_unref(session);
  return ret;
}

/* Links a InfdSessionProxy with a InfdDirectoryNode */
static void
infd_directory_node_link_session(InfdDirectory* directory,
                                 InfdDirectoryNode* node,
                                 InfdRequest* request,
                                 InfdSessionProxy* proxy)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(node->shared.note.session == NULL ||
           (node->shared.note.session == proxy &&
            node->shared.note.weakref == TRUE));

  iter.node = node;
  iter.node_id = node->id;

  inf_browser_subscribe_session(
    INF_BROWSER(directory),
    &iter,
    INF_SESSION_PROXY(proxy),
    INF_REQUEST(request)
  );
}

static void
infd_directory_node_unlink_session(InfdDirectory* directory,
                                   InfdDirectoryNode* node,
                                   InfdRequest* request)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(node->shared.note.session != NULL);

  iter.node = node;
  iter.node_id = node->id;

  inf_browser_unsubscribe_session(
    INF_BROWSER(directory),
    &iter,
    INF_SESSION_PROXY(node->shared.note.session),
    INF_REQUEST(request)
  );
}

/* Notes are saved into the storage when save_notes is TRUE. */
static void
infd_directory_node_unlink_child_sessions(InfdDirectory* directory,
                                          InfdDirectoryNode* node,
                                          InfdRequest* request,
                                          gboolean save_notes)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* child;
  gchar* path;
  GError* error;
  InfSession* session;

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
          request,
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

        if(priv->storage != NULL)
        {
          g_object_get(
            G_OBJECT(node->shared.note.session),
            "session", &session,
            NULL
          );

          node->shared.note.plugin->session_write(
            priv->storage,
            session,
            path,
            node->shared.note.plugin->user_data,
            &error
          );

          g_object_unref(session);
        }

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

      if(node->shared.note.weakref == FALSE)
        infd_directory_node_unlink_session(directory, node, request);
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

/* This function takes ownership of name. If write_acl the ACL is written to
 * the storage. This should be used for newly created nodes, but for nodes
 * read from storage it should be false, since it is pointless to write
 * the ACL again. */
static InfdDirectoryNode*
infd_directory_node_new_common(InfdDirectory* directory,
                               InfdDirectoryNode* parent,
                               InfdStorageNodeType type,
                               guint node_id,
                               gchar* name,
                               const InfAclSheetSet* sheet_set,
                               gboolean write_acl)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfBrowserIter iter;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(
    g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id)) == NULL
  );

  node = g_slice_new(InfdDirectoryNode);
  node->parent = parent;
  node->type = type;
  node->id = node_id;
  node->name = name;
  node->acl = NULL;
  node->acl_connections = NULL;

  if(sheet_set != NULL)
  {
    node->acl = inf_acl_sheet_set_merge_sheets(node->acl, sheet_set);
    if(write_acl == TRUE)
      infd_directory_write_acl(directory, node);
  }

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
                                     gchar* name,
                                     const InfAclSheetSet* sheet_set,
                                     gboolean write_acl)
{
  InfdDirectoryNode* node;

  node = infd_directory_node_new_common(
    directory,
    parent,
    INFD_STORAGE_NODE_SUBDIRECTORY,
    node_id,
    name,
    sheet_set,
    write_acl
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
                             const InfAclSheetSet* sheet_set,
                             gboolean write_acl,
                             const InfdNotePlugin* plugin)
{
  InfdDirectoryNode* node;

  node = infd_directory_node_new_common(
    directory,
    parent,
    INFD_STORAGE_NODE_NOTE,
    node_id,
    name,
    sheet_set,
    write_acl
  );

  node->shared.note.session = NULL;
  node->shared.note.plugin = plugin;
  node->shared.note.save_timeout = NULL;
  node->shared.note.weakref = FALSE;

  return node;
}

/* Required by infd_directory_node_free() */
static void
infd_directory_remove_sync_in(InfdDirectory* directory,
                              InfdDirectorySyncIn* sync_in);

static void
infd_directory_remove_subreq(InfdDirectory* directory,
                             InfdDirectorySubreq* request);

static void
infd_directory_node_free(InfdDirectory* directory,
                         InfdDirectoryNode* node)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;
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
    /* Sessions must have been explicitely unlinked before; we might still
     * have weak references though. */
    g_assert(node->shared.note.session == NULL ||
             node->shared.note.weakref == TRUE);

    if(node->shared.note.session != NULL)
    {
      infd_directory_release_session(
        directory,
        node,
        node->shared.note.session
      );
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }

  if(node->parent != NULL)
    infd_directory_node_unlink(node);
  g_slist_free(node->acl_connections);

  /* Only clear ACL table after unlink, so that ACL has effect until the very
   * moment where the node does not exist anymore, to avoid possible races. */
  if(node->acl != NULL)
    inf_acl_sheet_set_free(node->acl);

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

  /* Remove the connection from ACL connections of ourselves and all
   * children. Do not recurse, since the recursion has taken place
   * in the loop above only for explored subdirectories. */
  node->acl_connections = g_slist_remove(node->acl_connections, connection);
  for(child = node->shared.subdir.child;
      child != NULL;
      child = child->next)
  {
    child->acl_connections = g_slist_remove(
      child->acl_connections,
      connection
    );
  }
}

/*
 * Permission enforcement
 */

/* Return the permissions needed to create a new node */
static void
infd_directory_get_add_node_permissions(InfdDirectory* directory,
                                        InfAclMask* out,
                                        gboolean subdirectory,
                                        gboolean initial_subscribe,
                                        gboolean sync_in,
                                        const InfAclSheetSet* sheet_set)
{
  if(subdirectory)
    inf_acl_mask_set1(out, INF_ACL_CAN_ADD_SUBDIRECTORY);
  else
    inf_acl_mask_set1(out, INF_ACL_CAN_ADD_DOCUMENT);

  if(initial_subscribe == TRUE)
    inf_acl_mask_or1(out, INF_ACL_CAN_SUBSCRIBE_SESSION);
  if(sync_in == TRUE)
    inf_acl_mask_or1(out, INF_ACL_CAN_SYNC_IN);

  if(sheet_set != NULL && sheet_set->n_sheets > 0)
    inf_acl_mask_or1(out, INF_ACL_CAN_SET_ACL);
}

/* Enforces the ACL on the given node. If the exploration permission is no
 * longer given for this node, then explorations and subscriptions are also
 * removed from all children. Returns whether node is still explored by
 * the connection.
 *
 * Note this function does not enforce all ACLs recursively! */
static gboolean
infd_directory_enforce_single_acl(InfdDirectory* directory,
                                  InfXmlConnection* connection,
                                  InfdDirectoryNode* node,
                                  gboolean is_explored)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryConnectionInfo* info;
  const InfAclAccount* account;

  InfBrowser* browser;
  InfBrowserIter iter;
  InfAclMask mask;
  xmlNodePtr child_xml;
  InfdDirectoryNode* child;
  InfdSessionProxy* proxy;
  GSList* item;
  GSList* next;
  InfdDirectorySubreq* subreq;
  InfdDirectorySyncIn* sync_in;
  InfXmlConnection* sync_in_connection;
  gboolean retval;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  info = g_hash_table_lookup(priv->connections, connection);
  g_assert(info != NULL);
  account = &info->account->account;

  browser = INF_BROWSER(directory);
  iter.node = node;
  iter.node_id = node->id;

  retval = TRUE;
  if(node->type == INFD_STORAGE_NODE_SUBDIRECTORY)
  {
    if(g_slist_find(node->shared.subdir.connections, connection) != NULL)
    {
      /* Remove exploration if new account does not have permission, or
       * if one of the parent folders is no longer explored */
      inf_acl_mask_set1(&mask, INF_ACL_CAN_EXPLORE_NODE);
      if(!is_explored ||
         !inf_browser_check_acl(browser, &iter, account, &mask, NULL))
      {
        node->shared.subdir.connections =
          g_slist_remove(node->shared.subdir.connections, connection);
        retval = FALSE;

        /* If there are subscription requests to create a node into this node
         * from this connection, then mark them as canceled, so that we don't
         * create the node in handle_subscribe_ack(). The client might
         * actually create the node if it does not register the new ACL early
         * enough, but that's okay because it removes it again as
         * soon as it does. */
        for(item = priv->subscription_requests; item != NULL; item = next)
        {
          next = item->next;
          subreq = (InfdDirectorySubreq*)item->data;

          switch(subreq->type)
          {
          case INFD_DIRECTORY_SUBREQ_CHAT:
          case INFD_DIRECTORY_SUBREQ_SESSION:
            break;
          case INFD_DIRECTORY_SUBREQ_ADD_NODE:
            if(subreq->connection == connection)
              if(subreq->shared.add_node.parent->id == node->id)
                subreq->shared.add_node.parent = NULL;
            break;
          case INFD_DIRECTORY_SUBREQ_SYNC_IN:
          case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
            if(subreq->connection == connection)
              if(subreq->shared.sync_in.parent->id == node->id)
                subreq->shared.sync_in.parent = NULL;
            break;
          default:
            g_assert_not_reached();
            break;
          }
        }

        /* Remove sync-ins whose parent is gone */
        for(item = priv->sync_ins; item != NULL; item = next)
        {
          next = item->next;
          sync_in = (InfdDirectorySyncIn*)item->data;

          g_object_get(
            G_OBJECT(sync_in->request),
            "requestor", &sync_in_connection,
            NULL
          );

          if(sync_in_connection == connection && sync_in->parent == node)
            infd_directory_remove_sync_in(directory, sync_in);

          g_object_unref(sync_in_connection);
        }

        for(child = node->shared.subdir.child;
            child != NULL;
            child = child->next)
        {
          infd_directory_enforce_single_acl(
            directory,
            connection,
            child,
            FALSE
          );
        }
      }
    }
    else
    {
      retval = FALSE;
    }
  }
  else
  {
    retval = FALSE;
    proxy = node->shared.note.session;
    if(proxy != NULL)
    {
      if(infd_session_proxy_is_subscribed(proxy, connection))
      {
        /* Remove subscription if no longer allowed, or if parent directory
         * is no longer explored */
        inf_acl_mask_set1(&mask, INF_ACL_CAN_SUBSCRIBE_SESSION);
        if(!is_explored ||
           !inf_browser_check_acl(browser, &iter, account, &mask, NULL))
        {
          infd_session_proxy_unsubscribe(proxy, connection);
        }
        else
        {
          /* TODO: Remove joined users if join-user
           * permissions are no longer granted. */
        }
      }
    }
  }

  if(g_slist_find(node->acl_connections, connection) != NULL)
  {
    inf_acl_mask_set1(&mask, INF_ACL_CAN_QUERY_ACL);
    if(!is_explored ||
       !inf_browser_check_acl(browser, &iter, account, &mask, NULL))
    {
      node->acl_connections =
        g_slist_remove(node->acl_connections, connection);
    }
    else
    {
      /* Also remove it if the INF_ACL_CAN_QUERY_ACCOUNT_LIST flag was
       * removed on the root node, since the account list is required to
       * query ACLs. */
      iter.node = priv->root;
      iter.node_id = priv->root->id;
      inf_acl_mask_set1(&mask, INF_ACL_CAN_QUERY_ACCOUNT_LIST);
      if(!inf_browser_check_acl(browser, &iter, account, &mask, NULL))
      {
        node->acl_connections =
          g_slist_remove(node->acl_connections, connection);
      }
    }
  }

  return retval;
}

/* Enforce ACL for the given node and all its children for info. If
 * reply_xml is set, then fill it with information about ACL for all
 * nodes for info's account. This is used when a connection is
 * switching accounts. */
static void
infd_directory_enforce_acl(InfdDirectory* directory,
                           InfXmlConnection* conn,
                           InfdDirectoryNode* node,
                           xmlNodePtr reply_xml)
{
  InfdDirectoryPrivate* priv;
  InfBrowser* browser;
  InfBrowserIter iter;
  InfAclMask mask;

  InfdDirectoryNode* child;
  InfdDirectoryConnectionInfo* info;
  const InfAclAccount* account;
  const InfAclSheet* sheet;
  xmlNodePtr child_xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  info = g_hash_table_lookup(priv->connections, conn);
  g_assert(info != NULL);
  account = &info->account->account;

  if(node == priv->root)
  {
    browser = INF_BROWSER(directory);
    iter.node = priv->root;
    iter.node_id = priv->root->id;
    inf_acl_mask_set1(&mask, INF_ACL_CAN_QUERY_ACCOUNT_LIST);
    if(!inf_browser_check_acl(browser, &iter, account, &mask, NULL))
    {
      priv->account_list_connections =
        g_slist_remove(priv->account_list_connections, conn);
    }
  }

  if(infd_directory_enforce_single_acl(directory, conn, node, TRUE) == TRUE)
  {
    g_assert(node->type == INFD_STORAGE_NODE_SUBDIRECTORY);

    for(child = node->shared.subdir.child; child != NULL; child = child->next)
    {
      infd_directory_enforce_acl(directory, conn, child, reply_xml);
    }
  }

  /* If this node has ACLs set for the new account, then add this to the
   * reply XML, so that the remote host knows its own permissions
   * on the node */
  if(reply_xml != NULL)
  {
    if(node->acl != NULL)
    {
      sheet = inf_acl_sheet_set_find_const_sheet(node->acl, account);
      if(sheet != NULL)
      {
        child_xml = xmlNewChild(reply_xml, NULL, (const xmlChar*)"acl", NULL);
        inf_xml_util_set_attribute_uint(child_xml, "node-id", node->id);
        inf_acl_sheet_perms_to_xml(&sheet->mask, &sheet->perms, child_xml);
      }
    }
  }
}

static void
infd_directory_change_acl_account(InfdDirectory* directory,
                                  InfXmlConnection* connection,
                                  const InfdAclAccountInfo* new_account)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryConnectionInfo* info;
  gboolean is_default_account;
  gboolean is_acl_account_list_connection;
  xmlNodePtr xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(
    g_hash_table_lookup(
      priv->accounts,
      new_account->account.id
    ) == new_account
  );

  /* Set new account */
  info = g_hash_table_lookup(priv->connections, connection);
  g_assert(info != NULL);

  if(info->account == new_account) return;
  info->account = new_account;

  /* Check whether we need to transfer ACLs for the new account to the
   * connection. */
  is_default_account = FALSE;
  if(strcmp(new_account->account.id, "default") == 0)
    is_default_account = TRUE;

  is_acl_account_list_connection = FALSE;
  if(g_slist_find(priv->account_list_connections, connection) != NULL)
    is_acl_account_list_connection = TRUE;

  xml = xmlNewNode(NULL, (const xmlChar*)"change-acl-account");
  inf_acl_account_to_xml(&new_account->account, xml);

  /* Enforce the ACLs of the new account on the connection. While
   * we do this, we also fill in the ACL sheets for all nodes of the
   * new connection, to be transferred. */
  infd_directory_enforce_acl(
    directory,
    connection,
    priv->root,
    (is_default_account || is_acl_account_list_connection) ? NULL : xml
  );

  /* Send to client */
  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    xml
  );
}

static void
infd_directory_remove_acl_sheet_from_sheet_set(InfAclSheetSet* sheet_set,
                                               const InfAclAccount* account)
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
infd_directory_remove_account_from_sheets(InfdDirectory* directory,
                                          InfdDirectoryNode* node,
                                          const InfdAclAccountInfo* account)
{
  InfdDirectoryNode* child;
  InfAclSheet* sheet;
  InfAclSheetSet sheet_set;
  InfAclSheet announce_sheet;
  InfBrowserIter iter;

  if(node->type == INFD_STORAGE_NODE_SUBDIRECTORY &&
     node->shared.subdir.explored == TRUE)
  {
    for(child = node->shared.subdir.child; child != NULL; child = child->next)
    {
      infd_directory_remove_account_from_sheets(directory, child, account);
    }
  }

  if(node->acl != NULL)
  {
    sheet = inf_acl_sheet_set_find_sheet(node->acl, &account->account);
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

      inf_browser_acl_changed(
        INF_BROWSER(directory),
        &iter,
        &sheet_set,
        NULL
      );
    }
  }
}

static gnutls_x509_crt_t
infd_directory_create_certificate_from_crq(InfdDirectory* directory,
                                           gnutls_x509_crq_t crq,
                                           guint64 validity,
                                           GError** error)
{
  InfdDirectoryPrivate* priv;
  gnutls_x509_crt_t cert;
  int res;
  guint64 timestamp;
  gchar serial_buffer[5];

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->certificate == NULL || priv->private_key == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_OPERATION_UNSUPPORTED,
      "%s",
      _("Server does not support issuing certificates")
    );

    return NULL;
  }

  res = gnutls_x509_crt_init(&cert);
  if(res != GNUTLS_E_SUCCESS)
  {
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = gnutls_x509_crt_set_crq(cert, crq);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  timestamp = time(NULL);
  serial_buffer[4] = (timestamp      ) & 0xff;
  serial_buffer[3] = (timestamp >>  8) & 0xff;
  serial_buffer[2] = (timestamp >> 16) & 0xff;
  serial_buffer[1] = (timestamp >> 24) & 0xff;
  serial_buffer[0] = (timestamp >> 32) & 0xff;

  res = gnutls_x509_crt_set_serial(cert, serial_buffer, 5);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  /* Set the activation time a bit in the past, so that if the client
   * checks the certificate and has its clock slightly offset it doesn't
   * find the certificate invalid. */
  res = gnutls_x509_crt_set_activation_time(cert, timestamp - DAYS / 10);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = gnutls_x509_crt_set_expiration_time(cert, timestamp + validity);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = gnutls_x509_crt_set_basic_constraints(cert, 0, -1);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = gnutls_x509_crt_set_key_usage(cert, GNUTLS_KEY_DIGITAL_SIGNATURE);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  res = gnutls_x509_crt_set_version(cert, 3);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  /* The certificate is now set up, we can sign it. */
  res = gnutls_x509_crt_sign2(
    cert,
    inf_certificate_chain_get_own_certificate(priv->certificate),
    priv->private_key,
    GNUTLS_DIG_SHA256,
    0
  );

  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return NULL;
  }

  return cert;
}

static InfdAclAccountInfo*
infd_directory_create_acl_account_with_certificates(InfdDirectory* directory,
                                                    const gchar* account_id,
                                                    const gchar* account_name,
                                                    gboolean transient,
                                                    gnutls_x509_crt_t* certs,
                                                    guint n_certs,
                                                    InfXmlConnection* conn,
                                                    GError** error)
{
  InfdDirectoryPrivate* priv;
  guint i;
  gchar* fingerprint;
  InfdAclAccountInfo* info;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(g_hash_table_lookup(priv->accounts, account_id) != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_DUPLICATE_ACCOUNT,
      _("Account with ID \"%s\" exists already"),
      account_id
    );

    return NULL;
  }

  info = infd_acl_account_info_new(account_id, account_name, transient);

  /* Check that fingerprints do not overlap. */
  for(i = 0; i < n_certs; ++i)
  {
    /* TODO: Switch to DN */
    fingerprint = inf_cert_util_get_fingerprint(certs[i], GNUTLS_DIG_SHA256);
    if(g_hash_table_lookup(priv->accounts_by_certificate, fingerprint) != NULL)
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_DUPLICATE_ACCOUNT,
        "%s",
        _("The fingerprint of the certificate overlaps with another "
          "certificate. This is possible, but highly unlikely. Please "
          "try creating the certificate again and the error should disappear.")
      );

      g_free(fingerprint);
      infd_acl_account_info_free(info);
      return NULL;
    }

    infd_acl_account_info_add_certificate(info, fingerprint);
    g_free(fingerprint);
  }

  g_hash_table_insert(priv->accounts, info->account.id, info);

  for(i = 0; i < info->n_certificates; ++i)
  {
    g_hash_table_insert(
      priv->accounts_by_certificate,
      info->certificates[i],
      info
    );
  }

  infd_directory_announce_acl_account(directory, info, conn);
  if(transient == FALSE)
    infd_directory_write_account_list(directory);

  return info;
}

static gboolean
infd_directory_account_info_from_certificate(gnutls_x509_crt_t cert,
                                             gchar** account_id,
                                             gchar** account_name,
                                             GError** error)
{
  gchar* name;

  /* Check that a common name is set in the certificate. Without a common
   * name, we cannot associate the certificate to an account. */
  name = inf_cert_util_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME, 0);
  if(name == NULL || name[0] == '\0')
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_INVALID_CERTIFICATE,
      "%s",
      _("The certificate request has no common name set")
    );

    g_free(name);
    return FALSE;
  }

  *account_id = g_strdup_printf("user:%s", name);
  *account_name = name;

  return TRUE;
}

static InfdAclAccountInfo*
infd_directory_create_acl_account_with_certificate(InfdDirectory* directory,
                                                   const gchar* account_id,
                                                   const gchar* account_name,
                                                   gnutls_x509_crt_t cert,
                                                   InfXmlConnection* conn,
                                                   GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdAclAccountInfo* info;
  gchar* fingerprint;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  info = g_hash_table_lookup(priv->accounts, account_id);

  /* If there is a certificate with the same name, replace its
   * certificates with the new certificate. */
  if(info != NULL)
  {
    while(info->n_certificates > 0)
    {
      g_hash_table_remove(
        priv->accounts_by_certificate,
        info->certificates[0]
      );

      infd_acl_account_info_remove_certificate(info, info->certificates[0]);
    }

    fingerprint = inf_cert_util_get_fingerprint(cert, GNUTLS_DIG_SHA256);
    infd_acl_account_info_add_certificate(info, fingerprint);
    g_free(fingerprint);

    g_hash_table_insert(
      priv->accounts_by_certificate,
      info->certificates[0],
      info
    );

    infd_directory_write_account_list(directory);
  }
  else
  {
    info = infd_directory_create_acl_account_with_certificates(
      directory,
      account_id,
      account_name,
      FALSE,
      &cert,
      1,
      conn,
      error
    );
  }

  return info;
}

static void
infd_directory_remove_acl_account(InfdDirectory* directory,
                                  InfdAclAccountInfo* account,
                                  const gchar* seq,
                                  InfdRequest* request)
{
  InfdDirectoryPrivate* priv;
  GHashTableIter iter;
  gpointer key;
  gpointer value;
  InfdDirectoryConnectionInfo* info;
  const InfdAclAccountInfo* default_account;
  GSList* notify_connections;
  GSList* item;
  InfdDirectorySyncIn* sync_in;
  InfdDirectorySubreq* subreq;
  xmlNodePtr xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(
    g_hash_table_lookup(priv->accounts, account->account.id) == account
  );

  default_account = g_hash_table_lookup(priv->accounts, "default");
  g_assert(default_account != NULL);
  g_assert(account != default_account);

  /* First, demote all connections with this account to the default account,
   * and make a list of connections that need to be notified about the removed
   * account. */
  notify_connections = NULL;
  g_hash_table_iter_init(&iter, priv->connections);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {
    info = (InfdDirectoryConnectionInfo*)value;
    if(info->account == account)
    {
      info->account = default_account;

      infd_directory_enforce_acl(
        directory,
        (InfXmlConnection*)key,
        priv->root,
        NULL
      );

      notify_connections = g_slist_prepend(notify_connections, key);
    }
    else if(g_slist_find(priv->account_list_connections, key) != NULL)
    {
      notify_connections = g_slist_prepend(notify_connections, key);
    }
  }

  /* Next, remove this account from all ACL sheets. Note that we do not do
   * this for sheets that we do not have explored yet. The permissions will
   * be dropped automatically as soon as the node is explored. Note that this
   * is a bit of a security issue at the moment: if an account with the
   * previous ID is re-created, then the existing permissions of non-explored
   * nodes are taken for the new account. */
  /* TODO: This should be fixed in one way or another. */
  infd_directory_remove_account_from_sheets(directory, priv->root, account);

  /* Remove ACL sheet from sync-ins and subscription requests. */
  for(item = priv->sync_ins; item != NULL; item = item->next)
  {
    sync_in = (InfdDirectorySyncIn*)item->data;

    infd_directory_remove_acl_sheet_from_sheet_set(
      sync_in->sheet_set,
      &account->account
    );
  }

  for(item = priv->subscription_requests; item != NULL; item = item->next)
  {
    subreq = (InfdDirectorySubreq*)item->data;

    switch(subreq->type)
    {
    case INFD_DIRECTORY_SUBREQ_CHAT:
    case INFD_DIRECTORY_SUBREQ_SESSION:
      break;
    case INFD_DIRECTORY_SUBREQ_ADD_NODE:
      infd_directory_remove_acl_sheet_from_sheet_set(
        subreq->shared.add_node.sheet_set,
        &account->account
      );

      break;
    case INFD_DIRECTORY_SUBREQ_SYNC_IN:
    case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
      infd_directory_remove_acl_sheet_from_sheet_set(
        subreq->shared.sync_in.sheet_set,
        &account->account
      );

      break;
    default:
      g_assert_not_reached();
      break;
    }
  }

  /* Then, send requests */
  if(notify_connections != NULL)
  {
    xml = xmlNewNode(NULL, (const xmlChar*)"remove-acl-account");
    inf_xml_util_set_attribute(xml, "id", account->account.id);
    if(seq != NULL) inf_xml_util_set_attribute(xml, "seq", seq);

    for(item = notify_connections; item != NULL; item = item->next)
    {
      inf_communication_group_send_message(
        INF_COMMUNICATION_GROUP(priv->group),
        (InfXmlConnection*)item->data,
        (item->next == NULL) ? xml : xmlCopyNode(xml, 1)
      );
    }
  }

  g_slist_free(notify_connections);

  /* Then, make callback */
  g_hash_table_steal(priv->accounts, account->account.id);

  if(request != NULL)
  {
    inf_request_finish(
      INF_REQUEST(request),
      inf_request_result_make_remove_acl_account(
        INF_BROWSER(directory),
        &account->account
      )
    );
  }

  inf_browser_acl_account_removed(
    INF_BROWSER(directory),
    &account->account,
    NULL
  );

  infd_acl_account_info_free(account);
}

/*
 * Node synchronization.
 */

static xmlNodePtr
infd_directory_node_desc_register_to_xml(const gchar* node_name,
                                         guint node_id,
                                         InfdDirectoryNode* parent,
                                         const InfdNotePlugin* plugin,
                                         const gchar* name)
{
  xmlNodePtr xml;

  xml = xmlNewNode(NULL, (const xmlChar*)node_name);

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
    "add-node",
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

static gboolean
infd_directory_make_seq(InfdDirectory* directory,
                        InfXmlConnection* connection,
                        xmlNodePtr xml,
                        gchar** seq,
                        GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryConnectionInfo* info;
  GError* local_error;
  guint seq_num;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  local_error = NULL;
  if(!inf_xml_util_get_attribute_uint(xml, "seq", &seq_num, &local_error))
  {
    if(local_error)
    {
      g_propagate_error(error, local_error);
      return FALSE;
    }

    *seq = NULL;
    return TRUE;
  }

  info = g_hash_table_lookup(priv->connections, connection);
  g_assert(info != NULL);

  *seq = g_strdup_printf("%u/%u", info->seq_id, seq_num);
  return TRUE;
}

/* Announces the presence of a new node. This is not done in
 * infd_directory_node_new because we do not want to do this for all
 * nodes we create (namely not for the root node). */
static void
infd_directory_node_register(InfdDirectory* directory,
                             InfdDirectoryNode* node,
                             InfdRequest* request,
                             InfXmlConnection* except,
                             const gchar* seq)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;
  InfdDirectoryConnectionInfo* info;
  xmlNodePtr xml;
  xmlNodePtr copy_xml;
  GSList* item;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  iter.node_id = node->id;
  iter.node = node;

  inf_browser_node_added(
    INF_BROWSER(directory),
    &iter,
    INF_REQUEST(request)
  );

  xml = infd_directory_node_register_to_xml(node);
  if(seq != NULL)
   inf_xml_util_set_attribute(xml, "seq", seq);

  for(item = node->parent->shared.subdir.connections;
      item != NULL;
      item = g_slist_next(item))
  {
    if(item->data != except)
    {
      info = g_hash_table_lookup(priv->connections, item->data);
      g_assert(info != NULL);

      copy_xml = xmlCopyNode(xml, 1);

      if(node->acl != NULL)
      {
        infd_directory_acl_sheets_to_xml_for_connection(
          directory,
          node->acl_connections,
          node->acl,
          INF_XML_CONNECTION(item->data),
          copy_xml
        );
      }

      inf_communication_group_send_message(
        INF_COMMUNICATION_GROUP(priv->group),
        INF_XML_CONNECTION(item->data),
        copy_xml
      );
    }
  }

  xmlFreeNode(xml);
}

/* Announces that a node is removed. Again, this is not done in
 * infd_directory_node_free because we do not want to do this for
 * every subnode if a subdirectory is freed. */
static void
infd_directory_node_unregister(InfdDirectory* directory,
                               InfdDirectoryNode* node,
                               InfdRequest* request,
                               const gchar* seq)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;
  xmlNodePtr xml;
  GSList* item;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  iter.node_id = node->id;
  iter.node = node;

  inf_browser_node_removed(
    INF_BROWSER(directory),
    &iter,
    INF_REQUEST(request)
  );

  xml = infd_directory_node_unregister_to_xml(node);
  if(seq != NULL) inf_xml_util_set_attribute(xml, "seq", seq);

  for(item = node->parent->shared.subdir.connections;
      item != NULL;
      item = g_slist_next(item))
  {
    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      INF_XML_CONNECTION(item->data),
      xmlCopyNode(xml, 1)
    );
  }

  xmlFreeNode(xml);
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
  /* Synchronization failed. We simply remove the sync-in. There is no further
   * notification required since the synchronization failed on the remote site
   * as well. */
  InfdDirectorySyncIn* sync_in;
  InfdRequest* request;

  sync_in = (InfdDirectorySyncIn*)user_data;
  request = sync_in->request;

  g_object_ref(request);
  infd_directory_remove_sync_in(sync_in->directory, sync_in);
  inf_request_fail(INF_REQUEST(request), error);
  g_object_unref(request);
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
  InfBrowserIter iter;
  InfBrowserIter parent;
  InfdRequest* request;
  InfdSessionProxy* proxy;
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
    sync_in->sheet_set,
    TRUE,
    sync_in->plugin
  );

  sync_in->name = NULL; /* Don't free, we passed ownership */
  request = sync_in->request;
  g_object_ref(request);
  proxy = sync_in->proxy;
  g_object_ref(proxy);

  parent.node_id = sync_in->parent->id;
  parent.node = sync_in->parent;
  infd_directory_remove_sync_in(directory, sync_in);

  /* Don't send to conn since the completed synchronization already lets the
   * remote site know that the node was inserted. */
  infd_directory_node_register(directory, node, request, conn, NULL);
  infd_directory_node_link_session(directory, node, request, proxy);

  /* Save session initially */
  infd_directory_node_get_path(node, &path, NULL);

  error = NULL;

  if(priv->storage != NULL)
  {
    ret = sync_in->plugin->session_write(
      priv->storage,
      session,
      path,
      sync_in->plugin->user_data,
      &error
    );
  }
  else
  {
    ret = TRUE;
  }

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

  iter.node_id = node->id;
  iter.node = node;

  inf_request_finish(
    INF_REQUEST(request),
    inf_request_result_make_add_node(INF_BROWSER(directory), &parent, &iter)
  );

  g_object_unref(request);
}

static InfdDirectorySyncIn*
infd_directory_add_sync_in(InfdDirectory* directory,
                           InfdDirectoryNode* parent,
                           InfdRequest* request,
                           guint node_id,
                           const gchar* name,
                           const InfAclSheetSet* sheet_set,
                           const InfdNotePlugin* plugin,
                           InfdSessionProxy* proxy)
{
  InfdDirectoryPrivate* priv;
  InfdDirectorySyncIn* sync_in;
  InfSession* session;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  sync_in = g_slice_new(InfdDirectorySyncIn);

  sync_in->directory = directory;
  sync_in->parent = parent;
  sync_in->node_id = node_id;
  sync_in->name = g_strdup(name);
  if(sheet_set != NULL)
    sync_in->sheet_set = inf_acl_sheet_set_copy(sheet_set);
  else
    sync_in->sheet_set = NULL;
  sync_in->plugin = plugin;
  sync_in->proxy = proxy;
  sync_in->request = request;

  g_object_ref(sync_in->proxy);
  g_object_ref(sync_in->request);
  
  g_object_get(G_OBJECT(proxy), "session", &session, NULL);

  /* Connect after the default handler, so that the session status has changed
   * before our callbacks are called. This makes sure that the session status
   * is RUNNING when we emit the "subscribe-session" signal. */
  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-failed",
    G_CALLBACK(infd_directory_sync_in_synchronization_failed_cb),
    sync_in
  );

  g_signal_connect_after(
    G_OBJECT(session),
    "synchronization-complete",
    G_CALLBACK(infd_directory_sync_in_synchronization_complete_cb),
    sync_in
  );

  g_object_unref(session);

  priv->sync_ins = g_slist_prepend(priv->sync_ins, sync_in);
  return sync_in;
}

static void
infd_directory_remove_sync_in(InfdDirectory* directory,
                              InfdDirectorySyncIn* sync_in)
{
  InfdDirectoryPrivate* priv;
  InfSession* session;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_object_get(G_OBJECT(sync_in->proxy), "session", &session, NULL);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(infd_directory_sync_in_synchronization_failed_cb),
    sync_in
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(infd_directory_sync_in_synchronization_complete_cb),
    sync_in
  );

  g_object_unref(session);

  /* This cancels the synchronization: */
  g_object_unref(sync_in->proxy);

  /* TODO: Fail request with a cancelled error? */
  g_object_unref(sync_in->request);

  if(sync_in->sheet_set != NULL)
    inf_acl_sheet_set_free(sync_in->sheet_set);
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
                                  InfdRequest* request,
                                  guint node_id,
                                  InfdSessionProxy* proxy)
{
  InfdDirectorySubreq* subreq;

  subreq = infd_directory_add_subreq_common(
    directory,
    INFD_DIRECTORY_SUBREQ_SESSION,
    connection,
    node_id
  );

  subreq->shared.session.session = proxy; /* take ownership */
  subreq->shared.session.request = request;

  if(request != NULL)
    g_object_ref(request);
  return subreq;
}

static InfdDirectorySubreq*
infd_directory_add_subreq_add_node(InfdDirectory* directory,
                                   InfXmlConnection* connection,
                                   InfCommunicationHostedGroup* group,
                                   InfdRequest* request,
                                   InfdDirectoryNode* parent,
                                   guint node_id,
                                   const gchar* name,
                                   const InfAclSheetSet* sheet_set,
                                   const InfdNotePlugin* plugin,
                                   InfSession* session,
                                   GError** error)
{
  InfdDirectorySubreq* subreq;
  InfdSessionProxy* proxy;
  gboolean ensured;

  if(session != NULL)
  {
    proxy = infd_directory_create_session_proxy_with_group(
      directory,
      session,
      group
    );
  }
  else
  {
    proxy = infd_directory_create_session_proxy(
      directory,
      plugin,
      INF_SESSION_RUNNING,
      NULL,
      NULL,
      group
    );
  }

  ensured = infd_directory_session_proxy_ensure(
    directory,
    parent,
    name,
    plugin,
    proxy,
    error
  );

  if(ensured == FALSE)
    return NULL;

  subreq = infd_directory_add_subreq_common(
    directory,
    INFD_DIRECTORY_SUBREQ_ADD_NODE,
    connection,
    node_id
  );

  subreq->shared.add_node.parent = parent;
  subreq->shared.add_node.group = group;
  subreq->shared.add_node.plugin = plugin;
  subreq->shared.add_node.name = g_strdup(name);
  if(sheet_set != NULL)
    subreq->shared.add_node.sheet_set = inf_acl_sheet_set_copy(sheet_set);
  else
    subreq->shared.add_node.sheet_set = NULL;
  subreq->shared.add_node.proxy = proxy;
  subreq->shared.add_node.request = request;

  g_object_ref(request);
  g_object_ref(group);
  return subreq;
}

static InfdDirectorySubreq*
infd_directory_add_subreq_sync_in(InfdDirectory* directory,
                                  InfXmlConnection* connection,
                                  InfCommunicationHostedGroup* sync_group,
                                  InfCommunicationHostedGroup* sub_group,
                                  InfdRequest* request,
                                  InfdDirectoryNode* parent,
                                  guint node_id,
                                  const gchar* name,
                                  const InfAclSheetSet* sheet_set,
                                  const InfdNotePlugin* plugin,
                                  GError** error)
{
  InfdDirectorySubreq* subreq;
  InfdSessionProxy* proxy;
  gboolean ensured;

  /* Keep proxy in PRESYNC state, until we have the confirmation from the
   * remote site that the chosen method is OK and we can go on. */
  proxy = infd_directory_create_session_proxy(
    directory,
    plugin,
    INF_SESSION_PRESYNC,
    sync_group,
    connection,
    sub_group
  );

  ensured = infd_directory_session_proxy_ensure(
    directory,
    parent,
    name,
    plugin,
    proxy,
    error
  );

  if(ensured == FALSE)
    return NULL;

  subreq = infd_directory_add_subreq_common(
    directory,
    sync_group == sub_group ?
      INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE :
      INFD_DIRECTORY_SUBREQ_SYNC_IN,
    connection,
    node_id
  );

  subreq->shared.sync_in.parent = parent;
  subreq->shared.sync_in.synchronization_group = sync_group;
  subreq->shared.sync_in.subscription_group = sub_group;
  subreq->shared.sync_in.plugin = plugin;
  subreq->shared.sync_in.name = g_strdup(name);
  if(sheet_set != NULL)
    subreq->shared.sync_in.sheet_set = inf_acl_sheet_set_copy(sheet_set);
  else
    subreq->shared.sync_in.sheet_set = NULL;
  subreq->shared.sync_in.proxy = proxy;
  subreq->shared.sync_in.request = request;

  g_object_ref(request);
  g_object_ref(sync_group);
  g_object_ref(sub_group);

  return subreq;
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
    if(request->shared.session.request != NULL)
      g_object_unref(request->shared.session.request);
    break;
  case INFD_DIRECTORY_SUBREQ_ADD_NODE:
    g_free(request->shared.add_node.name);
    if(request->shared.add_node.sheet_set != NULL)
      inf_acl_sheet_set_free(request->shared.add_node.sheet_set);
    g_object_unref(request->shared.add_node.group);
    g_object_unref(request->shared.add_node.proxy);
    /* TODO: Fail with some cancelled error? */
    g_object_unref(request->shared.add_node.request);
    break;
  case INFD_DIRECTORY_SUBREQ_SYNC_IN:
  case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
    g_free(request->shared.sync_in.name);
    if(request->shared.sync_in.sheet_set != NULL)
      inf_acl_sheet_set_free(request->shared.sync_in.sheet_set);
    g_object_unref(request->shared.sync_in.synchronization_group);
    g_object_unref(request->shared.sync_in.subscription_group);
    g_object_unref(request->shared.sync_in.proxy);
    /* TODO: Fail with some cancelled error? */
    g_object_unref(request->shared.sync_in.request);
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
infd_directory_find_subreq_by_node_id(InfdDirectory* directory,
                                      InfdDirectorySubreqType type,
                                      guint node_id)
{
  InfdDirectoryPrivate* priv;
  GSList* item;
  InfdDirectorySubreq* subreq;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  for(item = priv->subscription_requests; item != NULL; item = item->next)
  {
    subreq = (InfdDirectorySubreq*)item->data;
    if(subreq->type == type && subreq->node_id == node_id)
      return subreq;
  }

  return NULL;
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
  gboolean has_sensible_character = FALSE;
  const gchar* p;

  for (p = name; *p != '\0'; p = g_utf8_next_char(p))
  {
    if(!g_unichar_isprint(*p))
    {
      g_set_error(
        error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_INVALID_NAME,
        _("Name \"%s\" is an invalid name: contains non-printable characters"),
        name
      );

      return FALSE;
    }
    else if(!g_unichar_isspace(*p))
    {
      has_sensible_character = TRUE;
    }
  }

  if(!has_sensible_character)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_INVALID_NAME,
      _("Name \"%s\" is an invalid name: contains only space characters"),
      name
    );

    return FALSE;
  }

  if(strchr(name, '/') != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_INVALID_NAME,
      _("Name \"%s\" is an invalid name: contains \"/\""),
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
      _("A node with name \"%s\" exists already"),
      name
    );

    return FALSE;
  }

  return TRUE;
}

static gboolean
infd_directory_node_explore(InfdDirectory* directory,
                            InfdDirectoryNode* node,
                            InfdProgressRequest* request,
                            GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdStorageNode* storage_node;
  InfdDirectoryNode* new_node;
  InfBrowserIter iter;
  InfdNotePlugin* plugin;
  GError* local_error;
  GSList* list;
  InfAclSheetSet* sheet_set;
  GPtrArray* acls;
  GSList* item;
  gchar* path;
  gsize len;
  gsize node_len;
  gsize path_len;
  guint index;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(priv->storage != NULL);
  g_assert(node->type == INFD_STORAGE_NODE_SUBDIRECTORY);
  g_assert(node->shared.subdir.explored == FALSE);

  local_error = NULL;
  infd_directory_node_get_path(node, &path, &len);
  list = infd_storage_read_subdirectory(priv->storage, path, &local_error);

  if(local_error != NULL)
  {
    g_free(path);
    if(request != NULL) inf_request_fail(INF_REQUEST(request), local_error);
    g_propagate_error(error, local_error);
    return FALSE;
  }

  /* First pass: Count the total number of items and read the ACLs for each
   * node. If there is a problem reading the ACL for one node, cancel the
   * full exploration. */
  path_len = len;
  acls = g_ptr_array_sized_new(16);
  for(item = list; item != NULL; item = g_slist_next(item))
  {
    storage_node = (InfdStorageNode*)item->data;

    /* Construct the storage path for this node */
    node_len = strlen(storage_node->name);
    if(len + 1 + node_len < path_len)
    {
      path_len = len + 1 + node_len;
      path = g_realloc(path, path_len + 1);
    }

    path[len] = '/';
    memcpy(path + len + 1, storage_node->name, node_len + 1);

    /* Read ACL */
    sheet_set = infd_directory_read_acl(
      directory,
      path,
      NULL,
      &local_error
    );

    if(local_error != NULL)
    {
      for(index = 0; index < acls->len; ++index)
        inf_acl_sheet_set_free(g_ptr_array_index(acls, index));
      g_ptr_array_free(acls, TRUE);
      infd_storage_node_list_free(list);
      g_free(path);
      if(request != NULL) inf_request_fail(INF_REQUEST(request), local_error);
      g_propagate_error(error, local_error);
      return FALSE;
    }

    g_ptr_array_add(acls, sheet_set);
  }

  node->shared.subdir.explored = TRUE;

  g_free(path);
  if(request != NULL) infd_progress_request_initiated(request, acls->len);

  /* Second pass, fill the directory tree */
  index = 0;
  for(item = list, index = 0;
      item != NULL;
      item = g_slist_next(item), ++index)
  {
    storage_node = (InfdStorageNode*)item->data;
    sheet_set = (InfAclSheetSet*)g_ptr_array_index(acls, index);
    new_node = NULL;

    switch(storage_node->type)
    {
    case INFD_STORAGE_NODE_SUBDIRECTORY:
      new_node = infd_directory_node_new_subdirectory(
        directory,
        node,
        priv->node_counter++,
        g_strdup(storage_node->name),
        sheet_set,
        FALSE
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
          priv->node_counter++,
          g_strdup(storage_node->name),
          sheet_set,
          FALSE,
          plugin
        );
      }

      break;
    default:
      g_assert_not_reached();
      break;
    }

    inf_acl_sheet_set_free(sheet_set);

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
      infd_directory_node_register(
        directory,
        new_node,
        INFD_REQUEST(request),
        NULL,
        NULL
      );
    }

    if(request != NULL) infd_progress_request_progress(request);
  }

  g_ptr_array_free(acls, TRUE);

  if(request != NULL)
  {
    iter.node_id = node->id;
    iter.node = node;

    inf_request_finish(
      INF_REQUEST(request),
      inf_request_result_make_explore_node(INF_BROWSER(directory), &iter)
    );
  }

  infd_storage_node_list_free(list);

  return TRUE;
}

static InfdDirectoryNode*
infd_directory_node_add_subdirectory(InfdDirectory* directory,
                                     InfdDirectoryNode* parent,
                                     InfdRequest* request,
                                     const gchar* name,
                                     const InfAclSheetSet* sheet_set,
                                     InfXmlConnection* connection,
                                     const gchar* seq,
                                     GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  gboolean result;
  gchar* path;

  InfBrowserIter parent_iter;
  InfBrowserIter iter;

  g_assert(parent->type == INFD_STORAGE_NODE_SUBDIRECTORY);
  g_assert(parent->shared.subdir.explored == TRUE);
  g_assert(request != NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(!infd_directory_node_is_name_available(directory, parent, name, error))
  {
    return NULL;
  }
  else
  {
    infd_directory_node_make_path(parent, name, &path, NULL);

    if(priv->storage != NULL)
      result = infd_storage_create_subdirectory(priv->storage, path, error);
    else
      result = TRUE;

    g_free(path);
    if(result == FALSE) return NULL;

    node = infd_directory_node_new_subdirectory(
      directory,
      parent,
      priv->node_counter++,
      g_strdup(name),
      sheet_set,
      TRUE
    );

    node->shared.subdir.explored = TRUE;

    infd_directory_node_register(directory, node, request, NULL, seq);

    parent_iter.node_id = parent->id;
    parent_iter.node = parent;
    iter.node_id = node->id;
    iter.node = node;

    inf_request_finish(
      INF_REQUEST(request),
      inf_request_result_make_add_node(
        INF_BROWSER(directory),
        &parent_iter,
        &iter
      )
    );

    return node;
  }
}

static gboolean
infd_directory_node_add_note(InfdDirectory* directory,
                             InfdDirectoryNode* parent,
                             InfdRequest* request,
                             const gchar* name,
                             const InfAclSheetSet* sheet_set,
                             const InfdNotePlugin* plugin,
                             InfSession* session,
                             InfXmlConnection* connection,
                             gboolean subscribe_connection,
                             const char* seq,
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
  InfdSessionProxy* proxy;
  gboolean ensured;
  GError* local_error;

  InfBrowserIter parent_iter;
  InfBrowserIter iter;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(parent->type == INFD_STORAGE_NODE_SUBDIRECTORY);
  g_assert(parent->shared.subdir.explored == TRUE);
  g_assert(request != NULL);

  local_error = NULL;
  infd_directory_node_is_name_available(
    directory,
    parent,
    name,
    &local_error
  );

  if(local_error == NULL)
  {
    node_id = priv->node_counter++;
    group = infd_directory_create_subscription_group(directory, node_id);

    if(subscribe_connection == TRUE)
    {
      /* Note that if session is non-zero at this point, the remote site has
       * to know the contents of the session. It is not synchronized
       * automatically.
       * Note also though this is in principle supported by the code, there is
       * currently no case that calls this function with non-zero session and
       * subscribe_connection set. */
      subreq = infd_directory_add_subreq_add_node(
        directory,
        connection,
        group,
        request,
        parent,
        node_id,
        name,
        sheet_set,
        plugin,
        session,
        error
      );

      if(subreq != NULL)
      {
        xml = infd_directory_node_desc_register_to_xml(
          "add-node",
          node_id,
          parent,
          plugin,
          name
        );

        inf_xml_util_set_attribute(xml, "seq", seq);

        if(sheet_set != NULL)
        {
          infd_directory_acl_sheets_to_xml_for_connection(
            directory,
            NULL,
            sheet_set,
            connection,
            xml
          );
        }

        child = xmlNewChild(xml, NULL, (const xmlChar*)"subscribe", NULL);
        inf_xml_util_set_attribute(
          child,
          "group",
          inf_communication_group_get_name(INF_COMMUNICATION_GROUP(group))
        );

        method = inf_communication_group_get_method_for_connection(
          INF_COMMUNICATION_GROUP(group),
          connection
        );

        /* "central" method should always be available */
        g_assert(method != NULL);

        inf_xml_util_set_attribute(child, "method", method);

        inf_communication_group_send_message(
          INF_COMMUNICATION_GROUP(priv->group),
          connection,
          xml
        );
      }
    }
    else
    {
      if(session != NULL)
      {
        proxy = infd_directory_create_session_proxy_with_group(
          directory,
          session,
          group
        );
      }
      else
      {
        proxy = infd_directory_create_session_proxy(
          directory,
          plugin,
          INF_SESSION_RUNNING,
          NULL,
          NULL,
          group
        );
      }

      ensured = infd_directory_session_proxy_ensure(
        directory,
        parent,
        name,
        plugin,
        proxy,
        &local_error
      );

      if(ensured == TRUE)
      {
        node = infd_directory_node_new_note(
          directory,
          parent,
          node_id,
          g_strdup(name),
          sheet_set,
          TRUE,
          plugin
        );

        infd_directory_node_register(directory, node, request, NULL, seq);
        infd_directory_node_link_session(directory, node, request, proxy);
        g_object_unref(proxy);

        parent_iter.node_id = parent->id;
        parent_iter.node = parent;
        iter.node_id = node->id;
        iter.node = node;

        inf_request_finish(
          INF_REQUEST(request),
          inf_request_result_make_add_node(
            INF_BROWSER(directory),
            &parent_iter,
            &iter
          )
        );
      }
    }

    g_object_unref(group);
  }

  if(local_error != NULL)
  {
    inf_request_fail(INF_REQUEST(request), local_error);
    g_propagate_error(error, local_error);
    return FALSE;
  }

  return TRUE;
}

static gboolean
infd_directory_node_add_sync_in(InfdDirectory* directory,
                                InfdDirectoryNode* parent,
                                InfdRequest* request,
                                const gchar* name,
                                const InfAclSheetSet* sheet_set,
                                const InfdNotePlugin* plugin,
                                InfXmlConnection* sync_conn,
                                gboolean subscribe_sync_conn,
                                const gchar *seq,
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
  GError* local_error;

  /* Synchronization is always between only two peers, so central method
   * is enough. */
  static const gchar* const sync_methods[] = { "central", NULL };

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(parent->type == INFD_STORAGE_NODE_SUBDIRECTORY);
  g_assert(parent->shared.subdir.explored == TRUE);

  local_error = NULL;
  infd_directory_node_is_name_available(
    directory,
    parent,
    name,
    &local_error
  );

  if(local_error == NULL)
  {
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
      synchronization_group,
      subscription_group,
      request,
      parent,
      node_id,
      name,
      sheet_set,
      plugin,
      error
    );

    if(subreq != NULL)
    {
      xml = infd_directory_node_desc_register_to_xml(
        "sync-in",
        node_id,
        parent,
        plugin,
        name
      );

      if(sheet_set != NULL)
      {
        infd_directory_acl_sheets_to_xml_for_connection(
          directory,
          NULL,
          sheet_set,
          sync_conn,
          xml
        );
      }

      inf_xml_util_set_attribute(
        xml,
        "group",
        inf_communication_group_get_name(
          INF_COMMUNICATION_GROUP(synchronization_group)
        )
      );

      inf_xml_util_set_attribute(xml, "method", method);
      if(seq != NULL) inf_xml_util_set_attribute(xml, "seq", seq);

      if(subscribe_sync_conn == TRUE)
      {
        /* Note that if subscribe_sync_conn is set, then sync_group is the
         * same as the subscription group, so we don't need to query the
         * subscription group here. */
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

      inf_communication_group_send_message(
        INF_COMMUNICATION_GROUP(priv->group),
        sync_conn,
        xml
      );
    }

    g_object_unref(synchronization_group);
    g_object_unref(subscription_group);
  }

  if(local_error != NULL)
  {
    inf_request_fail(INF_REQUEST(request), local_error);
    g_propagate_error(error, local_error);
    return FALSE;
  }

  return TRUE;
}

static gboolean
infd_directory_node_remove(InfdDirectory* directory,
                           InfdDirectoryNode* node,
                           InfdRequest* request,
                           const gchar* seq,
                           GError** error)
{
  InfdDirectoryPrivate* priv;
  gchar* path;
  InfBrowserIter iter;
  GError* local_error;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* Cannot remove the root node */
  /* TODO: Move the error check here so we have same error checking for
   * local and remote requests. */
  g_assert(node->parent != NULL);
  g_assert(request != NULL);

  local_error = NULL;
  if(priv->storage != NULL)
  {
    infd_directory_node_get_path(node, &path, NULL);

    infd_storage_remove_node(
      priv->storage,
      node->type == INFD_STORAGE_NODE_NOTE ?
        node->shared.note.plugin->note_type :
        NULL,
      path,
      &local_error
    );
    
    g_free(path);
  }

  iter.node_id = node->id;
  iter.node = node;
  if(local_error != NULL)
  {
    inf_request_fail(INF_REQUEST(request), local_error);
    g_propagate_error(error, local_error);
    return FALSE;
  }
  else
  {
    inf_request_finish(
      INF_REQUEST(request),
      inf_request_result_make_remove_node(INF_BROWSER(directory), &iter)
    );

    /* Need to unlink child sessions explicitely before unregistering, so
     * remove-session is emitted before node-removed. Don't save changes since
     * we just removed the note anyway. */
    infd_directory_node_unlink_child_sessions(
      directory,
      node,
      request,
      FALSE
    );

    infd_directory_node_unregister(directory, node, request, seq);
    infd_directory_node_free(directory, node);

    return TRUE;
  }
}

/* Returns the session for the given node. This does not link the session
 * (if it isn't already). This means that the next time this function is
 * called, the session will be created again if you don't link it yourself,
 * or if you don't create a subscription request for it. Unref the result. */
static InfdSessionProxy*
infd_directory_node_make_session(InfdDirectory* directory,
                                 InfdDirectoryNode* node,
                                 GError** error)
{
  InfdDirectoryPrivate* priv;
  InfSession* session;
  GSList* item;
  InfdDirectorySubreq* subreq;
  InfCommunicationHostedGroup* group;
  InfdSessionProxy* proxy;
  gchar* path;

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* Make sure the session is not already created */
  g_assert(node->shared.note.session == NULL ||
           node->shared.note.weakref == TRUE);

  g_assert(
    infd_directory_find_subreq_by_node_id(
      directory,
      INFD_DIRECTORY_SUBREQ_SESSION,
      node->id
    ) == NULL
  );

  /* Note the session might only be weak-refed in which case we re-use it */
  if(node->shared.note.session != NULL)
  {
    proxy = node->shared.note.session;

    g_object_ref(proxy);
    return proxy;
  }

  /* If we don't have a background storage then all nodes are in memory */
  g_assert(priv->storage != NULL);

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

  group = infd_directory_create_subscription_group(directory, node->id);

  proxy = infd_directory_create_session_proxy_with_group(
    directory,
    session,
    group
  );

  g_object_unref(group);
  g_object_unref(session);

  return proxy;
}

/*
 * Network command handling.
 */

static gboolean
infd_directory_check_auth(InfdDirectory* directory,
                          InfdDirectoryNode* node,
                          InfXmlConnection* connection,
                          const InfAclMask* mask,
                          GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryConnectionInfo* info;
  InfBrowserIter iter;
  gboolean result;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  info = g_hash_table_lookup(priv->connections, connection);
  g_assert(info != NULL);

  iter.node_id = node->id;
  iter.node = node;

  result = inf_browser_check_acl(
    INF_BROWSER(directory),
    &iter,
    &info->account->account,
    mask,
    NULL
  );

  if(result == FALSE)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_NOT_AUTHORIZED,
      "%s",
      _("Permission denied")
    );

    return FALSE;
  }

  return TRUE;
}

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
  const InfAclSheetSet* sheet_set;

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

  if(strcmp(info->account->account.id, "default") != 0)
  {
    child = xmlNewChild(xml, NULL, (const xmlChar*)"account", NULL);
    inf_acl_account_to_xml(&info->account->account, child);
  }

  /* Add default ACL for the root node */
  infd_directory_acl_sheets_to_xml_for_connection(
    directory,
    priv->root->acl_connections,
    priv->root->acl,
    connection,
    xml
  );

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    xml
  );
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
      "%s",
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

  /* TODO: Verify that the connection has explored this node */

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
  InfAclMask perms;
  GSList* item;
  InfdProgressRequest* request;
  InfBrowserIter iter;
  GError* local_error;
  InfdDirectoryNode* child;
  xmlNodePtr reply_xml;
  gchar* seq;
  guint total;
  const InfAclSheetSet* sheet_set;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  node = infd_directory_get_node_from_xml_typed(
    directory,
    xml,
    "id",
    INFD_STORAGE_NODE_SUBDIRECTORY,
    error
  );

  if(node == NULL) return FALSE;

  inf_acl_mask_set1(&perms, INF_ACL_CAN_EXPLORE_NODE);
  if(!infd_directory_check_auth(directory, node, connection, &perms, error))
    return FALSE;

  if(node->shared.subdir.explored == FALSE)
  {
    request = INFD_PROGRESS_REQUEST(
      g_object_new(
        INFD_TYPE_PROGRESS_REQUEST,
        "type", "explore-node",
        "node-id", node->id,
        "requestor", connection,
        NULL
      )
    );

    iter.node_id = node->id;
    iter.node = node;
    inf_browser_begin_request(
      INF_BROWSER(directory),
      &iter,
      INF_REQUEST(request)
    );

    local_error = NULL;
    infd_directory_node_explore(directory, node, request, &local_error);
    g_object_unref(request);

    if(local_error != NULL)
    {
      g_propagate_error(error, local_error);
      return FALSE;
    }
  }

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

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
    return FALSE;

  total = 0;
  for(child = node->shared.subdir.child; child != NULL; child = child->next)
    ++ total;

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"explore-begin");
  inf_xml_util_set_attribute_uint(reply_xml, "total", total);
  if(seq != NULL)
    inf_xml_util_set_attribute(reply_xml, "seq", seq);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  for(child = node->shared.subdir.child; child != NULL; child = child->next)
  {
    reply_xml = infd_directory_node_register_to_xml(child);
    if(seq != NULL)
      inf_xml_util_set_attribute(reply_xml, "seq", seq);

    if(child->acl != NULL)
    {
      infd_directory_acl_sheets_to_xml_for_connection(
        directory,
        child->acl_connections,
        child->acl,
        connection,
        reply_xml
      );
    }

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      connection,
      reply_xml
    );
  }

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"explore-end");

  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

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

  g_free(seq);
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
  InfBrowserIter parent_iter;
  InfdDirectoryNode* node;
  GError* local_error;
  InfAclSheetSet* sheet_set;
  InfAclMask perms;
  InfdNotePlugin* plugin;
  InfdRequest* request;
  xmlChar* name;
  xmlChar* type;
  gchar* seq;

  xmlNodePtr child;
  gboolean perform_sync_in;
  gboolean subscribe_sync_conn;
  gboolean is_subdirectory;
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

  local_error = NULL;
  sheet_set = inf_acl_sheet_set_from_xml(
    xml,
    infd_directory_acl_account_lookup_func,
    priv->accounts,
    &local_error
  );

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  type = inf_xml_util_get_attribute_required(xml, "type", error);
  if(type == NULL)
  {
    if(sheet_set != NULL)
      inf_acl_sheet_set_free(sheet_set);
    return FALSE;
  }

  if(strcmp((const gchar*)type, "InfSubdirectory") == 0)
    is_subdirectory = TRUE;
  else
    is_subdirectory = FALSE;
    
  /* Check for sync-in/subscribe flags */
  perform_sync_in = subscribe_sync_conn = FALSE;
  for(child = xml->children; child != NULL; child = child->next)
  {
    if(strcmp((const char*)child->name, "sync-in") == 0)
      perform_sync_in = TRUE;
    else if(strcmp((const char*)child->name, "subscribe") == 0)
      subscribe_sync_conn = TRUE;
  }

  infd_directory_get_add_node_permissions(
    directory,
    &perms,
    is_subdirectory,
    subscribe_sync_conn,
    perform_sync_in,
    sheet_set
  );

  if(!infd_directory_check_auth(directory, parent, connection, &perms, error))
  {
    if(sheet_set != NULL)
      inf_acl_sheet_set_free(sheet_set);
    xmlFree(type);
    return FALSE;
  }

  if(is_subdirectory == TRUE)
  {
    /* No plugin because we want to create a directory */
    plugin = NULL;
    xmlFree(type);
    
    /* TODO: Error if perform_sync_in or subscribe are set. Actually we
     * could interpret subscribe as initially explored... */
  }
  else
  {
    plugin = g_hash_table_lookup(priv->plugins, (const gchar*)type);
    xmlFree(type);

    if(plugin == NULL)
    {
      if(sheet_set != NULL)
        inf_acl_sheet_set_free(sheet_set);

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

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
  {
    if(sheet_set != NULL)
      inf_acl_sheet_set_free(sheet_set);
    return FALSE;
  }

  name = inf_xml_util_get_attribute_required(xml, "name", error);
  if(name == NULL)
  {
    if(sheet_set != NULL)
      inf_acl_sheet_set_free(sheet_set);
    g_free(seq);
    return FALSE;
  }

  request = INFD_REQUEST(
    g_object_new(
      INFD_TYPE_REQUEST,
      "type", "add-node",
      "node-id", parent->id,
      "requestor", connection,
      NULL
    )
  );

  parent_iter.node_id = parent->id;
  parent_iter.node = parent;
  inf_browser_begin_request(
    INF_BROWSER(directory),
    &parent_iter,
    INF_REQUEST(request)
  );

  if(plugin == NULL)
  {
    node = infd_directory_node_add_subdirectory(
      directory,
      parent,
      request,
      (const gchar*)name,
      sheet_set,
      connection,
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
    if(perform_sync_in == FALSE)
    {
      node_added = infd_directory_node_add_note(
        directory,
        parent,
        request,
        (const gchar*)name,
        sheet_set,
        plugin,
        NULL,
        connection,
        subscribe_sync_conn,
        seq,
        error
      );
    }
    else
    {
      node_added = infd_directory_node_add_sync_in(
        directory,
        parent,
        request,
        (const char*)name,
        sheet_set,
        plugin,
        connection,
        subscribe_sync_conn,
        seq,
        error
      );

      /* Note: The sync-in can still fail for various reasons. Maybe the
       * client doesn't support the communication method, the synchronization
       * fails or the parent folder is removed. */
    }
  }

  g_object_unref(request);

  if(sheet_set != NULL)
    inf_acl_sheet_set_free(sheet_set);
  xmlFree(name);
  g_free(seq);

  return node_added;
}

static gboolean
infd_directory_handle_remove_node(InfdDirectory* directory,
                                  InfXmlConnection* connection,
                                  const xmlNodePtr xml,
                                  GError** error)
{
  InfdDirectoryNode* node;
  gchar* seq;
  InfdRequest* request;
  InfBrowserIter iter;
  gboolean result;

  InfdDirectoryNode* up;
  InfAclMask perms;

  node = infd_directory_get_node_from_xml(directory, xml, "id", error);
  if(node == NULL) return FALSE;

  if(node->parent == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ROOT_NODE_REMOVE_ATTEMPT,
      "%s",
      _("The root node cannot be removed")
    );

    return FALSE;
  }
  else
  {
    /* Check the remove node permission on the parent node */
    up = node->parent;
    inf_acl_mask_set1(&perms, INF_ACL_CAN_REMOVE_NODE);
    if(!infd_directory_check_auth(directory, up, connection, &perms, error))
      return FALSE;

    if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
      return FALSE;

    request = INFD_REQUEST(
      g_object_new(
        INFD_TYPE_REQUEST,
        "type", "remove-node",
        "node-id", node->id,
        "requestor", connection,
        NULL
      )
    );

    iter.node_id = node->id;
    iter.node = node;
    inf_browser_begin_request(
      INF_BROWSER(directory),
      &iter,
      INF_REQUEST(request)
    );

    result = infd_directory_node_remove(directory, node, request, seq, error);
    g_object_unref(request);
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
  InfAclMask perms;
  GSList* item;
  InfdDirectorySubreq* subreq;
  InfdSessionProxy* proxy;
  InfBrowserIter iter;
  InfdRequest* request;
  InfCommunicationGroup* group;
  const gchar* method;
  gchar* seq;
  xmlNodePtr reply_xml;
  GError* local_error;

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

  inf_acl_mask_set1(&perms, INF_ACL_CAN_SUBSCRIBE_SESSION);
  if(!infd_directory_check_auth(directory, node, connection, &perms, error))
    return FALSE;

  /* TODO: Bail if this connection is either currently being synchronized to
   * or is already subscribed */

  request = NULL;
  proxy = NULL;

  /* Check if there exists already a session in a subreq */
  for(item = priv->subscription_requests; item != NULL; item = item->next)
  {
    subreq = (InfdDirectorySubreq*)item->data;
    if(subreq->type == INFD_DIRECTORY_SUBREQ_SESSION &&
       subreq->node_id == node->id)
    {
      if(subreq->connection == connection)
      {
        g_set_error(
          error,
          inf_directory_error_quark(),
          INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED,
          "%s",
          inf_directory_strerror(INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED)
        );

        return FALSE;
      }
      else
      {
        request = subreq->shared.session.request;
        proxy = subreq->shared.session.session;
      }
    }
  }

  if(node->shared.note.session != NULL && node->shared.note.weakref == FALSE)
  {
    g_assert(proxy == NULL || proxy == node->shared.note.session);
    proxy = node->shared.note.session;
  }

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
    return FALSE;

  /* Make a new request if there is no request yet and we don't have a proxy
   * already. If we do have a proxy, then we don't have to read anything from
   * storage. */
  if(request == NULL && proxy == NULL)
  {
    request = INFD_REQUEST(
      g_object_new(
        INFD_TYPE_REQUEST,
        "type", "subscribe-session",
        "node-id", node->id,
        "requestor", connection,
        NULL
      )
    );

    iter.node_id = node->id;
    iter.node = node;
    inf_browser_begin_request(
      INF_BROWSER(directory),
      &iter,
      INF_REQUEST(request)
    );
  }
  else if(request != NULL)
  {
    g_object_ref(request);
  }

  /* In case we don't have a proxy already, create a new one */
  if(proxy == NULL)
  {
    local_error = NULL;
    proxy = infd_directory_node_make_session(directory, node, &local_error);
    if(proxy == NULL)
    {
      /* Only if we have already a proxy we could not have a request here */
      g_assert(request != NULL);
      inf_request_fail(INF_REQUEST(request), local_error);
      g_error_free(local_error);
      g_object_unref(request);
      g_free(seq);
      return FALSE;
    }
  }
  else
  {
    g_object_ref(proxy);
  }

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
  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  /* This gives ownership of proxy to the subscription request */
  infd_directory_add_subreq_session(
    directory,
    connection,
    request,
    node->id,
    proxy
  );

  if(request != NULL)
    g_object_unref(request);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  g_free(seq);
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
  xmlNodePtr reply_xml;
  gchar* path;
  gchar* seq;
  InfSession* session;
  gboolean result;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  if(priv->storage == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NO_STORAGE,
      "%s",
      _("No background storage available")
    );

    return FALSE;
  }

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
      "%s",
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

  g_object_get(
    G_OBJECT(node->shared.note.session),
    "session", &session,
    NULL
  );

  /* TODO: Make a request */

  result = node->shared.note.plugin->session_write(
    priv->storage,
    session,
    path,
    node->shared.note.plugin->user_data,
    error
  );

  g_object_unref(session);

  /* TODO: unset modified flag of buffer if result == TRUE */

  /* The timeout should only be set when there aren't any connections
   * subscribed, however we just made sure that the connection the request
   * comes from is subscribed. */
  g_assert(node->shared.note.save_timeout == NULL);

  g_free(path);

  if(result == FALSE)
    return FALSE;
  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
    return FALSE;

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"session-saved");
  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  g_free(seq);
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
  gchar* seq;
  xmlNodePtr reply_xml;

  InfdDirectoryNode* node;
  InfAclMask perms;

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
      "%s",
      _("The chat is disabled")
    );

    return FALSE;
  }

  node = priv->root;
  inf_acl_mask_set1(&perms, INF_ACL_CAN_SUBSCRIBE_CHAT);
  if(!infd_directory_check_auth(directory, node, connection, &perms, error))
    return FALSE;

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
    return FALSE;

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

  if(seq) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  infd_directory_add_subreq_chat(directory, connection);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  g_free(seq);
  return TRUE;
}

static gboolean
infd_directory_handle_create_acl_account(InfdDirectory* directory,
                                         InfXmlConnection* connection,
                                         xmlNodePtr xml,
                                         GError** error)
{
  InfdDirectoryPrivate* priv;
  xmlNodePtr child;
  int res;

  gnutls_datum_t crq_text;
  gnutls_x509_crq_t crq;

  gnutls_x509_crt_t cert;
  size_t cert_size;
  gchar* cert_buffer;

  gchar* seq;
  xmlNodePtr reply_xml;
  InfdAclAccountInfo* info;

  InfdDirectoryNode* node;
  InfAclMask perms;
  gchar* name;
  gchar* id;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* TODO: Check that the issuing connection is authorized */

  crq_text.data = NULL;
  for(child = xml->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;

    if(strcmp((const char*)child->name, "crq") == 0)
    {
      if(child->children != NULL && child->children->type == XML_TEXT_NODE)
      {
        crq_text.data = (char*)child->children->content;
        crq_text.size = strlen(crq_text.data);
      }
    }
  }

  if(crq_text.data == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
      "%s",
      _("No certificate request provided")
    );

    return FALSE;
  }

  /* TODO: Some of the code below should be moved to inf-cert-util */
  
  res = gnutls_x509_crq_init(&crq);
  if(res != GNUTLS_E_SUCCESS)
  {
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  res = gnutls_x509_crq_import(crq, &crq_text, GNUTLS_X509_FMT_PEM);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crq_deinit(crq);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  res = gnutls_x509_crq_verify(crq, 0);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crq_deinit(crq);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  /* OK, so now we have a good certificate request in front of us. Now, go
   * ahead, create the certificate and sign it with the server's key. */

  cert = infd_directory_create_certificate_from_crq(
    directory,
    crq,
    365 * DAYS,
    error
  );

  gnutls_x509_crq_deinit(crq);
  if(cert == NULL) return FALSE;

  /* Export the certificate to PEM format and send it back to the client */
  cert_size = 0;
  res = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, NULL, &cert_size);
  if(res != GNUTLS_E_SHORT_MEMORY_BUFFER)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  cert_buffer = g_malloc(cert_size);
  res = gnutls_x509_crt_export(
    cert,
    GNUTLS_X509_FMT_PEM,
    cert_buffer,
    &cert_size
  );

  if(res != GNUTLS_E_SUCCESS)
  {
    g_free(cert_buffer);
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  /* Create seq */
  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
  {
    g_free(cert_buffer);
    gnutls_x509_crt_deinit(cert);
    return FALSE;
  }

  /* Check permissions */
  if(!infd_directory_account_info_from_certificate(cert, &id, &name, error))
  {
    g_free(cert_buffer);
    gnutls_x509_crt_deinit(cert);
    g_free(seq);
    return FALSE;
  }

  node = priv->root;
  inf_acl_mask_set1(&perms, INF_ACL_CAN_CREATE_ACCOUNT);
  if(g_hash_table_lookup(priv->accounts, id) != NULL)
    inf_acl_mask_or1(&perms, INF_ACL_CAN_OVERRIDE_ACCOUNT);

  if(!infd_directory_check_auth(directory, node, connection, &perms, error))
  {
    g_free(cert_buffer);
    gnutls_x509_crt_deinit(cert);
    g_free(id);
    g_free(name);
    g_free(seq);
    return FALSE;
  }

  /* At this point, the request is validated and nothing can fail anymore. */

  /* Create account. This function checks permissions of the connection. */
  info = infd_directory_create_acl_account_with_certificate(
    directory,
    id,
    name,
    cert,
    connection,
    error
  );

  g_free(id);
  g_free(name);
  gnutls_x509_crt_deinit(cert);

  if(info == NULL)
  {
    g_free(seq);
    g_free(cert_buffer);
    return FALSE;
  }

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"create-acl-account");
  child = xmlNewChild(reply_xml, NULL, (const xmlChar*)"certificate", NULL);
  xmlNodeAddContentLen(child, (const xmlChar*)cert_buffer, cert_size);
  g_free(cert_buffer);

  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);
  g_free(seq);

  if(g_slist_find(priv->account_list_connections, connection) != NULL)
    inf_acl_account_to_xml(&info->account, reply_xml);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  return TRUE;
}

static gboolean
infd_directory_handle_remove_acl_account(InfdDirectory* directory,
                                         InfXmlConnection* connection,
                                         xmlNodePtr xml,
                                         GError** error)
{
  InfdDirectoryPrivate* priv;
  xmlChar* account_id;
  InfdAclAccountInfo* info;
  gchar* seq;

  InfdDirectoryNode* node;
  InfAclMask perms;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* Connection needs to have account list queried in order to remove an
   * account. */
  if(g_slist_find(priv->account_list_connections, connection) == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ACCOUNT_LIST_NOT_QUERIED,
      "%s",
      _("The account list needs to be queried in order to remove an account")
    );

    return FALSE;
  }

  node = priv->root;
  inf_acl_mask_set1(&perms, INF_ACL_CAN_REMOVE_ACCOUNT);
  if(!infd_directory_check_auth(directory, node, connection, &perms, error))
    return FALSE;

  account_id = inf_xml_util_get_attribute_required(xml, "id", error);
  if(account_id == NULL) return FALSE;

  if(strcmp(account_id, "default") == 0)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NO_SUCH_ACCOUNT,
      "%s",
      _("The default account cannot be removed")
    );

    xmlFree(account_id);
    return FALSE;
  }

  info = g_hash_table_lookup(priv->accounts, account_id);
  if(info == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NO_SUCH_ACCOUNT,
      _("There is no such account with ID \"%s\""),
      (const gchar*)account_id
    );

    xmlFree(account_id);
    return FALSE;
  }

  xmlFree(account_id);

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
    return FALSE;

  infd_directory_remove_acl_account(directory, info, seq, NULL);
  g_free(seq);

  /* Note that since account removal is only possible for connections that
   * have queried the account list, the infd_directory_remove_acl_account()
   * call is sending the reply XML to this connection as well. There is
   * nothing left to do here. */
  return TRUE;
}

static gboolean
infd_directory_handle_query_acl_account_list(InfdDirectory* directory,
                                             InfXmlConnection* connection,
                                             const xmlNodePtr xml,
                                             GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryConnectionInfo* conn_info;
  InfAclMask perms;
  gchar* seq;
  GSList* item;
  xmlNodePtr reply_xml;
  guint known_accounts;

  GHashTableIter iter;
  gpointer key;
  gpointer value;
  const InfdAclAccountInfo* default_account;
  const InfdAclAccountInfo* info;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(g_slist_find(priv->account_list_connections, connection) != NULL)
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

  conn_info = g_hash_table_lookup(priv->connections, connection);
  g_assert(conn_info != NULL);

  node = priv->root;
  inf_acl_mask_set1(&perms, INF_ACL_CAN_QUERY_ACCOUNT_LIST);
  if(!infd_directory_check_auth(directory, node, connection, &perms, error))
    return FALSE;

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
    return FALSE;

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"acl-account-list-begin");

  /* Count the number of accounts already known by the client, to correctly
   * calculate the number of accounts to be transferred. */
  default_account = g_hash_table_lookup(priv->accounts, "default");
  g_assert(default_account != NULL);

  if(conn_info->account != default_account)
    known_accounts = 2;
  else
    known_accounts = 1;
  g_assert(g_hash_table_size(priv->accounts) >= known_accounts);

  inf_xml_util_set_attribute_uint(
    reply_xml,
    "total",
    g_hash_table_size(priv->accounts) - known_accounts
  );

  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  priv->account_list_connections =
    g_slist_prepend(priv->account_list_connections, connection);

  g_hash_table_iter_init(&iter, priv->accounts);
  while(g_hash_table_iter_next(&iter, &key, &value))
  {
    info = (const InfdAclAccountInfo*)value;

    /* Ignore accounts already known by the client: Its own account and the
     * default account. */
    if(info != default_account && info != conn_info->account)
    {
      reply_xml = xmlNewNode(NULL, (const xmlChar*)"add-acl-account");
      inf_acl_account_to_xml(&info->account, reply_xml);
      if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

      inf_communication_group_send_message(
        INF_COMMUNICATION_GROUP(priv->group),
        connection,
        reply_xml
      );
    }
  }

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"acl-account-list-end");
  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  g_free(seq);
  return TRUE;
}

static gboolean
infd_directory_handle_query_acl(InfdDirectory* directory,
                                InfXmlConnection* connection,
                                const xmlNodePtr xml,
                                GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfAclMask perms;
  gchar* seq;
  const InfAclSheetSet* sheet_set;
  xmlNodePtr reply_xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  node = infd_directory_get_node_from_xml(
    directory,
    xml,
    "id",
    error
  );
  
  if(node == NULL)
    return FALSE;

  if(g_slist_find(node->acl_connections, connection) != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ACL_ALREADY_QUERIED,
      "%s",
      _("The ACL for this node has already been queried")
    );

    return FALSE;
  }

  inf_acl_mask_set1(&perms, 1 << INF_ACL_CAN_QUERY_ACL);
  if(!infd_directory_check_auth(directory, node, connection, &perms, error))
    return FALSE;

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
    return FALSE;

  /* Add to ACL connections here so that
   * infd_directory_acl_sheets_to_xml_for_connection() will send the full
   * ACL, and not only the default sheet. */
  node->acl_connections = g_slist_prepend(node->acl_connections, connection);

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"set-acl");
  inf_xml_util_set_attribute_uint(reply_xml, "id", node->id);
  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  if(node->acl != NULL)
  {
    infd_directory_acl_sheets_to_xml_for_connection(
      directory,
      node->acl_connections,
      node->acl,
      connection,
      reply_xml
    );
  }

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  g_free(seq);
  return TRUE;
}

static gboolean
infd_directory_handle_set_acl(InfdDirectory* directory,
                              InfXmlConnection* connection,
                              const xmlNodePtr xml,
                              GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfAclMask perms;
  gchar* seq;
  GError* local_error;
  InfAclSheetSet* sheet_set;
  InfdRequest* request;
  InfBrowserIter iter;
  const InfAclAccount* default_account;
  const InfAclSheet* default_sheet;
  const InfAclSheet* account_sheet;
  GHashTableIter conn_iter;
  gpointer key;
  gpointer value;
  InfXmlConnection* conn;
  InfdDirectoryConnectionInfo* info;
  xmlNodePtr reply_xml;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  node = infd_directory_get_node_from_xml(
    directory,
    xml,
    "id",
    error
  );
  
  if(node == NULL)
    return FALSE;

  if(g_slist_find(node->acl_connections, connection) == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_ACL_NOT_QUERIED,
      "%s",
      _("The ACL for this node has not been queried yet")
    );

    return FALSE;
  }

  inf_acl_mask_set1(&perms, INF_ACL_CAN_SET_ACL);
  if(!infd_directory_check_auth(directory, node, connection, &perms, error))
    return FALSE;

  /* TODO: Introduce inf_acl_sheet_set_from_xml_required */
  local_error = NULL;
  sheet_set = inf_acl_sheet_set_from_xml(
    xml,
    infd_directory_acl_account_lookup_func,
    priv->accounts,
    &local_error
  );

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  if(sheet_set == NULL || sheet_set->n_sheets == 0)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
      "%s",
      _("The set-acl request does not have any ACL provided")
    );

    return FALSE;
  }

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
  {
    inf_acl_sheet_set_free(sheet_set);
    return FALSE;
  }

  request = INFD_REQUEST(
    g_object_new(
      INFD_TYPE_REQUEST,
      "type", "set-acl",
      "node-id", node->id,
      "requestor", connection,
      NULL
    )
  );

  iter.node_id = node->id;
  iter.node = node;

  inf_browser_begin_request(INF_BROWSER(directory),
    &iter,
    INF_REQUEST(request)
  );

  node->acl = inf_acl_sheet_set_merge_sheets(node->acl, sheet_set);

  /* Apply the effect of the new ACL */
  default_account = g_hash_table_lookup(priv->accounts, "default");
  g_assert(default_account != NULL);
  default_sheet =
    inf_acl_sheet_set_find_const_sheet(sheet_set, default_account);

  g_hash_table_iter_init(&conn_iter, priv->connections);
  while(g_hash_table_iter_next(&conn_iter, &key, &value))
  {
    conn = (InfXmlConnection*)key;
    info = (InfdDirectoryConnectionInfo*)value;

    if(default_sheet == NULL)
    {
      account_sheet = inf_acl_sheet_set_find_const_sheet(
        sheet_set,
        &info->account->account
      );
    }

    if(default_sheet != NULL || account_sheet != NULL)
      infd_directory_enforce_acl(directory, conn, node, NULL);
  }

  /* Announce to all connections but this one, since for this connection we
   * need to set the seq (done below) */
  infd_directory_announce_acl_sheets(
    directory,
    node,
    request,
    sheet_set,
    connection
  );

  infd_directory_write_acl(directory, node);

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"set-acl");
  inf_xml_util_set_attribute_uint(reply_xml, "id", node->id);
  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  infd_directory_acl_sheets_to_xml_for_connection(
    directory,
    node->acl_connections,
    sheet_set,
    connection,
    reply_xml
  );

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  inf_request_finish(
    INF_REQUEST(request),
    inf_request_result_make_set_acl(INF_BROWSER(directory), &iter)
  );

  g_object_unref(request);
  inf_acl_sheet_set_free(sheet_set);
  g_free(seq);
  return TRUE;
}

static gboolean
infd_directory_handle_subscribe_ack(InfdDirectory* directory,
                                    InfXmlConnection* connection,
                                    const xmlNodePtr xml,
                                    GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectorySubreq* subreq;
  InfdDirectoryNode* node;
  InfXmlConnection* conn;
  InfAclMask perms;
  xmlNodePtr reply_xml;

  GSList* item;
  InfdDirectorySubreq* subsubreq;
  InfdDirectorySyncIn* sync_in;
  InfdSessionProxy* proxy;
  InfSession* session;
  InfdDirectoryConnectionInfo* info;
  GError* local_error;

  InfBrowserIter parent_iter;
  InfBrowserIter iter;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  subreq = infd_directory_get_subreq_from_xml(
    directory,
    connection,
    xml,
    error
  );

  if(subreq == NULL) return FALSE;

  /* Unlink, so that the subreq itself does not cause the is_name_available
   * assertions below to fail. */
  infd_directory_unlink_subreq(directory, subreq);

  info = g_hash_table_lookup(priv->connections, connection);
  g_assert(info != NULL);

  switch(subreq->type)
  {
  case INFD_DIRECTORY_SUBREQ_CHAT:
    /* Note that it doesn't matter whether the chat was disabled+enabled
     * between subreq generation and <subscribe-ack/> handling - the group
     * is always called InfChat so the client joins the correct group in
     * all cases. */
    /* TODO: check for CAN_SUBSCRIBE_CHAT at this point... */
    if(priv->chat_session != NULL)
    {
      infd_session_proxy_subscribe_to(
        priv->chat_session,
        connection,
        info->seq_id,
        TRUE
      );
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
      infd_session_proxy_subscribe_to(
        priv->chat_session,
        connection,
        info->seq_id,
        TRUE
      );
      infd_directory_enable_chat(directory, FALSE);

      g_object_thaw_notify(G_OBJECT(directory));
    }

    break;
  case INFD_DIRECTORY_SUBREQ_SESSION:
    node = g_hash_table_lookup(
      priv->nodes,
      GUINT_TO_POINTER(subreq->node_id)
    );

    /* Remove the request from other subreqs, if any, because we are going
     * to finish it now. */
    for(item = priv->subscription_requests; item != NULL; item = item->next)
    {
      subsubreq = (InfdDirectorySubreq*)item->data;
      if(subsubreq->type == INFD_DIRECTORY_SUBREQ_SESSION &&
         subsubreq->node_id == subreq->node_id)
      {
        g_assert(subsubreq->shared.session.request ==
                 subreq->shared.session.request);
        if(subsubreq->shared.session.request != NULL)
        {
          g_object_unref(subsubreq->shared.session.request);
          subsubreq->shared.session.request = NULL;
        }
      }
    }

    /* The node this client wants to subscribe might have been removed in the
     * meanwhile. Also, make sure that the permissions are still granted. */
    local_error = NULL;
    conn = connection;
    inf_acl_mask_set1(&perms, INF_ACL_CAN_SUBSCRIBE_SESSION);
    if(node != NULL &&
       infd_directory_check_auth(directory, node, conn, &perms, &local_error))
    {
      g_assert(node->type == INFD_STORAGE_NODE_NOTE);

      g_assert(node->shared.note.session == NULL ||
               node->shared.note.session == subreq->shared.session.session);

      g_assert(
        ((node->shared.note.session == NULL ||
          node->shared.note.weakref == TRUE) &&
         subreq->shared.session.request != NULL) ||
        ((node->shared.note.session != NULL &&
          node->shared.note.weakref == FALSE) &&
         subreq->shared.session.request == NULL)
      );

      if(node->shared.note.session == NULL ||
         node->shared.note.weakref == TRUE)
      {
        infd_directory_node_link_session(
          directory,
          node,
          subreq->shared.session.request,
          subreq->shared.session.session
        );

        iter.node_id = node->id;
        iter.node = node;

        inf_request_finish(
          INF_REQUEST(subreq->shared.session.request),
          inf_request_result_make_subscribe_session(
            INF_BROWSER(directory),
            &iter,
            INF_SESSION_PROXY(subreq->shared.session.session)
          )
        );
      }
    }
    else
    {
      if(subreq->shared.session.request != NULL)
      {
        if(local_error == NULL)
        {
          g_set_error(
            &local_error,
            inf_directory_error_quark(),
            INF_DIRECTORY_ERROR_NO_SUCH_NODE,
            "%s",
            _("The node to be subscribed to has been removed")
          );
        }

        inf_request_fail(
          INF_REQUEST(subreq->shared.session.request),
          local_error
        );
      }

      if(local_error != NULL)
        g_error_free(local_error);
    }

    infd_session_proxy_subscribe_to(
      subreq->shared.session.session,
      connection,
      info->seq_id,
      TRUE
    );

    break;
  case INFD_DIRECTORY_SUBREQ_ADD_NODE:
    g_assert(subreq->shared.add_node.request != NULL);
    node = subreq->shared.add_node.parent;

    /* The parent node might have been removed meanwhile, also check that the
     * permissions are still granted. */
    local_error = NULL;
    conn = connection;
    infd_directory_get_add_node_permissions(
      directory,
      &perms,
      FALSE,
      TRUE,
      FALSE,
      subreq->shared.add_node.sheet_set
    );

    if(node != NULL &&
       infd_directory_check_auth(directory, node, conn, &perms, &local_error))
    {
      g_assert(
        infd_directory_node_is_name_available(
          directory,
          subreq->shared.add_node.parent,
          subreq->shared.add_node.name,
          NULL
        ) == TRUE
      );

      g_assert(
        g_slist_find(
          subreq->shared.add_node.parent->shared.subdir.connections,
          subreq->connection
        ) != NULL
      );

      proxy = subreq->shared.add_node.proxy;
      g_object_ref(proxy);

      node = infd_directory_node_new_note(
        directory,
        subreq->shared.add_node.parent,
        subreq->node_id,
        g_strdup(subreq->shared.add_node.name),
        subreq->shared.add_node.sheet_set,
        TRUE,
        subreq->shared.add_node.plugin
      );

      /* register to all but conn. conn already added the node after
       * having sent subscribe-ack. */
      infd_directory_node_register(
        directory,
        node,
        subreq->shared.add_node.request,
        connection,
        NULL
      );

      infd_directory_node_link_session(
        directory,
        node,
        subreq->shared.add_node.request,
        proxy
      );

      g_assert(subreq->shared.add_node.request != NULL);

      parent_iter.node_id = subreq->shared.add_node.parent->id;
      parent_iter.node = subreq->shared.add_node.parent;
      iter.node_id = node->id;
      iter.node = node;

      inf_request_finish(
        INF_REQUEST(subreq->shared.add_node.request),
        inf_request_result_make_add_node(
          INF_BROWSER(directory),
          &parent_iter,
          &iter
        )
      );
    }
    else
    {
      /* The add-node request can't be performed properly because the parent
       * node has been removed. Still create a session proxy and subscribe to
       * connection before unrefing it again, so that the remote host gets
       * notified that this session is no longer active. */
      proxy = subreq->shared.add_node.proxy;
      g_object_ref(proxy);

      if(local_error == NULL)
      {
        g_set_error(
          &local_error,
          inf_directory_error_quark(),
          INF_DIRECTORY_ERROR_NO_SUCH_NODE,
          "%s",
          _("The parent node of the node to be added has been removed")
        );
      }

      if(node != NULL)
      {
        /* If the parent node still exists, then the client has created the
         * node, and we refused it because the permissions are no longer
         * granted. Even though we close the session, the client does not know
         * that the node was not even created. For the client, the ACL
         * update comes too late, so it cannot know that the request failed.
         * Therefore, send a remove-node message for the client to get rid
         * of the node again. */
        reply_xml = xmlNewNode(NULL, (const xmlChar*)"remove-node");
        inf_xml_util_set_attribute_uint(reply_xml, "id", subreq->node_id);

        inf_communication_group_send_message(
          INF_COMMUNICATION_GROUP(priv->group),
          connection,
          reply_xml
        );
      }

      inf_request_fail(
        INF_REQUEST(subreq->shared.add_node.request),
        local_error
      );

      g_error_free(local_error);
    }

    /* Don't sync session to client if the client added this node, since the
     * node is empty anyway. */
    infd_session_proxy_subscribe_to(proxy, connection, info->seq_id, FALSE);
    g_object_unref(proxy);

    break;
  case INFD_DIRECTORY_SUBREQ_SYNC_IN:
  case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
    g_assert(subreq->shared.sync_in.request != NULL);
    node = subreq->shared.sync_in.parent;

    /* Group and method are OK for the client, so start synchronization */
    g_object_get(
      G_OBJECT(subreq->shared.sync_in.proxy),
      "session", &session,
      NULL
    );

    inf_session_synchronize_from(session);
    g_object_unref(session);

    /* The parent node might have been removed meanwhile, also check that the
     * permissions are still granted. */
    local_error = NULL;
    conn = connection;
    infd_directory_get_add_node_permissions(
      directory,
      &perms,
      FALSE,
      TRUE,
      FALSE,
      subreq->shared.sync_in.sheet_set
    );

    if(node != NULL &&
       infd_directory_check_auth(directory, node, conn, &perms, &local_error))
    {
      g_assert(
        infd_directory_node_is_name_available(
          directory,
          subreq->shared.sync_in.parent,
          subreq->shared.sync_in.name,
          NULL
        ) == TRUE
      );

      g_assert(
        g_slist_find(
          subreq->shared.sync_in.parent->shared.subdir.connections,
          subreq->connection
        ) != NULL
      );

      proxy = subreq->shared.sync_in.proxy;
      g_object_ref(proxy);

      sync_in = infd_directory_add_sync_in(
        directory,
        subreq->shared.sync_in.parent,
        subreq->shared.sync_in.request,
        subreq->node_id,
        subreq->shared.sync_in.name,
        subreq->shared.sync_in.sheet_set,
        subreq->shared.sync_in.plugin,
        proxy
      );
    }
    else
    {
      /* The sync-in can't be performed properly because the parent node of
       * the node to sync-in has been removed. Still create the corresponding
       * session and close it immediately (cancelling the synchronization, to
       * tell the client). */
      proxy = subreq->shared.sync_in.proxy;
      g_object_ref(proxy);

      if(local_error == NULL)
      {
        g_set_error(
          &local_error,
          inf_directory_error_quark(),
          INF_DIRECTORY_ERROR_NO_SUCH_NODE,
          "%s",
          _("The parent node of the node to be added has been removed")
        );
      }

      /* If the parent node still exists, then the client has created the
       * node, and we refused it because the permissions are no longer
       * granted. We don't need to do anything extra, though, since we
       * cancel the synchronization, and that will let the client know not
       * to create the node. */
      
      inf_request_fail(
        INF_REQUEST(subreq->shared.sync_in.request),
        local_error
      );

      g_error_free(local_error);
    }

    if(subreq->type == INFD_DIRECTORY_SUBREQ_SYNC_IN)
    {
      /* No subscription, so add connection to synchronization group
       * explicitely. */
      inf_communication_hosted_group_add_member(
        subreq->shared.sync_in.synchronization_group,
        connection
      );
    }
    else
    {
      /* subscribe_to adds connection to subscription group which is the
       * same as the synchronization group. */
      infd_session_proxy_subscribe_to(proxy, connection, info->seq_id, FALSE);
    }

    g_object_unref(proxy);

    break;
  default:
    g_assert_not_reached();
    break;
  }

  infd_directory_free_subreq(subreq);
  return TRUE;
}

static gboolean
infd_directory_handle_subscribe_nack(InfdDirectory* directory,
                                     InfXmlConnection* connection,
                                     const xmlNodePtr xml,
                                     GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectorySubreq* subreq;
  InfdDirectorySubreq* subsubreq;
  GSList* item;
  InfdRequest* other_request;
  gchar* path;
  gboolean result;
  GError* local_error;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  subreq = infd_directory_get_subreq_from_xml(
    directory,
    connection,
    xml,
    error
  );

  if(subreq == NULL) return FALSE;

  result = TRUE;
  if(priv->storage != NULL &&
     (subreq->type == INFD_DIRECTORY_SUBREQ_SESSION ||
      subreq->type == INFD_DIRECTORY_SUBREQ_ADD_NODE ||
      subreq->type == INFD_DIRECTORY_SUBREQ_SYNC_IN ||
      subreq->type == INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE))
  {
    local_error = NULL;
    g_set_error(
      &local_error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_SUBSCRIPTION_REJECTED,
      "%s",
      _("Client did not acknowledge initial subscription")
    );

    switch(subreq->type)
    {
    case INFD_DIRECTORY_SUBREQ_CHAT:
      break;
    case INFD_DIRECTORY_SUBREQ_SESSION:
      /* Only fail the request if there are no other requests with the same
       * node around. */

      other_request = NULL;
      
      /* Cannot use infd_directory_find_subreq_by_node_id() because that
       * way we cannot ignore the subreq currently being processed. */
      for(item = priv->subscription_requests; item != NULL; item = item->next)
      {
        subsubreq = (InfdDirectorySubreq*)item->data;
        if(subsubreq == subreq) continue;

        if(subsubreq->type == INFD_DIRECTORY_SUBREQ_SESSION &&
           subsubreq->node_id == subreq->node_id)
        {
          g_assert(subsubreq->shared.session.request ==
                   subreq->shared.session.request);
          other_request = subsubreq->shared.session.request;
          break;
        }
      }

      if(other_request == NULL)
      {
        inf_request_fail(
          INF_REQUEST(subreq->shared.session.request),
          local_error
        );
      }

      /* No need to remove the node from storage since it existed
       * already before */
      result = TRUE;
      break;
    case INFD_DIRECTORY_SUBREQ_ADD_NODE:
      inf_request_fail(
        INF_REQUEST(subreq->shared.add_node.request),
        local_error
      );

      infd_directory_node_make_path(
        subreq->shared.add_node.parent,
        subreq->shared.add_node.name,
        &path,
        NULL
      );

      result = infd_storage_remove_node(
        priv->storage,
        subreq->shared.add_node.plugin->note_type,
        path,
        error
      );

      g_free(path);

      break;
    case INFD_DIRECTORY_SUBREQ_SYNC_IN:
    case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
      inf_request_fail(
        INF_REQUEST(subreq->shared.sync_in.request),
        local_error
      );

      infd_directory_node_make_path(
        subreq->shared.sync_in.parent,
        subreq->shared.sync_in.name,
        &path,
        NULL
      );

      result = infd_storage_remove_node(
        priv->storage,
        subreq->shared.sync_in.plugin->note_type,
        path,
        error
      );

      g_free(path);

      break;
    default:
      g_assert_not_reached();
      break;
    }

    g_error_free(local_error);
  }

  infd_directory_remove_subreq(directory, subreq);
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
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfXmlConnection* connection;
  InfXmlConnectionStatus status;
  InfdDirectoryConnectionInfo* info;

  directory = INFD_DIRECTORY(user_data),
  priv = INFD_DIRECTORY_PRIVATE(directory);
  connection = INF_XML_CONNECTION(object);
  g_object_get(object, "status", &status, NULL);

  if(status == INF_XML_CONNECTION_OPEN)
  {
    info = (InfdDirectoryConnectionInfo*)g_hash_table_lookup(
      priv->connections,
      connection
    );

    g_assert(info != NULL);
    g_assert(info->account == NULL);

    info->account = infd_directory_login_by_certificate(
      directory,
      connection
    );

    infd_directory_send_welcome_message(directory, connection);
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
  InfdDirectorySyncIn* sync_in;
  InfXmlConnection* sync_in_connection;
  InfdDirectorySubreq* request;
  InfdDirectoryConnectionInfo* info;

  directory = INFD_DIRECTORY(user_data);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* TODO: Update last seen time, and write user list to storage */

  /* Remove sync-ins from this connection */
  item = priv->sync_ins;
  while(item != NULL)
  {
    sync_in = (InfdDirectorySyncIn*)item->data;
    item = item->next;

    g_object_get(
      G_OBJECT(sync_in->request),
      "requestor", &sync_in_connection,
      NULL
    );

    if(sync_in_connection == connection)
      infd_directory_remove_sync_in(directory, sync_in);

    g_object_unref(sync_in_connection);
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

  priv->account_list_connections =
    g_slist_remove(priv->account_list_connections, connection);

  if(priv->root != NULL)
  {
    if(priv->root->shared.subdir.explored == TRUE)
    {
      infd_directory_node_remove_connection(priv->root, connection);
    }
    else
    {
      /* If the root directory was not explored it could still happen that
       * the connection queried its ACL. */
      priv->root->acl_connections = g_slist_remove(
        priv->root->acl_connections,
        connection
      );
    }
  }

  info = g_hash_table_lookup(priv->connections, connection);
  g_slice_free(InfdDirectoryConnectionInfo, info);

  inf_signal_handlers_disconnect_by_func(G_OBJECT(connection),
    G_CALLBACK(infd_directory_connection_notify_status_cb),
    directory);

  g_hash_table_remove(priv->connections, connection);

  g_signal_emit(
    G_OBJECT(directory),
    directory_signals[CONNECTION_REMOVED],
    0,
    connection
  );

  g_object_unref(connection);
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
  GError* error;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_assert(priv->root != NULL);

  /* If we are setting a new storage, then remove all documents. If we are
   * going to no storage, then keep current set of documents. */
  if(storage != NULL)
  {
    /* TODO: Update last seen times of all connected users,
     * and write user list to storage. */

    if(priv->root->shared.subdir.explored == TRUE)
    {
      /* Clear directory tree. This will cause all sessions to be saved in
       * storage. Note that sessions are not closed, but further
       * modifications to the sessions will not be stored in storage. */
      while((child = priv->root->shared.subdir.child) != NULL)
      {
        /* TODO: Do make requests here */
        infd_directory_node_unlink_child_sessions(
          directory,
          child,
          NULL,
          TRUE
        );

        infd_directory_node_unregister(directory, child, NULL, NULL);
        infd_directory_node_free(directory, child);
      }
    }
  }

  if(priv->storage != NULL)
    g_object_unref(priv->storage);

  priv->storage = storage;

  if(storage != NULL)
  {
    /* Read user list from new storage, and new ACL for the root node. This
     * overwrites the current ACL for the root node. If no new storage is set,
     * then we keep the previous ACL for the root node. */
    infd_directory_read_root_acl(directory);

    /* root folder was explored before storage change, so keep it
     * explored. */
    if(priv->root->shared.subdir.explored == TRUE)
    {
      /* Need to set explored flag to FALSE to meet preconditions of
       * infd_directory_node_explore(). */
      priv->root->shared.subdir.explored = FALSE;

      /* Do not make a request here, since we don't formally re-explore the
       * root node -- once a node is explored, it always stays explored. */

      error = NULL;
      infd_directory_node_explore(directory, priv->root, NULL, &error);

      if(error != NULL)
      {
        g_warning(
          _("Failed to explore the root directory of the new storage: %s"),
          error->message
        );

        g_error_free(error);
      }
    }

    g_object_ref(storage);
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
  InfdAclAccountInfo* info;

  directory = INFD_DIRECTORY(instance);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  priv->io = NULL;
  priv->storage = NULL;
  priv->communication_manager = NULL;
  priv->group = NULL;

  priv->private_key = NULL;
  priv->certificate = NULL;

  priv->plugins = g_hash_table_new(g_str_hash, g_str_equal);
  priv->connections = g_hash_table_new(NULL, NULL);

  priv->accounts = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    NULL,
    (GDestroyNotify)infd_acl_account_info_free
  );

  priv->accounts_by_certificate = g_hash_table_new(g_str_hash, g_str_equal);
  priv->account_list_connections = NULL;

  priv->node_counter = 1;
  priv->nodes = g_hash_table_new(NULL, NULL);

  /* The root node has no name. At this point we also create the root node
   * with no ACL. The ACL is read from storage in the constructor, or if no
   * ACL exists in storage, a default ACL is used. */
  priv->root = infd_directory_node_new_subdirectory(
    directory,
    NULL,
    0,
    NULL,
    NULL,
    FALSE
  );

  priv->sync_ins = NULL;
  priv->subscription_requests = NULL;

  priv->chat_session = NULL;

  info = infd_acl_account_info_new("default", NULL, FALSE);
  g_hash_table_insert(priv->accounts, info->account.id, info);
}

static GObject*
infd_directory_constructor(GType type,
                           guint n_construct_properties,
                           GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  InfAclSheet sheet;
  InfAclSheetSet sheet_set;

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

  /* If we don't have a background storage then the root node has been
   * explored (there is simply no content yet, it has to be added via
   * infd_directory_add_note). */
  if(priv->storage == NULL)
  {
    priv->root->shared.subdir.explored = TRUE;

    /* Apply default permissions for the root node */
    sheet.account = g_hash_table_lookup(priv->accounts, "default");
    sheet.perms = INF_ACL_MASK_DEFAULT;
    sheet.mask = INF_ACL_MASK_ALL;

    g_assert(sheet.account != NULL);
    g_assert(priv->root->acl == NULL);

    sheet_set.own_sheets = NULL;
    sheet_set.sheets = &sheet;
    sheet_set.n_sheets = 1;

    priv->root->acl =
      inf_acl_sheet_set_merge_sheets(priv->root->acl, &sheet_set);
  }

  /* Note that if storage is non-NULL the root ACL has already been loaded
   * when the storage property was set. */

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

  /* First, remove all connections */
  for(g_hash_table_iter_init(&iter, priv->connections);
      g_hash_table_iter_next(&iter, &key, NULL);
      g_hash_table_iter_init(&iter, priv->connections))
  {
    inf_communication_hosted_group_remove_member(
      priv->group,
      INF_XML_CONNECTION(key)
    );
  }
  
  g_assert(g_hash_table_size(priv->connections) == 0);
  g_assert(priv->subscription_requests == NULL);
  g_assert(priv->account_list_connections == NULL);
  g_assert(priv->sync_ins == NULL);

  /* We have dropped all references to connections now, so these do not try
   * to tell anyone that the directory tree has gone or whatever. */
  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->group),
    G_CALLBACK(infd_directory_member_removed_cb),
    directory
  );

  /* Disable chat if any */
  if(priv->chat_session != NULL)
    infd_directory_enable_chat(directory, FALSE);

  /* This frees the complete directory tree and saves sessions into the
   * storage. */
  infd_directory_node_unlink_child_sessions(
    directory,
    priv->root,
    NULL,
    TRUE
  );

  infd_directory_set_storage(directory, NULL);

  infd_directory_node_free(directory, priv->root);
  priv->root = NULL;

  g_hash_table_destroy(priv->nodes);
  priv->nodes = NULL;

  g_object_unref(priv->group);
  g_object_unref(priv->communication_manager);

  g_hash_table_destroy(priv->connections);
  priv->connections = NULL;

  g_hash_table_destroy(priv->accounts_by_certificate);
  priv->accounts_by_certificate = NULL;

  g_hash_table_destroy(priv->accounts);
  priv->accounts = NULL;

  g_hash_table_destroy(priv->plugins);
  priv->plugins = NULL;

  if(priv->certificate != NULL)
  {
    inf_certificate_chain_unref(priv->certificate);
    priv->certificate = NULL;
  }

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
  case PROP_PRIVATE_KEY:
    priv->private_key = (gnutls_x509_privkey_t)g_value_get_pointer(value);
    break;
  case PROP_CERTIFICATE:
    priv->certificate = (InfCertificateChain*)g_value_dup_boxed(value);
    break;
  case PROP_CHAT_SESSION:
  case PROP_STATUS:
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
  case PROP_PRIVATE_KEY:
    g_value_set_pointer(value, priv->private_key);
    break;
  case PROP_CERTIFICATE:
    g_value_set_boxed(value, priv->certificate);
    break;
  case PROP_CHAT_SESSION:
    g_value_set_object(value, G_OBJECT(priv->chat_session));
    break;
  case PROP_STATUS:
    g_value_set_enum(value, INF_BROWSER_OPEN);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * InfCommunicationObject implementation.
 */

static InfCommunicationScope
infd_directory_communication_object_received(InfCommunicationObject* object,
                                             InfXmlConnection* connection,
                                             const xmlNodePtr node)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  GError* local_error;
  xmlNodePtr reply_xml;
  gchar* seq;

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
  else if(strcmp((const char*)node->name, "create-acl-account") == 0)
  {
    infd_directory_handle_create_acl_account(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "remove-acl-account") == 0)
  {
    infd_directory_handle_remove_acl_account(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "query-acl-account-list") == 0)
  {
    infd_directory_handle_query_acl_account_list(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "query-acl") == 0)
  {
    infd_directory_handle_query_acl(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "set-acl") == 0)
  {
    infd_directory_handle_set_acl(
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
    if(!infd_directory_make_seq(directory, connection, node, &seq, NULL))
      seq = NULL;

    /* An error happened, so tell the client that the request failed and
     * what has gone wrong. */
    reply_xml = inf_xml_util_new_node_from_error(local_error,
                                                 NULL,
                                                 "request-failed");

    if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);
    g_free(seq);

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      connection,
      reply_xml
    );

    g_error_free(local_error);
  }

  /* Never forward directory messages */
  return INF_COMMUNICATION_SCOPE_PTP;
}

/*
 * InfBrowser implementation
 */

static void
infd_directory_browser_subscribe_session(InfBrowser* browser,
                                         const InfBrowserIter* iter,
                                         InfSessionProxy* proxy,
                                         InfRequest* request)
{
  InfdDirectoryNode* node;

  node = (InfdDirectoryNode*)iter->node;

  g_assert(INFD_IS_SESSION_PROXY(proxy));
  g_assert(node->type == INFD_STORAGE_NODE_NOTE);

  g_assert(node->shared.note.session == NULL ||
           (node->shared.note.session == INFD_SESSION_PROXY(proxy) &&
            node->shared.note.weakref == TRUE));

  g_object_ref(proxy);

  /* Re-link a previous session which was kept around by somebody else */
  if(node->shared.note.session != NULL)
  {
    g_object_weak_unref(
      G_OBJECT(node->shared.note.session),
      infd_directory_session_weak_ref_cb,
      node
    );
  }
  else
  {
    node->shared.note.session = INFD_SESSION_PROXY(proxy);
  }

  node->shared.note.weakref = FALSE;

  g_object_set_qdata(
    G_OBJECT(proxy),
    infd_directory_node_id_quark,
    GUINT_TO_POINTER(node->id)
  );

  g_signal_connect(
    G_OBJECT(proxy),
    "notify::idle",
    G_CALLBACK(infd_directory_session_idle_notify_cb),
    INFD_DIRECTORY(browser)
  );

  g_signal_connect(
    G_OBJECT(proxy),
    "reject-user-join",
    G_CALLBACK(infd_directory_session_reject_user_join_cb),
    INFD_DIRECTORY(browser)
  );
  
  /* TODO: Drop the session if it gets closed; don't even weak-ref
   * it in that case */

  if(infd_session_proxy_is_idle(node->shared.note.session))
  {
    infd_directory_start_session_save_timeout(INFD_DIRECTORY(browser), node);
  }
}

static void
infd_directory_browser_unsubscribe_session(InfBrowser* browser,
                                           const InfBrowserIter* iter,
                                           InfSessionProxy* proxy,
                                           InfRequest* request)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;

  g_assert(INFD_IS_SESSION_PROXY(proxy));
  g_assert(node->type == INFD_STORAGE_NODE_NOTE);

  g_assert(node->shared.note.session == INFD_SESSION_PROXY(proxy));
  g_assert(node->shared.note.weakref == FALSE);

  /* Remove save timeout. We are just keeping a weak reference to the session
   * in order to be able to re-use it when it is requested again and if
   * someone else is going to keep it around anyway, but in all other regards
   * we behave like we have dropped the session fully from memory. */
  if(node->shared.note.save_timeout != NULL)
  {
    inf_io_remove_timeout(priv->io, node->shared.note.save_timeout);
    node->shared.note.save_timeout = NULL;
  }

  g_object_weak_ref(
    G_OBJECT(node->shared.note.session),
    infd_directory_session_weak_ref_cb,
    node
  );

  node->shared.note.weakref = TRUE;
  g_object_unref(node->shared.note.session);
}

static gboolean
infd_directory_browser_get_root(InfBrowser* browser,
                                InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  iter->node_id = priv->root->id;
  iter->node = priv->root;
  return TRUE;
}

static gboolean
infd_directory_browser_get_next(InfBrowser* browser,
                                InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  node = (InfdDirectoryNode*)iter->node;
  node = node->next;

  if(node == NULL) return FALSE;

  iter->node_id = node->id;
  iter->node = node;
  return TRUE;
}

static gboolean
infd_directory_browser_get_prev(InfBrowser* browser,
                                InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  node = (InfdDirectoryNode*)iter->node;
  node = node->prev;

  if(node == NULL) return FALSE;

  iter->node_id = node->id;
  iter->node = node;
  return TRUE;
}

static gboolean
infd_directory_browser_get_parent(InfBrowser* browser,
                                  InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  node = (InfdDirectoryNode*)iter->node;
  node = node->parent;

  if(node == NULL) return FALSE;

  iter->node_id = node->id;
  iter->node = node;
  return TRUE;
}

static gboolean
infd_directory_browser_get_child(InfBrowser* browser,
                                 InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_SUBDIRECTORY, FALSE);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, FALSE);

  node = node->shared.subdir.child;
  if(node == NULL) return FALSE;

  iter->node_id = node->id;
  iter->node = node;
  return TRUE;
}

static InfRequest*
infd_directory_browser_explore(InfBrowser* browser,
                               const InfBrowserIter* iter,
                               InfRequestFunc func,
                               gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdProgressRequest* request;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_SUBDIRECTORY, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == FALSE, NULL);

  request = g_object_new(
    INFD_TYPE_PROGRESS_REQUEST,
    "type", "explore-node",
    "node-id", node->id,
    "requestor", NULL,
    NULL
  );

  if(func != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(func),
      user_data
    );
  }

  inf_browser_begin_request(browser, iter, INF_REQUEST(request));

  infd_directory_node_explore(
    directory,
    node,
    request,
    NULL
  );

  g_object_unref(request);
  return NULL;
}

static gboolean
infd_directory_browser_get_explored(InfBrowser* browser,
                                    const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_SUBDIRECTORY, FALSE);
  return node->shared.subdir.explored;
}

static gboolean
infd_directory_browser_is_subdirectory(InfBrowser* browser,
                                       const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  node = (InfdDirectoryNode*)iter->node;
  if(node->type != INFD_STORAGE_NODE_SUBDIRECTORY)
    return FALSE;
  return TRUE;
}

static InfRequest*
infd_directory_browser_add_note(InfBrowser* browser,
                                const InfBrowserIter* iter,
                                const char* name,
                                const char* type,
                                const InfAclSheetSet* sheet_set,
                                InfSession* session,
                                gboolean initial_subscribe,
                                InfRequestFunc func,
                                gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  const InfdNotePlugin* plugin;
  InfdRequest* request;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_SUBDIRECTORY, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  plugin = infd_directory_lookup_plugin(directory, type);
  g_return_val_if_fail(plugin != NULL, NULL);

  request = g_object_new(
    INFD_TYPE_REQUEST,
    "type", "add-node",
    "node-id", node->id,
    "requestor", NULL,
    NULL
  );

  if(func != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(func),
      user_data
    );
  }

  inf_browser_begin_request(browser, iter, INF_REQUEST(request));

  infd_directory_node_add_note(
    directory,
    node,
    request,
    name,
    sheet_set,
    plugin,
    session,
    NULL,
    FALSE,
    NULL,
    NULL
  );

  g_object_unref(request);
  return NULL;
}

static InfRequest*
infd_directory_browser_add_subdirectory(InfBrowser* browser,
                                        const InfBrowserIter* iter,
                                        const char* name,
                                        const InfAclSheetSet* sheet_set,
                                        InfRequestFunc func,
                                        gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdRequest* request;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_SUBDIRECTORY, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  request = g_object_new(
    INFD_TYPE_REQUEST,
    "type", "add-node",
    "node-id", node->id,
    "requestor", NULL,
    NULL
  );

  if(func != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(func),
      user_data
    );
  }

  inf_browser_begin_request(browser, iter, INF_REQUEST(request));

  infd_directory_node_add_subdirectory(
    directory,
    node,
    request,
    name,
    sheet_set,
    NULL,
    NULL,
    NULL
  );

  g_object_unref(request);
  return NULL;
}

static InfRequest*
infd_directory_browser_remove_node(InfBrowser* browser,
                                   const InfBrowserIter* iter,
                                   InfRequestFunc func,
                                   gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdRequest* request;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;

  request = g_object_new(
    INFD_TYPE_REQUEST,
    "type", "remove-node",
    "node-id", node->id,
    "requestor", NULL,
    NULL
  );

  if(func != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(func),
      user_data
    );
  }

  inf_browser_begin_request(browser, iter, INF_REQUEST(request));

  infd_directory_node_remove(directory, node, request, NULL, NULL);

  g_object_unref(request);
  return NULL;
}

static const gchar*
infd_directory_browser_get_node_name(InfBrowser* browser,
                                     const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  return node->name;
}

static const gchar*
infd_directory_browser_get_node_type(InfBrowser* browser,
                                     const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_NOTE, NULL);
  return node->shared.note.plugin->note_type;
}

static InfRequest*
infd_directory_browser_subscribe(InfBrowser* browser,
                                 const InfBrowserIter* iter,
                                 InfRequestFunc func,
                                 gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectorySubreq* subreq;
  InfdRequest* request;
  InfdSessionProxy* proxy;
  GSList* item;
  GError* error;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_NOTE, NULL);
  g_return_val_if_fail(
    node->shared.note.session == NULL ||
    node->shared.note.weakref == TRUE,
    NULL
  );

  /* See whether there is a subreq for this node. If yes, take the request
   * from there instead of creating a new one. Note that this usually does
   * not happen, since clients will ask for pending requests first, and if
   * a pending request exists, they will wait for it to finish. However, in
   * this case we support subscribing to a session while a subreq is still
   * pending, so that the session can be created immediately on the server
   * side if needed, without waiting for the client acknowledging or denying
   * the subscription.
   *
   * TODO: This is a bit of a flaw in the API. To fix this, we could allow
   * calling this function (and the other request functions) while a request
   * is already in progress, and document it such that in this case the
   * existing request will be used and the passed function added to it.
   * On the server side this would not require many changes, since there
   * are no requests other than subscribe-session that do not finish
   * immediately (at the moment).
   *
   * Only the client side functions would need to be changed such that they
   * return existing requests. This would also simplify using the API, because
   * no explicit checks for whether they are already pending requests would
   * be necessary.
   */
  subreq = infd_directory_find_subreq_by_node_id(
    directory,
    INFD_DIRECTORY_SUBREQ_SESSION,
    node->id
  );

  if(subreq != NULL)
  {
    request = subreq->shared.session.request;
    g_object_ref(request);
  }
  else
  {
    request = g_object_new(
      INFD_TYPE_REQUEST,
      "type", "subscribe-session",
      "node-id", node->id,
      "requestor", NULL,
      NULL
    );
  }

  if(func != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(func),
      user_data
    );
  }

  /* Emit begin-request if we created a new request */
  if(subreq == NULL)
  {
    inf_browser_begin_request(browser, iter, INF_REQUEST(request));
  }

  /* Take the session proxy from pending subscription requests, if any. Also,
   * remove the request reference from them, since we will finish the
   * request. */
  proxy = NULL;
  for(item = priv->subscription_requests; item != NULL; item = item->next)
  {
    subreq = (InfdDirectorySubreq*)item->data;
    if(subreq->type == INFD_DIRECTORY_SUBREQ_SESSION)
    {
      if(subreq->node_id == node->id)
      {
        /* Note there can be more than one subreq */
        g_assert(subreq->shared.session.request == request);

        g_object_unref(subreq->shared.session.request);
        subreq->shared.session.request = NULL;

        /* Get the session proxy from the subreq, to avoid making it
         * again. */
        g_assert(proxy == NULL || proxy == subreq->shared.session.session);

        if(proxy == NULL)
        {
          proxy = subreq->shared.session.session;
          g_object_ref(proxy);
        }
      }
    }
  }

  /* If there was no subreq, create the session here */
  error = NULL;
  if(proxy == NULL)
  {
    proxy = infd_directory_node_make_session(directory, node, &error);
  }

  if(proxy != NULL)
  {
    g_assert(error == NULL);

    infd_directory_node_link_session(directory, node, request, proxy);
    g_object_unref(proxy);

    inf_request_finish(
      INF_REQUEST(request),
      inf_request_result_make_subscribe_session(
        INF_BROWSER(directory),
        iter,
        INF_SESSION_PROXY(proxy)
      )
    );
  }
  else
  {
    g_assert(error != NULL);

    inf_request_fail(INF_REQUEST(request), error);
    g_error_free(error);
  }

  g_object_unref(request);
  return NULL;
}

static InfSessionProxy*
infd_directory_browser_get_session(InfBrowser* browser,
                                   const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_NOTE, NULL);
  
  if(node->shared.note.session == NULL || node->shared.note.weakref == TRUE)
    return NULL;
  return INF_SESSION_PROXY(node->shared.note.session);
}

static GSList*
infd_directory_browser_list_pending_requests(InfBrowser* browser,
                                             const InfBrowserIter* iter,
                                             const gchar* request_type)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectorySubreq* subreq;
  InfRequest* request;
  gchar* type;
  gboolean right_type;
  GSList* list;
  GSList* item;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  node = NULL;
  if(iter != NULL)
  {
    infd_directory_return_val_if_iter_fail(directory, iter, NULL);
    node = (InfdDirectoryNode*)iter->node;
  }

  list = NULL;
  for(item = priv->subscription_requests; item != NULL; item = item->next)
  {
    request = NULL;
    subreq = (InfdDirectorySubreq*)item->data;
    switch(subreq->type)
    {
    case INFD_DIRECTORY_SUBREQ_CHAT:
      break;
    case INFD_DIRECTORY_SUBREQ_SESSION:
      if(iter != NULL && subreq->node_id == node->id)
        request = INF_REQUEST(subreq->shared.session.request);
      break;
    case INFD_DIRECTORY_SUBREQ_ADD_NODE:
      if(iter != NULL && subreq->shared.add_node.parent == node)
        request = INF_REQUEST(subreq->shared.add_node.request);
      break;
    case INFD_DIRECTORY_SUBREQ_SYNC_IN:
    case INFD_DIRECTORY_SUBREQ_SYNC_IN_SUBSCRIBE:
      if(iter != NULL && subreq->shared.sync_in.parent == node)
        request = INF_REQUEST(subreq->shared.sync_in.request);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    if(request != NULL)
    {
      right_type = TRUE;
      if(request_type != NULL)
      {
        g_object_get(G_OBJECT(request), "type", &type, NULL);
        if(strcmp(type, request_type) != 0)
          right_type = FALSE;
        g_free(type);
      }

      if(right_type == TRUE)
      {
        if(g_slist_find(list, request) == NULL)
          list = g_slist_prepend(list, request);
      }
    }
  }

  return list;
}

static gboolean
infd_directory_browser_iter_from_request(InfBrowser* browser,
                                         InfRequest* request,
                                         InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  guint node_id;
  
  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_object_get(G_OBJECT(request), "node-id", &node_id, NULL);
  if(node_id == G_MAXUINT) return FALSE;
  
  node = g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id));
  if(node == NULL) return FALSE;

  iter->node_id = node_id;
  iter->node = node;
  return TRUE;
}

static InfRequest*
infd_directory_browser_query_acl_account_list(InfBrowser* browser,
                                              InfRequestFunc func,
                                              gpointer user_data)
{
  /* This should not be called because get_acl_account_list always returns the
   * full list. */
  g_return_val_if_reached(NULL);
  return NULL;
}

static const InfAclAccount**
infd_directory_browser_get_acl_account_list(InfBrowser* browser,
                                            guint* n_accounts)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryGetAclAccountListData data;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  *n_accounts = g_hash_table_size(priv->accounts);

  data.accounts = g_malloc(sizeof(const InfAclAccount*) * (*n_accounts));
  data.index = 0;

  g_hash_table_foreach(
    priv->accounts,
    infd_directory_get_acl_account_list_foreach_func,
    &data
  );

  g_assert(data.index == g_hash_table_size(priv->accounts));
  return data.accounts;
}

static const InfAclAccount*
infd_directory_browser_get_acl_local_account(InfBrowser* browser)
{
  /* There is no local account. This means direct access to the directory and
   * no ACL applies for local operations. */
  return NULL;
}

static const InfAclAccount*
infd_directory_browser_lookup_acl_account(InfBrowser* browser,
                                          const gchar* id)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  const InfdAclAccountInfo* info;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);
  info = g_hash_table_lookup(priv->accounts, id);

  if(info == NULL) return NULL;
  return &info->account;
}

static InfRequest*
infd_directory_browser_create_acl_account(InfBrowser* browser,
                                          gnutls_x509_crq_t crq,
                                          InfRequestFunc func,
                                          gpointer user_data)
{
  GError* error;
  InfRequest* request;
  gnutls_x509_crt_t cert;
  gnutls_x509_crt_t* certs;
  InfCertificateChain* chain;
  InfdAclAccountInfo* info;

  gchar* account_id;
  gchar* account_name;
  gboolean ret;

  request = g_object_new(
    INFD_TYPE_REQUEST,
    "type", "create-acl-account",
    "requestor", NULL,
    NULL
  );

  if(func != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(func),
      user_data
    );
  }

  inf_browser_begin_request(browser, NULL, INF_REQUEST(request));

  error = NULL;

  cert = infd_directory_create_certificate_from_crq(
    INFD_DIRECTORY(browser),
    crq,
    365 * DAYS,
    &error
  );

  if(error != NULL)
  {
    inf_request_fail(request, error);
    g_object_unref(request);
    return NULL;
  }

  ret = infd_directory_account_info_from_certificate(
    cert,
    &account_id,
    &account_name,
    &error
  );

  if(error != NULL)
  {
    gnutls_x509_crt_deinit(cert);
    inf_request_fail(request, error);
    g_object_unref(request);
    return NULL;
  }

  info = infd_directory_create_acl_account_with_certificate(
    INFD_DIRECTORY(browser),
    account_id,
    account_name,
    cert,
    NULL,
    &error
  );

  g_free(account_id);
  g_free(account_name);

  if(error != NULL)
  {
    gnutls_x509_crt_deinit(cert);
    inf_request_fail(request, error);
    g_object_unref(request);
    return NULL;
  }

  certs = g_malloc(sizeof(gnutls_x509_crt_t));
  *certs = cert;

  chain = inf_certificate_chain_new(certs, 1);

  inf_request_finish(
    request,
    inf_request_result_make_create_acl_account(
      INF_BROWSER(browser),
      &info->account,
      chain
    )
  );

  inf_certificate_chain_unref(chain);
  g_object_unref(request);

  return NULL;
}

static InfRequest*
infd_directory_browser_remove_acl_account(InfBrowser* browser,
                                          const InfAclAccount* account,
                                          InfRequestFunc func,
                                          gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdRequest* request;
  InfdAclAccountInfo* info;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  request = g_object_new(
    INFD_TYPE_REQUEST,
    "type", "remove-acl-account",
    "requestor", NULL,
    NULL
  );

  if(func != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(func),
      user_data
    );
  }

  info = g_hash_table_lookup(priv->accounts, account->id);

  g_assert(info != NULL);
  g_assert(&info->account == account);

  inf_browser_begin_request(browser, NULL, INF_REQUEST(request));

  infd_directory_remove_acl_account(directory, info, NULL, request);

  g_object_unref(request);
  return NULL;
}

static InfRequest*
infd_directory_browser_query_acl(InfBrowser* browser,
                                 const InfBrowserIter* iter,
                                 InfRequestFunc func,
                                 gpointer user_data)
{
  /* We always have the full ACL since we read it directly with the
   * exploration of a node. Therefore, there is nothing to query and the
   * full ACL is available with inf_browser_get_acl(). */
  g_return_val_if_reached(NULL);
  return NULL;
}

static gboolean
infd_directory_browser_has_acl(InfBrowser* browser,
                               const InfBrowserIter* iter,
                               const InfAclAccount* account)
{
  /* The full ACL is always available */
  return TRUE;
}

static const InfAclSheetSet*
infd_directory_browser_get_acl(InfBrowser* browser,
                               const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);
  node = (InfdDirectoryNode*)iter->node;

  return node->acl;
}

static InfRequest*
infd_directory_browser_set_acl(InfBrowser* browser,
                               const InfBrowserIter* iter,
                               const InfAclSheetSet* sheet_set,
                               InfRequestFunc func,
                               gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdRequest* request;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;

  request = g_object_new(
    INFD_TYPE_REQUEST,
    "type", "set-acl",
    "node-id", node->id,
    "requestor", NULL,
    NULL
  );

  if(func != NULL)
  {
    g_signal_connect_after(
      G_OBJECT(request),
      "finished",
      G_CALLBACK(func),
      user_data
    );
  }

  inf_browser_begin_request(browser, iter, INF_REQUEST(request));

  node->acl = inf_acl_sheet_set_merge_sheets(node->acl, sheet_set);

  infd_directory_announce_acl_sheets(
    directory,
    node,
    request,
    sheet_set,
    NULL
  );

  infd_directory_write_acl(directory, node);

  inf_request_finish(
    INF_REQUEST(request),
    inf_request_result_make_set_acl(INF_BROWSER(directory), iter)
  );

  g_object_unref(request);
  return NULL;
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

  directory_class->connection_added = NULL;
  directory_class->connection_removed = NULL;

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
    PROP_PRIVATE_KEY,
    g_param_spec_pointer(
      "private-key",
      "Private Key",
      "The private key of the server with which belongs to its certificate",
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CERTIFICATE,
    g_param_spec_boxed(
      "certificate",
      "Certificate",
      "The certificate chain of the server",
      INF_TYPE_CERTIFICATE_CHAIN,
      G_PARAM_READWRITE
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
   * InfdDirectory::connection-added:
   * @directory: The #InfdDirectory emitting the signal.
   * @connection: The #InfXmlConnection that was added.
   *
   * This signal is emitted when a connection that is served by the
   * #InfdDirectory was added. The only way this can happen is by a call to
   * infd_directory_add_connection(). This can be done automatically by an
   * #InfdServerPool instance, however.
   **/
  directory_signals[CONNECTION_ADDED] = g_signal_new(
    "connection-added",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdDirectoryClass, connection_added),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XML_CONNECTION
  );

  /**
   * InfdDirectory::connection-removed:
   * @directory: The #InfdDirectory emitting the signal.
   * @connection: The #InfXmlConnection that was removed.
   *
   * This signal is emitted when a connection stopes being served by
   * @directory. Usually this happens only when the connection is closed.
   **/
  directory_signals[CONNECTION_REMOVED] = g_signal_new(
    "connection-removed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdDirectoryClass, connection_removed),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XML_CONNECTION
  );

  g_object_class_override_property(object_class, PROP_STATUS, "status");
}

  static void
infd_directory_communication_object_init(gpointer g_iface,
    gpointer iface_data)
{
  InfCommunicationObjectIface* iface;
  iface = (InfCommunicationObjectIface*)g_iface;

  iface->received = infd_directory_communication_object_received;
}

  static void
infd_directory_browser_init(gpointer g_iface,
    gpointer iface_data)
{
  InfBrowserIface* iface;
  iface = (InfBrowserIface*)g_iface;

  iface->error = NULL;
  iface->node_added = NULL;
  iface->node_removed = NULL;
  iface->subscribe_session = infd_directory_browser_subscribe_session;
  iface->unsubscribe_session = infd_directory_browser_unsubscribe_session;
  iface->begin_request = NULL;
  iface->acl_account_added = NULL;
  iface->acl_account_removed = NULL;
  iface->acl_local_account_changed = NULL;
  iface->acl_changed = NULL;

  iface->get_root = infd_directory_browser_get_root;
  iface->get_next = infd_directory_browser_get_next;
  iface->get_prev = infd_directory_browser_get_prev;
  iface->get_parent = infd_directory_browser_get_parent;
  iface->get_child = infd_directory_browser_get_child;
  iface->explore = infd_directory_browser_explore;
  iface->get_explored = infd_directory_browser_get_explored;
  iface->is_subdirectory = infd_directory_browser_is_subdirectory;
  iface->add_note = infd_directory_browser_add_note;
  iface->add_subdirectory = infd_directory_browser_add_subdirectory;
  iface->remove_node = infd_directory_browser_remove_node;
  iface->get_node_name = infd_directory_browser_get_node_name;
  iface->get_node_type = infd_directory_browser_get_node_type;
  iface->subscribe = infd_directory_browser_subscribe;
  iface->get_session = infd_directory_browser_get_session;
  iface->list_pending_requests = infd_directory_browser_list_pending_requests;
  iface->iter_from_request = infd_directory_browser_iter_from_request;

  iface->query_acl_account_list =
    infd_directory_browser_query_acl_account_list;
  iface->get_acl_account_list = infd_directory_browser_get_acl_account_list;
  iface->get_acl_local_account = infd_directory_browser_get_acl_local_account;
  iface->lookup_acl_account = infd_directory_browser_lookup_acl_account;
  iface->create_acl_account = infd_directory_browser_create_acl_account;
  iface->remove_acl_account = infd_directory_browser_remove_acl_account;
  iface->query_acl = infd_directory_browser_query_acl;
  iface->has_acl = infd_directory_browser_has_acl;
  iface->get_acl = infd_directory_browser_get_acl;
  iface->set_acl = infd_directory_browser_set_acl;
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

    static const GInterfaceInfo browser_info = {
      infd_directory_browser_init,
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

    g_type_add_interface_static(
      directory_type,
      INF_TYPE_BROWSER,
      &browser_info
    );
  }

  return directory_type;
}

/*
 * Public API.
 */

 /**
 * infd_directory_new:
 * @io: IO object to watch connections and schedule timeouts.
 * @storage: Storage backend that is used to read/write notes from
 * permanent memory into #InfBuffer objects, or %NULL.
 * @comm_manager: A #InfCommunicationManager to register added
 * connections to and which forwards incoming data to the directory
 * or running sessions.
 *
 * Creates a new #InfdDirectory. If @storage is %NULL then the directory
 * keeps all content in memory. This can make sense for ad-hoc sessions where
 * no central document storage is required.
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
  g_return_val_if_fail(storage == NULL || INFD_IS_STORAGE(storage), NULL);
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
 * Return Value: An #InfdStorage.
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
 * infd_directory_set_certificate:
 * @directory: A #InfdDirectory.
 * @key: The private key of the directory.
 * @cert: The certificate chain of the directory.
 *
 * Sets the private key and certificate chain of the directory. The directory
 * does not use these for certificate authentication with added connections.
 * Connections should already be authenticated, for example with the means of
 * #InfXmppConnection.
 *
 * At the moment, the directory certificate is used to sign incoming
 * certificate requests. Selected clients can request a certificate signed
 * with the server's certificates, see infc_browser_request_certificate().
 * If the server certificate is set with this function, the request is handled
 * properly. If no certificate is set with this function, such a request is
 * rejected.
 */
void
infd_directory_set_certificate(InfdDirectory* directory,
                               gnutls_x509_privkey_t key,
                               InfCertificateChain* cert)
{
  InfdDirectoryPrivate* priv;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->certificate != NULL)
    inf_certificate_chain_unref(priv->certificate);

  priv->private_key = key;
  priv->certificate = cert;

  if(cert != NULL)
    inf_certificate_chain_ref(cert);

  g_object_notify(G_OBJECT(directory), "private-key");
  g_object_notify(G_OBJECT(directory), "certificate");
}

/**
 * infd_directory_add_plugin:
 * @directory: A #InfdDirectory.
 * @plugin: A #InfdNotePlugin.
 *
 * Adds @plugin to @directory. This allows the directory to create sessions
 * of the plugin's type. Only one plugin of each type can be added to the
 * directory. The plugin's storage_type must match the storage of @directory.
 * If the directory's storage is %NULL, then the plugin's storage type does
 * not matter, and the plugin's @session_read and @session_write functions
 * will not be used (and can therefore be %NULL).
 *
 * Return Value: Whether the plugin was added successfully.
 **/
gboolean
infd_directory_add_plugin(InfdDirectory* directory,
                          const InfdNotePlugin* plugin)
{
  InfdDirectoryPrivate* priv;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  g_return_val_if_fail(plugin != NULL, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_return_val_if_fail(
    priv->storage == NULL ||
    strcmp(
      plugin->storage_type,
      g_type_name(G_TYPE_FROM_INSTANCE(priv->storage))
    ) == 0,
    FALSE
  );

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
 * @connection: A #InfXmlConnection.
 *
 * Adds @connection to the connections of @directory. The directory will then
 * receive requests from @connection. If the directory's method manager does
 * not contain a "central" method for connection's network, then the
 * connection will not be added and the function returns %FALSE.
 *
 * The connection is removed again automatically if it is closed.
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
  info->account = NULL;

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
  {
    info->account =
      infd_directory_login_by_certificate(directory, connection);
    infd_directory_send_welcome_message(directory, connection);
  }

  g_signal_emit(
    G_OBJECT(directory),
    directory_signals[CONNECTION_ADDED],
    0,
    connection
  );

  return TRUE;
}

/**
 * infd_directory_get_acl_account_for_connection:
 * @directory: A #InfdDirectory.
 * @connection: A @connection added to @directory.
 *
 * This function returns the #InfAclAccount that the given connection is
 * logged into. The @connection must have been added to the directory before
 * with infd_directory_add_connection(). If no special login was performed,
 * the default account is returned.
 *
 * Returns: A #InfAclAccount owned by @directory. It must not be freed.
 */
const InfAclAccount*
infd_directory_get_acl_account_for_connection(InfdDirectory* directory,
                                              InfXmlConnection* connection)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryConnectionInfo* info;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  info = (InfdDirectoryConnectionInfo*)g_hash_table_lookup(
    priv->connections,
    connection
  );

  g_assert(info != NULL);
  return &info->account->account;
}

/**
 * infd_directory_set_acl_account_for_connection:
 * @directory: A #InfdDirectory.
 * @connection: A @connection added to @directory.
 * @account: A #InfAclAccount available in @directory.
 *
 * This function changes the #InfAclAccount that the given connection is
 * logged into. The @connection must have been added to the directory before
 * with infd_directory_add_connection(). In order to remove a login, @account
 * should be set to the default account.
 */
void
infd_directory_set_acl_account_for_connection(InfdDirectory* directory,
                                              InfXmlConnection* connection,
                                              const InfAclAccount* account)
{
  InfdDirectoryPrivate* priv;
  const InfdAclAccountInfo* info;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(account != NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  info = (const InfdAclAccountInfo*)g_hash_table_lookup(
    priv->accounts,
    account->id
  );

  g_assert(info != NULL);
  infd_directory_change_acl_account(directory, connection, info);
}

/**
 * infd_directory_foreach_connection:
 * @directory: A #InfdDirectory.
 * @func: The function to call for each connection in @directory.
 * @user_data: Additional data to pass to the callback function.
 *
 * Calls @func for each connection in @directory that has previously been
 * added to the directory. It is allowed to add and remove connections while
 * this function is being called.
 */
void
infd_directory_foreach_connection(InfdDirectory* directory,
                                  InfdDirectoryForeachConnectionFunc func,
                                  gpointer userdata)
{
  InfdDirectoryPrivate* priv;
  GList* keys;
  GList* item;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  g_return_if_fail(func != NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  keys = g_hash_table_get_keys(priv->connections);

  for(item = keys; item != NULL; item = item->next)
  {
    /* Make sure the entry still exists: */
    if(g_hash_table_lookup(priv->connections, item->data) != NULL)
      func(INF_XML_CONNECTION(item->data), userdata);
  }

  g_list_free(keys);
}

/**
 * infd_directory_iter_save_session:
 * @directory: A #InfdDirectory.
 * @iter: A #InfBrowserIter pointing to a note in @directory.
 * @error: Location to store error information.
 *
 * Attempts to store the session the node @iter points to represents into the
 * background storage.
 *
 * Return Value: %TRUE if the operation succeeded, %FALSE otherwise.
 */
gboolean
infd_directory_iter_save_session(InfdDirectory* directory,
                                 InfBrowserIter* iter,
                                 GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  gchar* path;
  InfSession* session;
  gboolean result;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), FALSE);
  infd_directory_return_val_if_iter_fail(directory, iter, FALSE);

  priv = INFD_DIRECTORY_PRIVATE(directory);
  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_NOTE, FALSE);

  if(priv->storage == NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NO_STORAGE,
      "%s",
      _("No background storage available")
    );

    return FALSE;
  }

  infd_directory_node_get_path(node, &path, NULL);

  g_object_get(
    G_OBJECT(node->shared.note.session),
    "session", &session,
    NULL
  );

  result = node->shared.note.plugin->session_write(
    priv->storage,
    session,
    path,
    node->shared.note.plugin->user_data,
    error
  );

  /* TODO: Unset modified flag of buffer if result == TRUE */

  g_object_unref(session);
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
  InfChatSession* chat_session;
  InfSession* session;

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

      chat_session = inf_chat_session_new(
        priv->communication_manager,
        256,
        INF_SESSION_RUNNING,
        NULL,
        NULL
      );

      priv->chat_session = INFD_SESSION_PROXY(
        g_object_new(
          INFD_TYPE_SESSION_PROXY,
          "io", priv->io,
          "session", chat_session,
          "subscription-group", group,
          NULL
        )
      );

      /* TODO: Call inf_browser_subscribe_session */

      inf_communication_group_set_target(
        INF_COMMUNICATION_GROUP(group),
        INF_COMMUNICATION_OBJECT(priv->chat_session)
      );

      g_object_unref(chat_session);
      g_object_unref(group);

      g_object_notify(G_OBJECT(directory), "chat-session");
    }
  }
  else
  {
    if(priv->chat_session != NULL)
    {
      g_object_get(G_OBJECT(priv->chat_session), "session", &session, NULL);
      inf_session_close(session);
      g_object_unref(session);

      /* TODO: Call inf_browser_unsubscribe_session */

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

/**
 * infd_directory_create_acl_account:
 * @directory: A #InfdDirectory.
 * @account_id: The ID for the new account.
 * @account_name: The name of the new account.
 * @transient: Whether the account should be transient or not.
 * @certificates: An array of certificates to be associated to the account,
 * or %NULL.
 * @n_certificates: The number of certificates.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Creates a new account on the directory with the given @account_id and
 * @account_name. If the @certificates array is not empty and a clients connects
 * with one of the certificates, the client will automatically be logged into
 * the account.
 *
 * If the @transient parameter is %TRUE then the account is made transient,
 * i.e. it will not be stored to disk. When the server is re-started, the
 * account will no longer exist. If the parameter is %FALSE, then the account
 * is persistent.
 *
 * The given account ID must be unique, i.e. must not be in use already. The
 * same is the case for the DN of the certificates.
 *
 * This function is similar to inf_browser_create_acl_account(), but it
 * allows more options.
 *
 * Returns: A new #InfAclAccount, or %NULL in case of error.
 */
const InfAclAccount*
infd_directory_create_acl_account(InfdDirectory* directory,
                                  const gchar* account_id,
                                  const gchar* account_name,
                                  gboolean transient,
                                  gnutls_x509_crt_t* certificates,
                                  guint n_certificates,
                                  GError** error)
{
  InfdAclAccountInfo* info;

  g_return_val_if_fail(INFD_IS_DIRECTORY(directory), NULL);
  g_return_val_if_fail(account_id != NULL, NULL);
  g_return_val_if_fail(account_name != NULL, NULL);
  g_return_val_if_fail(certificates != NULL || n_certificates == 0, NULL);

  info = infd_directory_create_acl_account_with_certificates(
    directory,
    account_id,
    account_name,
    transient,
    certificates,
    n_certificates,
    NULL,
    error
  );

  if(info == NULL)
    return NULL;

  return &info->account;
}

/* vim:set et sw=2 ts=2: */
