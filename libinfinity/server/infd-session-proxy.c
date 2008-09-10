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

#include <libinfinity/server/infd-session-proxy.h>

#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>

#include <libinfinity/inf-marshal.h>

#include <string.h>

typedef struct _InfdSessionProxySubscription InfdSessionProxySubscription;
struct _InfdSessionProxySubscription {
  InfXmlConnection* connection;
  GSList* users; /* Available users joined via this connection */
};

typedef struct _InfdSessionProxyPrivate InfdSessionProxyPrivate;
struct _InfdSessionProxyPrivate {
  InfSession* session;
  InfConnectionManagerGroup* subscription_group;

  GSList* subscriptions;
  guint user_id_counter;

#if 0
  /* Only relevant if we get a session synchronized. This flag tells whether
   * we should subscribe the synchronizing connection after synchronization
   * is complete, so we do not have to synchronize the session the other way
   * around if that connection wants to be subscribed. */
  gboolean subscribe_sync_conn;
#endif

  /* Local users that do not belong to a particular connection */
  GSList* local_users;

  /* Whether there are any subscriptions / synchronizations */
  gboolean idle;
};

enum {
  PROP_0,

  /* construct/only */
  PROP_SESSION,
  PROP_SUBSCRIPTION_GROUP,
#if 0
  PROP_SUBSCRIBE_SYNC_CONN,
#endif

  /* read/only */
  PROP_IDLE
};

enum {
  ADD_SUBSCRIPTION,
  REMOVE_SUBSCRIPTION,
  
  LAST_SIGNAL
};

#define INFD_SESSION_PROXY_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_SESSION_PROXY, InfdSessionProxyPrivate))

static GObjectClass* parent_class;
static guint session_proxy_signals[LAST_SIGNAL];

/*
 * SessionProxy subscriptions.
 */

static InfdSessionProxySubscription*
infd_session_proxy_subscription_new(InfXmlConnection* connection)
{
  InfdSessionProxySubscription* subscription;
  subscription = g_slice_new(InfdSessionProxySubscription);

  subscription->connection = connection;
  subscription->users = NULL;

  g_object_ref(G_OBJECT(connection));
  return subscription;
}

static void
infd_session_proxy_subscription_free(InfdSessionProxySubscription* subscr)
{
  g_object_unref(G_OBJECT(subscr->connection));
  g_slist_free(subscr->users);
  g_slice_free(InfdSessionProxySubscription, subscr);
}

static GSList*
infd_session_proxy_find_subscription_item(InfdSessionProxy* proxy,
                                          InfXmlConnection* connection)
{
  InfdSessionProxyPrivate* priv;
  GSList* item;

  priv = INFD_SESSION_PROXY_PRIVATE(proxy);
  for(item = priv->subscriptions; item != NULL; item = g_slist_next(item))
    if( ((InfdSessionProxySubscription*)item->data)->connection == connection)
      return item;

  return NULL;
}

static InfdSessionProxySubscription*
infd_session_proxy_find_subscription(InfdSessionProxy* proxy,
                                     InfXmlConnection* connection)
{
  GSList* item;

  item = infd_session_proxy_find_subscription_item(proxy, connection);
  if(item == NULL) return NULL;

  return (InfdSessionProxySubscription*)item->data;
}

static void
infd_session_proxy_user_notify_status_cb(InfUser* user,
                                         const GParamSpec* pspec,
                                         gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;
  InfdSessionProxySubscription* subscr;

  if(inf_user_get_status(user) == INF_USER_UNAVAILABLE)
  {
    proxy = INFD_SESSION_PROXY(user_data);
    priv = INFD_SESSION_PROXY_PRIVATE(proxy);

    if(inf_user_get_connection(user))
    {
      subscr = infd_session_proxy_find_subscription(
        proxy,
        inf_user_get_connection(user)
      );

      g_assert(subscr != NULL);
      subscr->users = g_slist_remove(subscr->users, user);

      g_object_set(G_OBJECT(user), "connection", NULL, NULL);
    }
    else
    {
      priv->local_users = g_slist_remove(priv->local_users, user);
    }

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(user),
      G_CALLBACK(infd_session_proxy_user_notify_status_cb),
      proxy
    );
  }
}

/* Required by infd_session_proxy_release_connection() */
static void
infd_session_proxy_connection_notify_status_cb(InfXmlConnection* connection,
                                               const gchar* property,
                                               gpointer user_data);

/* Unlinks a subscription connection from the session. */
static void
infd_session_proxy_release_subscription(InfdSessionProxy* proxy,
                                        InfdSessionProxySubscription* subscr)
{
  InfdSessionProxyPrivate* priv;
  InfXmlConnection* connection;

  priv = INFD_SESSION_PROXY_PRIVATE(proxy);
  connection = subscr->connection;

  g_signal_emit(
    G_OBJECT(proxy),
    session_proxy_signals[REMOVE_SUBSCRIPTION],
    0,
    connection
  );
}

