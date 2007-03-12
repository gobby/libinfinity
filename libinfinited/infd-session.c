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

#include <libinfinited/infd-session.h>

#include <libinfinity/inf-error.h>

/* These are only used locally in this file to report errors that occured
 * while processing a client request. */
typedef enum _InfdSessionMessageError {
  /* Synchronization is still in progress. */
  INFD_SESSION_REQUEST_ERROR_SYNCHRONIZING,
  /* Received an unexpected message */
  INFD_SESSION_REQUEST_ERROR_UNEXPECTED_MESSAGE,

  INFD_SESSION_REQUEST_ERROR_FAILED
} InfdSessionMessageError;

typedef struct _InfdSessionMessage InfdSessionMessage;
struct _InfdSessionMessage {
  InfdSessionMessageFunc func;
};

typedef struct _InfdSessionSubscription InfdSessionSubscription;
struct _InfdSessionSubscription {
  GNetworkConnection* connection;
  GSList* users; /* Joined users via this connection */
};

typedef struct _InfdSessionPrivate InfdSessionPrivate;
struct _InfdSessionPrivate {
  GSList* subscriptions;
  guint user_id_counter;

  /* Only relevant if we get a session synchronized. This flag tells whether
   * we should subscribe the synchronizing connection after synchronization
   * is complete, so we do not have to synchronize the session the other way
   * around if that connection wants to be subscribed. */
  gboolean subscribe_sync_conn;

  /* Local users that do not belong to a particular connection */
  GSList* local_users;
};

enum {
  PROP_0,

  PROP_SUBSCRIBE_SYNC_CONN
};

#define INFD_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_SESSION, InfdSessionPrivate))

static InfSessionClass* parent_class;
static GQuark infd_session_request_error_quark;

/* TODO: Move these to libinfinity/inf-error.c, but make sure they get
 * initialized (wrapper function/macro). */
static GQuark infd_session_user_join_error_quark;
static GQuark infd_session_user_leave_error_quark;

/*
 * Session messages.
 */

static InfdSessionMessage*
infd_session_message_new(InfdSessionMessageFunc func)
{
  InfdSessionMessage* message;
  message = g_slice_new(InfdSessionMessage);

  message->func = func;
  return message;
}

static void
infd_session_message_free(InfdSessionMessage* message)
{
  g_slice_free(InfdSessionMessage, message);
}

/*
 * Session subscriptions.
 */

static InfdSessionSubscription*
infd_session_subscription_new(GNetworkConnection* connection)
{
  InfdSessionSubscription* subscription;
  subscription = g_slice_new(InfdSessionSubscription);

  subscription->connection = connection;
  subscription->users = NULL;

  g_object_ref(G_OBJECT(connection));
  return subscription;
}

static void
infd_session_subscription_free(InfdSessionSubscription* subscription)
{
  GSList* item;
  for(item = subscription->users; item != NULL; item = g_slist_next(item))
    g_object_set(G_OBJECT(item->data), "status", INF_USER_UNAVAILABLE, NULL);

  g_object_unref(G_OBJECT(subscription->connection));
  g_slist_free(subscription->users);
  g_slice_free(InfdSessionSubscription, subscription);
}

static GSList*
infd_session_find_subscription_item_by_connection(InfdSession* session,
                                                  GNetworkConnection* conn)
{
  InfdSessionPrivate* priv;
  GSList* item;

  priv = INFD_SESSION_PRIVATE(session);
  for(item = priv->subscriptions; item != NULL; item = g_slist_next(item))
    if( ((InfdSessionSubscription*)item->data)->connection == conn)
      return item;

  return NULL;
}

static InfdSessionSubscription*
infd_session_find_subscription_by_connection(InfdSession* session,
                                             GNetworkConnection* connection)
{
  GSList* item;

  item = infd_session_find_subscription_item_by_connection(
    session,
    connection
  );

  if(item == NULL) return NULL;
  return (InfdSessionSubscription*)item->data;
}

