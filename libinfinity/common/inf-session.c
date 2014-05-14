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
 * SECTION:inf-session
 * @title: InfSession
 * @short_description: Basic session object and synchronization
 * @include: libinfinity/common/inf-session.h
 * @stability: Unstable
 *
 * #InfSession represents an editing session. The actual type of document that
 * is edited is not specified, so instantiating #InfSession does not make
 * any sense. You rather want to use a derived class such as #InfTextSession.
 * Normally, the #InfcBrowser or #InfdDirectory, respectively, already take
 * care of instantiating the correct #InfSession.
 *
 * A session basically consists of the document being edited (also called
 * buffer, see #InfBuffer) and the users that are working on the document,
 * see #InfUserTable.
 *
 * A session can either start in %INF_SESSION_RUNNING state, in which it
 * is created with the initial buffer and user table. It may also start in
 * %INF_SESSION_SYNCHRONIZING state. In this case, both buffer and user table
 * are initially empty and are copied from another system over the network.
 * When the copy is complete, the session enters %INF_SESSION_RUNNING state.
 *
 * To be notified about changes other users make to a session, you need to
 * subscribe to the session (on client side), or wait for incoming
 * subscriptions (on server side). This is normally done by
 * infc_browser_iter_subscribe_session(). The first action that is performed
 * upon subscription is a synchronization as described above. When the
 * synchronization is complete, the #InfSession::synchronization-complete signal
 * is emitted.
 *
 * After subscription, one can observe modifications other users make, but it is
 * not possible to make own modifications. Before doing so, a #InfUser needs to
 * be joined. This is done by client/server specific API from
 * #InfSessionProxy, in particular inf_session_proxy_join_user(). The
 * required parameters still depend on the actual note type, which is why most
 * note implementations offer their own API to join a user.
 **/

#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-buffer.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/communication/inf-communication-object.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <string.h>

/* TODO: Set buffer to non-editable during synchronization */
/* TODO: Cache requests received by other group members
 * during synchronization and process them afterwards */

typedef struct _InfSessionSync InfSessionSync;
struct _InfSessionSync {
  InfCommunicationGroup* group;
  InfXmlConnection* conn;

  guint messages_total;
  guint messages_sent;
  InfSessionSyncStatus status;
};

typedef struct _InfSessionPrivate InfSessionPrivate;
struct _InfSessionPrivate {
  InfCommunicationManager* manager;
  InfBuffer* buffer;
  InfUserTable* user_table;
  InfSessionStatus status;

  /* Group of subscribed connections */
  InfCommunicationGroup* subscription_group;

  union {
    /* INF_SESSION_PRESYNC */
    struct {
      InfCommunicationGroup* group;
      InfXmlConnection* conn;
      gboolean closing;
    } presync;

    /* INF_SESSION_SYNCHRONIZING */
    struct {
      InfCommunicationGroup* group;
      InfXmlConnection* conn;
      guint messages_total;
      guint messages_received;
      gboolean closing;
    } sync;

    /* INF_SESSION_RUNNING */
    struct {
      GSList* syncs;
    } run;
  } shared;
};

typedef struct _InfSessionXmlData InfSessionXmlData;
struct _InfSessionXmlData {
  InfSession* session;
  xmlNodePtr xml;
};

enum {
  PROP_0,

  /* construct only */
  PROP_COMMUNICATION_MANAGER,
  PROP_BUFFER,
  PROP_USER_TABLE,

  PROP_STATUS,

  PROP_SYNC_CONNECTION,
  PROP_SYNC_GROUP,

  /* read/write */
  PROP_SUBSCRIPTION_GROUP
};

enum {
  CLOSE,
  ERROR,

  SYNCHRONIZATION_BEGIN,
  SYNCHRONIZATION_PROGRESS,
  SYNCHRONIZATION_COMPLETE,
  SYNCHRONIZATION_FAILED,

  LAST_SIGNAL
};

#define INF_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_SESSION, InfSessionPrivate))

static GObjectClass* parent_class;
static guint session_signals[LAST_SIGNAL];
static GQuark inf_session_sync_error_quark;

/*
 * Utility functions.
 */

static const gchar*
inf_session_sync_strerror(InfSessionSyncError errcode)
{
  switch(errcode)
  {
  case INF_SESSION_SYNC_ERROR_GOT_MESSAGE_IN_PRESYNC:
    return _("Unexpectedly got an XML message in presync");
  case INF_SESSION_SYNC_ERROR_UNEXPECTED_NODE:
    return _("Got unexpected XML node during synchronization");
  case INF_SESSION_SYNC_ERROR_ID_NOT_PRESENT:
    return _("'id' attribute in user message is missing");
  case INF_SESSION_SYNC_ERROR_ID_IN_USE:
    return _("User ID is already in use");
  case INF_SESSION_SYNC_ERROR_NAME_NOT_PRESENT:
    return _("'name' attribute in user message is missing");
  case INF_SESSION_SYNC_ERROR_NAME_IN_USE:
    return _("User Name is already in use");
  case INF_SESSION_SYNC_ERROR_CONNECTION_CLOSED:
    return _("The connection was closed unexpectedly");
  case INF_SESSION_SYNC_ERROR_SENDER_CANCELLED:
    return _("The sender cancelled the synchronization");
  case INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED:
    return _("The receiver cancelled the synchronization");
  case INF_SESSION_SYNC_ERROR_UNEXPECTED_BEGIN_OF_SYNC:
    return _("Got begin-of-sync message, but synchronization is already "
             "in progress");
  case INF_SESSION_SYNC_ERROR_NUM_MESSAGES_MISSING:
    return _("begin-of-sync message does not contain the number of messages "
             "to expect");
  case INF_SESSION_SYNC_ERROR_UNEXPECTED_END_OF_SYNC:
    return _("Got end-of-sync message, but synchronization is still in "
             "progress");
  case INF_SESSION_SYNC_ERROR_EXPECTED_BEGIN_OF_SYNC:
    return _("Expected begin-of-sync message as first message during "
             "synchronization");
  case INF_SESSION_SYNC_ERROR_EXPECTED_END_OF_SYNC:
    return _("Expected end-of-sync message as last message during "
             "synchronization");
  case INF_SESSION_SYNC_ERROR_FAILED:
    return _("An unknown synchronization error has occured");
  default:
    return _("An error with unknown error code occured");
  }
}

static const gchar*
inf_session_get_sync_error_message(GQuark domain,
                                   guint code)
{
  if(domain == inf_session_sync_error_quark)
    return inf_session_sync_strerror(code);

  /* TODO: Add a possibilty for sub classes to register their error domains
   * that can occur in process_xml_sync. Maybe via a translate_error_sync
   * vfunc. */
  return _("An error with unknown error domain occured");
}

static GSList*
inf_session_find_sync_item_by_connection(InfSession* session,
                                         InfXmlConnection* conn)
{
  InfSessionPrivate* priv;
  GSList* item;

  priv = INF_SESSION_PRIVATE(session);

  g_return_val_if_fail(priv->status == INF_SESSION_RUNNING, NULL);

  for(item = priv->shared.run.syncs; item != NULL; item = g_slist_next(item))
  {
    if( ((InfSessionSync*)item->data)->conn == conn)
      return item;
  }

  return NULL;
}

static InfSessionSync*
inf_session_find_sync_by_connection(InfSession* session,
                                    InfXmlConnection* conn)
{
  GSList* item;
  item = inf_session_find_sync_item_by_connection(session, conn);

  if(item == NULL) return NULL;
  return (InfSessionSync*)item->data;
}

/* Required by inf_session_release_connection() */
static void
inf_session_connection_notify_status_cb(InfXmlConnection* connection,
                                        GParamSpec* pspec,
                                        gpointer user_data);

static void
inf_session_release_connection(InfSession* session,
                               InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  InfSessionSync* sync;
  GSList* item;
/*  gboolean has_connection;*/

  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_PRESYNC:
    g_assert(priv->shared.presync.conn == connection);
    g_assert(priv->shared.presync.group != NULL);
    g_object_unref(priv->shared.presync.group);
    priv->shared.presync.conn = NULL;
    priv->shared.presync.group = NULL;
    break;
  case INF_SESSION_SYNCHRONIZING:
    g_assert(priv->shared.sync.conn == connection);
    g_assert(priv->shared.sync.group != NULL);

    g_object_unref(priv->shared.sync.group);

    priv->shared.sync.conn = NULL;
    priv->shared.sync.group = NULL;
    break;
  case INF_SESSION_RUNNING:
    item = inf_session_find_sync_item_by_connection(session, connection);
    g_assert(item != NULL);

    sync = item->data;

    g_object_unref(sync->group);

    g_slice_free(InfSessionSync, sync);
    priv->shared.run.syncs = g_slist_delete_link(
      priv->shared.run.syncs,
      item
    );

    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(connection),
    G_CALLBACK(inf_session_connection_notify_status_cb),
    session
  );

  g_object_unref(connection);
}

static void
inf_session_send_sync_error(InfSession* session,
                            GError* error)
{
  InfSessionPrivate* priv;
  xmlNodePtr node;

  priv = INF_SESSION_PRIVATE(session);

  g_return_if_fail(priv->status == INF_SESSION_SYNCHRONIZING);
  g_return_if_fail(priv->shared.sync.conn != NULL);

  node = inf_xml_util_new_node_from_error(error, NULL, "sync-error");

  inf_communication_group_send_message(
    priv->shared.sync.group,
    priv->shared.sync.conn,
    node
  );
}

/*
 * Signal handlers.
 */
