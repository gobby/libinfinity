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

#include <libinfinity/client/infc-session-proxy.h>
#include <libinfinity/client/infc-user-request.h>
#include <libinfinity/client/infc-request-manager.h>

#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>

typedef struct _InfcSessionProxyPrivate InfcSessionProxyPrivate;
struct _InfcSessionProxyPrivate {
  InfSession* session;
  InfCommunicationJoinedGroup* subscription_group;
  InfXmlConnection* connection;
  InfcRequestManager* request_manager;
};

#define INFC_SESSION_PROXY_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_SESSION_PROXY, InfcSessionProxyPrivate))

static GObjectClass* parent_class;

enum {
  PROP_0,

  PROP_SESSION,
  PROP_SUBSCRIPTION_GROUP,
  PROP_SEQUENCE_ID,
  PROP_CONNECTION
};

static void
infc_session_proxy_release_connection(InfcSessionProxy* proxy);

static void
infc_session_proxy_unsubscribe_connection(InfcSessionProxy* proxy)
{
  InfcSessionProxyPrivate* priv;
  InfXmlConnectionStatus status;
  xmlNodePtr xml;

  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  g_assert(priv->connection != NULL);
  g_assert(priv->session != NULL);

  status = inf_session_get_synchronization_status(
    priv->session,
    priv->connection
  );

  /* If synchronization is still in progress, the close default signal
   * handler in InfSession the base class will cancel the synchronization in
   * which case we do not need to send an extra session-unsubscribe
   * message. */

  /* However, in case we are in AWAITING_ACK status we send session
   * unsubscribe because we cannot cancel the synchronization anymore but
   * the server will go into RUNNING state before receiving this message. */
  if(status != INF_SESSION_SYNC_IN_PROGRESS)
  {
    xml = xmlNewNode(NULL, (const xmlChar*)"session-unsubscribe");

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->subscription_group),
      priv->connection,
      xml
    );
  }

  infc_session_proxy_release_connection(proxy);
}

/*
 * Signal handlers
 */

static void
infc_session_proxy_connection_notify_status_cb(InfXmlConnection* connection,
                                               GParamSpec* pspec,
                                               gpointer user_data)
{
  InfcSessionProxy* proxy;
  InfXmlConnectionStatus status;

  proxy = INFC_SESSION_PROXY(user_data);
  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSED ||
     status == INF_XML_CONNECTION_CLOSING)
  {
    /* Reset connection in case of closure */
    infc_session_proxy_release_connection(proxy);
  }
}

static void
infc_session_proxy_session_synchronization_complete_cb(InfSession* session,
                                                       InfXmlConnection* conn,
                                                       gpointer user_data)
{
  InfcSessionProxy* proxy;
  InfcSessionProxyPrivate* priv;
  InfSessionStatus status;

  proxy = INFC_SESSION_PROXY(user_data);
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  g_object_get(G_OBJECT(session), "status", &status, NULL);

  /* There are actually 4 different situations here, depending on status
   * and priv->connection:
   *
   * 1) status == SYNCHRONIZING and priv->connection == NULL
   * This means that someone synchronized its session to us, but we are not
   * subscribed to that session.
   *
   * 2) status == SYNCHRONIZING and priv->connection != NULL
   * This means that someone synchronized us and we are subscribed to that
   * session.
   *
   * 3) status == RUNNING and priv->connection == NULL
   * This means that we synchronized our session to someone else but are
   * not subscribed to any session.
   *
   * 4) status == RUNNING and priv->connection != NULL
   * This means that we synchronized our session to someone else and are
   * subscribed to a session (possibly on another host as the one we
   * synchronized to!). */

  if(status == INF_SESSION_SYNCHRONIZING)
  {
    if(priv->connection != NULL)
    {
      /* The connection that synchronized the session to us should be the
       * one we subscribed to. */
      g_assert(priv->connection == conn);
    }
  }
}

