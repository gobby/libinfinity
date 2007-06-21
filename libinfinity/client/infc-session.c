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

#include <libinfinity/client/infc-session.h>

#include <libinfinity/common/inf-error.h>
#include <libinfinity/common/inf-xml-connection.h>

#include <libxml/xmlsave.h>

#include <string.h>

typedef struct _InfcSessionMessage InfcSessionMessage;
struct _InfcSessionMessage {
  InfcSessionMessageFunc func;
};

typedef struct _InfcSessionPrivate InfcSessionPrivate;
struct _InfcSessionPrivate {
  InfXmlConnection* connection;

  guint seq_counter;
  GSList* requests;
};

#define INFC_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_SESSION, InfcSessionPrivate))

static InfSessionClass* parent_class;

enum {
  PROP_0,

  PROP_CONNECTION
};

/*
 * Session messages.
 */

static InfcSessionMessage*
infc_session_message_new(InfcSessionMessageFunc func)
{
  InfcSessionMessage* message;
  message = g_slice_new(InfcSessionMessage);

  message->func = func;
  return message;
}

static void
infc_session_message_free(InfcSessionMessage* message)
{
  g_slice_free(InfcSessionMessage, message);
}

/*
 * Signal handlers
 */

static void
infc_session_release_connection(InfcSession* session);

static void
infc_session_connection_notify_status_cb(InfXmlConnection* connection,
                                         GParamSpec* pspec,
                                         gpointer user_data)
{
  InfcSession* session;
  InfXmlConnectionStatus status;

  session = INFC_SESSION(user_data);
  g_object_get(G_OBJECT(connection), "status", &status, NULL);

  if(status == INF_XML_CONNECTION_CLOSED ||
     status == INF_XML_CONNECTION_CLOSING)
  {
    /* Reset connection in case of closure */
    infc_session_release_connection(session);
  }
}

/*
 * Helper functions
 */

static void
infc_session_release_connection_foreach_user_func(InfUser* user,
                                                  gpointer user_data)
{
  g_object_set(G_OBJECT(user), "status", INF_USER_UNAVAILABLE, NULL);
}

static void
infc_session_release_connection(InfcSession* session)
{
  InfcSessionPrivate* priv;
  priv = INFC_SESSION_PRIVATE(session);

  g_assert(priv->connection != NULL);

  /* TODO: Emit failed signal with some "cancelled" error? */
  while(priv->requests != NULL)
  {
    g_object_unref(G_OBJECT(priv->requests->data));
    priv->requests = g_slist_delete_link(priv->requests, priv->requests);
  }

  /* Set status of all users to unavailable */
  inf_session_foreach_user(
    INF_SESSION(session),
    infc_session_release_connection_foreach_user_func,
    NULL
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->connection),
    G_CALLBACK(infc_session_connection_notify_status_cb),
    session
  );

  inf_connection_manager_remove_object(
    inf_session_get_connection_manager(INF_SESSION(session)),
    priv->connection,
    INF_NET_OBJECT(session)
  );

  g_object_unref(G_OBJECT(priv->connection));
  priv->connection = NULL;

  g_object_notify(G_OBJECT(session), "connection");
}

/* TODO: These should probably be public, so that they may be used by
 * derived classes */
static InfcRequest*
infc_session_create_request(InfcSession* session,
                            const gchar* name)
{
  InfcSessionPrivate* priv;
  InfcRequest* request;

  priv = INFC_SESSION_PRIVATE(session);

  request = g_object_new(
    INFC_TYPE_REQUEST,
    "seq", ++priv->seq_counter,
    "name", name,
    NULL
  );

  priv->requests = g_slist_prepend(priv->requests, request);
  return request;
}

static xmlNodePtr
infc_session_request_to_xml(InfcRequest* request)
{
  xmlNodePtr xml;
  gchar seq_buffer[16];

  xml = xmlNewNode(NULL, (const xmlChar*)infc_request_get_name(request));
  sprintf(seq_buffer, "%u", infc_request_get_seq(request));

  xmlNewProp(xml, (const xmlChar*)"seq", (const xmlChar*)seq_buffer);
  return xml;
}