static void
infd_session_proxy_remove_subscription(InfdSessionProxy* proxy,
                                       InfdSessionProxySubscription* subscr)
{
  InfdSessionProxyPrivate* priv;
  xmlNodePtr xml;
  GSList* item;
  InfUser* user;

  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  for(item = subscr->users; item != NULL; item = g_slist_next(item))
  {
    user = INF_USER(item->data);

    /* Send user-status-change to remaining subscriptions. */
    /* Note: We cannot simply use inf_session_set_user_status because it
     * would also try to send the status change to the subscription we are
     * removing, and because it only works for local users. */
    xml = xmlNewNode(NULL, (const xmlChar*)"user-status-change");
    inf_xml_util_set_attribute_uint(xml, "id", inf_user_get_id(user));

    inf_xml_util_set_attribute(
      xml,
      "status",
      inf_user_status_to_string(INF_USER_UNAVAILABLE)
    );

    /* The actual status change is performed in the default signal handler
     * of the remove-subscription signal. */
    inf_session_send_to_subscriptions(priv->session, subscr->connection, xml);
  }

  infd_session_proxy_release_subscription(proxy, subscr);
}

/*
 * Utility functions.
 */

/* Performs a user join on the given proxy. If connection is not null, the
 * user join is made from that connection, otherwise a local user join is
 * performed. request_seq is the seq of the user join request and used in
 * the reply. It is ignored when connection is NULL. */
static InfUser*
infd_session_proxy_perform_user_join(InfdSessionProxy* proxy,
                                     InfXmlConnection* connection,
                                     const gchar* request_seq,
                                     GArray* user_props,
                                     GError** error)
{
  InfSessionClass* session_class;
  InfdSessionProxyPrivate* priv;
  InfdSessionProxySubscription* subscription;
  InfUser* user;
  const GParameter* name_param;
  GParameter* param;
  gboolean result;
  xmlNodePtr xml;
  guint i;

  priv = INFD_SESSION_PROXY_PRIVATE(proxy);
  session_class = INF_SESSION_GET_CLASS(priv->session);

  g_assert(session_class->validate_user_props != NULL);
  g_assert(session_class->user_new != NULL);

  name_param = inf_session_lookup_user_property(
    (const GParameter*)user_props->data,
    user_props->len,
    "name"
  );

  if(name_param == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
      "Request does not contain required attribute 'name'"
    );

    return NULL;
  }

  /* TODO: Isn't this already done in validate_user_props? */
  user = inf_user_table_lookup_user_by_name(
    inf_session_get_user_table(priv->session),
    g_value_get_string(&name_param->value)
  );

  if(user != NULL && inf_user_get_status(user) != INF_USER_UNAVAILABLE)
  {
    g_set_error(
      error,
      inf_user_error_quark(),
      INF_USER_ERROR_NAME_IN_USE,
      "Name '%s' already in use",
      g_value_get_string(&name_param->value)
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
      inf_user_error_quark(),
      INF_USER_ERROR_ID_PROVIDED,
      "%s",
      inf_user_strerror(INF_USER_ERROR_ID_PROVIDED)
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

  /* Check user status. It must not be unavailable on join/rejoin */
  param = inf_session_get_user_property(user_props, "status");
  if(G_IS_VALUE(&param->value))
  {
    if(g_value_get_enum(&param->value) == INF_USER_UNAVAILABLE)
    {
      g_set_error(
        error,
        inf_user_error_quark(),
        INF_USER_ERROR_STATUS_UNAVAILABLE,
        "'status' attribute is 'unavailable' in user join request"
      );

      return NULL;
    }
  }
  else
  {
    g_value_init(&param->value, INF_TYPE_USER_STATUS);
    g_value_set_enum(&param->value, INF_USER_ACTIVE);
  }

  /* flags should not be set by get_xml_user_props, nor given
   * to infd_session_proxy_add_user. */
  param = inf_session_get_user_property(user_props, "flags");
  g_assert(!G_IS_VALUE(&param->value));

  g_value_init(&param->value, INF_TYPE_USER_FLAGS);
  if(connection == NULL)
    g_value_set_flags(&param->value, INF_USER_LOCAL);
  else
    g_value_set_flags(&param->value, 0);


  /* same with connection */
  param = inf_session_get_user_property(user_props, "connection");
  g_assert(!G_IS_VALUE(&param->value));
  g_value_init(&param->value, INF_TYPE_XML_CONNECTION);
  g_value_set_object(&param->value, G_OBJECT(connection));

  if(user == NULL)
  {
    /* This validates properties */
    user = inf_session_add_user(
      priv->session,
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
      priv->session,
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
        g_object_set_property(G_OBJECT(user), param->name, &param->value);
    }

    g_object_thaw_notify(G_OBJECT(user));

    xml = xmlNewNode(NULL, (const xmlChar*)"user-rejoin");
  }

  inf_session_user_to_xml(priv->session, user, xml);

  /* TODO: Send with "connection" to subscriptions that are in the same
   * network, and that are non-local. */

  /* exclude the connection from which the request comes. The reply to it
   * is sent separately telling it that the user join was accepted. */
  inf_session_send_to_subscriptions(
    priv->session,
    connection,
    xmlCopyNode(xml, 1)
  );

  g_signal_connect(
    G_OBJECT(user),
    "notify::status",
    G_CALLBACK(infd_session_proxy_user_notify_status_cb),
    proxy
  );

  if(connection != NULL)
  {
    xmlNewProp(xml, (const xmlChar*)"seq", (const xmlChar*)request_seq);

    inf_connection_manager_group_send_to_connection(
      priv->subscription_group,
      connection,
      xml
    );

    subscription = infd_session_proxy_find_subscription(proxy, connection);
    g_assert(subscription != NULL);

    subscription->users = g_slist_prepend(subscription->users, user);
  }
  else
  {
    priv->local_users = g_slist_prepend(priv->local_users, user);
    xmlFreeNode(xml);
  }

  return user;
}

/*
 * Signal handlers.
 */

static void
infd_session_proxy_connection_notify_status_cb(InfXmlConnection* connection,
                                               const gchar* property,
                                               gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfdSessionProxySubscription* subscription;
  InfXmlConnectionStatus status;

  proxy = INFD_SESSION_PROXY(user_data);

  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSED ||
     status == INF_XML_CONNECTION_CLOSING)
  {
    subscription = infd_session_proxy_find_subscription(proxy, connection);
    g_assert(subscription != NULL);

    /* TODO: Only send user-status-change to users that don't have a direct
     * connection to the closed connection. */
    infd_session_proxy_remove_subscription(proxy, subscription);
  }
}

