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
#include <libinfinity/server/infd-node-request.h>
#include <libinfinity/server/infd-explore-request.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-chat-session.h>
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

typedef struct _InfdDirectoryNode InfdDirectoryNode;
struct _InfdDirectoryNode {
  InfdDirectoryNode* parent;
  InfdDirectoryNode* prev;
  InfdDirectoryNode* next;

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
  InfdNodeRequest* request;
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
      InfdNodeRequest* request;
    } session;

    struct {
      InfdDirectoryNode* parent;
      InfCommunicationHostedGroup* group;
      const InfdNotePlugin* plugin;
      gchar* name;
      InfAclSheetSet* sheet_set;
      /* TODO: Isn't group already present in proxy? */
      InfdSessionProxy* proxy;
      InfdNodeRequest* request;
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
      InfdNodeRequest* request;
    } sync_in;
  } shared;
};

/* Local request (to delay requests made by InfBrowser API) */
typedef enum _InfdDirectoryLocreqType {
  INFD_DIRECTORY_LOCREQ_EXPLORE_NODE,
  INFD_DIRECTORY_LOCREQ_ADD_NODE,
  INFD_DIRECTORY_LOCREQ_REMOVE_NODE,
  INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION,
  INFD_DIRECTORY_LOCREQ_SET_ACL
} InfdDirectoryLocreqType;

typedef struct _InfdDirectoryLocreq InfdDirectoryLocreq;
struct _InfdDirectoryLocreq {
  InfdDirectory* directory;
  InfdDirectoryLocreqType type;
  InfdNodeRequest* request;
  InfIoDispatch* dispatch;

  union {
    struct {
      InfdDirectoryNode* node;
    } explore_node;

    struct {
      InfdDirectoryNode* node;
      gchar* name;
      InfAclSheetSet* sheet_set;
      const InfdNotePlugin* plugin; /* NULL for subdirectory */
      InfSession* session; /* NULL for initially empty notes */
      gboolean initial_subscribe; /* Ignored for subdirectory */
    } add_node;

    struct {
      InfdDirectoryNode* node;
    } remove_node;

    struct {
      InfdDirectoryNode* node;
    } subscribe_session;

    struct {
      InfdDirectoryNode* node;
      InfAclSheetSet* sheet_set;
    } set_acl;
  } shared;
};

typedef struct _InfdDirectoryConnectionInfo InfdDirectoryConnectionInfo;
struct _InfdDirectoryConnectionInfo {
  guint seq_id;
  const InfAclUser* user;
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

  InfAclTable* acl_table;
  GSList* user_list_connections;

  guint node_counter;
  GHashTable* nodes; /* Mapping from id to node */
  InfdDirectoryNode* root;

  GSList* sync_ins;
  GSList* subscription_requests;
  GSList* local_requests;

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

static gboolean
infd_directory_get_connection_info_by_acl_user_func(gpointer key,
                                                    gpointer value,
                                                    gpointer user_data)
{
  if(key == user_data)
    return TRUE;
  return FALSE;
}

static InfdDirectoryConnectionInfo*
infd_directory_get_connection_info_by_acl_user(InfdDirectory* directory,
                                               const InfAclUser* user)
{
  /* TODO: There could be more than one. We should return a list
   * of infos, or so. */
  return g_hash_table_find(
    INFD_DIRECTORY_PRIVATE(directory)->connections,
    infd_directory_get_connection_info_by_acl_user_func,
    (gpointer)user
  );
}

static void
infd_directory_write_user_list(InfdDirectory* directory)
{
  InfdDirectoryPrivate* priv;
  const InfAclUser** users;
  guint n_users;
  GError* error;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  if(priv->storage != NULL)
  {
    error = NULL;
    users = inf_acl_table_get_user_list(priv->acl_table, &n_users);

    infd_storage_write_user_list(
      priv->storage,
      users,
      n_users,
      &error
    );

    if(error != NULL)
    {
      g_warning(
        _("Failed to write user list to storage: %s\n"
          "Keeping it in memory for now, but it will not be available "
          "after server restart"),
        error->message
      );

      g_error_free(error);
      error = NULL;
    }

    g_free(users);
  }
}

static void
infd_directory_acl_table_user_added_cb(InfAclTable* acl_table,
                                       const InfAclUser* user,
                                       gpointer user_data)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  GSList* item;
  xmlNodePtr xml;
  InfXmlConnection* connection;

  directory = INFD_DIRECTORY(user_data);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  xml = xmlNewNode(NULL, (const xmlChar*)"add-user");
  inf_acl_user_to_xml(user, xml, FALSE);

  for(item = priv->user_list_connections;
      item != NULL;
      item = g_slist_next(item))
  {
    connection = INF_XML_CONNECTION(item->data);

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      connection,
      xmlCopyNode(xml, 1)
    );
  }

  inf_browser_acl_user_added(INF_BROWSER(directory), user);

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
      inf_acl_table_sheet_set_to_xml(priv->acl_table, sheets, xml);
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
      if(strcmp(sheets->sheets[i].user->user_id, "default") == 0 ||
         sheets->sheets[i].user == info->user)
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
      inf_acl_table_sheet_set_to_xml(priv->acl_table, &set, xml);
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
  inf_browser_acl_changed(INF_BROWSER(directory), &iter, sheet_set);
}

static const InfAclUser*
infd_directory_add_user_for_certificate(InfdDirectory* directory,
                                        gnutls_x509_crt_t cert)
{
  InfdDirectoryPrivate* priv;
  gchar* fingerprint;
  gchar* name;
  InfAclUser* user;
  const InfAclUser* ret_user;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  fingerprint = inf_cert_util_get_fingerprint(cert, GNUTLS_DIG_SHA256);
  name = inf_cert_util_get_dn_by_oid(cert, GNUTLS_OID_X520_COMMON_NAME, 0);

  user = inf_acl_user_new(fingerprint, name);

  /* TODO: inf_acl_table_add_user should return the new user object */
  if(inf_acl_table_add_user(priv->acl_table, user, TRUE) == TRUE)
    infd_directory_write_user_list(directory);

  ret_user = inf_acl_table_get_user(priv->acl_table, fingerprint);

  g_free(name);
  g_free(fingerprint);

  return ret_user;
}

static const InfAclUser*
infd_directory_add_user_for_connection(InfdDirectory* directory,
                                       InfXmlConnection* connection)
{
  InfdDirectoryPrivate* priv;
  InfCertificateChain* chain;
  const InfAclUser* user;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  g_object_get(G_OBJECT(connection), "remote-certificate", &chain, NULL);

  user = NULL;
  if(chain != NULL)
  {
    user = infd_directory_add_user_for_certificate(
      directory,
      inf_certificate_chain_get_own_certificate(chain)
    );

    inf_certificate_chain_unref(chain);
  }
  else
  {
    user = inf_acl_table_get_user(priv->acl_table, "default");
  }

  g_assert(user != NULL);
  return user;
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
  const InfAclUser* user;
  InfAclUser* new_user;
  InfAclSheetSet* sheet_set;
  InfAclSheet* sheet;
  gboolean user_added;

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
  if(node != NULL)
  {
    iter.node_id = node->id;
    iter.node = node;
    sheet_set = inf_acl_table_get_clear_sheets(priv->acl_table, &iter);
  }
  else
  {
    sheet_set = inf_acl_sheet_set_new();
  }

  user_added = FALSE;
  for(item = acl; item != NULL; item = g_slist_next(item))
  {
    storage_acl = (InfdStorageAcl*)item->data;
    user = inf_acl_table_get_user(priv->acl_table, storage_acl->user_id);

    /* It is possible that we do not find this user in the user table. For
     * example this can happen if reading the initial user table failed. In
     * that case, create a user here. Whenever that user shows up we replace
     * the name and times. */
    if(user == NULL)
    {
      new_user = inf_acl_user_new(storage_acl->user_id, NULL);
      inf_acl_table_add_user(priv->acl_table, new_user, FALSE);
      user = new_user;
      user_added = TRUE;
    }

    sheet = inf_acl_sheet_set_add_sheet(sheet_set, user);
    sheet->mask = storage_acl->mask;
    sheet->perms = storage_acl->perms;
  }

  infd_storage_acl_list_free(acl);

  /* If we have added one or more new users to the user list, write it to
   * storage. */
  if(user_added == TRUE)
    infd_directory_write_user_list(directory);

  return sheet_set;
}