static void
infc_session_fail_request(InfcSession* session,
                          xmlNodePtr xml,
                          GError* error)
{
  InfcSessionPrivate* priv;
  xmlChar* seq_attr;
  guint seq;

  GSList* item;
  InfcRequest* request;

  priv = INFC_SESSION_PRIVATE(session);
  seq_attr = xmlGetProp(xml, (const xmlChar*)"seq");

  if(seq_attr != NULL)
  {
    seq = strtoul((const char*)seq_attr, NULL, 0);
    xmlFree(seq_attr);

    for(item = priv->requests; item != NULL; item = item->next)
    {
      request = INFC_REQUEST(item->data);
      if(infc_request_get_seq(request) == seq)
      {
        infc_request_failed(request, error);
        priv->requests = g_slist_remove(priv->requests, request);
        break;
      }
    }
  }
}

static void
infc_session_succeed_request(InfcSession* session,
                             xmlNodePtr xml,
                             gpointer data)
{
  InfcSessionPrivate* priv;
  xmlChar* seq_attr;
  guint seq;

  GSList* item;
  InfcRequest* request;

  priv = INFC_SESSION_PRIVATE(session);
  seq_attr = xmlGetProp(xml, (const xmlChar*)"seq");

  if(seq_attr != NULL)
  {
    seq = strtoul((const char*)seq_attr, NULL, 0);
    xmlFree(seq_attr);

    for(item = priv->requests; item != NULL; item = item->next)
    {
      request = INFC_REQUEST(item->data);
      if(infc_request_get_seq(request) == seq)
      {
        infc_request_succeeded(request, data);
        priv->requests = g_slist_remove(priv->requests, request);
        break;
      }
    }
  }
}

/*
 * GObject overrides.
 */

static void
infc_session_init(GTypeInstance* instance,
                  gpointer g_class)
{
  InfcSession* session;
  InfcSessionPrivate* priv;

  session = INFC_SESSION(instance);
  priv = INFC_SESSION_PRIVATE(session);

  priv->connection = NULL;
}