static void
inf_session_connection_notify_status_cb(InfXmlConnection* connection,
                                        GParamSpec* pspec,
                                        gpointer user_data)
{
  InfSession* session;
  InfSessionPrivate* priv;
  InfXmlConnectionStatus status;
  GError* error;

  session = INF_SESSION(user_data);
  priv = INF_SESSION_PRIVATE(session);
  error = NULL;

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSED ||
     status == INF_XML_CONNECTION_CLOSING)
  {
    g_set_error(
      &error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_CONNECTION_CLOSED,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_CONNECTION_CLOSED)
    );

    switch(priv->status)
    {
    case INF_SESSION_PRESYNC:
      g_assert(connection == priv->shared.presync.conn);

      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_FAILED],
        0,
        connection,
        error
      );

      break;
    case INF_SESSION_SYNCHRONIZING:
      g_assert(connection == priv->shared.sync.conn);

      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_FAILED],
        0,
        connection,
        error
      );

      break;
    case INF_SESSION_RUNNING:
      g_assert(
        inf_session_find_sync_by_connection(session, connection) != NULL
      );

      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_FAILED],
        0,
        connection,
        error
      );

      break;
    case INF_SESSION_CLOSED:
    default:
      g_assert_not_reached();
      break;
    }

    g_error_free(error);
  }
}

/*
 * GObject overrides.
 */

static void
inf_session_init(GTypeInstance* instance,
                 gpointer g_class)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(instance);
  priv = INF_SESSION_PRIVATE(session);

  priv->manager = NULL;
  priv->buffer = NULL;
  priv->user_table = NULL;
  priv->status = INF_SESSION_RUNNING;

  priv->shared.run.syncs = NULL;
}

static GObject*
inf_session_constructor(GType type,
                        guint n_construct_properties,
                        GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfSessionPrivate* priv;
  InfXmlConnection* sync_conn;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  priv = INF_SESSION_PRIVATE(object);

  /* Create empty user table if property was not initialized */
  if(priv->user_table == NULL)
    priv->user_table = inf_user_table_new();

  switch(priv->status)
  {
  case INF_SESSION_PRESYNC:
    g_assert(priv->shared.presync.conn != NULL &&
             priv->shared.presync.group != NULL);
    sync_conn = priv->shared.presync.conn;
    break;
  case INF_SESSION_SYNCHRONIZING:
    g_assert(priv->shared.sync.conn != NULL &&
             priv->shared.sync.group != NULL);
    sync_conn = priv->shared.sync.conn;
    break;
  case INF_SESSION_RUNNING:
  case INF_SESSION_CLOSED:
    sync_conn = NULL;
    break;
  default:
    g_assert_not_reached();
    break;
  }

  if(sync_conn != NULL)
  {
    g_signal_connect(
      G_OBJECT(sync_conn),
      "notify::status",
      G_CALLBACK(inf_session_connection_notify_status_cb),
      object
    );
  }

  return object;
}