static void
infc_session_proxy_session_synchronization_failed_cb(InfSession* session,
                                                     InfXmlConnection* conn,
                                                     const GError* error,
                                                     gpointer user_data)
{
  InfcSessionProxy* proxy;
  InfcSessionProxyPrivate* priv;
  InfSessionStatus status;
  
  proxy = INFC_SESSION_PROXY(user_data);
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  g_object_get(G_OBJECT(session), "status", &status, NULL);

  switch(status)
  {
  case INF_SESSION_SYNCHRONIZING:
    /* When the synchronization failed in synchronizing state, the session
     * will close itself anyway which is where we do the cleanup in this
     * case. */
    break;
  case INF_SESSION_RUNNING:
    /* We do not need to send an explicit session-unsubscribe, because the
     * failed synchronization should already let the host know that
     * subscription makes no sense anymore. */

    /* If the synchronization failed because the connection was closed, then
     * the connection might not exist anymore at this point since the
     * notify::status handler ran before this handler. */
    if(priv->connection != NULL)
      infc_session_proxy_release_connection(proxy);
    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }
}

static void
infc_session_proxy_session_close_cb(InfSession* session,
                                    gpointer user_data)
{
  InfcSessionProxy* proxy;
  InfcSessionProxyPrivate* priv;

  proxy = INFC_SESSION_PROXY(user_data);
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  if(priv->connection != NULL)
    infc_session_proxy_unsubscribe_connection(proxy);

  /* Don't release session so others can still access */
#if 0
  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(infc_session_proxy_session_close_cb),
    proxy
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(infc_session_proxy_session_synchronization_complete_cb),
    proxy
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(infc_session_proxy_session_synchronization_failed_cb),
    proxy
  );

  g_object_unref(G_OBJECT(priv->session));
  priv->session = NULL;
#endif
}

/*
 * Helper functions
 */

static void
infc_session_proxy_release_connection_foreach_user_func(InfUser* user,
                                                        gpointer user_data)
{
  g_object_set(G_OBJECT(user), "status", INF_USER_UNAVAILABLE, NULL);
}

static void
infc_session_proxy_release_connection(InfcSessionProxy* proxy)
{
  InfcSessionProxyPrivate* priv;
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  g_assert(priv->connection != NULL);
  g_assert(priv->subscription_group != NULL);

  /* TODO: Emit failed signal with some "cancelled" error? */
  infc_request_manager_clear(priv->request_manager);

  g_object_unref(priv->request_manager);
  priv->request_manager = NULL;

  /* Set status of all users to unavailable */
  /* TODO: Keep local users available */
  inf_user_table_foreach_user(
    inf_session_get_user_table(priv->session),
    infc_session_proxy_release_connection_foreach_user_func,
    NULL
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->connection),
    G_CALLBACK(infc_session_proxy_connection_notify_status_cb),
    proxy
  );

  inf_session_set_subscription_group(priv->session, NULL);

  g_object_unref(priv->subscription_group);
  priv->subscription_group = NULL;

  g_object_unref(G_OBJECT(priv->connection));
  priv->connection = NULL;

  g_object_notify(G_OBJECT(proxy), "connection");
  g_object_notify(G_OBJECT(proxy), "subscription-group");
}

static xmlNodePtr
infc_session_proxy_request_to_xml(InfcRequest* request)
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
infc_session_proxy_init(GTypeInstance* instance,
                        gpointer g_class)
{
  InfcSessionProxy* proxy;
  InfcSessionProxyPrivate* priv;

  proxy = INFC_SESSION_PROXY(instance);
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  priv->session = NULL;
  priv->subscription_group = NULL;
  priv->connection = NULL;
  priv->request_manager = NULL;
}

static void
infc_session_proxy_dispose(GObject* object)
{
  InfcSessionProxy* proxy;
  InfcSessionProxyPrivate* priv;

  proxy = INFC_SESSION_PROXY(object);
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  if(priv->connection)
    infc_session_proxy_unsubscribe_connection(proxy);

  /* Release session */
  if(priv->session != NULL)
  {
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->session),
      G_CALLBACK(infc_session_proxy_session_close_cb),
      proxy
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->session),
      G_CALLBACK(infc_session_proxy_session_synchronization_complete_cb),
      proxy
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->session),
      G_CALLBACK(infc_session_proxy_session_synchronization_failed_cb),
      proxy
    );

    g_object_unref(G_OBJECT(priv->session));
    priv->session = NULL;
  }

  g_assert(priv->request_manager == NULL);
  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