static void
infd_session_proxy_add_user_cb(InfUserTable* user_table,
                               InfUser* user,
                               gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;
  InfXmlConnection* sync_conn;
  InfdSessionProxySubscription* subscription;

  proxy = INFD_SESSION_PROXY(user_data);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  /* Make sure that we generate a non-existing user ID for the next user. */
  if(priv->user_id_counter <= inf_user_get_id(user))
    priv->user_id_counter = inf_user_get_id(user) + 1;

  if(inf_session_get_status(priv->session) == INF_SESSION_SYNCHRONIZING)
  {
    if(inf_user_get_status(user) != INF_USER_UNAVAILABLE)
    {
      g_object_get(
        G_OBJECT(priv->session),
        "sync-connection",
        &sync_conn,
        NULL
      );

      g_assert(sync_conn != NULL);
      subscription = infd_session_proxy_find_subscription(proxy, sync_conn);

      /* During synchronization, available users are always considered to
       * belong to the synchronizing connection. Everything else is just not
       * supported and causes session closure. */
      if(sync_conn != inf_user_get_connection(user) || subscription == NULL)
      {
        /* This actually cancels the synchronization: */
        inf_session_close(priv->session);
      }
      else
      {
        subscription->users = g_slist_prepend(subscription->users, user);

        g_signal_connect(
          G_OBJECT(user),
          "notify::status",
          G_CALLBACK(infd_session_proxy_user_notify_status_cb),
          proxy
        );
      }

      g_object_unref(sync_conn);
    }
  }
}

static void
infd_session_proxy_synchronization_begin_cb(InfSession* session,
                                            InfConnectionManagerGroup* group,
                                            InfXmlConnection* connection,
                                            gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;

  proxy = INFD_SESSION_PROXY(user_data);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  if(priv->idle)
  {
    priv->idle = FALSE;
    g_object_notify(G_OBJECT(proxy), "idle");
  }
}

static void
infd_session_proxy_synchronization_complete_cb_before(InfSession* session,
                                                      InfXmlConnection* conn,
                                                      gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;
#if 0
  InfSessionStatus status;
#endif

  proxy = INFD_SESSION_PROXY(user_data);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

#if 0
  g_object_get(session, "status", &status, NULL);

  if(status == INF_SESSION_SYNCHRONIZING)
    if(priv->subscribe_sync_conn == TRUE)
      infd_session_proxy_subscribe_to(proxy, conn, NULL, FALSE);
#endif
}

static void
infd_session_proxy_synchronization_complete_cb_after(InfSession* session,
                                                     InfXmlConnection* conn,
                                                     gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;

  proxy = INFD_SESSION_PROXY(user_data);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  /* Set idle if no more synchronizations are running */
  if(!priv->idle && priv->subscriptions == NULL &&
     !inf_session_has_synchronizations(session))
  {
    priv->idle = TRUE;
    g_object_notify(G_OBJECT(proxy), "idle");
  }
}

static void
infd_session_proxy_synchronization_failed_cb_before(InfSession* session,
                                                    InfXmlConnection* conn,
                                                    const GError* error,
                                                    gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfSessionStatus status;
  InfdSessionProxySubscription* subscription;

  proxy = INFD_SESSION_PROXY(user_data);

  g_object_get(session, "status", &status, NULL);

  /* We do not need handle the status == INF_SESSION_PROXY_SYNCHRONIZING case
   * since there cannot be any subscriptions while we are synchronizing. */

  if(status == INF_SESSION_RUNNING)
  {
    subscription = infd_session_proxy_find_subscription(proxy, conn);
    if(subscription != NULL)
    {
      /* Note that it should not matter whether we call
       * infd_session_proxy_release_subscription or
       * infd_session_proxy_remove_subscription
       * because there cannot be any users joined via the connection anyway,
       * because it was not yet synchronized. */
      infd_session_proxy_release_subscription(proxy, subscription);
    }
  }
}