static void
inf_session_dispose(GObject* object)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  if(priv->status != INF_SESSION_CLOSED)
  {
    /* Close session. This cancells all running synchronizations and tells
     * everyone that the session no longer exists. */
    inf_session_close(session);
  }

  g_object_unref(G_OBJECT(priv->user_table));
  priv->user_table = NULL;

  g_object_unref(G_OBJECT(priv->buffer));
  priv->buffer = NULL;

  g_object_unref(G_OBJECT(priv->manager));
  priv->manager = NULL;

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_session_finalize(GObject* object)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_session_set_property(GObject* object,
                         guint prop_id,
                         const GValue* value,
                         GParamSpec* pspec)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_COMMUNICATION_MANAGER:
    g_assert(priv->manager == NULL); /* construct only */
    priv->manager = INF_COMMUNICATION_MANAGER(g_value_dup_object(value));
    break;
  case PROP_BUFFER:
    g_assert(priv->buffer == NULL); /* construct only */
    priv->buffer = INF_BUFFER(g_value_dup_object(value));
    break;
  case PROP_USER_TABLE:
    g_assert(priv->user_table == NULL); /* construct only */
    priv->user_table = INF_USER_TABLE(g_value_dup_object(value));
    break;
  case PROP_STATUS:
    /* construct only, INF_SESSION_RUNNING is the default */
    g_assert(priv->status == INF_SESSION_RUNNING);
    priv->status = g_value_get_enum(value);
    switch(priv->status)
    {
    case INF_SESSION_PRESYNC:
      priv->shared.presync.conn = NULL;
      priv->shared.presync.group = NULL;
      priv->shared.presync.closing = FALSE;
      break;
    case INF_SESSION_SYNCHRONIZING:
      priv->shared.sync.conn = NULL;
      priv->shared.sync.group = NULL;
      priv->shared.sync.messages_total = 0;
      priv->shared.sync.messages_received = 0;
      priv->shared.sync.closing = FALSE;
      break;
    case INF_SESSION_RUNNING:
      /* was default */
      g_assert(priv->shared.run.syncs == NULL);
      break;
    case INF_SESSION_CLOSED:
      break;
    default:
      g_assert_not_reached();
      break;
    }

    break;
  case PROP_SYNC_CONNECTION:
    /* Need to have status sync or presync to set sync-connection */
    switch(priv->status)
    {
    case INF_SESSION_PRESYNC:
      g_assert(priv->shared.presync.conn == NULL); /* construct only */
      priv->shared.presync.conn =
        INF_XML_CONNECTION(g_value_dup_object(value));
      break;
    case INF_SESSION_SYNCHRONIZING:
      g_assert(priv->shared.sync.conn == NULL); /* construct only */
      priv->shared.sync.conn =
        INF_XML_CONNECTION(g_value_dup_object(value));
      break;
    case INF_SESSION_RUNNING:
      g_assert(g_value_get_object(value) == NULL);
      break;
    case INF_SESSION_CLOSED:
    default:
      g_assert_not_reached();
      break;
    }

    break;
  case PROP_SYNC_GROUP:
    switch(priv->status)
    {
    case INF_SESSION_PRESYNC:
      g_assert(priv->shared.presync.group == NULL); /* construct only */
      priv->shared.presync.group =
        INF_COMMUNICATION_GROUP(g_value_dup_object(value));
      break;
    case INF_SESSION_SYNCHRONIZING:
      g_assert(priv->shared.sync.group == NULL); /* construct only */
      priv->shared.sync.group =
        INF_COMMUNICATION_GROUP(g_value_dup_object(value));
      break;
    case INF_SESSION_RUNNING:
      g_assert(g_value_get_object(value) == NULL);
      break;
    case INF_SESSION_CLOSED:
    default:
      g_assert_not_reached();
      break;
    }

    break;
  case PROP_SUBSCRIPTION_GROUP:
    if(priv->subscription_group != NULL)
      g_object_unref(priv->subscription_group);

    priv->subscription_group =
      INF_COMMUNICATION_GROUP(g_value_dup_object(value));

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_session_get_property(GObject* object,
                         guint prop_id,
                         GValue* value,
                         GParamSpec* pspec)
{
  InfSession* session;
  InfSessionPrivate* priv;

  session = INF_SESSION(object);
  priv = INF_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_COMMUNICATION_MANAGER:
    g_value_set_object(value, G_OBJECT(priv->manager));
    break;
  case PROP_BUFFER:
    g_value_set_object(value, G_OBJECT(priv->buffer));
    break;
  case PROP_USER_TABLE:
    g_value_set_object(value, G_OBJECT(priv->user_table));
    break;
  case PROP_STATUS:
    g_value_set_enum(value, priv->status);
    break;
  case PROP_SYNC_CONNECTION:
    switch(priv->status)
    {
    case INF_SESSION_PRESYNC:
      g_value_set_object(value, G_OBJECT(priv->shared.presync.conn));
      break;
    case INF_SESSION_SYNCHRONIZING:
      g_value_set_object(value, G_OBJECT(priv->shared.sync.conn));
      break;
    case INF_SESSION_RUNNING:
    case INF_SESSION_CLOSED:
      g_value_set_object(value, NULL);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    break;
  case PROP_SYNC_GROUP:
    switch(priv->status)
    {
    case INF_SESSION_PRESYNC:
      g_value_set_object(value, G_OBJECT(priv->shared.presync.group));
      break;
    case INF_SESSION_SYNCHRONIZING:
      g_value_set_object(value, G_OBJECT(priv->shared.sync.group));
      break;
    case INF_SESSION_RUNNING:
    case INF_SESSION_CLOSED:
      g_value_set_object(value, NULL);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    break;
  case PROP_SUBSCRIPTION_GROUP:
    g_value_set_object(value, G_OBJECT(priv->subscription_group));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * Network messages
 */

static InfCommunicationScope
inf_session_handle_user_status_change(InfSession* session,
                                      InfXmlConnection* connection,
                                      xmlNodePtr xml,
                                      GError** error)
{
  InfSessionPrivate* priv;
  InfUser* user;
  guint id;
  xmlChar* status_attr;
  gboolean has_status;
  InfUserStatus status;

  priv = INF_SESSION_PRIVATE(session);
  if(!inf_xml_util_get_attribute_uint_required(xml, "id", &id, error))
    return INF_COMMUNICATION_SCOPE_PTP;

  user = inf_user_table_lookup_user_by_id(priv->user_table, id);
  if(user == NULL)
  {
    g_set_error(
      error,
      inf_user_error_quark(),
      INF_USER_ERROR_NO_SUCH_USER,
      _("No such user with ID %u"),
      id
    );

    return INF_COMMUNICATION_SCOPE_PTP;
  }

  if(inf_user_get_status(user) == INF_USER_UNAVAILABLE ||
     inf_user_get_connection(user) != connection)
  {
    g_set_error(
      error,
      inf_user_error_quark(),
      INF_USER_ERROR_NOT_JOINED,
      "%s",
      _("User did not join from this connection")
    );

    return INF_COMMUNICATION_SCOPE_PTP;
  }

  status_attr = xmlGetProp(xml, (const xmlChar*)"status");
  has_status =
    inf_user_status_from_string((const char*)status_attr, &status, error);
  xmlFree(status_attr);
  if(!has_status) return FALSE;

  if(inf_user_get_status(user) != status)
    g_object_set(G_OBJECT(user), "status", status, NULL);

  return INF_COMMUNICATION_SCOPE_GROUP;
}

/*
 * VFunc implementations.
 */

static void
inf_session_to_xml_sync_impl_foreach_func(InfUser* user,
                                          gpointer user_data)
{
  InfSessionXmlData* data;
  xmlNodePtr usernode;

  data = (InfSessionXmlData*)user_data;

  usernode = xmlNewNode(NULL, (const xmlChar*)"sync-user");
  inf_session_user_to_xml(data->session, user, usernode);

  xmlAddChild(data->xml, usernode);
}

static void
inf_session_to_xml_sync_impl(InfSession* session,
                             xmlNodePtr parent)
{
  InfSessionPrivate* priv;
  InfSessionXmlData data;

  priv = INF_SESSION_PRIVATE(session);
  data.session = session;
  data.xml = parent;

  inf_user_table_foreach_user(
    priv->user_table,
    inf_session_to_xml_sync_impl_foreach_func,
    &data
  );
}

static gboolean
inf_session_process_xml_sync_impl(InfSession* session,
                                  InfXmlConnection* connection,
                                  const xmlNodePtr xml,
                                  GError** error)
{
  InfSessionPrivate* priv;
  InfSessionClass* session_class;
  GArray* user_props;
  InfUser* user;
  guint i;
  const GParameter* param;
  GParameter* connparam;

  priv = INF_SESSION_PRIVATE(session);
  session_class = INF_SESSION_GET_CLASS(session);

  g_return_val_if_fail(session_class->get_xml_user_props != NULL, FALSE);

  g_return_val_if_fail(priv->status == INF_SESSION_SYNCHRONIZING, FALSE);
  g_return_val_if_fail(connection == priv->shared.sync.conn, FALSE);

  if(strcmp((const char*)xml->name, "sync-user") == 0)
  {
    user_props = session_class->get_xml_user_props(
      session,
      connection,
      xml
    );

    param = inf_session_lookup_user_property(
      (const GParameter*)user_props->data,
      user_props->len,
      "status"
    );

    if(param != NULL &&
       g_value_get_enum(&param->value) != INF_USER_UNAVAILABLE)
    {
      /* Assume that the connection for this available user is the one that
       * the synchronization comes from if the "connection" property is
       * not given. */
      connparam = inf_session_get_user_property(user_props, "connection");
      if(!G_IS_VALUE(&connparam->value))
      {
        g_value_init(&connparam->value, INF_TYPE_XML_CONNECTION);
        g_value_set_object(&connparam->value, G_OBJECT(connection));
      }
    }

    user = inf_session_add_user(
      session,
      (GParameter*)user_props->data,
      user_props->len
    );

    for(i = 0; i < user_props->len; ++ i)
      g_value_unset(&g_array_index(user_props, GParameter, i).value);

    g_array_free(user_props, TRUE);

    if(user == NULL) return FALSE;
    return TRUE;
  }
  else
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_UNEXPECTED_NODE,
      "Received unexpected XML message \"%s\" during synchronization",
      (const gchar*)xml->name
    );

    return FALSE;
  }
}

static InfCommunicationScope
inf_session_process_xml_run_impl(InfSession* session,
                                 InfXmlConnection* connection,
                                 const xmlNodePtr xml,
                                 GError** error)
{
  if(strcmp((const char*)xml->name, "user-status-change") == 0)
  {
    return inf_session_handle_user_status_change(
      session,
      connection,
      xml,
      error
    );
  }
  else
  {
    /* TODO: Proper error quark and code */
    g_set_error(
      error,
      g_quark_from_static_string("INF_SESSION_ERROR"),
      0,
      _("Received unhandled XML message '%s'"),
      (const gchar*)xml->name
    );

    return INF_COMMUNICATION_SCOPE_PTP;
  }
}

static GArray*
inf_session_get_xml_user_props_impl(InfSession* session,
                                    InfXmlConnection* conn,
                                    const xmlNodePtr xml)
{
  InfSessionPrivate* priv;
  GArray* array;
  GParameter* parameter;
  xmlChar* name;
  xmlChar* id;
  xmlChar* status;
#if 0
  xmlChar* connection;
  InfXmlConnection* real_conn;
#endif

  priv = INF_SESSION_PRIVATE(session);
  array = g_array_sized_new(FALSE, FALSE, sizeof(GParameter), 16);

  name = xmlGetProp(xml, (const xmlChar*)"name");
  id = xmlGetProp(xml, (const xmlChar*)"id");
  status = xmlGetProp(xml, (const xmlChar*)"status");
#if 0
  connection = xmlGetProp(xml, (const xmlChar*)"connection");
#endif

  if(id != NULL)
  {
    parameter = inf_session_get_user_property(array, "id");
    g_value_init(&parameter->value, G_TYPE_UINT);
    g_value_set_uint(&parameter->value, strtoul((const gchar*)id, NULL, 10));
    xmlFree(id);
  }

  if(name != NULL)
  {
    parameter = inf_session_get_user_property(array, "name");
    g_value_init(&parameter->value, G_TYPE_STRING);
    g_value_set_string(&parameter->value, (const gchar*)name);
    xmlFree(name);
  }

  if(status != NULL)
  {
    parameter = inf_session_get_user_property(array, "status");
    g_value_init(&parameter->value, INF_TYPE_USER_STATUS);

    if(strcmp((const char*)status, "active") == 0)
      g_value_set_enum(&parameter->value, INF_USER_ACTIVE);
    else if(strcmp((const char*)status, "inactive") == 0)
      g_value_set_enum(&parameter->value, INF_USER_INACTIVE);
    else
      /* TODO: Error reporting for get_xml_user_props */
      g_value_set_enum(&parameter->value, INF_USER_UNAVAILABLE);

    xmlFree(status);
  }

#if 0
  if(connection != NULL)
  {
    real_conn = inf_connection_manager_group_lookup_connection(
      priv->subscription_group,
      connection
    );

    if(real_conn != NULL)
    {
      parameter = inf_session_get_user_property(array, "connection");
      g_value_init(&parameter->value, INF_TYPE_XML_CONNECTION);
      g_value_set_object(&parameter->value, G_OBJECT(real_conn));
    }
    else
    {
      /* TODO: This should be an error. */
    }
  }
#endif

  return array;
}

static void
inf_session_set_xml_user_props_impl(InfSession* session,
                                    const GParameter* params,
                                    guint n_params,
                                    xmlNodePtr xml)
{
  guint i;
  gchar id_buf[16];
  const gchar* name;
  InfUserStatus status;
#if 0
  InfXmlConnection* conn;
  gchar* remote_address;
#endif

  for(i = 0; i < n_params; ++ i)
  {
    if(strcmp(params[i].name, "id") == 0)
    {
      sprintf(id_buf, "%u", g_value_get_uint(&params[i].value));
      xmlNewProp(xml, (const xmlChar*)"id", (const xmlChar*)id_buf);
    }
    else if(strcmp(params[i].name, "name") == 0)
    {
      name = g_value_get_string(&params[i].value);
      xmlNewProp(xml, (const xmlChar*)"name", (const xmlChar*)name);
    }
    else if(strcmp(params[i].name, "status") == 0)
    {
      status = g_value_get_enum(&params[i].value);

      inf_xml_util_set_attribute(
        xml,
        "status",
        inf_user_status_to_string(status)
      );
    }
/*    else if(strcmp(params[i].name, "connection") == 0)
    {
      connection = INF_XML_CONNECTION(g_value_get_object(&params[i].value));

      g_object_get_property(
        G_OBJECT(connection),
        "remote-address",
        &remote_address,
        NULL
      );

      g_free(addr);
    }*/
  }
}

static gboolean
inf_session_validate_user_props_impl(InfSession* session,
                                     const GParameter* params,
                                     guint n_params,
                                     InfUser* exclude,
                                     GError** error)
{
  InfSessionPrivate* priv;
  const GParameter* parameter;
  const gchar* name;
  InfUser* user;
  guint id;

  priv = INF_SESSION_PRIVATE(session);

  /* TODO: Use InfSessionError and/or InfRequestError here */
  parameter = inf_session_lookup_user_property(params, n_params, "id");
  if(parameter == NULL)
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_ID_NOT_PRESENT,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_ID_NOT_PRESENT)
    );

    return FALSE;
  }

  id = g_value_get_uint(&parameter->value);
  user = inf_user_table_lookup_user_by_id(priv->user_table, id);

  if(user != NULL && user != exclude)
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_ID_IN_USE,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_ID_IN_USE)
    );

    return FALSE;
  }

  parameter = inf_session_lookup_user_property(params, n_params, "name");
  if(parameter == NULL)
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_NAME_NOT_PRESENT,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_NAME_NOT_PRESENT)
    );

    return FALSE;
  }

  name = g_value_get_string(&parameter->value);
  user = inf_user_table_lookup_user_by_name(priv->user_table, name);

  if(user != NULL && user != exclude)
  {
    g_set_error(
      error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_NAME_IN_USE,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_NAME_IN_USE)
    );

    return FALSE;
  }

  return TRUE;
}

/*
 * InfCommunicationObject implementation.
 */