static void
infd_directory_write_acl(InfdDirectory* directory,
                         InfdDirectoryNode* node)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;
  const InfAclSheetSet* all_sheets;
  gchar* path;
  GError* error;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* Write the changed ACL into the storage */
  if(priv->storage != NULL)
  {
    iter.node_id = node->id;
    iter.node = node;

    all_sheets = inf_acl_table_get_sheets(priv->acl_table, &iter);

    infd_directory_node_get_path(node, &path, NULL);
    error = NULL;

    infd_storage_write_acl(priv->storage, path, all_sheets, &error);

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
  InfAclUser** users;
  guint i;
  guint n_users;
  gboolean merged;
  gboolean merge_result;
  gboolean connected;
  guint overlap;
  const InfAclUser* old_user;
  InfdDirectoryConnectionInfo* info;
  const InfAclUser* default_user;
  InfAclSheetSet* sheet_set;
  InfBrowserIter iter;
  InfAclSheet* default_sheet;
  guint64 default_mask;
  GError* error;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  error = NULL;

  users = infd_storage_read_user_list(
    priv->storage,
    &n_users,
    &error
  );

  if(error != NULL)
  {
    g_assert(users == NULL);

    g_warning(
      _("Failed to read user list from storage: %s\n"
        "Will start with an empty user list."),
      error->message
    );

    g_error_free(error);
    error = NULL;
  }
  else
  {
    merged = FALSE;
    overlap = 0;
    for(i = 0; i < n_users; ++i)
    {
      connected = FALSE;
      old_user = inf_acl_table_get_user(priv->acl_table, users[i]->user_id);
      if(old_user != NULL)
      {
        ++overlap;

        info = infd_directory_get_connection_info_by_acl_user(
          directory,
          old_user
        );

        /* In case of a conflict (user both present in storage and memory),
         * we take the settings from the user from storage except the user
         * is currently connected in which case we "know better" and take
         * the settings from the connected user. */
        if(info != NULL)
          connected = TRUE;
      }

      merge_result = inf_acl_table_add_user(
        priv->acl_table,
        users[i],
        connected
      );

      /* Remember whether any settings where changed with respect to what
       * was in storage. */
      merged = merged || merge_result;
    }

    /* Write updated user list back to disk, if anything changed, or if
     * we have users in memory that are not present in the list on disk. */
    /* TODO: Should we move this to the user_added signal handler? Would
     * make sense, but we should then have a possibility to process batch
     * updates. */
    if(merged == TRUE || overlap < inf_acl_table_get_n_users(priv->acl_table))
      infd_directory_write_user_list(directory);

    g_free(users);
  }

  sheet_set = infd_directory_read_acl(
    directory,
    "/",
    priv->root,
    &error
  );

  default_user = inf_acl_table_get_user(priv->acl_table, "default");
  g_assert(default_user != NULL);

  iter.node_id = priv->root->id;
  iter.node = priv->root;

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
    sheet_set = inf_acl_table_get_clear_sheets(priv->acl_table, &iter);
    default_sheet = inf_acl_sheet_set_add_sheet(sheet_set, default_user);
    default_sheet->mask = INF_ACL_MASK_ALL;
    default_sheet->perms = 0;

    /* Make sure this "revoke-everything" sheet is not written to disk. */
    inf_acl_table_insert_sheets(priv->acl_table, &iter, sheet_set);

    infd_directory_announce_acl_sheets(
      directory,
      priv->root,
      sheet_set,
      NULL
    );
  }
  else
  {
    /* Note that the sheets array already includes sheets that clear the
     * existing ACL. */

    /* Make sure there are permissions for the default user defined. If there
     * are not, use sensible default permissions. */
    default_sheet = inf_acl_sheet_set_add_sheet(sheet_set, default_user);

    default_mask = default_sheet->mask;
    default_sheet->perms |= (INF_ACL_MASK_DEFAULT & ~default_mask);
    default_sheet->mask = INF_ACL_MASK_ALL;

    inf_acl_table_insert_sheets(priv->acl_table, &iter, sheet_set);

    infd_directory_announce_acl_sheets(
      directory,
      priv->root,
      sheet_set,
      NULL
    );

    if((default_mask & INF_ACL_MASK_ALL) != INF_ACL_MASK_ALL)
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

/* Called after a session proxy has been created for a newly added node.
 * If attempts to store the node in the storage. If it cannot be stored, the
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
                                 InfdSessionProxy* proxy)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);
  g_assert(node->shared.note.session == NULL);

  iter.node = node;
  iter.node_id = node->id;

  inf_browser_subscribe_session(
    INF_BROWSER(directory),
    &iter,
    INF_SESSION_PROXY(proxy)
  );
}

static void
infd_directory_node_unlink_session(InfdDirectory* directory,
                                   InfdDirectoryNode* node)
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
    INF_SESSION_PROXY(node->shared.note.session)
  );
}

/* Notes are saved into the storage when save_notes is TRUE. */
static void
infd_directory_node_unlink_child_sessions(InfdDirectory* directory,
                                          InfdDirectoryNode* node,
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
  node->acl_connections = NULL;

  if(parent != NULL)
  {
    infd_directory_node_link(node, parent);
  }
  else
  {
    node->prev = NULL;
    node->next = NULL;
  }

  if(sheet_set != NULL)
  {
    /* Set the node ACL immediately after it was created, to avoid possible
     * races. Note this does not require the hash table entry yet, and
     * especially that the "node-added" signal was not emitted yet. */
    iter.node_id = node_id;
    iter.node = node;
    inf_acl_table_insert_sheets(priv->acl_table, &iter, sheet_set);

    if(write_acl == TRUE)
      infd_directory_write_acl(directory, node);
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
  InfdDirectoryLocreq* locreq;

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
  iter.node_id = node->id;
  iter.node = node;
  inf_acl_table_clear_sheets(priv->acl_table, &iter);

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

  for(item = priv->local_requests; item != NULL; item = item->next)
  {
    locreq = (InfdDirectoryLocreq*)item->data;
    switch(locreq->type)
    {
    case INFD_DIRECTORY_LOCREQ_EXPLORE_NODE:
      if(locreq->shared.explore_node.node == node)
        locreq->shared.explore_node.node = NULL;
      break;
    case INFD_DIRECTORY_LOCREQ_ADD_NODE:
      if(locreq->shared.add_node.node == node)
        locreq->shared.add_node.node = NULL;
      break;
    case INFD_DIRECTORY_LOCREQ_REMOVE_NODE:
      if(locreq->shared.remove_node.node == node)
        locreq->shared.remove_node.node = NULL;
      break;
    case INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION:
      if(locreq->shared.subscribe_session.node == node)
        locreq->shared.subscribe_session.node = NULL;
      break;
    case INFD_DIRECTORY_LOCREQ_SET_ACL:
      if(locreq->shared.set_acl.node == node)
        locreq->shared.set_acl.node = NULL;
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
                             InfXmlConnection* except,
                             const gchar* seq)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;
  const InfAclSheetSet* sheet_set;
  InfdDirectoryConnectionInfo* info;
  xmlNodePtr xml;
  xmlNodePtr copy_xml;
  GSList* item;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  iter.node_id = node->id;
  iter.node = node;

  sheet_set = inf_acl_table_get_sheets(priv->acl_table, &iter);
  inf_browser_node_added(INF_BROWSER(directory), &iter);

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

      if(sheet_set != NULL)
      {
        infd_directory_acl_sheets_to_xml_for_connection(
          directory,
          node->acl_connections,
          sheet_set,
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
                               const gchar* seq)
{
  InfdDirectoryPrivate* priv;
  InfBrowserIter iter;
  xmlNodePtr xml;
  GSList* item;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  iter.node_id = node->id;
  iter.node = node;

  inf_browser_node_removed(INF_BROWSER(directory), &iter);

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
  InfdNodeRequest* request;

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
  InfdNodeRequest* request;
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

  infd_directory_node_link_session(directory, node, sync_in->proxy);

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

  sync_in->name = NULL; /* Don't free, we passed ownership */
  request = sync_in->request;
  g_object_ref(request);
  infd_directory_remove_sync_in(directory, sync_in);

  /* Don't send to conn since the completed synchronization already lets the
   * remote site know that the node was inserted. */
  infd_directory_node_register(directory, node, conn, NULL);

  iter.node_id = node->id;
  iter.node = node;

  inf_node_request_finished(
    INF_NODE_REQUEST(request),
    &iter,
    NULL
  );

  g_object_unref(request);
}

static InfdDirectorySyncIn*
infd_directory_add_sync_in(InfdDirectory* directory,
                           InfdDirectoryNode* parent,
                           InfdNodeRequest* request,
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

  g_signal_connect(
    G_OBJECT(session),
    "synchronization-failed",
    G_CALLBACK(infd_directory_sync_in_synchronization_failed_cb),
    sync_in
  );

  g_signal_connect(
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
                                  InfdNodeRequest* request,
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
                                   InfdNodeRequest* request,
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
    proxy = infd_directory_create_session_proxy_for_node(
      directory,
      node_id,
      session
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
                                  InfdNodeRequest* request,
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


static InfdDirectoryNode*
infd_directory_node_create_new_note(InfdDirectory* directory,
                                    InfdDirectoryNode* parent,
                                    InfCommunicationHostedGroup* group,
                                    guint node_id,
                                    const gchar* name,
                                    const InfAclSheetSet* sheet_set,
                                    const InfdNotePlugin* plugin,
                                    InfSession* session,
                                    GError** error)
{
  InfdSessionProxy* proxy;
  InfdDirectoryNode* node;
  gboolean ensured;

  if(session != NULL)
  {
    proxy = infd_directory_create_session_proxy_for_node(
      directory,
      node_id,
      session
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

  node = infd_directory_node_new_note(
    directory,
    parent,
    node_id,
    g_strdup(name),
    sheet_set,
    TRUE,
    plugin
  );

  infd_directory_node_link_session(directory, node, proxy);
  g_object_unref(proxy);

  return node;
}

static gboolean
infd_directory_node_explore(InfdDirectory* directory,
                            InfdDirectoryNode* node,
                            InfdExploreRequest* request,
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
    inf_request_fail(INF_REQUEST(request), local_error);
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
      inf_request_fail(INF_REQUEST(request), local_error);
      g_propagate_error(error, local_error);
      return FALSE;
    }

    g_ptr_array_add(acls, sheet_set);
  }

  g_free(path);
  infd_explore_request_initiated(request, acls->len);

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
      infd_directory_node_register(directory, new_node, NULL, NULL);
    }

    infd_explore_request_progress(request);
  }

  g_ptr_array_free(acls, TRUE);

  iter.node_id = node->id;
  iter.node = node;
  inf_node_request_finished(
    INF_NODE_REQUEST(request),
    &iter,
    NULL
  );

  infd_storage_node_list_free(list);

  node->shared.subdir.explored = TRUE;
  return TRUE;
}

static InfdDirectoryNode*
infd_directory_node_add_subdirectory(InfdDirectory* directory,
                                     InfdDirectoryNode* parent,
                                     InfdNodeRequest* request,
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

    infd_directory_node_register(directory, node, NULL, seq);
    return node;
  }
}

static gboolean
infd_directory_node_add_note(InfdDirectory* directory,
                             InfdDirectoryNode* parent,
                             InfdNodeRequest* request,
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
  InfBrowserIter iter;
  guint node_id;
  InfCommunicationHostedGroup* group;
  xmlNodePtr xml;
  xmlNodePtr child;
  const gchar* method;
  InfdDirectorySubreq* subreq;
  GError* local_error;

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
      node = infd_directory_node_create_new_note(
        directory,
        parent,
        group,
        node_id,
        name,
        sheet_set,
        plugin,
        session,
        &local_error
      );

      if(node != NULL)
      {
        infd_directory_node_register(directory, node, NULL, seq);

        iter.node_id = node->id;
        iter.node = node;

        inf_node_request_finished(
          INF_NODE_REQUEST(request),
          &iter,
          NULL
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
                                InfdNodeRequest* request,
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
                           InfdNodeRequest* request,
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
    inf_node_request_finished(INF_NODE_REQUEST(request), &iter, NULL);

    /* Need to unlink child sessions explicitely before unregistering, so
     * remove-session is emitted before node-removed. Don't save changes since
     * we just removed the note anyway. */
    infd_directory_node_unlink_child_sessions(directory, node, FALSE);
    infd_directory_node_unregister(directory, node, seq);
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

  proxy = infd_directory_create_session_proxy_for_node(
    directory,
    node->id,
    session
  );

  g_object_unref(session);

  return proxy;
}

/*
 * Local requests.
 */

static void
infd_directory_begin_locreq_request(InfdDirectory* directory,
                                    InfdDirectoryLocreq* locreq)
{
  InfBrowserIter iter;
  InfdDirectoryNode* node;

  switch(locreq->type)
  {
  case INFD_DIRECTORY_LOCREQ_EXPLORE_NODE:
    node = locreq->shared.explore_node.node;
    break;
  case INFD_DIRECTORY_LOCREQ_ADD_NODE:
    node = locreq->shared.add_node.node;
    break;
  case INFD_DIRECTORY_LOCREQ_REMOVE_NODE:
    node = locreq->shared.remove_node.node;
    break;
  case INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION:
    node = locreq->shared.subscribe_session.node;
    break;
  case INFD_DIRECTORY_LOCREQ_SET_ACL:
    node = locreq->shared.set_acl.node;
    break;
  default:
    g_assert_not_reached();
    break;
  }

  iter.node_id = node->id;
  iter.node = node;

  inf_browser_begin_request(
    INF_BROWSER(directory),
    &iter,
    INF_REQUEST(locreq->request)
  );
}

static InfdDirectoryLocreq*
infd_directory_add_locreq_common(InfdDirectory* directory,
                                 InfdDirectoryLocreqType type,
                                 InfdNodeRequest* request)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryLocreq* locreq;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  locreq = g_slice_new(InfdDirectoryLocreq);

  locreq->directory = directory;
  locreq->type = type;
  locreq->request = request;
  locreq->dispatch = NULL;

  priv->local_requests = g_slist_prepend(priv->local_requests, locreq);
  return locreq;
}

static InfdDirectoryLocreq*
infd_directory_add_locreq_explore_node(InfdDirectory* directory,
                                       InfdDirectoryNode* node)
{
  InfdDirectoryLocreq* locreq;
  GObject* request;

  g_assert(node->type == INFD_STORAGE_NODE_SUBDIRECTORY);

  request = g_object_new(
    INFD_TYPE_EXPLORE_REQUEST,
    "type", "explore-node",
    "node-id", node->id,
    "requestor", NULL,
    NULL
  );

  locreq = infd_directory_add_locreq_common(
    directory,
    INFD_DIRECTORY_LOCREQ_EXPLORE_NODE,
    INFD_NODE_REQUEST(request)
  );

  locreq->shared.explore_node.node = node;

  infd_directory_begin_locreq_request(directory, locreq);
  return locreq;
}

static InfdDirectoryLocreq*
infd_directory_add_locreq_add_node(InfdDirectory* directory,
                                   InfdDirectoryNode* node,
                                   const gchar* name,
                                   const InfAclSheetSet* sheet_set,
                                   const InfdNotePlugin* plugin,
                                   InfSession* session,
                                   gboolean initial_subscribe)
{
  InfdDirectoryLocreq* locreq;
  GObject* request;

  g_assert(node->type == INFD_STORAGE_NODE_SUBDIRECTORY);

  request = g_object_new(
    INFD_TYPE_NODE_REQUEST,
    "type", "add-node",
    "node-id", node->id,
    "requestor", NULL,
    NULL
  );

  locreq = infd_directory_add_locreq_common(
    directory,
    INFD_DIRECTORY_LOCREQ_ADD_NODE,
    INFD_NODE_REQUEST(request)
  );

  locreq->shared.add_node.node = node;
  locreq->shared.add_node.name = g_strdup(name);
  if(sheet_set != NULL)
    locreq->shared.add_node.sheet_set = inf_acl_sheet_set_copy(sheet_set);
  else
    locreq->shared.add_node.sheet_set = NULL;
  locreq->shared.add_node.plugin = plugin;
  locreq->shared.add_node.session = session;
  locreq->shared.add_node.initial_subscribe = initial_subscribe;

  if(session != NULL)
    g_object_ref(session);

  infd_directory_begin_locreq_request(directory, locreq);
  return locreq;
}

static InfdDirectoryLocreq*
infd_directory_add_locreq_remove_node(InfdDirectory* directory,
                                      InfdDirectoryNode* node)
{
  InfdDirectoryLocreq* locreq;
  GObject* request;

  request = g_object_new(
    INFD_TYPE_NODE_REQUEST,
    "type", "remove-node",
    "node-id", node->id,
    "requestor", NULL,
    NULL
  );

  locreq = infd_directory_add_locreq_common(
    directory,
    INFD_DIRECTORY_LOCREQ_REMOVE_NODE,
    INFD_NODE_REQUEST(request)
  );

  locreq->shared.remove_node.node = node;

  infd_directory_begin_locreq_request(directory, locreq);
  return locreq;
}

static InfdDirectoryLocreq*
infd_directory_add_locreq_subscribe_session(InfdDirectory* directory,
                                            InfdDirectoryNode* node)
{
  InfdDirectoryPrivate* priv;
  GObject* request;
  InfdDirectorySubreq* subreq;
  InfdDirectoryLocreq* locreq;

  g_assert(node->type == INFD_STORAGE_NODE_NOTE);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* See whether there is a subreq for this node. If yes, take the request
   * from there instead of creating a new one. */
  subreq = infd_directory_find_subreq_by_node_id(
    directory,
    INFD_DIRECTORY_SUBREQ_SESSION,
    node->id
  );

  if(subreq != NULL)
  {
    request = G_OBJECT(subreq->shared.session.request);
  }
  else
  {
    request = g_object_new(
      INFD_TYPE_NODE_REQUEST,
      "type", "subscribe-session",
      "node-id", node->id,
      "requestor", NULL,
      NULL
    );
  }

  locreq = infd_directory_add_locreq_common(
    directory,
    INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION,
    INFD_NODE_REQUEST(request)
  );

  locreq->shared.subscribe_session.node = node;

  /* Emit begin-request if we created a new request */
  if(subreq == NULL)
    infd_directory_begin_locreq_request(directory, locreq);
  return locreq;
}

static InfdDirectoryLocreq*
infd_directory_add_locreq_set_acl(InfdDirectory* directory,
                                  InfdDirectoryNode* node,
                                  const InfAclSheetSet* sheet_set)
{
  InfdDirectoryPrivate* priv;
  GObject* request;
  InfdDirectoryLocreq* locreq;

  request = g_object_new(
    INFD_TYPE_NODE_REQUEST,
    "type", "set-acl",
    "node-id", node->id,
    "requestor", NULL,
    NULL
  );

  locreq = infd_directory_add_locreq_common(
    directory,
    INFD_DIRECTORY_LOCREQ_SET_ACL,
    INFD_NODE_REQUEST(request)
  );

  locreq->shared.set_acl.node = node;
  locreq->shared.set_acl.sheet_set = inf_acl_sheet_set_copy(sheet_set);

  infd_directory_begin_locreq_request(directory, locreq);
  return locreq;
}

static void
infd_directory_remove_locreq(InfdDirectory* directory,
                             InfdDirectoryLocreq* locreq)
{
  InfdDirectoryPrivate* priv;
  priv = INFD_DIRECTORY_PRIVATE(directory);

  priv->local_requests = g_slist_remove(priv->local_requests, locreq);

  switch(locreq->type)
  {
  case INFD_DIRECTORY_LOCREQ_EXPLORE_NODE:
    break;
  case INFD_DIRECTORY_LOCREQ_ADD_NODE:
    g_free(locreq->shared.add_node.name);
    if(locreq->shared.add_node.sheet_set != NULL)
      inf_acl_sheet_set_free(locreq->shared.add_node.sheet_set);
    if(locreq->shared.add_node.session != NULL)
      g_object_unref(locreq->shared.add_node.session);
    break;
  case INFD_DIRECTORY_LOCREQ_REMOVE_NODE:
  case INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION:
    break;
  case INFD_DIRECTORY_LOCREQ_SET_ACL:
    inf_acl_sheet_set_free(locreq->shared.set_acl.sheet_set);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  if(locreq->dispatch != NULL)
    inf_io_remove_dispatch(priv->io, locreq->dispatch);

  /* Might be NULL if the request was taken over (and executed) before the
   * locreq was run. */
  if(locreq->request != NULL)
  {
    /* TODO: Fail request with some sort of "cancelled" error? */
    g_object_unref(locreq->request);
  }

  g_slice_free(InfdDirectoryLocreq, locreq);
}

static void
infd_directory_start_locreq_func(gpointer user_data)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryLocreq* locreq;
  GSList* item;
  InfdDirectorySubreq* subreq;
  InfdSessionProxy* proxy;
  InfBrowserIter iter;
  GError* error;

  locreq = (InfdDirectoryLocreq*)user_data;
  priv = INFD_DIRECTORY_PRIVATE(locreq->directory);

  locreq->dispatch = NULL;

  error = NULL;
  switch(locreq->type)
  {
  case INFD_DIRECTORY_LOCREQ_EXPLORE_NODE:
    if(locreq->shared.explore_node.node == NULL)
    {
      g_set_error(
        &error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NO_SUCH_NODE,
        "%s",
        _("The node to be explored has been removed")
      );
    }
    else
    {
      g_assert(INFD_IS_EXPLORE_REQUEST(locreq->request));

      infd_directory_node_explore(
        locreq->directory,
        locreq->shared.explore_node.node,
        INFD_EXPLORE_REQUEST(locreq->request),
        NULL
      );
    }

    break;
  case INFD_DIRECTORY_LOCREQ_ADD_NODE:
    if(locreq->shared.add_node.node == NULL)
    {
      g_set_error(
        &error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NO_SUCH_NODE,
        "%s",
        _("The subdirectory into which a node was supposed to be inserted "
          "has been removed")
      );
    }
    else if(locreq->shared.add_node.plugin == NULL)
    {
      infd_directory_node_add_subdirectory(
        locreq->directory,
        locreq->shared.add_node.node,
        locreq->request,
        locreq->shared.add_node.name,
        locreq->shared.add_node.sheet_set,
        NULL,
        NULL,
        NULL
      );
    }
    else
    {
      infd_directory_node_add_note(
        locreq->directory,
        locreq->shared.add_node.node,
        locreq->request,
        locreq->shared.add_node.name,
        locreq->shared.add_node.sheet_set,
        locreq->shared.add_node.plugin,
        locreq->shared.add_node.session,
        NULL,
        FALSE,
        NULL,
        NULL
      );
    }
    break;
  case INFD_DIRECTORY_LOCREQ_REMOVE_NODE:
    if(locreq->shared.remove_node.node == NULL)
    {
      g_set_error(
        &error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NO_SUCH_NODE,
        "%s",
        _("The node to be removed has already been removed")
      );
    }
    else
    {
      infd_directory_node_remove(
        locreq->directory,
        locreq->shared.remove_node.node,
        locreq->request,
        NULL,
        NULL
      );
    }

    break;
  case INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION:
    if(locreq->shared.subscribe_session.node == NULL)
    {
      g_set_error(
        &error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NO_SUCH_NODE,
        "%s",
        _("The node to be subscribed to has been removed")
      );
    }
    else
    {
      if(locreq->shared.subscribe_session.node->shared.note.session == NULL ||
         locreq->shared.subscribe_session.node->shared.note.weakref == TRUE)
      {
        proxy = NULL;
        for(item = priv->subscription_requests;
            item != NULL;
            item = item->next)
        {
          /* Check if there is a subreq. If yes, this means that after we
           * started the locreq a subscription request came in (we wouldn't
           * have started the locreq if we knew about the subscription request
           * before, instead we would have failed and required the API user to
           * watch the existing pending_request). This is OK. We are making
           * and linking the session here, which is okay as well. All we have
           * to do is to remove the request from the subreq, so that it does
           * not try to finish it again. */
          subreq = (InfdDirectorySubreq*)item->data;
          if(subreq->type == INFD_DIRECTORY_SUBREQ_SESSION)
          {
            if(subreq->node_id == locreq->shared.subscribe_session.node->id)
            {
              /* Note there can be more than one subreq */
              g_assert(subreq->shared.session.request == locreq->request);

              g_object_unref(subreq->shared.session.request);
              subreq->shared.session.request = NULL;

              /* Get the session proxy from the subreq, to avoid making it
               * again. */
              g_assert(proxy == NULL ||
                       proxy == subreq->shared.session.session);

              proxy = subreq->shared.session.session;
              g_object_ref(proxy);
            }
          }
        }

        if(proxy == NULL)
        {
          proxy = infd_directory_node_make_session(
            locreq->directory,
            locreq->shared.subscribe_session.node,
            &error
          );
        }

        if(proxy != NULL)
        {
          infd_directory_node_link_session(
            locreq->directory,
            locreq->shared.subscribe_session.node,
            proxy
          );

          g_object_unref(proxy);

          iter.node_id = locreq->shared.subscribe_session.node->id;
          iter.node = locreq->shared.subscribe_session.node;
          inf_node_request_finished(
            INF_NODE_REQUEST(locreq->request),
            &iter,
            NULL
          );
        }
      }
      else
      {
        g_set_error(
          &error,
          inf_directory_error_quark(),
          INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED,
          "%s",
          inf_directory_strerror(INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED)
        );
      }
    }
    break;
  case INFD_DIRECTORY_LOCREQ_SET_ACL:
    if(locreq->shared.set_acl.node == NULL)
    {
      g_set_error(
        &error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NO_SUCH_NODE,
        "%s",
        _("The node to change the ACL for has been removed")
      );
    }
    else
    {
      iter.node_id = locreq->shared.set_acl.node->id;
      iter.node = locreq->shared.set_acl.node;

      inf_acl_table_insert_sheets(
        priv->acl_table,
        &iter,
        locreq->shared.set_acl.sheet_set
      );

      infd_directory_announce_acl_sheets(
        locreq->directory,
        locreq->shared.set_acl.node,
        locreq->shared.set_acl.sheet_set,
        NULL
      );

      infd_directory_write_acl(
        locreq->directory,
        locreq->shared.set_acl.node
      );
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }

  if(error != NULL)
  {
    inf_request_fail(INF_REQUEST(locreq->request), error);
    g_error_free(error);
  }

  infd_directory_remove_locreq(locreq->directory, locreq);
}

static void
infd_directory_locreq_free(gpointer user_data)
{
  InfdDirectoryLocreq* locreq;
  locreq = (InfdDirectoryLocreq*)user_data;
  infd_directory_remove_locreq(locreq->directory, locreq);
}

static void
infd_directory_start_locreq(InfdDirectory* directory,
                            InfdDirectoryLocreq* locreq)
{
  InfdDirectoryPrivate* priv;
  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_assert(locreq->dispatch == NULL);

  locreq->dispatch = inf_io_add_dispatch(
    priv->io,
    infd_directory_start_locreq_func,
    locreq,
    infd_directory_locreq_free
  );
}

/*
 * Network command handling.
 */

static gboolean
infd_directory_check_auth(InfdDirectory* directory,
                          InfdDirectoryNode* node,
                          InfXmlConnection* connection,
                          guint64 mask,
                          GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryConnectionInfo* info;
  InfBrowserIter iter;
  guint64 check_mask;

  priv = INFD_DIRECTORY_PRIVATE(directory);
  info = g_hash_table_lookup(priv->connections, connection);
  g_assert(info != NULL);

  iter.node_id = node->id;
  iter.node = node;
  check_mask = mask;

  check_mask = inf_browser_check_acl(
    INF_BROWSER(directory),
    &iter,
    info->user,
    mask
  );

  if(check_mask != mask)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_NOT_AUTHORIZED,
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
  InfBrowserIter browser_iter;
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

  /* Add default ACL for the root node */
  browser_iter.node_id = priv->root->id;
  browser_iter.node = priv->root;
  sheet_set = inf_acl_table_get_sheets(priv->acl_table, &browser_iter);

  infd_directory_acl_sheets_to_xml_for_connection(
    directory,
    priv->root->acl_connections,
    sheet_set,
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
  GSList* item;
  InfdDirectoryLocreq* locreq;
  InfdExploreRequest* request;
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

  if(node->shared.subdir.explored == FALSE)
  {
    request = NULL;

    /* Check if there is a locreq; if yes we finish it implicitely here */
    for(item = priv->local_requests; item != NULL; item = item->next)
    {
      locreq = (InfdDirectoryLocreq*)item->data;
      if(locreq->type == INFD_DIRECTORY_LOCREQ_EXPLORE_NODE)
      {
        if(locreq->shared.explore_node.node == node)
        {
          g_assert(INFD_IS_EXPLORE_REQUEST(locreq->request));
          request = INFD_EXPLORE_REQUEST(locreq->request);
          locreq->request = NULL;
          infd_directory_remove_locreq(directory, locreq);
          break;
        }
      }
    }

    if(request == NULL)
    {
      request = INFD_EXPLORE_REQUEST(
        g_object_new(
          INFD_TYPE_EXPLORE_REQUEST,
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
    }

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

    iter.node_id = child->id;
    iter.node = child;
    sheet_set = inf_acl_table_get_sheets(priv->acl_table, &iter);

    if(sheet_set != NULL)
    {
      infd_directory_acl_sheets_to_xml_for_connection(
        directory,
        child->acl_connections,
        sheet_set,
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
  guint64 perms;
  InfdNotePlugin* plugin;
  InfdNodeRequest* request;
  xmlChar* name;
  xmlChar* type;
  gchar* seq;

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

  local_error = NULL;
  sheet_set = inf_acl_table_sheet_set_from_xml(
    priv->acl_table,
    xml,
    &local_error
  );

  if(local_error != NULL)
  {
    xmlFree(name);
    g_free(seq);
    g_propagate_error(error, local_error);
    return FALSE;
  }

  perms = 0; /* TODO: (1 << INF_ACL_CAN_ADD_NODE) */
  if(sheet_set != NULL && sheet_set->n_sheets > 0)
    perms |= (1 << INF_ACL_CAN_SET_ACL);

  if(!infd_directory_check_auth(directory, parent, connection, perms, error))
    return FALSE;

  type = inf_xml_util_get_attribute_required(xml, "type", error);
  if(type == NULL)
  {
    if(sheet_set != NULL)
      inf_acl_sheet_set_free(sheet_set);
    return FALSE;
  }

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

  request = INFD_NODE_REQUEST(
    g_object_new(
      INFD_TYPE_NODE_REQUEST,
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
  InfdNodeRequest* request;
  InfBrowserIter iter;
  gboolean result;

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
    if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
      return FALSE;

    request = INFD_NODE_REQUEST(
      g_object_new(
        INFD_TYPE_NODE_REQUEST,
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
  guint64 perms;
  GSList* item;
  InfdDirectorySubreq* subreq;
  InfdDirectoryLocreq* locreq;
  InfdSessionProxy* proxy;
  InfBrowserIter iter;
  InfdNodeRequest* request;
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

  perms = (1 << INF_ACL_CAN_SUBSCRIBE_SESSION);
  if(!infd_directory_check_auth(directory, node, connection, perms, error))
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

  for(item = priv->local_requests; item != NULL; item = item->next)
  {
    locreq = (InfdDirectoryLocreq*)item->data;
    if(locreq->type == INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION &&
       locreq->shared.subscribe_session.node == node)
    {
      request = locreq->request;
      break;
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
    request = INFD_NODE_REQUEST(
      g_object_new(
        INFD_TYPE_NODE_REQUEST,
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
      /* The situation here is the following. We don't have any subreqs
       * because otherwise we would have taken the proxy from there. We can
       * have a locreq. In that case, we have kind of handled the locreq
       * here: fail the request and remove the locreq. */
      g_assert(request != NULL);
      if(locreq != NULL)
        infd_directory_remove_locreq(directory, locreq);
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
infd_directory_handle_request_certificate(InfdDirectory* directory,
                                          InfXmlConnection* connection,
                                          xmlNodePtr xml,
                                          GError** error)
{
  InfdDirectoryPrivate* priv;
  gchar* seq;
  xmlNodePtr child;
  xmlNodePtr subchild;
  int res;

  const char* extra;
  gnutls_datum_t crq_text;
  gnutls_x509_crq_t crq;

  gnutls_x509_crt_t cert;
  size_t cert_size;
  gchar* cert_buffer;

  time_t timestamp;
  gchar serial_buffer[5];

  xmlNodePtr reply_xml;

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

    return FALSE;
  }

  /* TODO: Check that the issuing connection is authorized */

  crq_text.data = NULL;
  for(child = xml->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;

    if(strcmp((const char*)child->name, "extra") == 0)
    {
      if(child->children != NULL && child->children->type == XML_TEXT_NODE)
        extra = (const gchar*)child->content;
    }
    else if(strcmp((const char*)child->name, "crq") == 0)
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

  res = gnutls_x509_crt_init(&cert);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crq_deinit(crq);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  res = gnutls_x509_crt_set_crq(cert, crq);
  gnutls_x509_crq_deinit(crq);

  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
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
    return FALSE;
  }

  res = gnutls_x509_crt_set_activation_time(cert, timestamp);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  res = gnutls_x509_crt_set_expiration_time(cert, timestamp + 365 * DAYS);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  res = gnutls_x509_crt_set_basic_constraints(cert, 0, -1);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  res = gnutls_x509_crt_set_key_usage(cert, GNUTLS_KEY_DIGITAL_SIGNATURE);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  res = gnutls_x509_crt_set_version(cert, 3);
  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  /* The certificate is now set up, we can sign it. */
  res = gnutls_x509_crt_sign2(
    cert,
    inf_certificate_chain_get_own_certificate(priv->certificate),
    priv->private_key,
    GNUTLS_DIG_SHA1,
    0
  );

  if(res != GNUTLS_E_SUCCESS)
  {
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  /* Now, export it to PEM format and send it back to the client */
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

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
  {
    g_free(cert_buffer);
    gnutls_x509_crt_deinit(cert);
    inf_gnutls_set_error(error, res);
    return FALSE;
  }

  /* Add the user into the ACL table, so that ACLs can immediately be
   * set for the new user. */
  infd_directory_add_user_for_certificate(directory, cert);
  gnutls_x509_crt_deinit(cert);

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"certificate-generated");
  child = xmlNewChild(reply_xml, NULL, (const xmlChar*)"certificate", NULL);
  xmlNodeAddContentLen(child, (const xmlChar*)cert_buffer, cert_size);
  g_free(cert_buffer);

  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);
  g_free(seq);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  return TRUE;
}

static gboolean
infd_directory_handle_query_user_list(InfdDirectory* directory,
                                      InfXmlConnection* connection,
                                      const xmlNodePtr xml,
                                      GError** error)
{
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  guint64 perms;
  gchar* seq;
  GSList* item;
  xmlNodePtr reply_xml;
  const InfAclUser** user_list;
  guint n_users;
  guint i;

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(g_slist_find(priv->user_list_connections, connection) != NULL)
  {
    g_set_error(
      error,
      inf_directory_error_quark(),
      INF_DIRECTORY_ERROR_USER_LIST_ALREADY_QUERIED,
      "%s",
      _("The user list has already been queried")
    );

    return FALSE;
  }

  node = priv->root;
  perms = (1 << INF_ACL_CAN_QUERY_USER_LIST);
  if(!infd_directory_check_auth(directory, node, connection, perms, error))
    return FALSE;

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
    return FALSE;

  user_list = inf_acl_table_get_user_list(priv->acl_table, &n_users);

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"user-list-begin");
  inf_xml_util_set_attribute_uint(reply_xml, "n-users", n_users);
  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  priv->user_list_connections =
    g_slist_prepend(priv->user_list_connections, connection);

  for(i = 0; i < n_users; ++i)
  {
    reply_xml = xmlNewNode(NULL, (const xmlChar*)"add-user");
    inf_acl_user_to_xml(user_list[i], reply_xml, FALSE);
    if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->group),
      connection,
      reply_xml
    );
  }

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"user-list-end");
  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->group),
    connection,
    reply_xml
  );

  g_free(seq);
  g_free(user_list);
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
  guint64 perms;
  gchar* seq;
  InfBrowserIter iter;
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

  perms = (1 << INF_ACL_CAN_QUERY_ACL);
  if(!infd_directory_check_auth(directory, node, connection, perms, error))
    return FALSE;

  if(!infd_directory_make_seq(directory, connection, xml, &seq, error))
    return FALSE;

  /* Add to ACL connections here so that
   * infd_directory_acl_sheets_to_xml_for_connection() will send the full
   * ACL, and not only the default sheet. */
  node->acl_connections = g_slist_prepend(node->acl_connections, connection);

  iter.node_id = node->id;
  iter.node = node;
  sheet_set = inf_acl_table_get_sheets(priv->acl_table, &iter);

  reply_xml = xmlNewNode(NULL, (const xmlChar*)"set-acl");
  inf_xml_util_set_attribute_uint(reply_xml, "id", node->id);
  if(seq != NULL) inf_xml_util_set_attribute(reply_xml, "seq", seq);

  if(sheet_set != NULL)
  {
    infd_directory_acl_sheets_to_xml_for_connection(
      directory,
      node->acl_connections,
      sheet_set,
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
  InfdDirectoryNode* parent;
  guint64 perms;
  gchar* seq;
  GError* local_error;
  InfAclSheetSet* sheet_set;
  InfdNodeRequest* request;
  InfBrowserIter iter;
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

  /* In order to change the ACL for a node, one needs CAN_SET_ACL permissions
   * for its parent node. */
  if(node->parent != NULL)
    parent = node->parent;
  else
    parent = priv->root;

  perms = (1 << INF_ACL_CAN_SET_ACL);
  if(!infd_directory_check_auth(directory, parent, connection, perms, error))
    return FALSE;

  /* TODO: Introduce inf_acl_table_sheet_set_from_xml_required */
  local_error = NULL;
  sheet_set = inf_acl_table_sheet_set_from_xml(
    priv->acl_table,
    xml,
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

  request = INFD_NODE_REQUEST(
    g_object_new(
      INFD_TYPE_NODE_REQUEST,
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

  inf_acl_table_insert_sheets(priv->acl_table, &iter, sheet_set);

  /* Announce to all connections but this one, since for this connection we
   * need to set the seq (done below) */
  infd_directory_announce_acl_sheets(directory, node, sheet_set, connection);
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

  inf_node_request_finished(
    INF_NODE_REQUEST(request),
    &iter,
    NULL
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
  InfBrowserIter iter;
  GSList* item;
  InfdDirectorySubreq* subsubreq;
  InfdDirectoryLocreq* locreq;
  InfdDirectorySyncIn* sync_in;
  InfdSessionProxy* proxy;
  InfSession* session;
  InfdDirectoryConnectionInfo* info;
  GError* local_error;

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
          subsubreq->shared.session.request = NULL;
          g_object_unref(subsubreq->shared.session.request);
        }
      }
    }

    /* If there's a locreq around for this node then remove it, since we
     * are handling the request now. */
    for(item = priv->local_requests; item != NULL; item = item->next)
    {
      locreq = (InfdDirectoryLocreq*)item->data;
      /* Don't match by node because node might have been removed */
      if(locreq->type == INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION &&
         locreq->request == subreq->shared.session.request)
      {
        infd_directory_remove_locreq(directory, locreq);
        break;
      }
    }

    /* The node this client wants to subscribe might have been removed in the
     * meanwhile. */
    if(node != NULL)
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
          subreq->shared.session.session
        );

        iter.node_id = node->id;
        iter.node = node;

        inf_node_request_finished(
          INF_NODE_REQUEST(subreq->shared.session.request),
          &iter,
          NULL
        );
      }
    }
    else
    {
      if(subreq->shared.session.request != NULL)
      {
        local_error = NULL;
        g_set_error(
          &local_error,
          inf_directory_error_quark(),
          INF_DIRECTORY_ERROR_NO_SUCH_NODE,
          "%s",
          _("The node to be subscribed to has been removed")
        );

        inf_request_fail(
          INF_REQUEST(subreq->shared.session.request),
          local_error
        );

        g_error_free(local_error);
      }
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
    if(subreq->shared.add_node.parent != NULL)
    {
      g_assert(
        infd_directory_node_is_name_available(
          directory,
          subreq->shared.add_node.parent,
          subreq->shared.add_node.name,
          NULL
        ) == TRUE
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

      infd_directory_node_link_session(directory, node, proxy);

      /* register to all but conn. conn already added the node after
       * having sent subscribe-ack. */
      infd_directory_node_register(directory, node, connection, NULL);

      g_assert(subreq->shared.add_node.request != NULL);

      iter.node_id = node->id;
      iter.node = node;

      inf_node_request_finished(
        INF_NODE_REQUEST(subreq->shared.add_node.request),
        &iter,
        NULL
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

      local_error = NULL;
      g_set_error(
        &local_error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NO_SUCH_NODE,
        "%s",
        _("The parent node of the node to be added has been removed")
      );

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

    /* Group and method are OK for the client, so start synchronization */
    g_object_get(
      G_OBJECT(subreq->shared.sync_in.proxy),
      "session", &session,
      NULL
    );

    inf_session_synchronize_from(session);
    g_object_unref(session);

    if(subreq->shared.sync_in.parent != NULL)
    {
      g_assert(
        infd_directory_node_is_name_available(
          directory,
          subreq->shared.sync_in.parent,
          subreq->shared.sync_in.name,
          NULL
        ) == TRUE
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

      local_error = NULL;
      g_set_error(
        &local_error,
        inf_directory_error_quark(),
        INF_DIRECTORY_ERROR_NO_SUCH_NODE,
        "%s",
        _("The parent node of the node to be added has been removed")
      );

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
  InfdDirectoryLocreq* locreq;
  GSList* item;
  InfdNodeRequest* other_request;
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
       * node around. There could be subreqs or locreqs. */

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
      
      for(item = priv->local_requests; item != NULL; item = item->next)
      {
        locreq = (InfdDirectoryLocreq*)item->data;
        if(locreq->type == INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION &&
           locreq->shared.subscribe_session.node != NULL &&
           locreq->shared.subscribe_session.node->id == subreq->node_id)
        {
          g_assert(locreq->request == subreq->shared.session.request);
          other_request = locreq->request;
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
    g_assert(info->user == NULL);

    info->user = infd_directory_add_user_for_connection(
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
  InfdDirectorySubreq* request;
  InfdDirectoryConnectionInfo* info;

  directory = INFD_DIRECTORY(user_data);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  /* TODO: Update last seen time, and write user list to storage */

  priv->user_list_connections =
    g_slist_remove(priv->user_list_connections, connection);
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

  priv = INFD_DIRECTORY_PRIVATE(directory);

  if(priv->storage != NULL)
  {
    /* TODO: Update last seen times of all connected users,
     * and write user list to storage */

    /* priv->root may be NULL if this is called from dispose. */
    if(priv->root != NULL && priv->root->shared.subdir.explored == TRUE)
    {
      /* Clear directory tree. This will cause all sessions to be saved in
       * storage. Note that sessions are not closed, but further
       * modifications to the sessions will not be stored in storage. */
      while((child = priv->root->shared.subdir.child) != NULL)
      {
        /* TODO: Do make requests here */
        infd_directory_node_unlink_child_sessions(directory, child, TRUE);
        infd_directory_node_unregister(directory, child, NULL);
        infd_directory_node_free(directory, child);
      }
    }

    g_object_unref(G_OBJECT(priv->storage));
  }

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

      /* TODO: Error handling? */
      /* TODO: Do make requests here */
      infd_directory_node_explore(directory, priv->root, NULL, NULL);
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
  priv->group = NULL;

  priv->private_key = NULL;
  priv->certificate = NULL;

  priv->plugins = g_hash_table_new(g_str_hash, g_str_equal);
  priv->connections = g_hash_table_new(NULL, NULL);

  priv->acl_table = inf_acl_table_new();
  priv->user_list_connections = NULL;

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
  priv->local_requests = NULL;

  priv->chat_session = NULL;

  inf_acl_table_add_user(
    priv->acl_table,
    inf_acl_user_new("default", NULL),
    TRUE
  );

  g_signal_connect(
    G_OBJECT(priv->acl_table),
    "user-added",
    G_CALLBACK(infd_directory_acl_table_user_added_cb),
    directory
  );
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
  InfBrowserIter iter;

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
    sheet.user = inf_acl_table_get_user(priv->acl_table, "default");
    sheet.perms = INF_ACL_MASK_DEFAULT;
    sheet.mask = INF_ACL_MASK_ALL;
    iter.node = priv->root;
    iter.node_id = priv->root->id;
    inf_acl_table_insert_sheet(priv->acl_table, &iter, &sheet);
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
  
  g_assert(g_hash_table_size(priv->connections) == 0);

  /* Should have been cleared by removing all connections */
  g_assert(priv->subscription_requests == NULL);

  while(priv->local_requests != NULL)
  {
    infd_directory_remove_locreq(
      directory,
      (InfdDirectoryLocreq*)priv->local_requests->data
    );
  }

  g_slist_free(priv->user_list_connections);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->acl_table),
    G_CALLBACK(infd_directory_acl_table_user_added_cb),
    directory
  );

  g_object_unref(priv->acl_table);

  /* We have dropped all references to connections now, so these do not try
   * to tell anyone that the directory tree has gone or whatever. */

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->group),
    G_CALLBACK(infd_directory_member_removed_cb),
    directory
  );

  g_object_unref(priv->group);
  g_object_unref(priv->communication_manager);
  infd_directory_set_storage(directory, NULL);

  g_hash_table_destroy(priv->connections);
  priv->connections = NULL;

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
  else if(strcmp((const char*)node->name, "request-certificate") == 0)
  {
    infd_directory_handle_request_certificate(
      directory,
      connection,
      node,
      &local_error
    );
  }
  else if(strcmp((const char*)node->name, "query-user-list") == 0)
  {
    infd_directory_handle_query_user_list(
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
                                         InfSessionProxy* proxy)
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
                                           InfSessionProxy* proxy)
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

static InfExploreRequest*
infd_directory_browser_explore(InfBrowser* browser,
                               const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryLocreq* locreq;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_SUBDIRECTORY, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == FALSE, NULL);

  locreq = infd_directory_add_locreq_explore_node(directory, node);
  infd_directory_start_locreq(directory, locreq);
  return INF_EXPLORE_REQUEST(locreq->request);
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

static InfNodeRequest*
infd_directory_browser_add_note(InfBrowser* browser,
                                const InfBrowserIter* iter,
                                const char* name,
                                const char* type,
                                const InfAclSheetSet* sheet_set,
                                InfSession* session,
                                gboolean initial_subscribe)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryLocreq* locreq;
  const InfdNotePlugin* plugin;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_SUBDIRECTORY, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  plugin = infd_directory_lookup_plugin(directory, type);
  g_return_val_if_fail(plugin != NULL, NULL);

  locreq = infd_directory_add_locreq_add_node(
    directory,
    node,
    name,
    sheet_set,
    plugin,
    session,
    initial_subscribe
  );

  infd_directory_start_locreq(directory, locreq);

  return INF_NODE_REQUEST(locreq->request);
}

static InfNodeRequest*
infd_directory_browser_add_subdirectory(InfBrowser* browser,
                                        const InfBrowserIter* iter,
                                        const char* name,
                                        const InfAclSheetSet* sheet_set)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryLocreq* locreq;
  const InfdNotePlugin* plugin;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_SUBDIRECTORY, NULL);
  g_return_val_if_fail(node->shared.subdir.explored == TRUE, NULL);

  locreq = infd_directory_add_locreq_add_node(
    directory,
    node,
    name,
    sheet_set,
    NULL,
    NULL,
    FALSE
  );

  infd_directory_start_locreq(directory, locreq);

  return INF_NODE_REQUEST(locreq->request);
}

static InfNodeRequest*
infd_directory_browser_remove_node(InfBrowser* browser,
                                   const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryLocreq* locreq;
  const InfdNotePlugin* plugin;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;

  locreq = infd_directory_add_locreq_remove_node(directory, node);
  infd_directory_start_locreq(directory, locreq);
  return INF_NODE_REQUEST(locreq->request);
}

static const gchar*
infd_directory_browser_get_node_name(InfBrowser* browser,
                                     const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryLocreq* locreq;
  const InfdNotePlugin* plugin;

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
  InfdDirectoryLocreq* locreq;
  const InfdNotePlugin* plugin;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  g_return_val_if_fail(node->type == INFD_STORAGE_NODE_NOTE, NULL);
  return node->shared.note.plugin->note_type;
}

static InfNodeRequest*
infd_directory_browser_subscribe(InfBrowser* browser,
                                 const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryLocreq* locreq;
  const InfdNotePlugin* plugin;

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

  locreq = infd_directory_add_locreq_subscribe_session(directory, node);
  infd_directory_start_locreq(directory, locreq);
  return INF_NODE_REQUEST(locreq->request);
}

static InfSessionProxy*
infd_directory_browser_get_session(InfBrowser* browser,
                                   const InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryLocreq* locreq;
  const InfdNotePlugin* plugin;

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
  InfdDirectoryLocreq* locreq;
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

  for(item = priv->local_requests; item != NULL; item = item->next)
  {
    locreq = (InfdDirectoryLocreq*)item->data;

    switch(locreq->type)
    {
    case INFD_DIRECTORY_LOCREQ_EXPLORE_NODE:
      if(iter != NULL && locreq->shared.explore_node.node == node)
        request = INF_REQUEST(locreq->request);
    case INFD_DIRECTORY_LOCREQ_ADD_NODE:
      if(iter != NULL && locreq->shared.add_node.node == node)
        request = INF_REQUEST(locreq->request);
    case INFD_DIRECTORY_LOCREQ_REMOVE_NODE:
      if(iter != NULL && locreq->shared.remove_node.node == node)
        request = INF_REQUEST(locreq->request);
    case INFD_DIRECTORY_LOCREQ_SUBSCRIBE_SESSION:
      if(iter != NULL && locreq->shared.subscribe_session.node == node)
        request = INF_REQUEST(locreq->request);
    case INFD_DIRECTORY_LOCREQ_SET_ACL:
      if(iter != NULL && locreq->shared.set_acl.node == node)
        request = INF_REQUEST(locreq->request);
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
                                         InfNodeRequest* request,
                                         InfBrowserIter* iter)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  guint node_id;
  
  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_object_get(G_OBJECT(request), "node-id", &node_id, NULL);
  
  node = g_hash_table_lookup(priv->nodes, GUINT_TO_POINTER(node_id));
  if(node == NULL) return FALSE;

  iter->node_id = node_id;
  iter->node = node;
  return TRUE;
}

static InfAclUserListRequest*
infd_directory_browser_query_acl_user_list(InfBrowser* browser)
{
  /* This should not be called because get_acl_user_list always returns the
   * full list. */
  g_return_val_if_reached(NULL);
  return NULL;
}

static const InfAclUser**
infd_directory_browser_get_acl_user_list(InfBrowser* browser,
                                         guint* n_users)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  return inf_acl_table_get_user_list(priv->acl_table, n_users);
}

static const InfAclUser*
infd_directory_browser_get_acl_local_user(InfBrowser* browser)
{
  /* There is no local user. This means direct access to the directory and
   * no ACL applies for local operations. */
  return NULL;
}

static const InfAclUser*
infd_directory_browser_lookup_acl_user(InfBrowser* browser,
                                       const gchar* user_id)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  return inf_acl_table_get_user(priv->acl_table, user_id);
}

static InfNodeRequest*
infd_directory_browser_query_acl(InfBrowser* browser,
                                 const InfBrowserIter* iter)
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
                               const InfAclUser* user)
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

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);
  return inf_acl_table_get_sheets(priv->acl_table, iter);
}

static InfNodeRequest*
infd_directory_browser_set_acl(InfBrowser* browser,
                               const InfBrowserIter* iter,
                               const InfAclSheetSet* sheet_set)
{
  InfdDirectory* directory;
  InfdDirectoryPrivate* priv;
  InfdDirectoryNode* node;
  InfdDirectoryLocreq* locreq;

  directory = INFD_DIRECTORY(browser);
  priv = INFD_DIRECTORY_PRIVATE(directory);

  infd_directory_return_val_if_iter_fail(directory, iter, NULL);

  node = (InfdDirectoryNode*)iter->node;
  locreq = infd_directory_add_locreq_set_acl(directory, node, sheet_set);
  infd_directory_start_locreq(directory, locreq);

  return INF_NODE_REQUEST(locreq->request);
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
  iface->acl_user_added = NULL;
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

  iface->query_acl_user_list = infd_directory_browser_query_acl_user_list;
  iface->get_acl_user_list = infd_directory_browser_get_acl_user_list;
  iface->get_acl_local_user = infd_directory_browser_get_acl_local_user;
  iface->lookup_acl_user = infd_directory_browser_lookup_acl_user;
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
  info->user = NULL;

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
    info->user =
      infd_directory_add_user_for_connection(directory, connection);
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
 * infd_directory_foreach_connection:
 * @directory: A #InfdDirectory.
 * @func: The function to call for each connection in @directory.
 * @user_data: Additional data to pass to the callback function.
 *
 * Calls @func for each connection in @directory that has previously been
 * added to the directory.
 */
void
infd_directory_foreach_connection(InfdDirectory* directory,
                                  InfdDirectoryForeachConnectionFunc func,
                                  gpointer userdata)
{
  InfdDirectoryPrivate* priv;
  GHashTableIter iter;
  gpointer key;

  g_return_if_fail(INFD_IS_DIRECTORY(directory));
  g_return_if_fail(func != NULL);

  priv = INFD_DIRECTORY_PRIVATE(directory);

  g_hash_table_iter_init(&iter, priv->connections);
  while (g_hash_table_iter_next(&iter, &key, NULL))
  {
    func(INF_XML_CONNECTION(key), userdata);
  }
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