static void
infd_session_proxy_synchronization_failed_cb_after(InfSession* session,
                                                   InfXmlConnection* conn,
                                                   const GError* error,
                                                   gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;

  proxy = INFD_SESSION_PROXY(user_data);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  /* Set idle if no more synchronizations are running */
  if(!priv->idle && priv->subscriptions == NULL &&
     !inf_session_has_synchronizations(session))
  {
    priv->idle = TRUE;
    g_object_notify(G_OBJECT(proxy), "idle");
  }
}

static void
infd_session_proxy_session_close_cb(InfSession* session,
                                    gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;
  InfdSessionProxySubscription* subscription;
  InfSessionSyncStatus status;
  xmlNodePtr xml;

  proxy = INFD_SESSION_PROXY(user_data);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  while(priv->subscriptions != NULL)
  {
    subscription = (InfdSessionProxySubscription*)priv->subscriptions->data;

    status = inf_session_get_synchronization_status(
      priv->session,
      subscription->connection
    );

    /* If synchronization is still in progress, the default handler of
     * InfSession will cancel the synchronization in which case we do
     * not need to send an extra session-close message. */

    /* We send session_close when we are in AWAITING_ACK status. In
     * AWAITING_ACK status we cannot cancel the synchronization anymore
     * because everything has already been sent out. Therefore the client
     * will eventuelly get in RUNNING state when he receives this message,
     * and process it correctly. */
    if(status != INF_SESSION_SYNC_IN_PROGRESS)
    {
      xml = xmlNewNode(NULL, (const xmlChar*)"session-close");

      inf_connection_manager_group_send_to_connection(
        priv->subscription_group,
        subscription->connection,
        xml
      );
    }

    /* Do not call remove_subscription because this would try to send
     * messages about leaving users, but we are sending session-close
     * to all subscriptions anyway. */
    infd_session_proxy_release_subscription(proxy, subscription);
  }

  inf_connection_manager_group_unref(priv->subscription_group);
  priv->subscription_group = NULL;
}

/*
 * GObject overrides.
 */

static void
infd_session_proxy_init(GTypeInstance* instance,
                        gpointer g_class)
{
  InfdSessionProxy* session_proxy;
  InfdSessionProxyPrivate* priv;

  session_proxy = INFD_SESSION_PROXY(instance);
  priv = INFD_SESSION_PROXY_PRIVATE(session_proxy);

  priv->subscriptions = NULL;
  priv->subscription_group = NULL;
  priv->user_id_counter = 1;
#if 0
  priv->subscribe_sync_conn = FALSE;
#endif
  priv->local_users = NULL;
  priv->idle = TRUE;
}

static GObject*
infd_session_proxy_constructor(GType type,
                               guint n_construct_properties,
                               GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfdSessionProxy* session_proxy;
  InfdSessionProxyPrivate* priv;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  session_proxy = INFD_SESSION_PROXY(object);
  priv = INFD_SESSION_PROXY_PRIVATE(session_proxy);

  g_assert(priv->subscription_group != NULL);
  g_assert(priv->session != NULL);

  /* Set unidle when session is currently being synchronized */
  if(inf_session_get_status(priv->session) == INF_SESSION_SYNCHRONIZING)
    priv->idle = FALSE;

  /* TODO: We could perhaps optimize by only setting the subscription
   * group when there are subscribed connections. */
  inf_session_set_subscription_group(priv->session, priv->subscription_group);

  return object;
}

static void
infd_session_proxy_dispose(GObject* object)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;
  InfConnectionManager* manager;

  proxy = INFD_SESSION_PROXY(object);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  manager = inf_session_get_connection_manager(priv->session);
  g_object_ref(G_OBJECT(manager));

  g_slist_free(priv->local_users);
  priv->local_users = NULL;

  /* We need to close the session explicitely before we unref so that
   * the signal handler for the close signal is called. */
  /* Note this emits the close signal, removing all subscriptions and
   * the subscription group */
  if(inf_session_get_status(priv->session) != INF_SESSION_CLOSED)
    inf_session_close(priv->session);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(infd_session_proxy_session_close_cb),
    proxy
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(inf_session_get_user_table(priv->session)),
    G_CALLBACK(infd_session_proxy_add_user_cb),
    proxy
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(infd_session_proxy_synchronization_begin_cb),
    proxy
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(infd_session_proxy_synchronization_complete_cb_before),
    proxy
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(infd_session_proxy_synchronization_complete_cb_after),
    proxy
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(infd_session_proxy_synchronization_failed_cb_before),
    proxy
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(infd_session_proxy_synchronization_failed_cb_after),
    proxy
  );

  g_object_unref(G_OBJECT(priv->session));
  priv->session = NULL;

  g_assert(priv->subscription_group == NULL);
  g_assert(priv->subscriptions == NULL);

  g_object_unref(G_OBJECT(manager));
}

static void
infd_session_proxy_session_init_user_func(InfUser* user,
                                          gpointer user_data)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;

  proxy = INFD_SESSION_PROXY(user_data);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  if(priv->user_id_counter <= inf_user_get_id(user))
    priv->user_id_counter = inf_user_get_id(user) + 1;
}