static gboolean
inf_session_handle_received_sync_message(InfSession* session,
                                         InfXmlConnection* connection,
                                         const xmlNodePtr node,
                                         GError** error)
{
  InfSessionClass* session_class;
  InfSessionPrivate* priv;
  xmlChar* num_messages;
  gboolean result;
  xmlNodePtr xml_reply;
  GError* local_error;

  session_class = INF_SESSION_GET_CLASS(session);
  priv = INF_SESSION_PRIVATE(session);

  g_assert(session_class->process_xml_sync != NULL);
  g_assert(priv->status == INF_SESSION_SYNCHRONIZING);

  if(strcmp((const gchar*)node->name, "sync-cancel") == 0)
  {
    local_error = NULL;

    g_set_error(
      &local_error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_SENDER_CANCELLED,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_SENDER_CANCELLED)
    );

    g_signal_emit(
      G_OBJECT(session),
      session_signals[SYNCHRONIZATION_FAILED],
      0,
      connection,
      local_error
    );

#if 0
    /* Synchronization was cancelled by remote site, so release connection
     * prior to closure, otherwise we would try to tell the remote site
     * that the session was closed, but there is no point in this because
     * it just was the other way around. */
    /* Note: This is actually done by the default handler of the 
     * synchronization-failed signal. */
    inf_session_close(session);
#endif
    g_error_free(local_error);

    /* Return FALSE, but do not set error because we handled it. Otherwise,
     * inf_session_communication_object_received() would try to send a
     * sync-error to the synchronizer which is pointless as mentioned
     * above. */
    return FALSE;
  }
  else if(strcmp((const gchar*)node->name, "sync-begin") == 0)
  {
    if(priv->shared.sync.messages_total > 0)
    {
      g_set_error(
        error,
        inf_session_sync_error_quark,
        INF_SESSION_SYNC_ERROR_UNEXPECTED_BEGIN_OF_SYNC,
        "%s",
        inf_session_sync_strerror(
          INF_SESSION_SYNC_ERROR_UNEXPECTED_BEGIN_OF_SYNC
        )
      );

      return FALSE;
    }
    else
    {
      num_messages = xmlGetProp(node, (const xmlChar*)"num-messages");
      if(num_messages == NULL)
      {
        g_set_error(
          error,
          inf_session_sync_error_quark,
          INF_SESSION_SYNC_ERROR_NUM_MESSAGES_MISSING,
          "%s",
          inf_session_sync_strerror(
            INF_SESSION_SYNC_ERROR_NUM_MESSAGES_MISSING
          )
        );

        return FALSE;
      }
      else
      {
        /* 2 + [...] because we also count this initial sync-begin message
         * and the sync-end. This way, we can use a messages_total of 0 to
         * indicate that we did not yet get a sync-begin, even if the
         * whole sync does not contain any messages. */
        priv->shared.sync.messages_total = 2 + strtoul(
          (const gchar*)num_messages,
          NULL,
          0
        );

        priv->shared.sync.messages_received = 1;
        xmlFree(num_messages);

        g_signal_emit(
          G_OBJECT(session),
          session_signals[SYNCHRONIZATION_PROGRESS],
          0,
          connection,
          1.0 / (double)priv->shared.sync.messages_total
        );
 
        return TRUE;
      }
    }
  }
  else if(strcmp((const gchar*)node->name, "sync-end") == 0)
  {
    ++ priv->shared.sync.messages_received;
    if(priv->shared.sync.messages_received !=
       priv->shared.sync.messages_total)
    {
      g_set_error(
        error,
        inf_session_sync_error_quark,
        INF_SESSION_SYNC_ERROR_UNEXPECTED_END_OF_SYNC,
        "%s",
        inf_session_sync_strerror(
          INF_SESSION_SYNC_ERROR_UNEXPECTED_END_OF_SYNC
        )
      );

      return FALSE;
    }
    else
    {
      /* Server is waiting for ACK so that he knows the synchronization cannot
       * fail anymore. */
      xml_reply = xmlNewNode(NULL, (const xmlChar*)"sync-ack");

      inf_communication_group_send_message(
        priv->shared.sync.group,
        connection,
        xml_reply
      );

      /* Synchronization complete */
      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_COMPLETE],
        0,
        connection
      );

      return TRUE;
    }
  }
  else
  {
    if(priv->shared.sync.messages_received == 0)
    {
      g_set_error(
        error,
        inf_session_sync_error_quark,
        INF_SESSION_SYNC_ERROR_EXPECTED_BEGIN_OF_SYNC,
        "%s",
        inf_session_sync_strerror(
          INF_SESSION_SYNC_ERROR_EXPECTED_BEGIN_OF_SYNC
        )
      );

      return FALSE;
    }
    else if(priv->shared.sync.messages_received ==
            priv->shared.sync.messages_total - 1)
    {
      g_set_error(
        error,
        inf_session_sync_error_quark,
        INF_SESSION_SYNC_ERROR_EXPECTED_END_OF_SYNC,
        "%s",
        inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_EXPECTED_END_OF_SYNC)
      );

      return FALSE;
    }
    else
    {
      result = session_class->process_xml_sync(
        session,
        connection,
        node,
        error
      );

      if(result == FALSE) return FALSE;

      /* Some callback could have cancelled the synchronization via
       * inf_session_cancel_synchronization. */
      if(priv->status == INF_SESSION_CLOSED)
        return TRUE;

      ++ priv->shared.sync.messages_received;

      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_PROGRESS],
        0,
        connection,
        (double)priv->shared.sync.messages_received /
          (double)priv->shared.sync.messages_total
      );

      return TRUE;
    }
  }
}

static void
inf_session_communication_object_sent(InfCommunicationObject* comm_object,
                                      InfXmlConnection* connection,
                                      const xmlNodePtr node)
{
  InfSession* session;
  InfSessionPrivate* priv;
  InfSessionSync* sync;

  session = INF_SESSION(comm_object);
  priv = INF_SESSION_PRIVATE(session);

  if(priv->status == INF_SESSION_RUNNING)
  {
    sync = inf_session_find_sync_by_connection(session, connection);

    /* This can be any message from some session that is not related to
     * the synchronization, so do not assert here. Also, we might already have
     * sent stuff that is meant to be processed after the synchronization, so
     * make sure here that this messages still belongs to the
     * synchronization. */
    if(sync != NULL && sync->messages_sent < sync->messages_total)
    {
      ++ sync->messages_sent;

      g_signal_emit(
        G_OBJECT(comm_object),
        session_signals[SYNCHRONIZATION_PROGRESS],
        0,
        connection,
        (gdouble)sync->messages_sent / (gdouble)sync->messages_total
      );

      /* We need to wait for the sync-ack before synchronization is
       * completed so that the synchronizee still has a chance to tell
       * us if something goes wrong. */
    }
  }
}

static void
inf_session_communication_object_enqueued(InfCommunicationObject* comm_object,
                                          InfXmlConnection* connection,
                                          const xmlNodePtr node)
{
  InfSessionSync* sync;

  if(strcmp((const gchar*)node->name, "sync-end") == 0)
  {
    /* Remember when the last synchronization messages is enqueued because
     * we cannot cancel any synchronizations beyond that point. */
    sync = inf_session_find_sync_by_connection(
      INF_SESSION(comm_object),
      connection
    );

    /* This should really be in the list if the node's name is sync-end,
     * otherwise most probably someone else sent a sync-end message via
     * this communication object. */
    g_assert(sync != NULL);
    g_assert(sync->status == INF_SESSION_SYNC_IN_PROGRESS);

    sync->status = INF_SESSION_SYNC_AWAITING_ACK;
  }
}

static InfCommunicationScope
inf_session_communication_object_received(InfCommunicationObject* comm_object,
                                          InfXmlConnection* connection,
                                          const xmlNodePtr node)
{
  InfSessionClass* session_class;
  InfSession* session;
  InfSessionPrivate* priv;
  InfSessionSync* sync;
  gboolean result;
  InfCommunicationScope scope;
  GError* local_error;
  const gchar* local_message;

  session = INF_SESSION(comm_object);
  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_PRESYNC:
    g_assert(connection == priv->shared.presync.conn);

    /* We do not expect any messages in presync. The whole idea of presync is
     * that one can keep a session around that is going to be synchronized
     * later, ie. when telling the remote site about it. */
    local_error = NULL;

    g_set_error(
      &local_error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_GOT_MESSAGE_IN_PRESYNC,
      _("Unexpectedly received XML message \"%s\" in presync"),
      (const gchar*)node->name
    );

     g_signal_emit(
      G_OBJECT(session),
      session_signals[ERROR],
      0,
      connection,
      node,
      local_error
    );

    /* Don't forward unexpected message */
    return INF_COMMUNICATION_SCOPE_PTP;
  case INF_SESSION_SYNCHRONIZING:
    g_assert(connection == priv->shared.sync.conn);

    local_error = NULL;
    result = inf_session_handle_received_sync_message(
      session,
      connection,
      node,
      &local_error
    );

    if(result == FALSE && local_error != NULL)
    {
      inf_session_send_sync_error(session, local_error);

      /* Note the default handler resets shared->sync.conn and
       * shared->sync.group. */
      g_signal_emit(
        G_OBJECT(session),
        session_signals[SYNCHRONIZATION_FAILED],
        0,
        connection,
        local_error
      );

      g_error_free(local_error);
    }

    /* Synchronization is always ptp only, don't forward */
    return INF_COMMUNICATION_SCOPE_PTP;
  case INF_SESSION_RUNNING:
    sync = inf_session_find_sync_by_connection(session, connection);
    if(sync != NULL)
    {
      if(strcmp((const gchar*)node->name, "sync-error") == 0)
      {
        /* There was an error during synchronization, cancel remaining
         * messages. */
        inf_communication_group_cancel_messages(sync->group, connection);

        local_error = inf_xml_util_new_error_from_node(node);

        if(local_error != NULL)
        {
          local_message =
            inf_session_get_sync_error_message(local_error->domain,
                                               local_error->code);
          if(local_message != NULL)
          {
            if(local_error->message != NULL)
              g_free(local_error->message);
            local_error->message = g_strdup(local_message);
          }
        }
        else
        {
          g_set_error(
            &local_error,
            inf_session_sync_error_quark,
            INF_SESSION_SYNC_ERROR_FAILED,
            "%s",
            inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_FAILED)
          );
        }

        /* Note the default handler actually removes the sync */
        g_signal_emit(
          G_OBJECT(session),
          session_signals[SYNCHRONIZATION_FAILED],
          0,
          connection,
          local_error
        );

        g_error_free(local_error);
      }
      else if(strcmp((const gchar*)node->name, "sync-ack") == 0 &&
              sync->status == INF_SESSION_SYNC_AWAITING_ACK)
      {
        /* Got ack we were waiting for */
        g_signal_emit(
          G_OBJECT(comm_object),
          session_signals[SYNCHRONIZATION_COMPLETE],
          0,
          connection
        );
      }
      else
      {
        local_error = NULL;

        g_set_error(
          &local_error,
          inf_session_sync_error_quark,
          INF_SESSION_SYNC_ERROR_UNEXPECTED_NODE,
          _("Received unexpected XML message \"%s\" while synchronizing"),
          (const gchar*)node->name
        );

        /* We got sent something by the other session, even though we do not
         * expect anything really. We consider it an error, and cancel the
         * synchronization here. */

        g_error_free(local_error);
      }

      /* Synchronization is always ptp only, don't forward */
      return INF_COMMUNICATION_SCOPE_PTP;
    }
    else
    {
      session_class = INF_SESSION_GET_CLASS(session);
      g_assert(session_class->process_xml_run != NULL);

      local_error = NULL;
      scope = session_class->process_xml_run(
        session,
        connection,
        node,
        &local_error
      );

      if(local_error != NULL)
      {
        /* At this point we don't really know what's wrong with the session,
         * or whether the error is fatal in a sense or not. We emit the
         * "error" signal here, so that anyone who is interested can handle
         * it, for example by closing the session, or logging it into a file
         * for human inspection. */
        g_signal_emit(
          G_OBJECT(session),
          session_signals[ERROR],
          0,
          connection,
          node,
          local_error
        );

        g_error_free(local_error);
      }

      return scope;
    }
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    return INF_COMMUNICATION_SCOPE_PTP;
  }
}