/* Required by infd_session_release_connection() */
static void
infd_session_connection_notify_status_cb(GNetworkConnection* connection,
                                         const gchar* property,
                                         gpointer user_data);

/* Unlinks a subscription connection the session. */
static void
infd_session_release_subscription(InfdSession* session,
                                  InfdSessionSubscription* subscription)
{
  InfdSessionPrivate* priv;
  GNetworkConnection* connection;

  priv = INFD_SESSION_PRIVATE(session);
  connection = subscription->connection;

  g_signal_handler_disconnect_by_func(
    G_OBJECT(connection),
    G_CALLBACK(infd_session_connection_notify_status_cb),
    session
  );

  inf_connection_manager_remove_object(
    inf_session_get_connection_manager(INF_SESSION(session)),
    connection,
    INF_NET_OBJECT(session)
  );

  priv->subscriptions = g_slist_remove(priv->subscriptions, subscription);
  infd_session_subscription_free(subscription);
}

static void
infd_session_remove_subscription(InfdSession* session,
                                 InfdSessionSubscription* subscription)
{
  xmlNodePtr xml;
  GSList* item;
  gchar id_buf[16];

  for(item = subscription->users; item != NULL; item = g_slist_next(item))
  {
    sprintf(id_buf, "%u", inf_user_get_id(INF_USER(item->data)));

    xml = xmlNewNode(NULL, (const xmlChar*)"user-status-change");
    xmlNewProp(xml, (const xmlChar*)"id", (const xmlChar*)id_buf);
    xmlNewProp(xml, (const xmlChar*)"status", (const xmlChar*)"unavailable");

    infd_session_send_to_subscriptions(
      session,
      subscription->connection,
      xml
    );
  }

  infd_session_release_subscription(session, subscription);
}

/*
 * Utility functions.
 */