infc_session_proxy_set_property(GObject* object,
                                guint prop_id,
                                const GValue* value,
                                GParamSpec* pspec)
{
  InfcSessionProxy* proxy;
  InfcSessionProxyPrivate* priv;

  proxy = INFC_SESSION_PROXY(object);
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  switch(prop_id)
  {
  case PROP_SESSION:
    g_assert(priv->session == NULL); /* construct only */
    priv->session = INF_SESSION(g_value_dup_object(value));

    g_signal_connect(
      G_OBJECT(priv->session),
      "close",
      G_CALLBACK(infc_session_proxy_session_close_cb),
      proxy
    );

    g_signal_connect(
      G_OBJECT(priv->session),
      "synchronization-complete",
      G_CALLBACK(infc_session_proxy_session_synchronization_complete_cb),
      proxy
    );

    g_signal_connect(
      G_OBJECT(priv->session),
      "synchronization-failed",
      G_CALLBACK(infc_session_proxy_session_synchronization_failed_cb),
      proxy
    );

    break;
  case PROP_SUBSCRIPTION_GROUP:
  case PROP_CONNECTION:
    /* these are read-only because they can only be changed both at once,
     * refer to infc_session_proxy_set_connection(). */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_session_proxy_get_property(GObject* object,
                                guint prop_id,
                                GValue* value,
                                GParamSpec* pspec)
{
  InfcSessionProxy* session;
  InfcSessionProxyPrivate* priv;

  session = INFC_SESSION_PROXY(object);
  priv = INFC_SESSION_PROXY_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_SESSION:
    g_value_set_object(value, G_OBJECT(priv->session));
    break;
  case PROP_SUBSCRIPTION_GROUP:
    g_value_set_boxed(value, priv->subscription_group);
    break;
  case PROP_CONNECTION:
    g_value_set_object(value, G_OBJECT(priv->connection));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static GError*
infc_session_proxy_translate_error_impl(InfcSessionProxy* proxy,
                                        GQuark domain,
                                        guint code)
{
  GError* error;
  const gchar* error_msg;

  if(domain == inf_request_error_quark())
    error_msg = inf_request_strerror(code);
  else if(domain == inf_user_error_quark())
    error_msg = inf_user_strerror(code);
  else
    error_msg = NULL;

  error = NULL;
  if(error_msg != NULL)
  {
    g_set_error(&error, domain, code, "%s", error_msg);
  }
  else
  {
    /* TODO: Check whether a human-readable error string was sent (that
     * we cannot translate then, of course). */
    g_set_error(
      &error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_UNKNOWN_DOMAIN,
      _("Error comes from unknown error domain '%s' (code %u)"),
      g_quark_to_string(domain),
      (guint)code
    );
  }

  return error;
}

/*
 * Message handling.
 */

static gboolean
infc_session_proxy_handle_user_join(InfcSessionProxy* proxy,
                                    InfXmlConnection* connection,
                                    xmlNodePtr xml,
                                    GError** error)
{
  InfcSessionProxyPrivate* priv;
  InfSessionClass* session_class;
  InfcRequest* request;
  GArray* array;
  InfUser* user;
  GParameter* param;
  guint i;

  priv = INFC_SESSION_PROXY_PRIVATE(proxy);
  session_class = INF_SESSION_GET_CLASS(priv->session);

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "user-join",
    xml,
    NULL
  );

  array = session_class->get_xml_user_props(priv->session, connection, xml);

  /* Set local flag if the join was requested by us (seq is present in
   * server response). */
  param = inf_session_get_user_property(array, "flags");
  g_assert(!G_IS_VALUE(&param->value)); /* must not have been set already */

  g_value_init(&param->value, INF_TYPE_USER_FLAGS);
  if(request != NULL)
    g_value_set_flags(&param->value, INF_USER_LOCAL);
  else
    g_value_set_flags(&param->value, 0);

  /* Set connection. If none was given, use publisher connection */
  param = inf_session_get_user_property(array, "connection");
  if(!G_IS_VALUE(&param->value))
  {
    g_value_init(&param->value, INF_TYPE_XML_CONNECTION);
    g_value_set_object(&param->value, G_OBJECT(connection));
  }

  /* This validates properties */
  user = inf_session_add_user(
    priv->session,
    (const GParameter*)array->data,
    array->len,
    error
  );

  for(i = 0; i < array->len; ++ i)
    g_value_unset(&g_array_index(array, GParameter, i).value);
  g_array_free(array, TRUE);

  if(user != NULL)
  {
    if(request != NULL)
    {
      infc_user_request_finished(INFC_USER_REQUEST(request), user);
      infc_request_manager_remove_request(priv->request_manager, request);
    }

    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

static gboolean
infc_session_proxy_handle_user_rejoin(InfcSessionProxy* proxy,
                                      InfXmlConnection* connection,
                                      xmlNodePtr xml,
                                      GError** error)
{
  InfcSessionProxyPrivate* priv;
  InfSessionClass* session_class;
  InfcRequest* request;
  GArray* array;
  InfUser* user;
  const GParameter* idparam;
  GParameter* param;
  guint id;
  gboolean result;
  guint i;

  priv = INFC_SESSION_PROXY_PRIVATE(proxy);
  session_class = INF_SESSION_GET_CLASS(priv->session);

  request = infc_request_manager_get_request_by_xml(
    priv->request_manager,
    "user-join",
    xml,
    NULL
  );

  array = session_class->get_xml_user_props(priv->session, connection, xml);

  /* Find rejoining user first */
  idparam = inf_session_lookup_user_property(
    (const GParameter*)array->data,
    array->len,
    "id"
  );

  if(idparam == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
      _("Request does not contain required attribute 'id'")
    );

    goto error;
  }

  id = g_value_get_uint(&idparam->value);
  user = inf_user_table_lookup_user_by_id(
    inf_session_get_user_table(priv->session),
    id
  );

  if(user == NULL)
  {
    g_set_error(
      error,
      inf_user_error_quark(),
      INF_USER_ERROR_NO_SUCH_USER,
      _("No such user with ID %u"),
      id
    );

    goto error;
  }

  /* Set local flag if the join was requested by us (seq is present in
   * server response). */
  param = inf_session_get_user_property(array, "flags");
  g_assert(!G_IS_VALUE(&param->value)); /* must not have been set already */

  g_value_init(&param->value, INF_TYPE_USER_FLAGS);
  if(request != NULL)
    g_value_set_flags(&param->value, INF_USER_LOCAL);
  else
    g_value_set_flags(&param->value, 0);

  /* Set connection. If none was given, use publisher connection */
  param = inf_session_get_user_property(array, "connection");
  if(!G_IS_VALUE(&param->value))
  {
    g_value_init(&param->value, INF_TYPE_XML_CONNECTION);
    g_value_set_object(&param->value, G_OBJECT(connection));
  }

  result = session_class->validate_user_props(
    priv->session,
    (const GParameter*)array->data,
    array->len,
    user,
    error
  );

  if(result == FALSE)
    goto error;

  /* Set properties on the found user object, performing the rejoin */
  g_object_freeze_notify(G_OBJECT(user));

  for(i = 0; i < array->len; ++ i)
  {
    param = &g_array_index(array, GParameter, i);

    /* Don't set ID because the ID is the same anyway (we did the user lookup
     * by it). The "id" property is construct only anyway. */
    if(strcmp(param->name, "id") != 0)
      g_object_set_property(G_OBJECT(user), param->name, &param->value);
  }

  /* TODO: Set user status to available, if the server did not send the
   * status property? Require the status property being set on a rejoin?
   * Make sure it is not unavailable? */

  g_object_thaw_notify(G_OBJECT(user));
  for(i = 0; i < array->len; ++ i)
    g_value_unset(&g_array_index(array, GParameter, i).value);
  g_array_free(array, TRUE);

  if(request != NULL)
  {
    infc_user_request_finished(INFC_USER_REQUEST(request), user);
    infc_request_manager_remove_request(priv->request_manager, request);
  }

  return TRUE;

error:
  for(i = 0; i < array->len; ++ i)
    g_value_unset(&g_array_index(array, GParameter, i).value);
  g_array_free(array, TRUE);
  return FALSE;
}

static gboolean
infc_session_proxy_handle_request_failed(InfcSessionProxy* proxy,
                                         InfXmlConnection* connection,
                                         xmlNodePtr xml,
                                         GError** error)
{
  InfcSessionProxyPrivate* priv;
  InfcSessionProxyClass* proxy_class;

  xmlChar* domain;
  gboolean has_code;
  guint code;
  GError* req_error;
  InfcRequest* request;

  priv = INFC_SESSION_PROXY_PRIVATE(proxy);
  proxy_class = INFC_SESSION_PROXY_GET_CLASS(proxy);

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
  request = infc_request_manager_get_request_by_xml_required(
    priv->request_manager,
    NULL,
    xml,
    error
  );

  if(request == NULL) return FALSE;

  g_assert(proxy_class->translate_error != NULL);

  /* TODO: Add a GError* paramater to translate_error so that an error
   * can be reported if the error could not be translated. */
  req_error = proxy_class->translate_error(
    proxy,
    g_quark_from_string((const gchar*)domain),
    code
  );

  infc_request_manager_fail_request(
    priv->request_manager,
    request,
    req_error
  );

  g_error_free(req_error);

  xmlFree(domain);
  return TRUE;
}

static gboolean
infc_session_proxy_handle_session_close(InfcSessionProxy* proxy,
                                        InfXmlConnection* connection,
                                        xmlNodePtr xml,
                                        GError** error)
{
  InfcSessionProxyPrivate* priv;
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  g_assert(priv->connection != NULL);
  infc_session_proxy_release_connection(proxy);

  /* Do not call inf_session_close so the session can be reused by
   * reconnecting/synchronizing to another host. */

  return TRUE;
}

/*
 * InfNetObject implementation
 */

static void
infc_session_proxy_communication_object_sent(InfCommunicationObject* object,
                                             InfXmlConnection* connection,
                                             xmlNodePtr node)
{
  InfcSessionProxy* proxy;
  InfcSessionProxyPrivate* priv;

  proxy = INFC_SESSION_PROXY(object);
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  /* TODO: Don't forward for messages the proxy issued */

  g_assert(priv->session != NULL);

  inf_communication_object_sent(
    INF_COMMUNICATION_OBJECT(priv->session),
    connection,
    node
  );
}

static void
infc_session_proxy_communication_object_enqueued(InfCommunicationObject* obj,
                                                 InfXmlConnection* connection,
                                                 xmlNodePtr node)
{
  InfcSessionProxy* proxy;
  InfcSessionProxyPrivate* priv;

  proxy = INFC_SESSION_PROXY(obj);
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);

  /* TODO: Don't forward for messages the proxy issued */

  g_assert(priv->session != NULL);

  inf_communication_object_enqueued(
    INF_COMMUNICATION_OBJECT(priv->session),
    connection,
    node
  );
}

static InfCommunicationScope
infc_session_proxy_communication_object_received(InfCommunicationObject* obj,
                                                 InfXmlConnection* connection,
                                                 xmlNodePtr node,
                                                 GError** error)
{
  InfcSessionProxy* proxy;
  InfcSessionProxyPrivate* priv;
  InfcSessionProxyClass* proxy_class;
  InfSessionSyncStatus status;
  GError* local_error;

  InfcRequest* request;
  GError* seq_error;

  proxy = INFC_SESSION_PROXY(obj);
  priv = INFC_SESSION_PROXY_PRIVATE(proxy);
  proxy_class = INFC_SESSION_PROXY_GET_CLASS(proxy);
  status = inf_session_get_synchronization_status(priv->session, connection);
  local_error = NULL;

  g_assert(status != INF_SESSION_SYNC_NONE || priv->connection == connection);
  g_assert(inf_session_get_status(priv->session) != INF_SESSION_CLOSED);

  if(status != INF_SESSION_SYNC_NONE)
  {
    /* Direct delegate during synchronization */
    return inf_communication_object_received(
      INF_COMMUNICATION_OBJECT(priv->session),
      connection,
      node,
      error
    );
  }
  else
  {
    if(strcmp((const char*)node->name, "user-join") == 0)
    {
      infc_session_proxy_handle_user_join(
        proxy,
        connection,
        node,
        &local_error
      );
    }
    else if(strcmp((const char*)node->name, "user-rejoin") == 0)
    {
      infc_session_proxy_handle_user_rejoin(
        proxy,
        connection,
        node,
        &local_error
      );
    }
    else if(strcmp((const char*)node->name, "request-failed") == 0)
    {
      infc_session_proxy_handle_request_failed(
        proxy,
        connection,
        node,
        &local_error
      );
    }
    else if(strcmp((const char*)node->name, "session-close") == 0)
    {
      infc_session_proxy_handle_session_close(
        proxy,
        connection,
        node,
        &local_error
      );
    }
    else
    {
      return inf_communication_object_received(
        INF_COMMUNICATION_OBJECT(priv->session),
        connection,
        node,
        error
      );
    }
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
      /* If the request had a seq set, we cancel the corresponding request
       * because the reply could not be processed. */
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

    g_propagate_error(error, local_error);
  }

  /* Don't forward any of the handled messages */
  return FALSE;
}

/*
 * GType registration.
 */

static void
infc_session_proxy_class_init(gpointer g_class,
                              gpointer class_data)
{
  GObjectClass* object_class;
  InfcSessionProxyClass* proxy_class;

  object_class = G_OBJECT_CLASS(g_class);
  proxy_class = INFC_SESSION_PROXY_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcSessionProxyPrivate));

  object_class->dispose = infc_session_proxy_dispose;
  object_class->set_property = infc_session_proxy_set_property;
  object_class->get_property = infc_session_proxy_get_property;

  proxy_class->translate_error = infc_session_proxy_translate_error_impl;

  g_object_class_install_property(
    object_class,
    PROP_SESSION,
    g_param_spec_object(
      "session",
      "Session",
      "The underlaying session object",
      INF_TYPE_SESSION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SUBSCRIPTION_GROUP,
    g_param_spec_object(
      "subscription-group",
      "Subscription group",
      "The communication group of subscribed connections",
      INF_COMMUNICATION_TYPE_JOINED_GROUP,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CONNECTION,
    g_param_spec_object(
      "connection",
      "Subscription connection",
      "The connection with which the session communicates with the server",
      INF_TYPE_XML_CONNECTION,
      G_PARAM_READABLE
    )
  );
}

static void
infc_session_proxy_communication_object_init(gpointer g_iface,
                                             gpointer iface_data)
{
  InfCommunicationObjectIface* iface;
  iface = (InfCommunicationObjectIface*)g_iface;

  iface->sent = infc_session_proxy_communication_object_sent;
  iface->enqueued = infc_session_proxy_communication_object_enqueued;
  iface->received = infc_session_proxy_communication_object_received;
}

GType
infc_session_proxy_get_type(void)
{
  static GType session_proxy_type = 0;

  if(!session_proxy_type)
  {
    static const GTypeInfo session_proxy_type_info = {
      sizeof(InfcSessionProxyClass),    /* class_size */
      NULL,                             /* base_init */
      NULL,                             /* base_finalize */
      infc_session_proxy_class_init,    /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      sizeof(InfcSessionProxy),         /* instance_size */
      0,                                /* n_preallocs */
      infc_session_proxy_init,          /* instance_init */
      NULL                              /* value_table */
    };

    static const GInterfaceInfo communication_object_info = {
      infc_session_proxy_communication_object_init,
      NULL,
      NULL
    };

    session_proxy_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcSessionProxy",
      &session_proxy_type_info,
      0
    );

    g_type_add_interface_static(
      session_proxy_type,
      INF_COMMUNICATION_TYPE_OBJECT,
      &communication_object_info
    );
  }

  return session_proxy_type;
}

/*
 * Public API.
 */

/**
 * infc_session_proxy_set_connection:
 * @proxy: A #InfcSessionProxy.
 * @group: A #InfCommunicationJoinedGroup of subscribed connections. Ignored
 * if @connection is %NULL.
 * @connection: A #InfXmlConnection.
 * @seq_id: A sequence identifier for @connection. Ignored if @connection
 * is %NULL.
 *
 * Sets the subscription connection for the given session. The subscription
 * connection is the connection through which session requests are transmitted
 * during subscription.
 *
 * The subscription connection might be set even if the session is in
 * SYNCHRONIZING state in which case the session is immediately subscribed
 * after synchronization. Note that no attempt is made to tell the other end
 * about the subscription.
 *
 * @seq_id should be a sequence identifier obtained from the server side. It
 * must be the same number that is used on the corresponding #InfdSessionProxy
 * on the remote side, see infd_session_proxy_subscribe_to(). Normally
 * #InfdDirectory or #InfcBrowser, respectively, take care of passing the
 * correct sequence identifier.
 *
 * When the subscription connection is being closed or replaced (by a
 * subsequent call to this function), all pending requests are dropped and
 * all users are set to be unavailable, but the session will not be closed,
 * so it may be reused by setting another subscription connection. However,
 * the session might not be synchronized again, but it is fully okay to close
 * the session by hand (using inf_session_close) and create a new session
 * that is synchronized.
 **/
void
infc_session_proxy_set_connection(InfcSessionProxy* proxy,
                                  InfCommunicationJoinedGroup* group,
                                  InfXmlConnection* connection,
                                  guint seq_id)
{
  InfcSessionProxyPrivate* priv;
  xmlNodePtr xml;

  g_return_if_fail(INFC_IS_SESSION_PROXY(proxy));
  g_return_if_fail(connection == NULL || INF_IS_XML_CONNECTION(connection));
  g_return_if_fail(
    (group == NULL && connection == NULL) ||
    (INF_COMMUNICATION_IS_JOINED_GROUP(group) && connection != NULL)
  );

  priv = INFC_SESSION_PROXY_PRIVATE(proxy);
  g_return_if_fail(priv->session != NULL);

  g_object_freeze_notify(G_OBJECT(proxy));
  g_object_freeze_notify(G_OBJECT(priv->session));

  if(priv->connection != NULL)
  {
    /* Unsubscribe from running session. Always send the unsubscribe request
     * because synchronizations are not cancelled through this call. */
    xml = xmlNewNode(NULL, (const xmlChar*)"session-unsubscribe");

    inf_communication_group_send_message(
      INF_COMMUNICATION_GROUP(priv->subscription_group),
      priv->connection,
      xml
    );

    /* Note that this would cause a notify on the connection property, but
     * notifications have been freezed until the end of this function call.
     * Same with the subscription-group property of priv->session. */
    infc_session_proxy_release_connection(proxy);
  }

  priv->connection = connection;

  if(connection != NULL)
  {
    priv->connection = connection;
    g_object_ref(G_OBJECT(connection));

    g_signal_connect(
      G_OBJECT(connection),
      "notify::status",
      G_CALLBACK(infc_session_proxy_connection_notify_status_cb),
      proxy
    );

    /* Set new group */
    priv->subscription_group = group;
    g_object_ref(priv->subscription_group);

    g_assert(priv->request_manager == NULL);
    priv->request_manager = infc_request_manager_new(seq_id);
  }

  inf_session_set_subscription_group(
    priv->session,
    INF_COMMUNICATION_GROUP(priv->subscription_group)
  );

  g_object_notify(G_OBJECT(proxy), "connection");
  g_object_notify(G_OBJECT(proxy), "subscription-group");
  g_object_thaw_notify(G_OBJECT(priv->session));
  g_object_thaw_notify(G_OBJECT(proxy));
}

/**
 * infc_session_proxy_join_user:
 * @proxy: A #InfcSessionProxy.
 * @params: Construction properties for the InfUser (or derived) object.
 * @n_params: Number of parameters.
 * @error: Location to store error information.
 *
 * Requests a user join for a user with the given properties (which must not
 * include ID and status since these are initially set by the server).
 *
 * Return Value: A #InfcUserRequest object that may be used to get notified
 * when the request succeeds or fails.
 **/
InfcUserRequest*
infc_session_proxy_join_user(InfcSessionProxy* proxy,
                             const GParameter* params,
                             guint n_params,
                             GError** error)
{
  InfcSessionProxyPrivate* priv;
  InfSessionClass* session_class;
  InfSessionStatus status;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_SESSION_PROXY(proxy), NULL);
  g_return_val_if_fail(params != NULL || n_params == 0, NULL);

  priv = INFC_SESSION_PROXY_PRIVATE(proxy);
  g_return_val_if_fail(priv->session != NULL, NULL);

  session_class = INF_SESSION_GET_CLASS(priv->session);

  /* Make sure we are subscribed */
  g_object_get(G_OBJECT(priv->session), "status", &status, NULL);
  g_return_val_if_fail(status == INF_SESSION_RUNNING, NULL);
  g_return_val_if_fail(priv->connection != NULL, NULL);
  g_return_val_if_fail(priv->request_manager != NULL, NULL);

  /* TODO: Check params locally */

  request = infc_request_manager_add_request(
    priv->request_manager,
    INFC_TYPE_USER_REQUEST,
    "user-join",
    NULL
  );

  xml = infc_session_proxy_request_to_xml(INFC_REQUEST(request));

  g_assert(session_class->set_xml_user_props != NULL);
  session_class->set_xml_user_props(priv->session, params, n_params, xml);

  inf_communication_group_send_message(
    INF_COMMUNICATION_GROUP(priv->subscription_group),
    priv->connection,
    xml
  );

  return INFC_USER_REQUEST(request);
}