/*
 * Default signal handlers.
 */

static void
inf_session_close_handler(InfSession* session)
{
  InfSessionPrivate* priv;
  InfSessionSync* sync;

  priv = INF_SESSION_PRIVATE(session);

  g_object_freeze_notify(G_OBJECT(session));

  switch(priv->status)
  {
  case INF_SESSION_PRESYNC:
    if(priv->shared.presync.closing == FALSE)
    {
      /* So that the "synchronization-failed" default signal handler does
       * does not emit the close signal again: */
      priv->shared.presync.closing = TRUE;

      inf_session_cancel_synchronization(session, priv->shared.presync.conn);
    }

    inf_session_release_connection(session, priv->shared.presync.conn);
    break;
  case INF_SESSION_SYNCHRONIZING:
    if(priv->shared.sync.closing == FALSE)
    {
      /* So that the "synchronization-failed" default signal handler does
       * does not emit the close signal again: */
      priv->shared.sync.closing = TRUE;

      inf_session_cancel_synchronization(session, priv->shared.sync.conn);
    }

    inf_session_release_connection(session, priv->shared.sync.conn);
    break;
  case INF_SESSION_RUNNING:
    /* TODO: Set status of all users (except local) to unavailable? We
     * probably should do that here instead of in the session proxies,
     * or at least in addition (InfcSessionProxy needs to do it anway,
     * because it keeps the running state even on disconnection...) */

    while(priv->shared.run.syncs != NULL)
    {
      sync = (InfSessionSync*)priv->shared.run.syncs->data;
      inf_session_cancel_synchronization(session, sync->conn);
    }

    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }

  if(priv->subscription_group != NULL)
  {
    g_object_unref(priv->subscription_group);
    priv->subscription_group = NULL;

    g_object_notify(G_OBJECT(session), "subscription-group");
  }

  priv->status = INF_SESSION_CLOSED;
  g_object_notify(G_OBJECT(session), "status");
  g_object_thaw_notify(G_OBJECT(session));
}

static void
inf_session_synchronization_begin_handler(InfSession* session,
                                          InfCommunicationGroup* group,
                                          InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  InfSessionClass* session_class;
  InfSessionSync* sync;
  xmlNodePtr messages;
  xmlNodePtr next;
  xmlNodePtr xml;
  gchar num_messages_buf[16];

  priv = INF_SESSION_PRIVATE(session);

  g_assert(priv->status == INF_SESSION_RUNNING);
  g_assert(inf_session_find_sync_by_connection(session, connection) == NULL);

  session_class = INF_SESSION_GET_CLASS(session);
  g_return_if_fail(session_class->to_xml_sync != NULL);

  sync = g_slice_new(InfSessionSync);
  sync->conn = connection;
  sync->messages_sent = 0;
  sync->messages_total = 2; /* including sync-begin and sync-end */
  sync->status = INF_SESSION_SYNC_IN_PROGRESS;

  g_object_ref(G_OBJECT(connection));
  priv->shared.run.syncs = g_slist_prepend(priv->shared.run.syncs, sync);

  g_signal_connect_after(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(inf_session_connection_notify_status_cb),
    session
  );

  sync->group = group;
  g_object_ref(sync->group);

  /* The group needs to contain that connection, of course. */
  g_assert(inf_communication_group_is_member(sync->group, connection));

  /* Name is irrelevant because the node is only used to collect the child
   * nodes via the to_xml_sync vfunc. */
  messages = xmlNewNode(NULL, (const xmlChar*)"sync-container");
  session_class->to_xml_sync(session, messages);

  for(xml = messages->children; xml != NULL; xml = xml->next)
    ++ sync->messages_total;

  sprintf(num_messages_buf, "%u", sync->messages_total - 2);

  xml = xmlNewNode(NULL, (const xmlChar*)"sync-begin");

  xmlNewProp(
    xml,
    (const xmlChar*)"num-messages",
    (const xmlChar*)num_messages_buf
  );

  inf_communication_group_send_message(sync->group, connection, xml);

  /* TODO: Add a function that can send multiple messages */
  for(xml = messages->children; xml != NULL; xml = next)
  {
    next = xml->next;
    xmlUnlinkNode(xml);

    inf_communication_group_send_message(sync->group, connection, xml);
  }

  xmlFreeNode(messages);
  xml = xmlNewNode(NULL, (const xmlChar*)"sync-end");
  inf_communication_group_send_message(sync->group, connection, xml);
}

static void
inf_session_synchronization_complete_handler(InfSession* session,
                                             InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_PRESYNC:
    g_assert_not_reached();
    break;
  case INF_SESSION_SYNCHRONIZING:
    g_assert(connection == priv->shared.sync.conn);

    inf_session_release_connection(session, connection);

    priv->status = INF_SESSION_RUNNING;
    priv->shared.run.syncs = NULL;

    g_object_notify(G_OBJECT(session), "status");
    break;
  case INF_SESSION_RUNNING:
    g_assert(
      inf_session_find_sync_by_connection(session, connection) != NULL
    );

    inf_session_release_connection(session, connection);
    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }
}