static void
infd_session_proxy_set_property(GObject* object,
                                guint prop_id,
                                const GValue* value,
                                GParamSpec* pspec)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;

  proxy = INFD_SESSION_PROXY(object);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  switch(prop_id)
  {
  case PROP_SESSION:
    g_assert(priv->session == NULL); /* construct only */
    priv->session = INF_SESSION(g_value_dup_object(value));

    /* Adjust user id counter so the next joining user gets a free ID */
    /* TODO: Add local users to priv->local_users, assert that there are no
     * available non-local users. */
    inf_user_table_foreach_user(
      inf_session_get_user_table(priv->session),
      infd_session_proxy_session_init_user_func,
      proxy
    );

    g_signal_connect(
      G_OBJECT(priv->session),
      "close",
      G_CALLBACK(infd_session_proxy_session_close_cb),
      proxy
    );

    g_signal_connect(
      G_OBJECT(inf_session_get_user_table(priv->session)),
      "add-user",
      G_CALLBACK(infd_session_proxy_add_user_cb),
      proxy
    );

    g_signal_connect_after(
      G_OBJECT(priv->session),
      "synchronization-begin",
      G_CALLBACK(infd_session_proxy_synchronization_begin_cb),
      proxy
    );

    g_signal_connect(
      G_OBJECT(priv->session),
      "synchronization-complete",
      G_CALLBACK(infd_session_proxy_synchronization_complete_cb_before),
      proxy
    );

    g_signal_connect_after(
      G_OBJECT(priv->session),
      "synchronization-complete",
      G_CALLBACK(infd_session_proxy_synchronization_complete_cb_after),
      proxy
    );

    g_signal_connect(
      G_OBJECT(priv->session),
      "synchronization-failed",
      G_CALLBACK(infd_session_proxy_synchronization_failed_cb_before),
      proxy
    );

    g_signal_connect_after(
      G_OBJECT(priv->session),
      "synchronization-failed",
      G_CALLBACK(infd_session_proxy_synchronization_failed_cb_after),
      proxy
    );

    break;
  case PROP_SUBSCRIPTION_GROUP:
    g_assert(priv->subscription_group == NULL); /* construct only */
    priv->subscription_group =
      (InfConnectionManagerGroup*)g_value_dup_boxed(value);
    break;
#if 0
  case PROP_SUBSCRIBE_SYNC_CONN:
    priv->subscribe_sync_conn = g_value_get_boolean(value);
    break;
#endif
  case PROP_IDLE:
    /* read/only */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_session_proxy_get_property(GObject* object,
                                guint prop_id,
                                GValue* value,
                                GParamSpec* pspec)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;

  proxy = INFD_SESSION_PROXY(object);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  switch(prop_id)
  {
  case PROP_SESSION:
    g_value_set_object(value, G_OBJECT(priv->session));
    break;
  case PROP_SUBSCRIPTION_GROUP:
    g_value_set_boxed(value, priv->subscription_group);
    break;
#if 0
  case PROP_SUBSCRIBE_SYNC_CONN:
    g_value_set_boolean(value, priv->subscribe_sync_conn);
    break;
#endif
  case PROP_IDLE:
    g_value_set_boolean(value, priv->idle);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * Default signal handlers
 */

static void
infd_session_proxy_add_subscription_handler(InfdSessionProxy* proxy,
                                            InfXmlConnection* connection)
{
  InfdSessionProxyPrivate* priv;
  InfdSessionProxySubscription* subscription;

  priv = INFD_SESSION_PROXY_PRIVATE(proxy);
  g_assert(infd_session_proxy_find_subscription(proxy, connection) == NULL);

  g_signal_connect(
    G_OBJECT(connection),
    "notify::status",
    G_CALLBACK(infd_session_proxy_connection_notify_status_cb),
    proxy
  );

  subscription = infd_session_proxy_subscription_new(connection);
  priv->subscriptions = g_slist_prepend(priv->subscriptions, subscription);

  if(priv->idle)
  {
    priv->idle = FALSE;
    g_object_notify(G_OBJECT(proxy), "idle");
  }
}

static void
infd_session_proxy_remove_subscription_handler(InfdSessionProxy* proxy,
                                               InfXmlConnection* connection)
{
  InfdSessionProxyPrivate* priv;
  InfdSessionProxySubscription* subscr;

  priv = INFD_SESSION_PROXY_PRIVATE(proxy);
  subscr = infd_session_proxy_find_subscription(proxy, connection);

  g_assert(subscr != NULL);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(connection),
    G_CALLBACK(infd_session_proxy_connection_notify_status_cb),
    proxy
  );

  /* TODO: Cancel synchronization if the synchronization to this subscription
   * did not yet finish. */

  inf_connection_manager_group_remove_connection(
    priv->subscription_group,
    connection
  );

  while(subscr->users)
  {
    /* The signal handler of the user's notify::status signal removes the user
     * from the subscription. */
    g_object_set(
      G_OBJECT(subscr->users->data),
      "status", INF_USER_UNAVAILABLE,
      NULL
    );
  }

  priv->subscriptions = g_slist_remove(priv->subscriptions, subscr);
  infd_session_proxy_subscription_free(subscr);

  if(!priv->idle && priv->subscriptions == NULL &&
     !inf_session_has_synchronizations(priv->session))
  {
    priv->idle = TRUE;
    g_object_notify(G_OBJECT(proxy), "idle");
  }
}

/*
 * Message handling.
 */

static gboolean
infd_session_proxy_handle_user_join(InfdSessionProxy* proxy,
                                    InfXmlConnection* connection,
                                    xmlNodePtr xml,
                                    GError** error)
{
  InfdSessionProxyPrivate* priv;
  InfSessionClass* session_class;
  GArray* array;
  InfUser* user;
  xmlChar* seq_attr;
  guint i;

  priv = INFD_SESSION_PROXY_PRIVATE(proxy);
  session_class = INF_SESSION_GET_CLASS(priv->session);

  array = session_class->get_xml_user_props(
    priv->session,
    connection,
    xml
  );

  seq_attr = xmlGetProp(xml, (const xmlChar*)"seq");
  user = infd_session_proxy_perform_user_join(
    proxy,
    connection,
    (const gchar*)seq_attr,
    array,
    error
  );

  xmlFree(seq_attr);

  for(i = 0; i < array->len; ++ i)
    g_value_unset(&g_array_index(array, GParameter, i).value);

  g_array_free(array, TRUE);

  if(user == NULL)
    return FALSE;

  return TRUE;
}

static gboolean
infd_session_proxy_handle_session_unsubscribe(InfdSessionProxy* proxy,
                                              InfXmlConnection* connection,
                                              const xmlNodePtr xml,
                                              GError** error)
{
  InfdSessionProxySubscription* subscription;

  subscription = infd_session_proxy_find_subscription(proxy, connection);
  g_assert(subscription != NULL);

  infd_session_proxy_remove_subscription(proxy, subscription);
  return TRUE;
}

/*
 * InfNetObject implementation
 */

static void
infd_session_proxy_net_object_sent(InfNetObject* net_object,
                                   InfXmlConnection* connection,
                                   xmlNodePtr node)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;

  proxy = INFD_SESSION_PROXY(net_object);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  /* TODO: Don't forward for messages the proxy issued */

  g_assert(priv->session != NULL);
  inf_net_object_sent(INF_NET_OBJECT(priv->session), connection, node);
}