static InfUser*
infd_session_perform_user_join(InfdSession* session,
                               GNetworkConnection* connection,
                               GArray* user_props,
                               GError** error)
{
  InfSessionClass* session_class;
  InfdSessionPrivate* priv;
  InfdSessionSubscription* subscription;
  InfUser* user;
  const GParameter* name_param;
  GParameter* param;
  gboolean result;
  xmlNodePtr xml;
  guint i;

  priv = INFD_SESSION_PRIVATE(session);
  session_class = INF_SESSION_GET_CLASS(session);

  g_return_val_if_fail(session_class->validate_user_props != NULL, NULL);
  g_return_val_if_fail(session_class->user_to_xml != NULL, NULL);
  g_return_val_if_fail(session_class->user_new != NULL, NULL);

  name_param = inf_session_lookup_user_property(
    (const GParameter*)user_props->data,
    user_props->len,
    "name"
  );

  if(name_param == NULL)
  {
    g_set_error(
      error,
      infd_session_user_join_error_quark,
      INF_USER_JOIN_ERROR_NAME_MISSING,
      "%s",
      inf_user_join_strerror(INF_USER_JOIN_ERROR_NAME_MISSING)
    );

    return NULL;
  }

  user = inf_session_lookup_user_by_name(
    INF_SESSION(session),
    g_value_get_string(&name_param->value)
  );

  if(user != NULL && inf_user_get_status(user) != INF_USER_UNAVAILABLE)
  {
    g_set_error(
      error,
      infd_session_user_join_error_quark,
      INF_USER_JOIN_ERROR_NAME_IN_USE,
      "%s",
      inf_user_join_strerror(INF_USER_JOIN_ERROR_NAME_IN_USE)
    );

    return NULL;
  }

  /* User join requests must not have the id value set because the server
   * chooses an ID, or reuses an existing one in the case of a rejoin. */
  param = inf_session_get_user_property(user_props, "id");
  if(G_IS_VALUE(&param->value))
  {
    g_set_error(
      error,
      infd_session_user_join_error_quark,
      INF_USER_JOIN_ERROR_ID_PROVIDED,
      "%s",
      inf_user_join_strerror(INF_USER_JOIN_ERROR_ID_PROVIDED)
    );

    return NULL;
  }

  /* The user ID counter is increased in the add-user default signal
   * handler. */
  g_value_init(&param->value, G_TYPE_UINT);

  /* Reuse user ID on rejoin. */
  if(user != NULL)
    g_value_set_uint(&param->value, inf_user_get_id(user));
  else
    g_value_set_uint(&param->value, priv->user_id_counter);

  /* Again, if a user joins, the status is always available, so it should
   * not be already provided. */
  param = inf_session_get_user_property(user_props, "status");
  if(G_IS_VALUE(&param->value))
  {
    g_set_error(
      error,
      infd_session_user_join_error_quark,
      INF_USER_JOIN_ERROR_STATUS_PROVIDED,
      "%s",
      inf_user_join_strerror(INF_USER_JOIN_ERROR_STATUS_PROVIDED)
    );

    return NULL;
  }

  g_value_init(&param->value, G_TYPE_ENUM);
  g_value_set_enum(&param->value, INF_USER_AVAILABLE);

  if(user == NULL)
  {
    /* This validates properties */
    user = inf_session_add_user(
      INF_SESSION(session),
      (const GParameter*)user_props->data,
      user_props->len,
      error
    );

    if(user == NULL)
      return NULL;

    xml = xmlNewNode(NULL, (const xmlChar*)"user-join");
  }
  else
  {
    /* Validate properties, but exclude the rejoining user from the check.
     * Otherwise, we would get conflicts because the name and the ID
     * of the request and the rejoining user are the same. */
    result = session_class->validate_user_props(
      INF_SESSION(session),
      (const GParameter*)user_props->data,
      user_props->len,
      user,
      error
    );

    if(result == FALSE)
      return NULL;

    g_object_freeze_notify(G_OBJECT(user));

    /* Set properties on already existing user object. */
    for(i = 0; i < user_props->len; ++ i)
    {
      param = &g_array_index(user_props, GParameter, i);

      /* Don't set name and ID because they did not change, and we are not
       * even allowed to set ID because it is construct only. */
      if(strcmp(param->name, "name") != 0 && strcmp(param->name, "id") != 0)
      {
        g_object_set_property(
          G_OBJECT(user),
          g_array_index(user_props, GParameter, i).name,
          &g_array_index(user_props, GParameter, i).value
        );
      }
    }

    g_object_thaw_notify(G_OBJECT(user));

    xml = xmlNewNode(NULL, (const xmlChar*)"user-rejoin");
  }

  session_class->user_to_xml(INF_SESSION(session), user, xml);

  /* exclude the connection from which the request comes. The reply to it
   * is sent separately telling it that the user join was accepted. */
  infd_session_send_to_subscriptions(
    session,
    connection,
    xmlCopyNode(xml, 1)
  );

  if(connection != NULL)
  {
    xmlNewProp(xml, (const xmlChar*)"self", (const xmlChar*)"true");

    inf_connection_manager_send(
      inf_session_get_connection_manager(INF_SESSION(session)),
      connection,
      INF_NET_OBJECT(session),
      xml
    );

    subscription = infd_session_find_subscription_by_connection(
      session,
      connection
    );

    g_assert(subscription != NULL);
    subscription->users = g_slist_prepend(subscription->users, user);
  }
  else
  {
    priv->local_users = g_slist_prepend(priv->local_users, user);
  }

  return user;
}

/* Subscribes the given connection to the session without synchronizing it. */
static void
infd_session_subscribe_connection(InfdSession* session,
                                  GNetworkConnection* connection,
                                  const gchar* identifier)
{
  InfdSessionPrivate* priv;
  InfdSessionSubscription* subscription;

  priv = INFD_SESSION_PRIVATE(session);

  /* Note that if this is called from (the public) infd_session_subscribe_to,
   * then the InfSession has already been the connection in
   * inf_session_synchronize_to(). However, since we want to keep it after
   * the synchronization finishes we have to add another reference here. */
  inf_connection_manager_add_object(
    inf_session_get_connection_manager(INF_SESSION(session)),
    connection,
    INF_NET_OBJECT(session),
    identifier
  );

  g_signal_connect_after(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(infd_session_connection_notify_status_cb),
    session
  );

  subscription = infd_session_subscription_new(connection);
  priv->subscriptions = g_slist_prepend(priv->subscriptions, subscription);
}