static void
inf_session_synchronization_failed_handler(InfSession* session,
                                           InfXmlConnection* connection,
                                           const GError* error)
{
  InfSessionPrivate* priv;
  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_PRESYNC:
    g_assert(connection == priv->shared.presync.conn);
    if(priv->shared.presync.closing == FALSE)
    {
      /* So that the "close" default signal handler does not emit the 
       * "synchronization failed" signal again. */
      priv->shared.presync.closing = TRUE;
      inf_session_close(session);
    }

    break;
  case INF_SESSION_SYNCHRONIZING:
    g_assert(connection == priv->shared.sync.conn);
    if(priv->shared.sync.closing == FALSE)
    {
      /* So that the "close" default signal handler does not emit the 
       * "synchronization failed" signal again. */
      priv->shared.sync.closing = TRUE;
      inf_session_close(session);
    }

    break;
  case INF_SESSION_RUNNING:
    g_assert(
      inf_session_find_sync_by_connection(session, connection) != NULL
    );

    inf_session_release_connection(session, connection);
    break;
  case INF_SESSION_CLOSED:
    /* Don't assert, some signal handler could have called inf_session_close()
     * between the start of the signal emission and the run of the default
     * handler. */
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

/*
 * Gype registration.
 */

static void
inf_session_class_init(gpointer g_class,
                       gpointer class_data)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;

  object_class = G_OBJECT_CLASS(g_class);
  session_class = INF_SESSION_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfSessionPrivate));

  object_class->constructor = inf_session_constructor;
  object_class->dispose = inf_session_dispose;
  object_class->finalize = inf_session_finalize;
  object_class->set_property = inf_session_set_property;
  object_class->get_property = inf_session_get_property;

  session_class->to_xml_sync = inf_session_to_xml_sync_impl;
  session_class->process_xml_sync = inf_session_process_xml_sync_impl;
  session_class->process_xml_run = inf_session_process_xml_run_impl;

  session_class->get_xml_user_props = inf_session_get_xml_user_props_impl;
  session_class->set_xml_user_props = inf_session_set_xml_user_props_impl;
  session_class->validate_user_props = inf_session_validate_user_props_impl;

  session_class->user_new = NULL;

  session_class->close = inf_session_close_handler;
  session_class->error = NULL;
  session_class->synchronization_begin =
    inf_session_synchronization_begin_handler;
  session_class->synchronization_progress = NULL;
  session_class->synchronization_complete =
    inf_session_synchronization_complete_handler;
  session_class->synchronization_failed =
    inf_session_synchronization_failed_handler;

  inf_session_sync_error_quark = g_quark_from_static_string(
    "INF_SESSION_SYNC_ERROR"
  );

  g_object_class_install_property(
    object_class,
    PROP_COMMUNICATION_MANAGER,
    g_param_spec_object(
      "communication-manager",
      "Communication manager",
      "The communication manager used for sending requests",
      INF_COMMUNICATION_TYPE_MANAGER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_BUFFER,
    g_param_spec_object(
      "buffer",
      "Buffer",
      "The buffer in which the document content is stored",
      INF_TYPE_BUFFER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_USER_TABLE,
    g_param_spec_object(
      "user-table",
      "User table",
      "User table containing the users of the session",
      INF_TYPE_USER_TABLE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_STATUS,
    g_param_spec_enum(
      "status",
      "Session Status",
      "Current status of the session",
      INF_TYPE_SESSION_STATUS,
      INF_SESSION_RUNNING,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SYNC_CONNECTION,
    g_param_spec_object(
      "sync-connection",
      "Synchronizing connection",
      "Connection which synchronizes the initial session state",
      INF_TYPE_XML_CONNECTION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SYNC_GROUP,
    g_param_spec_object(
      "sync-group",
      "Synchronization group",
      "Communication group in which to perform synchronization",
      INF_COMMUNICATION_TYPE_GROUP,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SUBSCRIPTION_GROUP,
    g_param_spec_object(
      "subscription-group",
      "Subscription group",
      "Communication group of subscribed connections",
      INF_COMMUNICATION_TYPE_GROUP,
      G_PARAM_READWRITE
    )
  );

  /**
   * InfSession::close:
   * @session: The #InfSession that is being closed
   *
   * This signal is emitted if the session is closed. Note that this signal
   * is not called as a client if the connection to the sessions has merely
   * been lost, only the relevant #InfXmlConnection has its
   * #InfXmlConnection:status property changed.
   */
  session_signals[CLOSE] = g_signal_new(
    "close",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, close),
    NULL, NULL,
    inf_marshal_VOID__VOID,
    G_TYPE_NONE,
    0
  );

  /**
   * InfSession::error:
   * @session: The #InfSession having an error.
   * @connection: The #InfXmlConnection which sent the erroneous request.
   * @xml: The XML request that produced the error.
   * @error: A #GError providing information about the error.
   *
   * This signal is emitted when the session detects an error. The session
   * itself does not know much about the nature of the error. It might mean
   * the session is in an inconsistent state, or it might be recoverable.
   * This signal can be used to handle the error or to write error
   * information into a log file or bring to a user's attention in another
   * manner. */
  session_signals[ERROR] = g_signal_new(
    "error",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, error),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_POINTER_POINTER,
    G_TYPE_NONE,
    3,
    INF_TYPE_XML_CONNECTION,
    G_TYPE_POINTER, /* actually a xmlNodePtr */
    G_TYPE_POINTER /* actually a GError* */
  );

  /**
   * InfSession::synchronization-begin:
   * @session: The #InfSession that is synchronizing.
   * @group: The #InfCommunicationGroup used for synchronization.
   * @connection: The #InfXmlConnection to which the session is synchronized.
   *
   * This signal is emitted whenever the session is started to be synchronized
   * to another connection. Note that, in contrast to
   * #InfSession::synchronization-progress,
   * #InfSession::synchronization-failed and
   * #InfSession::synchronization-complete it cannot happen that the signal
   * is emitted when @session is being synchronized itself, because that can
   * happen at construction time only when no one had a chance to connect
   * signal handlers anyway.
   **/
  session_signals[SYNCHRONIZATION_BEGIN] = g_signal_new(
    "synchronization-begin",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, synchronization_begin),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_OBJECT,
    G_TYPE_NONE,
    2,
    INF_COMMUNICATION_TYPE_GROUP,
    INF_TYPE_XML_CONNECTION
  );

  /**
   * InfSession::synchronization-progress:
   * @session: The #InfSession that is synchronizing or being synchronized
   * @connection: The #InfXmlConnection through which progress is made
   * @progress: A #gdouble value ranging from 0.0 to 1.0.
   *
   * This signal is emitted whenever a new XML node has been sent or received
   * over @connection as part of a synchronization. The process is completed
   * when @progress reaches the value 1.0. At this point,
   * #InfSession::synchronization-complete is also emitted.
   *
   * If @session&apos;s status is %INF_SESSION_SYNCHRONIZING, the local
   * side is being synchronized by the remote side. If the status is
   * %INF_SESSION_RUNNING, the local side is updating the remote side.
   */
  session_signals[SYNCHRONIZATION_PROGRESS] = g_signal_new(
    "synchronization-progress",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, synchronization_progress),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_DOUBLE,
    G_TYPE_NONE,
    2,
    INF_TYPE_XML_CONNECTION,
    G_TYPE_DOUBLE
  );

  /**
   * InfSession::synchronization-complete:
   * @session: The #InfSession that has or was synchronized
   * @connection: The #InfXmlConnection through which synchronization happened
   *
   * This signal is emitted when synchronization has completed, in addition
   * to #InfSession::synchronization-progress with a progress value of 1.0.
   *
   * If a callback is connected before the default handler, it can find out
   * whether the remote side is synchronizing the local side by comparing
   * @sessions&apos;s status with %INF_SESSION_SYNCHRONIZING. The default
   * signal handler sets the status to %INF_SESSION_RUNNING, so checking
   * afterwards is not too useful.
   */
  session_signals[SYNCHRONIZATION_COMPLETE] = g_signal_new(
    "synchronization-complete",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, synchronization_complete),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XML_CONNECTION
  );

  /**
   * InfSession::synchronization-failed:
   * @session: The #InfSession that failed to synchronize or be synchronized
   * @connection: The #InfXmlConnection through which synchronization happened
   * @error: A pointer to a #GError object with details on the error
   *
   * This signal is emitted when synchronization has failed before its
   * completion due to malformed data from the other side or network failure.
   *
   * If this happens during initial synchronization, #InfSession::close is
   * emitted as well at this point.
   */
  session_signals[SYNCHRONIZATION_FAILED] = g_signal_new(
    "synchronization-failed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfSessionClass, synchronization_failed),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_POINTER,
    G_TYPE_NONE,
    2,
    INF_TYPE_XML_CONNECTION,
    G_TYPE_POINTER /* actually a GError* */
  );
}

static void
inf_session_communication_object_init(gpointer g_iface,
                                      gpointer iface_data)
{
  InfCommunicationObjectIface* iface;
  iface = (InfCommunicationObjectIface*)g_iface;

  iface->sent = inf_session_communication_object_sent;
  iface->enqueued = inf_session_communication_object_enqueued;
  iface->received = inf_session_communication_object_received;
}

GType
inf_session_status_get_type(void)
{
  static GType session_status_type = 0;

  if(!session_status_type)
  {
    static const GEnumValue session_status_type_values[] = {
      {
        INF_SESSION_PRESYNC,
        "INF_SESSION_PRESYNC",
        "presync"
      }, {
        INF_SESSION_SYNCHRONIZING,
        "INF_SESSION_SYNCHRONIZING",
        "synchronizing"
      }, {
        INF_SESSION_RUNNING,
        "INF_SESSION_RUNNING",
        "running"
      }, {
        INF_SESSION_CLOSED,
        "INF_SESSION_CLOSED",
        "closed"
      }, {
        0,
        NULL,
        NULL
      }
    };

    session_status_type = g_enum_register_static(
      "InfSessionStatus",
      session_status_type_values
    );
  }

  return session_status_type;
}

GType
inf_session_get_type(void)
{
  static GType session_type = 0;

  if(!session_type)
  {
    static const GTypeInfo session_type_info = {
      sizeof(InfSessionClass),  /* class_size */
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      inf_session_class_init,   /* class_init */
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof(InfSession),       /* instance_size */
      0,                        /* n_preallocs */
      inf_session_init,         /* instance_init */
      NULL                      /* value_table */
    };

    static const GInterfaceInfo communication_object_info = {
      inf_session_communication_object_init,
      NULL,
      NULL
    };

    session_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfSession",
      &session_type_info,
      0
    );

    g_type_add_interface_static(
      session_type,
      INF_COMMUNICATION_TYPE_OBJECT,
      &communication_object_info
    );
  }

  return session_type;
}

/*
 * Public API.
 */

/**
 * inf_session_lookup_user_property:
 * @params: A pointer to an array of containing #GParameter values.
 * @n_params: The number of elements in the aforementioned array
 * @name: Name to look up.
 *
 * Looks up the parameter with the given name in @array.
 *
 * Return Value: A #GParameter, or %NULL.
 **/