static void
infd_session_proxy_net_object_enqueued(InfNetObject* net_object,
                                       InfXmlConnection* connection,
                                       xmlNodePtr node)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;

  proxy = INFD_SESSION_PROXY(net_object);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  /* TODO: Don't forward for messages the proxy issued */

  g_assert(priv->session != NULL);
  inf_net_object_enqueued(INF_NET_OBJECT(priv->session), connection, node);
}

static gboolean
infd_session_proxy_net_object_received(InfNetObject* net_object,
                                       InfXmlConnection* connection,
                                       xmlNodePtr node,
                                       GError** error)
{
  InfdSessionProxy* proxy;
  InfdSessionProxyPrivate* priv;
  InfSessionSyncStatus status;
  GError* local_error;
  xmlNodePtr reply_xml;
  xmlChar* seq_attr;

  proxy = INFD_SESSION_PROXY(net_object);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  /* TODO: Don't forward for messages the proxy issued */

  g_assert(priv->session != NULL);
  status = inf_session_get_synchronization_status(priv->session, connection);
  local_error = NULL;

  if(status != INF_SESSION_SYNC_NONE)
  {
    return inf_net_object_received(
      INF_NET_OBJECT(priv->session),
      connection,
      node,
      error
    );
  }
  else
  {
    if(strcmp((const char*)node->name, "user-join") == 0)
    {
      infd_session_proxy_handle_user_join(
        proxy,
        connection,
        node,
        &local_error
      );
    }
    else if(strcmp((const char*)node->name, "session-unsubscribe") == 0)
    {
      /* TODO: Handle this in InfSession, if possible */
      infd_session_proxy_handle_session_unsubscribe(
        proxy,
        connection,
        node,
        &local_error
      );
    }
    else
    {
      return inf_net_object_received(
        INF_NET_OBJECT(priv->session),
        connection,
        node,
        error
      );
    }
  }

  if(local_error != NULL)
  {
    /* Only send request-failed when it was a proxy-related request */
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

    inf_connection_manager_group_send_to_connection(
      priv->subscription_group,
      connection,
      reply_xml
    );

    /* TODO: Only propagate on fatal errors. If a user join fails because
     * a user name is already in use or something, we do not need the
     * connection manager to print a warning that the session might have
     * become inconsistent. */

    g_propagate_error(error, local_error);
  }

  /* Don't forward proxy-related messages */
  return FALSE;
}

/*
 * GType registration.
 */