/*
 * Signal handlers.
 */

static void
infd_session_connection_notify_status_cb(GNetworkConnection* connection,
                                         const gchar* property,
                                         gpointer user_data)
{
  InfdSession* session;
  InfdSessionSubscription* subscription;
  GNetworkConnectionStatus status;

  session = INFD_SESSION(user_data);

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == GNETWORK_CONNECTION_CLOSED ||
     status == GNETWORK_CONNECTION_CLOSING)
  {
    subscription = infd_session_find_subscription_by_connection(
      session,
      connection
    );

    g_assert(subscription != NULL);

    infd_session_remove_subscription(session, subscription);
  }
}

/*
 * GObject overrides.
 */

static void
infd_session_init(GTypeInstance* instance,
                  gpointer g_class)
{
  InfdSession* session;
  InfdSessionPrivate* priv;

  session = INFD_SESSION(instance);
  priv = INFD_SESSION_PRIVATE(session);

  priv->subscriptions = NULL;
  priv->user_id_counter = 1;
  priv->subscribe_sync_conn = FALSE;
  priv->local_users = NULL;
}

static void
infd_session_set_property(GObject* object,
                          guint prop_id,
                          const GValue* value,
                          GParamSpec* pspec)
{
  InfdSession* session;
  InfdSessionPrivate* priv;

  session = INFD_SESSION(object);
  priv = INFD_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_SUBSCRIBE_SYNC_CONN:
    priv->subscribe_sync_conn = g_value_get_boolean(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_session_get_property(GObject* object,
                          guint prop_id,
                          GValue* value,
                          GParamSpec* pspec)
{
  InfdSession* session;
  InfdSessionPrivate* priv;

  session = INFD_SESSION(object);
  priv = INFD_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_SUBSCRIBE_SYNC_CONN:
    g_value_set_boolean(value, priv->subscribe_sync_conn);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_session_dispose(GObject* object)
{
  InfdSession* session;
  InfdSessionPrivate* priv;

  session = INFD_SESSION(object);
  priv = INFD_SESSION_PRIVATE(session);

  /* The base class will call close() in which we remove subscriptions */
  g_slist_free(priv->local_users);
  priv->local_users = NULL;

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

/*
 * vfunc and default signal handler implementations.
 */

static void
infd_session_process_xml_run_impl(InfSession* session,
                                  GNetworkConnection* connection,
                                  const xmlNodePtr xml)
{
  InfdSessionClass* sessiond_class;
  InfSessionSyncStatus status;
  InfdSessionMessage* message;
  GError* error;
  gboolean result;

  error = NULL;
  status = inf_session_get_synchronization_status(session, connection);

  if(session != INF_SESSION_SYNC_NONE)
  {
    result = FALSE;
    g_set_error(
      &error,
      infd_session_request_error_quark,
      INFD_SESSION_REQUEST_ERROR_SYNCHRONIZING,
      "Synchronization is still in progress"
    );
  }
  else
  {
    message = g_hash_table_lookup(
      sessiond_class->message_table,
      (const gchar*)xml->name
    );

    if(message == NULL)
    {
      result = FALSE;
      g_set_error(
        &error,
        infd_session_request_error_quark,
        INFD_SESSION_REQUEST_ERROR_UNEXPECTED_MESSAGE,
        "Unexpected message: '%s'",
        (const gchar*)xml->name
      );
    }
    else
    {
      result = message->func(INFD_SESSION(session), connection, xml, &error);
    }
  }

  if(result == FALSE && error != NULL)
  {
    /* TODO: Send error to client? */

    g_warning("Received bad XML request: %s\n", error->message);
    g_error_free(error);
  }

  if(parent_class->process_xml_run != NULL)
    parent_class->process_xml_run(session, connection, xml);
}

static void
infd_session_close_impl(InfSession* session)
{
  InfSessionClass* session_class;
  InfdSessionPrivate* priv;
  InfdSessionSubscription* subscription;
  InfSessionSyncStatus status;
  xmlNodePtr xml;

  session_class = INF_SESSION_GET_CLASS(session);
  priv = INFD_SESSION_PRIVATE(session);

  while(priv->subscriptions != NULL)
  {
    subscription = (InfdSessionSubscription*)priv->subscriptions->data;

    status = inf_session_get_synchronization_status(
      session,
      subscription->connection
    );

    /* If synchronization is still in progress, the close implementation of
     * the base class will cancel the synchronization in which case we do
     * not need to send an extra session-close message. */
    if(status != INF_SESSION_SYNC_IN_PROGRESS)
    {
      xml = xmlNewNode(NULL, (const xmlChar*)"session-close");

      inf_connection_manager_send(
        inf_session_get_connection_manager(session),
        subscription->connection,
	INF_NET_OBJECT(session),
        xml
      );
    }

    /* Do not call remove_subscription because this would try to send
     * messages about leaving players, but we are sending session-close
     * to all subscriptions anyway. */
    infd_session_release_subscription(INFD_SESSION(session), subscription);
  }

  if(parent_class->close != NULL)
    parent_class->close(session);
}

static void
infd_session_add_user_impl(InfSession* session,
                           InfUser* user)
{
  InfdSessionPrivate* priv;
  priv = INFD_SESSION_PRIVATE(session);

  if(parent_class->add_user != NULL)
    parent_class->add_user(session, user);

  /* Make sure that we generate a non-existing user ID for the next user. */
  if(priv->user_id_counter <= inf_user_get_id(user))
    priv->user_id_counter = inf_user_get_id(user) + 1;
}

static void
infd_session_synchronization_complete_impl(InfSession* session,
                                           GNetworkConnection* connection)
{
  InfdSessionPrivate* priv;
  InfSessionStatus status;
  gchar* identifier;

  priv = INFD_SESSION_PRIVATE(session);
  g_object_get(G_OBJECT(session), "status", &status, NULL);

  if(status == INF_SESSION_SYNCHRONIZING)
  {
    if(priv->subscribe_sync_conn == TRUE)
    {
      g_object_get(G_OBJECT(session), "sync-identifier", &identifier, NULL);

      /* Do not use subscribe_to here because this would synchronize the
       * session to connection. However, we just got it synchronized the
       * other way around and therefore no further synchronization is
       * required. */
      infd_session_subscribe_connection(
        INFD_SESSION(session),
        connection,
        identifier
      );

      g_free(identifier);
    }
  }

  if(parent_class->synchronization_complete != NULL)
    parent_class->synchronization_complete(session, connection);
}

static void
infd_session_synchronization_failed_impl(InfSession* session,
                                         GNetworkConnection* connection,
                                         const GError* error)
{
  InfSessionStatus status;
  InfdSessionSubscription* subscription;

  g_object_get(G_OBJECT(session), "status", &status, NULL);

  /* We do not need handle the status == INF_SESSION_SYNCHRONIZING case
   * since there cannot be any subscriptions while we are synchronizing. */

  if(status == INF_SESSION_RUNNING)
  {
    subscription = infd_session_find_subscription_by_connection(
      INFD_SESSION(session),
      connection
    );

    if(subscription != NULL)
    {
      /* Note that it should not matter whether we call
       * infd_session_release_subscription or infd_session_remove_subscription
       * because there cannot be any users joined via the connection anyway,
       * because it was not yet synchronized. */
      infd_session_release_subscription(INFD_SESSION(session), subscription);
    }
  }

  if(parent_class->synchronization_failed != NULL)
    parent_class->synchronization_failed(session, connection, error);
}

/*
 * Message handling.
 */

static gboolean
infd_session_handle_user_join(InfdSession* session,
                              GNetworkConnection* connection,
                              const xmlNodePtr xml,
                              GError** error)
{
  InfSessionClass* session_class;
  GArray* array;
  InfUser* user;
  xmlNodePtr reply_xml;
  gchar code_buf[16];
  GError* local_error;

  session_class = INF_SESSION_CLASS(session);

  array = session_class->get_xml_user_props(
    INF_SESSION(session),
    connection,
    xml
  );

  /* Use a local error variable here because we want to handle any
   * errors (with a user-join-failed reply) instead of propagating them. */
  local_error = NULL;

  user = infd_session_perform_user_join(
    session,
    connection,
    array,
    &local_error
  );

  g_array_free(array, TRUE);

  /* If error is not set but user is NULL, the error was handled by
   * infd_session_handle_user_join. */
  if(user == NULL && local_error != NULL)
  {
    sprintf(code_buf, "%u", (unsigned int)local_error->code);

    reply_xml = xmlNewNode(NULL, (const xmlChar*)"user-join-failed");
    xmlNewProp(reply_xml, (const xmlChar*)"code", (const xmlChar*)code_buf);

    xmlNewProp(
      reply_xml,
      (const xmlChar*)"domain",
      (const xmlChar*)g_quark_to_string(local_error->domain)
    );

    inf_connection_manager_send(
      inf_session_get_connection_manager(INF_SESSION(session)),
      connection,
      INF_NET_OBJECT(session),
      reply_xml
    );

    g_error_free(local_error);

    /* Request failed, but we handled the error. */
    return FALSE;
  }

  return TRUE;
}

static gboolean
infd_session_handle_user_leave(InfdSession* session,
                               GNetworkConnection* connection,
                               const xmlNodePtr xml,
                               GError** error)
{
  InfdSessionSubscription* subscription;
  InfUser* user;
  xmlChar* id_attr;
  guint id;

  xmlNodePtr reply_xml;
  gchar id_buf[16];

  subscription = infd_session_find_subscription_by_connection(
    INFD_SESSION(session),
    connection
  );

  g_assert(subscription != NULL);

  id_attr = xmlGetProp(xml, (const xmlChar*)"id");
  if(id_attr == NULL)
  {
    g_set_error(
      error,
      infd_session_user_leave_error_quark,
      INF_USER_LEAVE_ERROR_ID_NOT_PRESENT,
      "%s",
      inf_user_leave_strerror(INF_USER_LEAVE_ERROR_ID_NOT_PRESENT)
    );

    return FALSE;
  }

  id = strtoul((const gchar*)id_attr, NULL, 0);
  xmlFree(id_attr);

  user = inf_session_lookup_user_by_id(INF_SESSION(session), id);
  if(g_slist_find(subscription->users, user) == NULL)
  {
    g_set_error(
      error,
      infd_session_user_leave_error_quark,
      INF_USER_LEAVE_ERROR_NOT_JOINED,
      "%s",
      inf_user_leave_strerror(INF_USER_LEAVE_ERROR_NOT_JOINED)
    );

    return FALSE;
  }

  sprintf(id_buf, "%u", id);
  reply_xml = xmlNewNode(NULL, (const xmlChar*)"user-leave");
  xmlNewProp(reply_xml, (const xmlChar*)"id", (const xmlChar*)id_buf);

  xmlNewProp(
    reply_xml,
    (const xmlChar*)"status",
    (const xmlChar*)"unavailable"
  );

  infd_session_send_to_subscriptions(session, NULL, reply_xml);
  subscription->users = g_slist_remove(subscription->users, user);

  return TRUE;
}

static gboolean
infd_session_handle_session_unsubscribe(InfdSession* session,
                                        GNetworkConnection* connection,
                                        const xmlNodePtr xml,
                                        GError** error)
{
  InfdSessionSubscription* subscription;

  subscription = infd_session_find_subscription_by_connection(
    session,
    connection
  );

  g_assert(subscription != NULL);

  infd_session_remove_subscription(session, subscription);
  return TRUE;
}

/*
 * GType registration.
 */

static void
infd_session_class_init(gpointer g_class,
                        gpointer class_data)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;
  InfdSessionClass* sessiond_class;

  object_class = G_OBJECT_CLASS(g_class);
  session_class = INF_SESSION_CLASS(g_class);

  parent_class = INF_SESSION_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdSessionPrivate));

  object_class->dispose = infd_session_dispose;
  object_class->set_property = infd_session_set_property;
  object_class->get_property = infd_session_get_property;

  session_class->process_xml_run = infd_session_process_xml_run_impl;
  session_class->close = infd_session_close_impl;

  session_class->add_user = infd_session_add_user_impl;
  session_class->synchronization_complete =
    infd_session_synchronization_complete_impl;
  session_class->synchronization_failed =
    infd_session_synchronization_failed_impl;

  infd_session_request_error_quark = g_quark_from_static_string(
    "INFD_REQUEST_ERROR"
  );

  infd_session_user_join_error_quark = g_quark_from_static_string(
    "INF_USER_JOIN_ERROR"
  );

  infd_session_user_leave_error_quark = g_quark_from_static_string(
    "INF_USER_LEAVE_ERROR"
  );

  g_object_class_install_property(
    object_class,
    PROP_SUBSCRIBE_SYNC_CONN,
    g_param_spec_boolean(
      "subscribe-sync-connection",
      "Subscribe synchronizing connection",
      "Whether to subscribe the initial synchronizing connection after "
      "successful synchronization",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  sessiond_class->message_table = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    NULL,
    (GDestroyNotify)infd_session_message_free
  );

  infd_session_class_register_message(
    sessiond_class,
    "user-join",
    infd_session_handle_user_join
  );

  infd_session_class_register_message(
    sessiond_class,
    "user-leave",
    infd_session_handle_user_leave
  );

  infd_session_class_register_message(
    sessiond_class,
    "session-unsubscribe",
    infd_session_handle_session_unsubscribe
  );
}

static void
infd_session_class_finalize(gpointer g_class,
                            gpointer class_data)
{
  InfdSessionClass* session_class;
  session_class = INFD_SESSION_CLASS(g_class);

  g_hash_table_destroy(session_class->message_table);
}

GType
infd_session_get_type(void)
{
  static GType session_type = 0;

  if(!session_type)
  {
    static const GTypeInfo session_type_info = {
      sizeof(InfdSessionClass),    /* class_size */
      NULL,                        /* base_init */
      NULL,                        /* base_finalize */
      infd_session_class_init,     /* class_init */
      infd_session_class_finalize, /* class_finalize */
      NULL,                        /* class_data */
      sizeof(InfdSession),         /* instance_size */
      0,                           /* n_preallocs */
      infd_session_init,           /* instance_init */
      NULL                         /* value_table */
    };

    session_type = g_type_register_static(
      INF_TYPE_SESSION,
      "InfdSession",
      &session_type_info,
      0
    );
  }

  return session_type;
}

/*
 * Public API.
 */

/** infd_session_class_register_message:
 *
 * @session_class: The class for which to register a message.
 * @message: The message to register.
 * @func: The function to be called when @message is received.
 *
 * Registers a message for the given session class. Whenever an XML request
 * with the given message is received, the given function will be called.
 *
 * Return Value: Whether the registration was successful.
 **/
gboolean
infd_session_class_register_message(InfdSessionClass* session_class,
                                    const gchar* message,
                                    InfdSessionMessageFunc func)
{
  g_return_val_if_fail(INFD_IS_SESSION_CLASS(session_class), FALSE);
  g_return_val_if_fail(message != NULL, FALSE);
  g_return_val_if_fail(func != NULL, FALSE);

  if(g_hash_table_lookup(session_class->message_table, message) != NULL)
    return FALSE;

  g_hash_table_insert(
    session_class->message_table,
    (gpointer)message,
    infd_session_message_new(func)
  );

  return TRUE;
}

/** infd_session_add_user:
 *
 * @session: A #InfdSession.
 * @params: Construction properties for the #InfUser (or derived) object.
 * @n_params: Number of parameters.
 * @error: Location to store error information.
 *
 * Adds a local user to @session. @params must not contain the 'id' property
 * because it will be choosen by the session. Also, if the 'name' property is
 * already in use be an existing, but unavailable user, this user will be
 * re-used.
 *
 * Return Value: The #InfUser that has been added, or %NULL in case of an
 * error.
 **/
InfUser*
infd_session_add_user(InfdSession* session,
                      const GParameter* params,
		      guint n_params,
		      GError** error)
{
  InfUser* user;
  GArray* array;

  g_return_val_if_fail(INFD_IS_SESSION(session), NULL);

  array = g_array_sized_new(FALSE, FALSE, sizeof(GParameter), n_params + 2);
  g_array_append_vals(array, params, n_params);

  user = infd_session_perform_user_join(session, NULL, array, error);
  g_array_free(array, TRUE);

  return user;
}

/** infd_session_subscribe_to:
 *
 * @session: A #InfdSession with state %INF_SESSION_RUNNING.
 * @connection: A #GNetworkConnection that is not yet subscribed.
 * @identifier: A session identifier.
 *
 * Subscribes @connection to @session. The first thing that will be done
 * is a synchronization (see inf_session_synchronize_to). Then, all changes
 * to the session are propagated to @connection. @identifier is used as an
 * identifier for the subscription in the connection manager.
 *
 * A subscription can only be initialted if @session is in state
 * %INF_SESSION_RUNNING.
 **/
void
infd_session_subscribe_to(InfdSession* session,
                          GNetworkConnection* connection,
                          const gchar* identifier)
{
  InfdSessionPrivate* priv;

  g_return_if_fail(INFD_IS_SESSION(session));
  g_return_if_fail(GNETWORK_IS_CONNECTION(connection));
  g_return_if_fail(identifier != NULL);

  g_return_if_fail(
    infd_session_find_subscription_by_connection(session, connection) == NULL
  );

  priv = INFD_SESSION_PRIVATE(session);

  inf_session_synchronize_to(INF_SESSION(session), connection, identifier);
  infd_session_subscribe_connection(session, connection, identifier);
}

/** infd_session_send_to_subscriptions:
 *
 * @session: A #InfdSession.
 * @exclude: A connection not to send the message, or %NULL.
 * @xml: The message to send.
 *
 * Sends an XML request to all subscribed connections, except @exclude. The
 * function takes ownership of @xml.
 **/
void
infd_session_send_to_subscriptions(InfdSession* session,
                                   GNetworkConnection* exclude,
                                   xmlNodePtr xml)
{
  InfdSessionPrivate* priv;
  InfdSessionSubscription* subscription;
  InfdSessionSubscription* first;
  GSList* item;

  g_return_if_fail(INFD_IS_SESSION(session));
  g_return_if_fail(xml != NULL);

  priv = INFD_SESSION_PRIVATE(session);

  first = NULL;
  for(item = priv->subscriptions; item != NULL; item = g_slist_next(item))
  {
    subscription = (InfdSessionSubscription*)item->data;
    if(subscription->connection != exclude)
    {
      if(first == NULL)
      {
        /* Remember first subscription to send the original xml to it
	 * later. */
        first = subscription;
      }
      else
      {
        /* Make a copy of xml because we need the original one to send
	 * to first. */
        inf_connection_manager_send(
	  inf_session_get_connection_manager(INF_SESSION(session)),
	  subscription->connection,
	  INF_NET_OBJECT(session),
	  xmlCopyNode(xml, 1)
	);
      }
    }
  }

  if(first != NULL)
  {
    inf_connection_manager_send(
      inf_session_get_connection_manager(INF_SESSION(session)),
      first->connection,
      INF_NET_OBJECT(session),
      xml
    );
  }
  else
  {
    xmlFreeNode(xml);
  }
}