const GParameter*
inf_session_lookup_user_property(const GParameter* params,
                                 guint n_params,
                                 const gchar* name)
{
  guint i;

  g_return_val_if_fail(params != NULL || n_params == 0, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  for(i = 0; i < n_params; ++ i)
    if(strcmp(params[i].name, name) == 0)
      return &params[i];

  return NULL;
}

/**
 * inf_session_get_user_property:
 * @array: A #GArray containing #GParameter values.
 * @name: Name to look up.
 *
 * Looks up the paremeter with the given name in @array. If there is no such
 * parameter, a new one will be created.
 *
 * Return Value: A #GParameter.
 **/
GParameter*
inf_session_get_user_property(GArray* array,
                              const gchar* name)
{
  GParameter* parameter;
  guint i;

  g_return_val_if_fail(array != NULL, NULL);
  g_return_val_if_fail(name != NULL, NULL);

  for(i = 0; i < array->len; ++ i)
    if(strcmp(g_array_index(array, GParameter, i).name, name) == 0)
      return &g_array_index(array, GParameter, i);

  g_array_set_size(array, array->len + 1);
  parameter = &g_array_index(array, GParameter, array->len - 1);

  parameter->name = name;
  memset(&parameter->value, 0, sizeof(GValue));
  return parameter;
}

/**
 * inf_session_user_to_xml:
 * @session: A #InfSession.
 * @user: A #InfUser contained in @session.
 * @xml: An XML node to which to add user information.
 *
 * This is a convenience function that queries @user's properties and
 * calls set_xml_user_props with them. This adds the properties of @user
 * to @xml.
 *
 * An equivalent user object may be built by calling the get_xml_user_props
 * vfunc on @xml and then calling the user_new vfunc with the resulting
 * properties.
 **/
void
inf_session_user_to_xml(InfSession* session,
                        InfUser* user,
                        xmlNodePtr xml)
{
  InfSessionClass* session_class;
  GParamSpec** pspecs;
  GParameter* params;
  guint n_params;
  guint i;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(INF_IS_USER(user));
  g_return_if_fail(xml != NULL);

  session_class = INF_SESSION_GET_CLASS(session);
  g_return_if_fail(session_class->set_xml_user_props != NULL);

  pspecs = g_object_class_list_properties(
    G_OBJECT_CLASS(INF_USER_GET_CLASS(user)),
    &n_params
  );

  params = g_malloc(n_params * sizeof(GParameter));
  for(i = 0; i < n_params; ++ i)
  {
    params[i].name = pspecs[i]->name;
    memset(&params[i].value, 0, sizeof(GValue));
    g_value_init(&params[i].value, pspecs[i]->value_type);
    g_object_get_property(G_OBJECT(user), params[i].name, &params[i].value);
  }

  session_class->set_xml_user_props(session, params, n_params, xml);

  for(i = 0; i < n_params; ++ i)
    g_value_unset(&params[i].value);

  g_free(params);
  g_free(pspecs);
}

/**
 * inf_session_close:
 * @session: A #InfSession.
 *
 * Closes a running session. When a session is closed, it unrefs all
 * connections and no longer handles requests.
 */
void
inf_session_close(InfSession* session)
{
  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(inf_session_get_status(session) != INF_SESSION_CLOSED);
  g_signal_emit(G_OBJECT(session), session_signals[CLOSE], 0);
}

/**
 * inf_session_get_communication_manager:
 * @session: A #InfSession.
 *
 * Returns the communication manager for @session.
 *
 * Return Value: A #InfCommunicationManager.
 **/
InfCommunicationManager*
inf_session_get_communication_manager(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  return INF_SESSION_PRIVATE(session)->manager;
}

/**
 * inf_session_get_buffer:
 * @session: A #InfSession.
 *
 * Returns the buffer used by @session.
 *
 * Return Value: A #InfBuffer.
 **/
InfBuffer*
inf_session_get_buffer(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  return INF_SESSION_PRIVATE(session)->buffer;
}

/**
 * inf_session_get_user_table:
 * @session:A #InfSession.
 *
 * Returns the user table used by @session.
 *
 * Return Value: A #InfUserTable.
 **/
InfUserTable*
inf_session_get_user_table(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  return INF_SESSION_PRIVATE(session)->user_table;
}

/**
 * inf_session_get_status:
 * @session: A #InfSession.
 *
 * Returns the session's status.
 *
 * Return Value: The status of @session.
 **/
InfSessionStatus
inf_session_get_status(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), INF_SESSION_CLOSED);
  return INF_SESSION_PRIVATE(session)->status;
}

/**
 * inf_session_add_user:
 * @session: A #InfSession.
 * @params: Construction parameters for the #InfUser (or derived) object.
 * @n_params: Number of parameters.
 * @error: Location to store error information.
 *
 * Adds a user to @session. The user object is constructed via the
 * user_new vfunc of #InfSessionClass. This will create a new #InfUser
 * object by default, but may be overridden by subclasses to create
 * different kinds of users.
 *
 * Note that this function does not tell the other participants that the
 * user was added. To avoid conflicts, normally only the publisher of the
 * session can add users and notifies others accordingly. This is handled
 * by #InfdSessionProxy or #InfcSessionProxy, respectively.
 *
 * You should not call this function unless you know what you are doing.
 * If @params comes from an untrusted source, they should be checked first
 * with the validate_user_props virtual function.
 *
 * Return Value: The new #InfUser, or %NULL in case of an error.
 **/
InfUser*
inf_session_add_user(InfSession* session,
                     const GParameter* params,
                     guint n_params)
{
  InfSessionPrivate* priv;
  InfSessionClass* session_class;
  InfUser* user;
  gboolean result;

  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  session_class = INF_SESSION_GET_CLASS(session);

  g_return_val_if_fail(session_class->validate_user_props != NULL, NULL);
  g_return_val_if_fail(session_class->user_new != NULL, NULL);

  priv = INF_SESSION_PRIVATE(session);

  g_return_val_if_fail(
    session_class->validate_user_props(session, params, n_params, NULL, NULL),
    NULL
  );

  /* No idea why g_object_newv wants unconst GParameter list */
  user = session_class->user_new(
    session,
    *(GParameter**)(gpointer)&params,
    n_params
  );

  inf_user_table_add_user(priv->user_table, user);
  g_object_unref(user); /* We rely on the usertable holding a reference */

  return user;
}

/**
 * inf_session_set_user_status:
 * @session: A #InfSession.
 * @user: A local #InfUser from @session's user table.
 * @status: New status for @user.
 *
 * Changes the status of the given @user which needs to have the
 * %INF_USER_LOCAL flag set for this function to be called. If the status
 * is changed to %INF_USER_UNAVAILABLE, then the user leaves the session. To
 * rejoin use infc_session_proxy_join_user() or infd_session_proxy_add_user(),
 * respectively for a proxy proxying @session.
 **/
void
inf_session_set_user_status(InfSession* session,
                            InfUser* user,
                            InfUserStatus status)
{
  InfSessionPrivate* priv;
  xmlNodePtr xml;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(INF_IS_USER(user));
  g_return_if_fail(inf_session_get_status(session) == INF_SESSION_RUNNING);
  g_return_if_fail(inf_user_get_status(user) != INF_USER_UNAVAILABLE);
  g_return_if_fail( (inf_user_get_flags(user) & INF_USER_LOCAL) != 0);

  priv = INF_SESSION_PRIVATE(session);

  if(inf_user_get_status(user) != status)
  {
    xml = xmlNewNode(NULL, (const xmlChar*)"user-status-change");
    inf_xml_util_set_attribute_uint(xml, "id", inf_user_get_id(user));

    inf_xml_util_set_attribute(
      xml,
      "status",
      inf_user_status_to_string(status)
    );

    if(priv->subscription_group != NULL)
      inf_session_send_to_subscriptions(session, xml);

    g_object_set(G_OBJECT(user), "status", status, NULL);
  }
}

/**
 * inf_session_synchronize_from:
 * @session: A #InfSession in status %INF_SESSION_PRESYNC.
 *
 * Switches @session's status to %INF_SESSION_SYNCHRONIZING. In
 * %INF_SESSION_PRESYNC, all messages from incoming the synchronizing
 * connection are ignored, and no cancellation request is sent to the remote
 * site if the status changes to %INF_SESSION_CLOSED. The rationale behind
 * that status is that one can prepare a session for synchronization but start
 * the actual synchronization later, after having made sure that the remote
 * site is ready to perform the synchronization.
 */
void
inf_session_synchronize_from(InfSession* session)
{
  InfSessionPrivate* priv;
  InfCommunicationGroup* group;
  InfXmlConnection* connection;

  /* TODO: Maybe add InfCommunicationGroup*, InfXmlConnection* arguments,
   * and remove them from the priv->shared.presync. This might simplify code
   * elsewhere. */

  g_return_if_fail(inf_session_get_status(session) == INF_SESSION_PRESYNC);

  priv = INF_SESSION_PRIVATE(session);
  g_return_if_fail(priv->shared.presync.closing == FALSE);

  group = priv->shared.presync.group;
  connection = priv->shared.presync.conn;

  priv->status = INF_SESSION_SYNCHRONIZING;

  priv->shared.sync.group = group;
  priv->shared.sync.conn = connection;
  priv->shared.sync.messages_total = 0;
  priv->shared.sync.messages_received = 0;
  priv->shared.sync.closing = FALSE;

  g_object_notify(G_OBJECT(session), "status");
}

/**
 * inf_session_synchronize_to:
 * @session: A #InfSession in status %INF_SESSION_RUNNING.
 * @group: A #InfCommunicationGroup.
 * @connection: A #InfXmlConnection.
 *
 * Initiates a synchronization to @connection. On the other end of
 * @connection, a new session with the sync-connection and sync-group
 * construction properties set should have been created. @group is used
 * as a group in the communication manager. It is allowed for @group to have
 * another #InfCommunicationObject than @session, however, you should forward
 * the #InfCommunicationObject messages your object receives to @session then.
 * Also, @connection must already be present in @group, and should not be
 * removed until synchronization finished.
 *
 * A synchronization can only be initiated if @session is in state
 * %INF_SESSION_RUNNING.
 **/
void
inf_session_synchronize_to(InfSession* session,
                           InfCommunicationGroup* group,
                           InfXmlConnection* connection)
{
  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(group != NULL);
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_return_if_fail(inf_session_get_status(session) == INF_SESSION_RUNNING);
  g_return_if_fail(
    inf_session_find_sync_by_connection(session, connection) == NULL
  );

  g_signal_emit(
    G_OBJECT(session),
    session_signals[SYNCHRONIZATION_BEGIN],
    0,
    group,
    connection
  );
}

/**
 * inf_session_cancel_synchronization:
 * @session: A #InfSession.
 * @connection: The #InfXmlConnection with which to cancel synchronization.
 *
 * Cancells an ongaing synchronization to or from @connection. If @session
 * is in state %INF_SESSION_PRESYNC or %INF_SESSION_SYNCHRONIZING,
 * @connection must match the connection that @session is synchronizing with.
 * If @session is in state %INF_SESSION_RUNNING, @connection can be any
 * connection that the session is currently being synchronized to.
 *
 * In any case, the #InfSession::synchronization-failed signal will be
 * emitted for the cancelled synchronization. If the session is in state
 * %INF_SESSION_PRESYNC or %INF_SESSION_SYNCHRONIZING, the session will also
 * be closed, with the #InfSession::close signal being emited.
 */