static void
infd_session_proxy_class_init(gpointer g_class,
                              gpointer class_data)
{
  GObjectClass* object_class;
  InfdSessionProxyClass* proxy_class;

  object_class = G_OBJECT_CLASS(g_class);
  proxy_class = INFD_SESSION_PROXY_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdSessionProxyPrivate));

  object_class->constructor = infd_session_proxy_constructor;
  object_class->dispose = infd_session_proxy_dispose;
  object_class->set_property = infd_session_proxy_set_property;
  object_class->get_property = infd_session_proxy_get_property;

  proxy_class->add_subscription = infd_session_proxy_add_subscription_handler;
  proxy_class->remove_subscription =
    infd_session_proxy_remove_subscription_handler;

  g_object_class_install_property(
    object_class,
    PROP_SESSION,
    g_param_spec_object(
      "session",
      "Session",
      "The underlying session",
      INF_TYPE_SESSION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SUBSCRIPTION_GROUP,
    g_param_spec_boxed(
      "subscription-group",
      "Subscription group",
      "The connection manager group of subscribed connections",
      INF_TYPE_CONNECTION_MANAGER_GROUP,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

#if 0
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
#endif

  g_object_class_install_property(
    object_class,
    PROP_IDLE,
    g_param_spec_boolean(
      "idle",
      "Idle",
      "The session is considered idle when are no subscriptions and no "
      "synchronizations",
      TRUE,
      G_PARAM_READABLE
    )
  );

  /**
   * InfdSessionProxy::add-subscription:
   * @proxy: The #InfdSessionProxy emitting the signal.
   * @connection: The subscribed #InfXmlConnection.
   *
   * Emitted every time a connection is subscribed to the session.
   **/
  session_proxy_signals[ADD_SUBSCRIPTION] = g_signal_new(
    "add-subscription",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdSessionProxyClass, add_subscription),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XML_CONNECTION
  );

  /**
   * InfdSessionProxy::remove-subscription:
   * @proxy: The #InfdSessionProxy emitting the signal.
   * @connection: The unsubscribed #InfXmlConnection.
   *
   * Emitted every time a connection is unsubscribed to the session, or a
   * subscription is removed because the session is closed.
   **/
  session_proxy_signals[REMOVE_SUBSCRIPTION] = g_signal_new(
    "remove-subscription",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfdSessionProxyClass, remove_subscription),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_TYPE_XML_CONNECTION
  );
}

static void
infd_session_proxy_net_object_init(gpointer g_iface,
                                   gpointer iface_data)
{
  InfNetObjectIface* iface;
  iface = (InfNetObjectIface*)g_iface;

  iface->sent = infd_session_proxy_net_object_sent;
  iface->enqueued = infd_session_proxy_net_object_enqueued;
  iface->received = infd_session_proxy_net_object_received;
}

GType
infd_session_proxy_get_type(void)
{
  static GType session_proxy_type = 0;

  if(!session_proxy_type)
  {
    static const GTypeInfo session_proxy_type_info = {
      sizeof(InfdSessionProxyClass),    /* class_size */
      NULL,                             /* base_init */
      NULL,                             /* base_finalize */
      infd_session_proxy_class_init,    /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      sizeof(InfdSessionProxy),         /* instance_size */
      0,                                /* n_preallocs */
      infd_session_proxy_init,          /* instance_init */
      NULL                              /* value_table */
    };

    static const GInterfaceInfo net_object_info = {
      infd_session_proxy_net_object_init,
      NULL,
      NULL
    };

    session_proxy_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdSessionProxy",
      &session_proxy_type_info,
      0
    );

    g_type_add_interface_static(
      session_proxy_type,
      INF_TYPE_NET_OBJECT,
      &net_object_info
    );
  }

  return session_proxy_type;
}

/*
 * Public API.
 */

/**
 * inf_session_proxy_get_session:
 * @proxy: A #InfdSessionProxy.
 *
 * Returns the session proxied by @proxy. Returns %NULL if the session was
 * closed.
 *
 * Return Value: A #InfSession, or %NULL.
 **/
InfSession*
infd_session_proxy_get_session(InfdSessionProxy* proxy)
{
  g_return_val_if_fail(INFD_IS_SESSION_PROXY(proxy), NULL);
  return INFD_SESSION_PROXY_PRIVATE(proxy)->session;
}

/**
 * infd_session_proxy_add_user:
 * @proxy: A #InfdSessionProxy.
 * @params: Construction properties for the #InfUser (or derived) object.
 * @n_params: Number of parameters.
 * @error: Location to store error information.
 *
 * Adds a local user to @proxy's session. @params must not contain the
 * 'id' property because it will be choosen by the proxy. Also, if the 'name'
 * property is already in use by an existing, but unavailable user, this user 
 * will be re-used.
 *
 * Return Value: The #InfUser that has been added, or %NULL in case of an
 * error.
 **/
InfUser*
infd_session_proxy_add_user(InfdSessionProxy* proxy,
                            const GParameter* params,
                            guint n_params,
                            GError** error)
{
  InfUser* user;
  GArray* array;

  g_return_val_if_fail(INFD_IS_SESSION_PROXY(proxy), NULL);

  /* TODO: Make sure values added by infd_session_proxy_perform_user_join are
   * released, for example by inserting copies into the array, and freeing
   * the values after the call. */
  array = g_array_sized_new(FALSE, FALSE, sizeof(GParameter), n_params + 2);
  g_array_append_vals(array, params, n_params);

  user = infd_session_proxy_perform_user_join(
    proxy,
    NULL,
    NULL,
    array,
    error
  );

  g_array_free(array, TRUE);

  return user;
}