static void
infc_session_set_property(GObject* object,
                          guint prop_id,
                          const GValue* value,
                          GParamSpec* pspec)
{
  InfcSession* session;
  InfcSessionPrivate* priv;

  session = INFC_SESSION(object);
  priv = INFC_SESSION_PRIVATE(session);

  switch(prop_id)
  {
    /* TODO: Make the connection property read/write, but this way one
     * cannot set the connection identifier by setting the property. */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_session_get_property(GObject* object,
                          guint prop_id,
                          GValue* value,
                          GParamSpec* pspec)
{
  InfcSession* session;
  InfcSessionPrivate* priv;

  session = INFC_SESSION(object);
  priv = INFC_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_CONNECTION:
    g_value_set_object(value, G_OBJECT(priv->connection));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_session_dispose(GObject* object)
{
  InfcSession* session;
  InfcSessionPrivate* priv;

  session = INFC_SESSION(object);
  priv = INFC_SESSION_PRIVATE(session);

  /* The base class will call close() in which we remove the connection
   * and several allocated resources. */

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

/*
 * vfunc and default signal handler implementations.
 */

static void
infc_session_process_xml_run_impl(InfSession* session,
                                  InfXmlConnection* connection,
                                  xmlNodePtr xml)
{
  InfcSessionClass* sessionc_class;
  InfSessionSyncStatus status;
  InfcSessionMessage* message;
  GError* error;
  gboolean result;
  xmlBufferPtr buffer;
  xmlSaveCtxtPtr ctx;
  GError* seq_error;

  sessionc_class = INFC_SESSION_GET_CLASS(session);
  status = inf_session_get_synchronization_status(session, connection);
  error = NULL;

  if(status != INF_SESSION_SYNC_NONE)
  {
    result = FALSE;
    g_set_error(
      &error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_SYNCHRONIZING,
      "%s",
      inf_request_strerror(INF_REQUEST_ERROR_SYNCHRONIZING)
    );
  }
  else
  {
    message = g_hash_table_lookup(
      sessionc_class->message_table,
      (const gchar*)xml->name
    );

    if(message == NULL)
    {
      result = FALSE;
      g_set_error(
        &error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_UNEXPECTED_MESSAGE,
        "Message '%s' not understood",
        (const gchar*)xml->name
      );
    }
    else
    {
      result = message->func(INFC_SESSION(session), connection, xml, &error);
    }
  }

  if(result == FALSE && error != NULL)
  {
    buffer = xmlBufferCreate();

    /* TODO: Use the locale's encoding? */
    ctx = xmlSaveToBuffer(buffer, "UTF-8", XML_SAVE_FORMAT);
    xmlSaveTree(ctx, xml);
    xmlSaveClose(ctx);

    g_warning(
      "Received bad XML request: %s\n\nThe request could not be "
      "processed, thus the session is no longer guaranteed to be in "
      "a consistent state. Subsequent requests might therefore fail "
      "as well. The failed request was:\n\n%s",
      error->message,
      (const gchar*)xmlBufferContent(buffer)
    );
 
    xmlBufferFree(buffer);

    /* If the request had a seq set, we cancel the corresponding request
     * because the reply could not be processed. */
    seq_error = NULL;
    g_set_error(
      &seq_error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_REPLY_UNPROCESSED,
      "Server reply could not be processed: %s",
      error->message
    );

    infc_session_fail_request(INFC_SESSION(session), xml, seq_error);
    g_error_free(error);
  }

  if(parent_class->process_xml_run != NULL)
    parent_class->process_xml_run(session, connection, xml);
}

static void
infc_session_close_impl(InfSession* session)
{
  InfSessionClass* session_class;
  InfcSessionPrivate* priv;
  InfSessionSyncStatus status;
  xmlNodePtr xml;

  session_class = INF_SESSION_GET_CLASS(session);
  priv = INFC_SESSION_PRIVATE(session);

  if(priv->connection != NULL)
  {
    status = inf_session_get_synchronization_status(
      session,
      priv->connection
    );

    /* If synchronization is still in progress, the close implementation of
     * the base class will cancel the synchronization in which case we do
     * not need to send an extra session-unsubscribe message. */
    
    /* However, in case we are in AWAITING_ACK status we send session
     * unsubscribe because we cannot cancel the synchronization anymore but
     * the server will go into RUNNING state before receiving this message. */
    if(status != INF_SESSION_SYNC_IN_PROGRESS)
    {
      xml = xmlNewNode(NULL, (const xmlChar*)"session-unsubscribe");

      inf_connection_manager_send(
        inf_session_get_connection_manager(session),
        priv->connection,
        INF_NET_OBJECT(session),
        xml
      );
    }

    infc_session_release_connection(INFC_SESSION(session));
  }

  if(parent_class->close != NULL)
    parent_class->close(session);
}

static void
infc_session_synchronization_complete_impl(InfSession* session,
                                           InfXmlConnection* connection)
{
  InfcSessionPrivate* priv;
  InfSessionStatus status;

  priv = INFC_SESSION_PRIVATE(session);
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
      g_assert(priv->connection == connection);
    }
  }

  if(parent_class->synchronization_complete != NULL)
    parent_class->synchronization_complete(session, connection);
}

static void
infc_session_synchronization_failed_impl(InfSession* session,
                                         InfXmlConnection* connection,
                                         const GError* error)
{
  InfSessionStatus status;

  g_object_get(G_OBJECT(session), "status", &status, NULL);

  switch(status)
  {
  case INF_SESSION_SYNCHRONIZING:
    /* When the synchronization failed in synchronizing state, the base
     * class will close the session anyway which is where we do the cleanup
     * in this case. */
    break;
  case INF_SESSION_RUNNING:
    /* We do not need to send an explicit session-unsubscribe, because the
     * failed synchronization should already let the host know that
     * subscription makes no sense anymore. */
    infc_session_release_connection(INFC_SESSION(session));
    break;
  case INF_SESSION_CLOSED:
  default:
    g_assert_not_reached();
    break;
  }

  if(parent_class->synchronization_failed != NULL)
    parent_class->synchronization_failed(session, connection, error);
}

static GError*
infc_session_translate_error_impl(InfcSession* session,
                                  GQuark domain,
                                  guint code)
{
  GError* error;
  const gchar* error_msg;

  if(domain == inf_request_error_quark())
    error_msg = inf_request_strerror(code);
  else if(domain == inf_user_join_error_quark())
    error_msg = inf_user_join_strerror(code);
  else if(domain == inf_user_leave_error_quark())
    error_msg = inf_user_leave_strerror(code);
  else
    error_msg = NULL;

  error = NULL;
  if(error_msg != NULL)
  {
    g_set_error(&error, domain, code, "%s", error_msg);
  }
  else
  {
    g_set_error(
      &error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_UNKNOWN_DOMAIN,
      "Error comes from unknown error domain '%s' (code %u)",
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
infc_session_handle_user_join(InfcSession* session,
                              InfXmlConnection* connection,
                              xmlNodePtr xml,
                              GError** error)
{
  InfSessionClass* session_class;
  GArray* array;
  InfUser* user;
  guint i;

  session_class = INF_SESSION_CLASS(session);

  array = session_class->get_xml_user_props(
    INF_SESSION(session),
    connection,
    xml
  );

  /* This validates properties */
  user = inf_session_add_user(
    INF_SESSION(session),
    (const GParameter*)array->data,
    array->len,
    error
  );

  for(i = 0; i < array->len; ++ i)
    g_value_unset(&g_array_index(array, GParameter, i).value);
  g_array_free(array, TRUE);

  if(user != NULL)
  {
    infc_session_succeed_request(session, xml, user);
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

static gboolean
infc_session_handle_user_rejoin(InfcSession* session,
                                InfXmlConnection* connection,
                                xmlNodePtr xml,
                                GError** error)
{
  InfSessionClass* session_class;
  GArray* array;
  InfUser* user;
  const GParameter* idparam;
  GParameter* param;
  guint id;
  gboolean result;
  guint i;

  session_class = INF_SESSION_CLASS(session);

  array = session_class->get_xml_user_props(
    INF_SESSION(session),
    connection,
    xml
  );

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
      inf_user_join_error_quark(),
      INF_USER_JOIN_ERROR_ID_NOT_PRESENT,
      "%s",
      inf_user_join_strerror(INF_USER_JOIN_ERROR_ID_NOT_PRESENT)
    );

    goto error;
  }

  id = g_value_get_uint(&idparam->value);
  user = inf_session_lookup_user_by_id(INF_SESSION(session), id);

  if(user == NULL)
  {
    g_set_error(
      error,
      inf_user_join_error_quark(),
      INF_USER_JOIN_ERROR_NO_SUCH_USER,
      "%s",
      inf_user_join_strerror(INF_USER_JOIN_ERROR_NO_SUCH_USER)
    );

    goto error;
  }

  result = session_class->validate_user_props(
    INF_SESSION(session),
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
   * status property? Require the status property being set on a rejoin? */

  g_object_thaw_notify(G_OBJECT(user));
  for(i = 0; i < array->len; ++ i)
    g_value_unset(&g_array_index(array, GParameter, i).value);
  g_array_free(array, TRUE);

  return TRUE;

error:
  for(i = 0; i < array->len; ++ i)
    g_value_unset(&g_array_index(array, GParameter, i).value);
  g_array_free(array, TRUE);
  return FALSE;
}

static gboolean
infc_session_handle_request_failed(InfcSession* session,
                                   InfXmlConnection* connection,
                                   xmlNodePtr xml,
                                   GError** error)
{
  InfcSessionPrivate* priv;
  InfcSessionClass* sessionc_class;

  xmlChar* domain_attr;
  xmlChar* code_attr;
  guint code;
  GError* req_error;

  priv = INFC_SESSION_PRIVATE(session);
  sessionc_class = INFC_SESSION_GET_CLASS(session);

  domain_attr = xmlGetProp(xml, (const xmlChar*)"domain");
  code_attr = xmlGetProp(xml, (const xmlChar*)"code");
  req_error = NULL;

  if(domain_attr == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_DOMAIN_MISSING,
      "%s",
      inf_request_strerror(INF_REQUEST_ERROR_DOMAIN_MISSING)
    );

    if(code_attr != NULL) xmlFree(code_attr);
    return FALSE;
  }
  else if(code_attr  == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_CODE_MISSING,
      "%s",
      inf_request_strerror(INF_REQUEST_ERROR_CODE_MISSING)
    );

    if(domain_attr != NULL) xmlFree(domain_attr);
    return FALSE;
  }
  else
  {
    code = strtoul((gchar*)code_attr, NULL, 0);

    g_assert(sessionc_class->translate_error != NULL);

    req_error = sessionc_class->translate_error(
      session,
      g_quark_from_string((gchar*)domain_attr),
      code
    );

    infc_session_fail_request(session, xml, req_error);
    g_error_free(req_error);

    xmlFree(code_attr);
    xmlFree(domain_attr);
    return TRUE;
  }
}

static gboolean
infc_session_handle_user_leave(InfcSession* session,
                               InfXmlConnection* connection,
                               xmlNodePtr xml,
                               GError** error)
{
  xmlChar* id_attr;
  guint id;
  InfUser* user;

  id_attr = xmlGetProp(xml, (const xmlChar*)"id");
  if(id_attr == NULL)
  {
    g_set_error(
      error,
      inf_user_leave_error_quark(),
      INF_USER_LEAVE_ERROR_ID_NOT_PRESENT,
      "%s",
      inf_user_leave_strerror(INF_USER_LEAVE_ERROR_ID_NOT_PRESENT)
    );

    return FALSE;
  }

  id = strtoul((const gchar*)id_attr, NULL, 0);
  xmlFree(id_attr);

  user = inf_session_lookup_user_by_id(INF_SESSION(session), id);
  if(user == NULL)
  {
    g_set_error(
      error,
      inf_user_leave_error_quark(),
      INF_USER_LEAVE_ERROR_NO_SUCH_USER,
      "%s",
      inf_user_leave_strerror(INF_USER_LEAVE_ERROR_NO_SUCH_USER)
    );

    return FALSE;
  }

  /* Complete request, if any */
  infc_session_succeed_request(session, xml, user);

  /* Do not remove from session to recognize the user on rejoin */
  g_object_set(G_OBJECT(user), "status", INF_USER_UNAVAILABLE, NULL);
  return TRUE;
}

static gboolean
infc_session_handle_session_close(InfcSession* session,
                                  InfXmlConnection* connection,
                                  xmlNodePtr xml,
                                  GError** error)
{
  InfcSessionPrivate* priv;
  priv = INFC_SESSION_PRIVATE(session);

  g_assert(priv->connection != NULL);
  infc_session_release_connection(session);

  /* Do not call inf_session_close so the session can be reused by
   * reconnecting/synchronizing to another host. */

  return TRUE;
}

/*
 * GType registration.
 */

static void
infc_session_class_init(gpointer g_class,
                        gpointer class_data)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;
  InfcSessionClass* sessionc_class;

  object_class = G_OBJECT_CLASS(g_class);
  session_class = INF_SESSION_CLASS(g_class);

  parent_class = INF_SESSION_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcSessionPrivate));

  object_class->dispose = infc_session_dispose;
  object_class->set_property = infc_session_set_property;
  object_class->get_property = infc_session_get_property;

  session_class->process_xml_run = infc_session_process_xml_run_impl;
  session_class->close = infc_session_close_impl;

  session_class->synchronization_complete =
    infc_session_synchronization_complete_impl;
  session_class->synchronization_failed =
    infc_session_synchronization_failed_impl;

  sessionc_class->translate_error = infc_session_translate_error_impl;

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

  sessionc_class->message_table = g_hash_table_new_full(
    g_str_hash,
    g_str_equal,
    NULL,
    (GDestroyNotify)infc_session_message_free
  );

  infc_session_class_register_message(
    sessionc_class,
    "user-join",
    infc_session_handle_user_join
  );

  infc_session_class_register_message(
    sessionc_class,
    "user-rejoin",
    infc_session_handle_user_rejoin
  );

  infc_session_class_register_message(
    sessionc_class,
    "user-leave",
    infc_session_handle_user_leave
  );

  infc_session_class_register_message(
    sessionc_class,
    "request-failed",
    infc_session_handle_request_failed
  );

  infc_session_class_register_message(
    sessionc_class,
    "session-close",
    infc_session_handle_session_close
  );
}

static void
infc_session_class_finalize(gpointer g_class,
                            gpointer class_data)
{
  InfcSessionClass* session_class;
  session_class = INFC_SESSION_CLASS(g_class);

  g_hash_table_destroy(session_class->message_table);
}

GType
infc_session_get_type(void)
{
  static GType session_type = 0;

  if(!session_type)
  {
    static const GTypeInfo session_type_info = {
      sizeof(InfcSessionClass),    /* class_size */
      NULL,                        /* base_init */
      NULL,                        /* base_finalize */
      infc_session_class_init,     /* class_init */
      infc_session_class_finalize, /* class_finalize */
      NULL,                        /* class_data */
      sizeof(InfcSession),         /* instance_size */
      0,                           /* n_preallocs */
      infc_session_init,           /* instance_init */
      NULL                         /* value_table */
    };

    session_type = g_type_register_static(
      INF_TYPE_SESSION,
      "InfcSession",
      &session_type_info,
      0
    );
  }

  return session_type;
}

/*
 * Public API.
 */

/** infc_session_class_register_message:
 *
 * @session_class: The class for which to register a message.
 * @message: The message to register.
 * @func: The function to be called when @message is received.
 *
 * Registers a message for the given session class. Whenever a XML request
 * with the given message is received, the given function will be called.
 *
 * Return Value: Whether the registration was successful.
 **/
gboolean
infc_session_class_register_message(InfcSessionClass* session_class,
                                    const gchar* message,
                                    InfcSessionMessageFunc func)
{
  g_return_val_if_fail(INFC_IS_SESSION_CLASS(session_class), FALSE);
  g_return_val_if_fail(message != NULL, FALSE);
  g_return_val_if_fail(func != NULL, FALSE);

  if(g_hash_table_lookup(session_class->message_table, message) != NULL)
    return FALSE;

  g_hash_table_insert(
    session_class->message_table,
    (gpointer)message,
    infc_session_message_new(func)
  );

  return TRUE;
}

/** infc_session_set_connection:
 *
 * @session: A #InfcSession.
 * @connection: A #InfXmlConnection.
 * @identifier: Identifier for the subscription to use. Ignored when
 * @connection is %NULL.
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
 * When the subscription connection is being closed or replaced (by a
 * subsequent call to this function), all pending requests are dropped and
 * all users are set to be unavailable, but the session will not be closed,
 * so it may be reused by setting another subscription connection. Howover,
 * the session might not be synchronized again, but it is fully okay to close
 * the session by hand (using inf_session_close) and create a new session
 * that is synchronized.
 **/
void
infc_session_set_connection(InfcSession* session,
                            InfXmlConnection* connection,
                            const gchar* identifier)
{
  InfcSessionPrivate* priv;
  xmlNodePtr xml;

  priv = INFC_SESSION_PRIVATE(session);
  g_object_freeze_notify(G_OBJECT(session));

  if(priv->connection != NULL)
  {
    /* Unsubscribe from running session. Always send the unsubscribe request
     * because synchronizations are not cancelled through this call. */
    xml = xmlNewNode(NULL, (const xmlChar*)"session-unsubscribe");

    inf_connection_manager_send(
      inf_session_get_connection_manager(INF_SESSION(session)),
      priv->connection,
      INF_NET_OBJECT(session),
      xml
    );

    /* Note that this would cause a notify on the connection property, but
     * notifications have been freezed until the end of this function call. */
    infc_session_release_connection(INFC_SESSION(session));
  }

  priv->connection = connection;

  if(connection != NULL)
  {
    priv->connection = connection;
    g_object_ref(G_OBJECT(connection));

    g_signal_connect(
      G_OBJECT(connection),
      "notify::status",
      G_CALLBACK(infc_session_connection_notify_status_cb),
      session
    );

    inf_connection_manager_add_object(
      inf_session_get_connection_manager(INF_SESSION(session)),
      connection,
      INF_NET_OBJECT(session),
      identifier
    );
  }

  g_object_notify(G_OBJECT(session), "connection");
  g_object_thaw_notify(G_OBJECT(session));
}

/** infc_session_join_user:
 *
 * @session: A #InfcSession.
 * @params: Construction properties for the InfUser (or derived) object.
 * @n_params: Number of parameters.
 * @error: Location to store error information.
 *
 * Requests a user join for a user with the given properties (which must not
 * include ID and status since these are initially set by the server).
 *
 * Return Value: A #InfcRequest object that may be used to get notified when
 * the request succeeds or fails.
 **/
InfcRequest*
infc_session_join_user(InfcSession* session,
                       const GParameter* params,
                       guint n_params,
                       GError** error)
{
  InfcSessionPrivate* priv;
  InfSessionClass* session_class;
  InfSessionStatus status;
  InfcRequest* request;
  xmlNodePtr xml;

  g_return_val_if_fail(INFC_IS_SESSION(session), NULL);
  g_return_val_if_fail(params != NULL || n_params == 0, NULL);

  priv = INFC_SESSION_PRIVATE(session);
  session_class = INF_SESSION_GET_CLASS(session);

  /* Make sure we are subscribed */
  g_object_get(G_OBJECT(session), "status", &status, NULL);
  g_return_val_if_fail(status == INF_SESSION_RUNNING, NULL);
  g_return_val_if_fail(priv->connection != NULL, NULL);

  /* TODO: Check params locally */

  request = infc_session_create_request(session, "user-join");
  xml = infc_session_request_to_xml(request);

  g_assert(session_class->set_xml_user_props != NULL);
  session_class->set_xml_user_props(
    INF_SESSION(session),
    params,
    n_params,
    xml
  );

  inf_connection_manager_send(
    inf_session_get_connection_manager(INF_SESSION(session)),
    priv->connection,
    INF_NET_OBJECT(session),
    xml
  );

  return request;
}

/** infc_session_leave_user:
 *
 * @session: A #InfcSession.
 * @user: A #InfUser with status %INF_USER_AVAILABLE.
 * @error: Location to store error information.
 *
 * Requests a user leave for the given user which must be available and
 * which must have been joined via this session.
 *
 * Return Value: A #InfcRequest object that may be used to get notified when
 * the request succeeds or fails. 
 **/
InfcRequest*
infc_session_leave_user(InfcSession* session,
                        InfUser* user,
                        GError** error)
{
  InfcSessionPrivate* priv;
  InfSessionStatus status;
  InfcRequest* request;
  xmlNodePtr xml;
  gchar id_buf[16];

  g_return_val_if_fail(INFC_IS_SESSION(session), NULL);
  g_return_val_if_fail(INF_IS_USER(user), NULL);
  priv = INFC_SESSION_PRIVATE(session);

  /* Make sure we are subscribed */
  g_object_get(G_OBJECT(session), "status", &status, NULL);
  g_return_val_if_fail(status == INF_SESSION_RUNNING, NULL);
  g_return_val_if_fail(priv->connection != NULL, NULL);

  /* TODO: Check user locally */

  request = infc_session_create_request(session, "user-leave");
  xml = infc_session_request_to_xml(request);

  sprintf(id_buf, "%u", inf_user_get_id(user));
  xmlNewProp(xml, (const xmlChar*)"id", (const xmlChar*)id_buf);

  inf_connection_manager_send(
    inf_session_get_connection_manager(INF_SESSION(session)),
    priv->connection,
    INF_NET_OBJECT(session),
    xml
  );

  return request;
}