/**
 * infc_session_proxy_get_session:
 * @proxy: A #InfcSessionProxy.
 *
 * Returns the session proxied by @proxy, or %NULL if the session has been
 * closed.
 *
 * Return Value: A #InfSession, or %NULL.
 **/
InfSession*
infc_session_proxy_get_session(InfcSessionProxy* proxy)
{
  g_return_val_if_fail(INFC_IS_SESSION_PROXY(proxy), NULL);
  return INFC_SESSION_PROXY_PRIVATE(proxy)->session;
}

/**
 * infc_session_proxy_get_connection:
 * @proxy: A #InfcSessionProxy.
 *
 * Returns the connection that is used to transmit requests to the publisher
 * of the session, or %NULL if the session is not subscribed.
 *
 * Returns: A #InfXmlConnection, or %NULL.
 **/
InfXmlConnection*
infc_session_proxy_get_connection(InfcSessionProxy* proxy)
{
  g_return_val_if_fail(INFC_IS_SESSION_PROXY(proxy), NULL);
  return INFC_SESSION_PROXY_PRIVATE(proxy)->connection;
}

/**
 * infc_session_proxy_get_group:
 * @proxy: A #InfcSessionProxy.
 *
 * Returns the group with all subscribed connections to the session, or %NULL
 * if the session is not subscribed.
 *
 * Returns: A #InfCommunicationJoinedGroup, or %NULL.
 **/
InfCommunicationJoinedGroup*
infc_session_proxy_get_subscription_group(InfcSessionProxy* proxy)
{
  g_return_val_if_fail(INFC_IS_SESSION_PROXY(proxy), NULL);
  return INFC_SESSION_PROXY_PRIVATE(proxy)->subscription_group;
}

/* vim:set et sw=2 ts=2: */