/**
 * infd_session_proxy_subscribe_to:
 * @proxy: A #InfdSessionProxy.
 * @connection: A #InfConnection that is not yet subscribed.
 * @parent_group: A #InfConnectionManagerGroup, or %NULL.
 * @synchronize: If %TRUE, then synchronize the session to @connection first.
 *
 * Subscribes @connection to @proxy's session. The first thing that will be
 * done is a synchronization (see inf_session_synchronize_to()). Then, all
 * changes to the session are propagated to @connection.
 *
 * Normally, you want to set @synchronize to %TRUE in which case the whole
 * session state will be synchronized to @connection (within the subscription
 * group). However, if for whatever reason the remote site already has a
 * copy of the session, then you may set @synchronize to %FALSE to skip
 * synchronization. This happens for example for newly created documents, or
 * when the remote site synchronized the local session and wants to be
 * initially subscribed.
 *
 * If @proxy's session is not in %INF_SESSION_RUNNING status, but in
 * %INF_SESSION_SYNCHRONIZING, then @connection must be the connection that
 * synchronizes the session and @synchronize needs to be set to %FALSE. This
 * causes the synchronizing connection to initially be subscribed. This
 * needs to be called directly after having created the session proxy (i.e.
 * without returning to the main loop before) so that the synchronization
 * connection is added to the subscription group for synchronization.
 *
 * If you told @connection about the subscription in some
 * #InfConnectionManagerGroup, then pass that group as the @parent_group
 * parameter to this function so that synchronization or subscription
 * messages are kept back until all messages in @parent_queue to @connection
 * have been sent, so that @connection knows about the subscription before
 * the first synchronization or subscription message arrives. See also
 * inf_connection_manager_add_connection().
 *
 * A subscription can only be initialted if @proxy's session is in state
 * %INF_SESSION_RUNNING.
 **/
void
infd_session_proxy_subscribe_to(InfdSessionProxy* proxy,
                                InfXmlConnection* connection,
                                InfConnectionManagerGroup* parent_group,
                                gboolean synchronize)
{
  InfdSessionProxyPrivate* priv;

  g_return_if_fail(INFD_IS_SESSION_PROXY(proxy));
  g_return_if_fail(INF_IS_XML_CONNECTION(connection));

  g_return_if_fail(
    infd_session_proxy_find_subscription(proxy, connection) == NULL
  );

  priv = INFD_SESSION_PROXY_PRIVATE(proxy);
  g_return_if_fail(priv->session != NULL);

  /* TODO: Also check connection against sync-conn in synchronizing case: */
  g_return_if_fail(
    inf_session_get_status(priv->session) == INF_SESSION_RUNNING ||
    (synchronize == FALSE)
  );

  /* Note we can't do this in the default signal handler since it doesn't
   * know the parent group. */
  inf_connection_manager_group_add_connection(
    priv->subscription_group,
    connection,
    parent_group
  );

  g_signal_emit(
    G_OBJECT(proxy),
    session_proxy_signals[ADD_SUBSCRIPTION],
    0,
    connection
  );

  /* Make sure the default handler ran. Stopping the signal emission before
   * would leave us in an inconsistent state. */
  g_assert(infd_session_proxy_find_subscription(proxy, connection) != NULL);

  if(synchronize)
  {
    /* Directly synchronize within the subscription group so that we do not
     * need a group change after synchronization, and the connection already
     * receives requests from other group members to process after
     * synchronization. */
    inf_session_synchronize_to(
      priv->session,
      priv->subscription_group,
      connection
    );
  }
}

/**
 * infd_session_proxy_has_subscriptions:
 * @proxy: A #InfdSessionProxy.
 *
 * Returns whether there are subscribed connections to the session.
 *
 * Returns: Whether there are subscribed connections.
 **/
gboolean
infd_session_proxy_has_subscriptions(InfdSessionProxy* proxy)
{
  InfdSessionProxyPrivate* priv;

  g_return_val_if_fail(INFD_IS_SESSION_PROXY(proxy), FALSE);
  priv = INFD_SESSION_PROXY_PRIVATE(proxy);

  if(priv->subscriptions == NULL)
    return FALSE;

  return TRUE;
}

/**
 * infd_session_proxy_is_subscribed:
 * @proxy: A #InfdSessionProxy.
 * @connection: The connection to check for being subscribed.
 *
 * Returns %TRUE when @connection is subscribed to the session and %FALSE
 * otherwise.
 *
 * Returns: Whether @connection is subscribed.
 **/
gboolean
infd_session_proxy_is_subscribed(InfdSessionProxy* proxy,
                                 InfXmlConnection* connection)
{
  g_return_val_if_fail(INFD_IS_SESSION_PROXY(proxy), FALSE);
  g_return_val_if_fail(INF_IS_XML_CONNECTION(connection), FALSE);

  if(infd_session_proxy_find_subscription(proxy, connection) == NULL)
    return FALSE;

  return TRUE;
}

/**
 * infd_session_proxy_is_idle:
 * @proxy: A #InfdSessionProxy.
 *
 * Returns whether the session is idle. The session is considered idle when
 * there are no subscriptions and no synchronizations (in either direction).
 *
 * Returns: Whether the session is idle.
 **/
gboolean
infd_session_proxy_is_idle(InfdSessionProxy* proxy)
{
  g_return_val_if_fail(INFD_IS_SESSION_PROXY(proxy), FALSE);
  return INFD_SESSION_PROXY_PRIVATE(proxy)->idle;
}

/* vim:set et sw=2 ts=2: */