void
inf_session_cancel_synchronization(InfSession* session,
                                   InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  InfXmlConnectionStatus status;
  InfSessionSync* sync;
  xmlNodePtr xml;
  GError* error;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  priv = INF_SESSION_PRIVATE(session);
  error = NULL;

  switch(priv->status)
  {
  case INF_SESSION_PRESYNC:
    g_return_if_fail(connection == priv->shared.presync.conn);

    g_set_error(
      &error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED)
    );

    break;
  case INF_SESSION_SYNCHRONIZING:
    g_return_if_fail(connection == priv->shared.sync.conn);

    g_set_error(
      &error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_RECEIVER_CANCELLED)
    );

    g_object_get(G_OBJECT(connection), "status", &status, NULL);
    if(status == INF_XML_CONNECTION_OPEN)
      inf_session_send_sync_error(session, error);

    break;
  case INF_SESSION_RUNNING:
    sync = inf_session_find_sync_by_connection(session, connection);
    g_return_if_fail(sync != NULL);

    /* If the sync-end message has already been enqueued within the
     * connection manager, we cannot cancel it anymore, so the remote
     * site will receive the full sync nevertheless, so we do not need
     * to cancel anything. */
    if(sync->status == INF_SESSION_SYNC_IN_PROGRESS)
    {
      inf_communication_group_cancel_messages(sync->group, sync->conn);

      xml = xmlNewNode(NULL, (const xmlChar*)"sync-cancel");
      inf_communication_group_send_message(sync->group, sync->conn, xml);
    }

    g_set_error(
      &error,
      inf_session_sync_error_quark,
      INF_SESSION_SYNC_ERROR_SENDER_CANCELLED,
      "%s",
      inf_session_sync_strerror(INF_SESSION_SYNC_ERROR_SENDER_CANCELLED)
    );

    break;
  case INF_SESSION_CLOSED:
    g_return_if_reached();
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_signal_emit(
    G_OBJECT(session),
    session_signals[SYNCHRONIZATION_FAILED],
    0,
    connection,
    error
  );

  g_error_free(error);
}

/**
 * inf_session_get_synchronization_status:
 * @session: A #InfSession.
 * @connection: A #InfXmlConnection.
 *
 * If @session is in status %INF_SESSION_SYNCHRONIZING, this always returns
 * %INF_SESSION_SYNC_IN_PROGRESS if @connection is the connection with which
 * the session is synchronized, and %INF_SESSION_SYNC_NONE otherwise.
 *
 * If @session is in status %INF_SESSION_RUNNING, this returns the status
 * of the synchronization to @connection. %INF_SESSION_SYNC_NONE is returned,
 * when there is currently no synchronization ongoing to @connection,
 * %INF_SESSION_SYNC_IN_PROGRESS is returned, if there is one, and
 * %INF_SESSION_SYNC_AWAITING_ACK if the synchronization is finished but we
 * are waiting for the acknowledgement from the remote site that all
 * synchronization data has been progressed successfully. The synchronization
 * can still fail in this state but it can no longer by cancelled.
 *
 * If @session is in status $INF_SESSION_CLOSED, this always returns
 * %INF_SESSION_SYNC_NONE.
 *
 * Return Value: The synchronization status of @connection.
 **/
InfSessionSyncStatus
inf_session_get_synchronization_status(InfSession* session,
                                       InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  InfSessionSync* sync;

  g_return_val_if_fail(INF_IS_SESSION(session), INF_SESSION_SYNC_NONE);

  g_return_val_if_fail(
    INF_IS_XML_CONNECTION(connection),
    INF_SESSION_SYNC_NONE
  );

  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_SYNCHRONIZING:
    if(connection == priv->shared.sync.conn)
      return INF_SESSION_SYNC_IN_PROGRESS;
    return INF_SESSION_SYNC_NONE;
  case INF_SESSION_RUNNING:
    sync = inf_session_find_sync_by_connection(session, connection);
    if(sync == NULL) return INF_SESSION_SYNC_NONE;

    return sync->status;
  case INF_SESSION_CLOSED:
    return INF_SESSION_SYNC_NONE;
  default:
    g_assert_not_reached();
    break;
  }
}

/**
 * inf_session_get_synchronization_progress:
 * @session: A #InfSession.
 * @connection: A #InfXmlConnection.
 *
 * This function requires that the synchronization status of @connection
 * is %INF_SESSION_SYNC_IN_PROGRESS or %INF_SESSION_SYNC_AWAITING_ACK
 * (see inf_session_get_synchronization_status()). Then, it returns a value
 * between 0.0 and 1.0 specifying how much synchronization data has already
 * been transferred to the remote site.
 *
 * Note that if the session is in status %INF_SESSION_RUNNING, it is
 * possible that this function returns 1.0 (i.e. all data has been
 * transmitted) but the synchronization is not yet complete, because the
 * remote site must still acknowledge the synchronization. The synchronization
 * then is in status %INF_SESSION_SYNC_AWAITING_ACK.
 *
 * Return Value: A value between 0.0 and 1.0.
 **/
gdouble
inf_session_get_synchronization_progress(InfSession* session,
                                         InfXmlConnection* connection)
{
  InfSessionPrivate* priv;
  InfSessionSync* sync;

  g_return_val_if_fail(INF_IS_SESSION(session), 0.0);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), 0.0);

  g_return_val_if_fail(
    inf_session_get_synchronization_status(
      session,
      connection
    ) != INF_SESSION_SYNC_NONE,
    0.0
  );

  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_PRESYNC:
    g_assert(connection == priv->shared.presync.conn);
    return 0.0;
  case INF_SESSION_SYNCHRONIZING:
    g_assert(connection == priv->shared.sync.conn);

    /* messages_total is still zero in case we did not yet even receive
     * sync-begin. We are at the very beginning of the synchronization in
     * that case. */
    if(priv->shared.sync.messages_total == 0)
      return 0.0;

    return (gdouble)priv->shared.sync.messages_received /
           (gdouble)priv->shared.sync.messages_total;

  case INF_SESSION_RUNNING:
    sync = inf_session_find_sync_by_connection(session, connection);
    g_assert(sync != NULL);

    return (gdouble)sync->messages_sent / (gdouble)sync->messages_total;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    return 0.0;
  }
}

/**
 * inf_session_has_synchronizations:
 * @session: A #InfSession.
 *
 * Returns whether there are currently ongoing synchronizations. If the
 * session is in status %INF_SESSION_SYNCHRONIZING, then this returns always
 * %TRUE, if it is in %INF_SESSION_CLOSED, then it returns always %FALSE.
 * If the session is in status %INF_SESSION_RUNNING, then it returns %TRUE
 * when the session is currently at least synchronized to one connection and
 * %FALSE otherwise.
 *
 * Returns: Whether there are ongoing synchronizations.
 **/
gboolean
inf_session_has_synchronizations(InfSession* session)
{
  InfSessionPrivate* priv;
  g_return_val_if_fail(INF_IS_SESSION(session), FALSE);
  priv = INF_SESSION_PRIVATE(session);

  switch(priv->status)
  {
  case INF_SESSION_PRESYNC:
  case INF_SESSION_SYNCHRONIZING:
    return TRUE;
  case INF_SESSION_RUNNING:
    if(priv->shared.run.syncs == NULL)
      return FALSE;
    else
      return TRUE;
  case INF_SESSION_CLOSED:
    return FALSE;
  default:
    g_assert_not_reached();
    return FALSE;
  }
}

/**
 * inf_session_get_subscription_group:
 * @session: A #InfSession.
 *
 * Returns the subscription group for @session, if any.
 *
 * Return Value: A #InfCommunicationGroup, or %NULL.
 **/
InfCommunicationGroup*
inf_session_get_subscription_group(InfSession* session)
{
  g_return_val_if_fail(INF_IS_SESSION(session), NULL);
  return INF_SESSION_PRIVATE(session)->subscription_group;
}

/**
 * inf_session_set_subscription_group:
 * @session: A #InfSession.
 * @group: A #InfCommunicationGroup.
 *
 * Sets the subscription group for @session. The subscription group is the
 * group in which all connections subscribed to the session are a member of.
 *
 * #InfSession itself does not deal with subscriptions, so it is your job
 * to keep @group up-to-date (for example if you add non-local users to
 * @session). This is normally done by a so-called session proxy such as
 * #InfcSessionProxy or #InfdSessionProxy, respectively.
 **/
void
inf_session_set_subscription_group(InfSession* session,
                                   InfCommunicationGroup* group)
{
  InfSessionPrivate* priv;

  g_return_if_fail(INF_IS_SESSION(session));

  priv = INF_SESSION_PRIVATE(session);

  if(priv->subscription_group != group)
  {
    if(priv->subscription_group != NULL)
      g_object_unref(priv->subscription_group);

    priv->subscription_group = group;

    if(group != NULL)
      g_object_ref(group);

    g_object_notify(G_OBJECT(session), "subscription-group");
  }
}

/**
 * inf_session_send_to_subscriptions:
 * @session: A #InfSession.
 * @xml: The message to send.
 *
 * Sends a XML message to the all members of @session's subscription group.
 * This function can only be called if the subscription group is non-%NULL. It
 * takes ownership of @xml.
 **/
void
inf_session_send_to_subscriptions(InfSession* session,
                                  xmlNodePtr xml)
{
  InfSessionPrivate* priv;

  g_return_if_fail(INF_IS_SESSION(session));
  g_return_if_fail(xml != NULL);

  priv = INF_SESSION_PRIVATE(session);
  g_return_if_fail(priv->subscription_group != NULL);

  inf_communication_group_send_group_message(priv->subscription_group, xml);
}

/* vim:set et sw=2 ts=2: */
