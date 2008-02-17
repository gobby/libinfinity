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

/* TODO: Noop requests for local users after not having sent something for some time */
/* TODO: warning if no update from a particular non-local user for some time */
/* TODO: Use InfUserTable's add-local-user and remove-local-user signals */

#include <libinfinity/adopted/inf-adopted-session.h>
#include <libinfinity/common/inf-xml-util.h>

#include <string.h>

typedef struct _InfAdoptedSessionToXmlSyncForeachData
  InfAdoptedSessionToXmlSyncForeachData;
struct _InfAdoptedSessionToXmlSyncForeachData {
  InfAdoptedSession* session;
  xmlNodePtr parent_xml;
};

typedef struct _InfAdoptedSessionLocalUser InfAdoptedSessionLocalUser;
struct _InfAdoptedSessionLocalUser {
  InfAdoptedUser* user;
  InfAdoptedStateVector* last_send_vector;
};

typedef struct _InfAdoptedSessionPrivate InfAdoptedSessionPrivate;
struct _InfAdoptedSessionPrivate {
  InfIo* io;

  InfAdoptedAlgorithm* algorithm;
  GSList* local_users; /* having zero or one item in 99.999% of all cases */
};

enum {
  PROP_0,

  /* construct only */
  PROP_IO,

  /* read only */
  PROP_ALGORITHM
};

#define INF_ADOPTED_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_SESSION, InfAdoptedSessionPrivate))

static InfSessionClass* parent_class;
static GQuark inf_adopted_session_error_quark;

/*
 * Utility functions.
 */

InfAdoptedSessionLocalUser*
inf_adopted_session_lookup_local_user(InfAdoptedSession* session,
                                      InfAdoptedUser* user)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedSessionLocalUser* local;
  GSList* item;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = (InfAdoptedSessionLocalUser*)item->data;
    if(local->user == user)
      return local;
  }

  return NULL;
}

/* Checks whether request can be inserted into log */
/* TODO: Move into request log class? */
static gboolean
inf_adopted_session_validate_request(InfAdoptedRequestLog* log,
                                     InfAdoptedRequest* request,
                                     GError** error)
{
  InfAdoptedStateVector* vector;
  guint user_id;
  guint n;

  guint begin;
  guint end;

  vector = inf_adopted_request_get_vector(request);
  user_id = inf_adopted_request_get_user_id(request);
  n = inf_adopted_state_vector_get(vector, user_id);
  
  begin = inf_adopted_request_log_get_begin(log);
  end = inf_adopted_request_log_get_end(log);

  /* TODO: Actually, begin != end is only relevant for the first request
   * in request log. */
  if(end != n && begin != end)
  {
    g_set_error(
      error,
      inf_adopted_session_error_quark,
      INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST,
      "Request has index '%u', but index '%u' was expected",
      n,
      inf_adopted_request_log_get_end(log)
    );

    return FALSE;
  }
  else
  {
    switch(inf_adopted_request_get_request_type(request))
    {
    case INF_ADOPTED_REQUEST_DO:
      /* Nothing to check for */
      return TRUE;
    case INF_ADOPTED_REQUEST_UNDO:
      if(inf_adopted_request_log_next_undo(log) == NULL)
      {
        g_set_error(
          error,
          inf_adopted_session_error_quark,
          INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST,
          "Undo received, but no previous request found" /* powered by pkern */
        );

        return FALSE;
      }
      else
      {
        return TRUE;
      }
    case INF_ADOPTED_REQUEST_REDO:
      if(inf_adopted_request_log_next_redo(log) == NULL)
      {
        g_set_error(
          error,
          inf_adopted_session_error_quark,
          INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST,
          "Redo received, but no previous request found" /* powered by pkern */
        );

        return FALSE;
      }
      else
      {
        return TRUE;
      }
    default:
      g_assert_not_reached();
      return FALSE;
    }
  }
}

static void
inf_adopted_session_request_to_xml(InfAdoptedSession* session,
                                   InfAdoptedRequest* request,
                                   xmlNodePtr xml,
                                   gboolean for_sync)
{
  InfAdoptedSessionClass* session_class;
  InfAdoptedSessionPrivate* priv;
  InfAdoptedStateVector* vector;
  InfAdoptedSessionLocalUser* local;
  InfUserTable* user_table;
  InfUser* user;
  guint user_id;
  gchar* vec_str;

  InfAdoptedOperation* operation;
  xmlNodePtr op_xml;

  session_class = INF_ADOPTED_SESSION_GET_CLASS(session);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  user_table = inf_session_get_user_table(INF_SESSION(session));

  g_assert(session_class->operation_to_xml != NULL);

  vector = inf_adopted_request_get_vector(request);
  user_id = inf_adopted_request_get_user_id(request);

  inf_xml_util_set_attribute_uint(xml, "user", user_id);

  if(for_sync)
  {
    /* TODO: diff to previous request, if any */
    vec_str = inf_adopted_state_vector_to_string(vector);
  }
  else
  {
    user = inf_user_table_lookup_user_by_id(user_table, user_id);
    g_assert(user != NULL);

    local = inf_adopted_session_lookup_local_user(
      session,
      INF_ADOPTED_USER(user)
    );

    g_assert(local != NULL);

    vec_str = inf_adopted_state_vector_to_string_diff(
      vector,
      local->last_send_vector
    );

    inf_adopted_state_vector_free(local->last_send_vector);
    local->last_send_vector = inf_adopted_state_vector_copy(vector);

    /* Add this request to last send vector if it increases vector time
     * (-> affects buffer). */
    if(inf_adopted_request_affects_buffer(request) == TRUE)
      inf_adopted_state_vector_add(local->last_send_vector, user_id, 1);
  }

  inf_xml_util_set_attribute(xml, "time", vec_str);
  g_free(vec_str);

  switch(inf_adopted_request_get_request_type(request))
  {
  case INF_ADOPTED_REQUEST_DO:
    operation = inf_adopted_request_get_operation(request);

    /*inf_xml_util_set_attribute(xml, "type", "do");*/
    op_xml = session_class->operation_to_xml(session, operation, for_sync);
    xmlAddChild(xml, op_xml);
    break;
  case INF_ADOPTED_REQUEST_UNDO:
    op_xml = xmlNewNode(NULL, (const xmlChar*)"undo");
    xmlAddChild(xml, op_xml);
    break;
  case INF_ADOPTED_REQUEST_REDO:
    op_xml = xmlNewNode(NULL, (const xmlChar*)"redo");
    xmlAddChild(xml, op_xml);
    break;
  default:
    g_assert_not_reached();
    break;
  }
}

static InfAdoptedRequest*
inf_adopted_session_xml_to_request(InfAdoptedSession* session,
                                   xmlNodePtr xml,
                                   gboolean for_sync,
                                   GError** error)
{
  InfAdoptedSessionClass* session_class;
  InfAdoptedRequestType type;
  InfAdoptedStateVector* vector;
  InfAdoptedOperation* operation;
  InfAdoptedRequest* request;
  InfUserTable* user_table;
  InfUser* user;
  guint user_id;
  xmlNodePtr child;
  xmlChar* attr;

  session_class = INF_ADOPTED_SESSION_GET_CLASS(session);
  g_assert(session_class->xml_to_operation != NULL);

  user_table = inf_session_get_user_table(INF_SESSION(session));

  if(!inf_xml_util_get_attribute_uint_required(xml, "user", &user_id, error))
    return FALSE;

  user = inf_user_table_lookup_user_by_id(user_table, user_id);

  if(user == NULL)
  {
    g_set_error(
      error,
      inf_adopted_session_error_quark,
      INF_ADOPTED_SESSION_ERROR_NO_SUCH_USER,
      "No such user with user ID '%u'",
      user_id
    );

    return NULL;
  }

  g_assert(INF_ADOPTED_IS_USER(user));

  attr = inf_xml_util_get_attribute_required(xml, "time", error);
  if(attr == NULL) return NULL;

  if(for_sync == TRUE)
  {
    /* TODO: diff to previous request, if any? */
    vector = inf_adopted_state_vector_from_string((const gchar*)attr, error);
    xmlFree(attr);

    if(vector == NULL) return NULL;
  }
  else
  {
    vector = inf_adopted_state_vector_from_string_diff(
      (const gchar*)attr,
      inf_adopted_user_get_vector(INF_ADOPTED_USER(user)),
      error
     );

    xmlFree(attr);

    if(vector == NULL) return NULL;
  }

  /* Get first child element */
  child = xml->children;
  while(child != NULL && child->type != XML_ELEMENT_NODE)
    child = child->next;

  if(child == NULL)
  {
    g_set_error(
      error,
      inf_adopted_session_error_quark,
      INF_ADOPTED_SESSION_ERROR_MISSING_OPERATION,
      "Operation for request request missing"
    );

    inf_adopted_state_vector_free(vector);
    return NULL;
  }

  if(strcmp((const char*)child->name, "undo") == 0)
  {
    type = INF_ADOPTED_REQUEST_UNDO;
  }
  else if(strcmp((const char*)child->name, "redo") == 0)
  {
    type = INF_ADOPTED_REQUEST_REDO;
  }
  else
  {
    type = INF_ADOPTED_REQUEST_DO;

    operation = session_class->xml_to_operation(
      session,
      INF_ADOPTED_USER(user),
      child,
      for_sync,
      error
    );

    if(operation == NULL)
    {
      inf_adopted_state_vector_free(vector);
      return NULL;
    }
  }

  switch(type)
  {
  case INF_ADOPTED_REQUEST_DO:
    request = inf_adopted_request_new_do(vector, user_id, operation);
    g_object_unref(G_OBJECT(operation));
    break;
  case INF_ADOPTED_REQUEST_UNDO:
    request = inf_adopted_request_new_undo(vector, user_id);
    break;
  case INF_ADOPTED_REQUEST_REDO:
    request = inf_adopted_request_new_redo(vector, user_id);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  inf_adopted_state_vector_free(vector);
  return request;
}

/*
 * Signal handlers.
 */

static void
inf_adopted_session_user_notify_flags_cb(GObject* object,
                                         GParamSpec* pspec,
                                         gpointer user_data)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;
  InfAdoptedSessionLocalUser* local;

  session = INF_ADOPTED_SESSION(user_data);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  g_assert(priv->algorithm != NULL);

  local = inf_adopted_session_lookup_local_user(
    session,
    INF_ADOPTED_USER(object)
  );

  if( (inf_user_get_flags(INF_USER(object)) & INF_USER_LOCAL) != 0)
  {
    if(local == NULL)
    {
      local = g_slice_new(InfAdoptedSessionLocalUser);
      local->user = INF_ADOPTED_USER(object);

      /* TODO: This is a rather bad hack: On session join (the only place
       * where the INF_USER_LOCAL flag can be turned on), the vector property
       * of user is still the one set during the request and thus the one
       * that all others have. Therefore, we need to create a diff against
       * that one for the first request we make. */
      local->last_send_vector = inf_adopted_state_vector_copy(
        inf_adopted_user_get_vector(INF_ADOPTED_USER(object))
      );

      /* Set current vector for local user, this is kept up-to-date by
       * InfAdoptedAlgorithm. TODO: Also do this in InfAdoptedAlgorithm? */
      inf_adopted_user_set_vector(
        INF_ADOPTED_USER(object),
        inf_adopted_state_vector_copy(
          inf_adopted_algorithm_get_current(priv->algorithm)
        )
      );

      priv->local_users = g_slist_prepend(priv->local_users, local);
    }
  }
  else
  {
    if(local != NULL)
    {
      inf_adopted_state_vector_free(local->last_send_vector);
      priv->local_users = g_slist_remove(priv->local_users, local);
      g_slice_free(InfAdoptedSessionLocalUser, local);
    }
  }
}

static void
inf_adopted_session_add_user_cb(InfUserTable* user_table,
                                InfUser* user,
                                gpointer user_data)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;
  InfSessionStatus status;
  InfAdoptedSessionLocalUser* local;

  g_assert(INF_ADOPTED_IS_USER(user));

  session = INF_ADOPTED_SESSION(user_data);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  g_object_get(G_OBJECT(session), "status", &status, NULL);

  switch(status)
  {
  case INF_SESSION_SYNCHRONIZING:
    /* User will be added to algorithm when synchronization is complete,
     * algorithm does not exist yet */
    g_assert(priv->algorithm == NULL);
    /* Cannot be local while synchronizing */
    g_assert( (inf_user_get_flags(user) & INF_USER_LOCAL) == 0);
    break;
  case INF_SESSION_RUNNING:
    g_assert(priv->algorithm != NULL);

    if( (inf_user_get_flags(user) & INF_USER_LOCAL) != 0)
    {
      local = g_slice_new(InfAdoptedSessionLocalUser);
      local->user = INF_ADOPTED_USER(user);

      /* TODO: This is the same hack as in
       * inf_adopted_session_user_notify_flags_cb(). */
      local->last_send_vector = inf_adopted_state_vector_copy(
        inf_adopted_user_get_vector(INF_ADOPTED_USER(user))
      );

      /* Set current vector for local user, this is kept up-to-date by
       * InfAdoptedAlgorithm. TODO: Also do this in InfAdoptedAlgorithm? */
      inf_adopted_user_set_vector(
        INF_ADOPTED_USER(user),
        inf_adopted_state_vector_copy(
          inf_adopted_algorithm_get_current(priv->algorithm)
        )
      );

      priv->local_users = g_slist_prepend(priv->local_users, local);
    }

    break;
  case INF_SESSION_CLOSED:
    /* fallthrough */
  default:
    g_assert_not_reached();
    break;
  }

  /* TODO: Disconnect this on dispose */
  g_signal_connect(
    G_OBJECT(user),
    "notify::flags",
    G_CALLBACK(inf_adopted_session_user_notify_flags_cb),
    session
  );
}

/*
 * GObject overrides.
 */

static void
inf_adopted_session_init(GTypeInstance* instance,
                         gpointer g_class)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;

  session = INF_ADOPTED_SESSION(instance);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  priv->io = NULL;
  priv->algorithm = NULL;
  priv->local_users = NULL;
}

static GObject*
inf_adopted_session_constructor(GType type,
                                guint n_construct_properties,
                                GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;
  InfSessionStatus status;
  InfUserTable* user_table;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  session = INF_ADOPTED_SESSION(object);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  g_object_get(G_OBJECT(session), "status", &status, NULL);

  user_table = inf_session_get_user_table(INF_SESSION(session));

  g_signal_connect(
    G_OBJECT(user_table),
    "add-user",
    G_CALLBACK(inf_adopted_session_add_user_cb),
    session
  );

  switch(status)
  {
  case INF_SESSION_SYNCHRONIZING:
    /* algorithm is created during initial synchronization when parameters
     * like initial vector time, max total log size etc. are known. */
    break;
  case INF_SESSION_RUNNING:
    g_assert(inf_session_get_buffer(INF_SESSION(session)) != NULL);
    priv->algorithm = inf_adopted_algorithm_new(
      user_table,
      inf_session_get_buffer(INF_SESSION(session))
    );

    break;
  case INF_SESSION_CLOSED:
    /* Session should not be initially closed */
  default:
    g_assert_not_reached();
    break;
  }

  return object;
}

static void
inf_adopted_session_dispose(GObject* object)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;
  InfUserTable* user_table;

  session = INF_ADOPTED_SESSION(object);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  user_table = inf_session_get_user_table(INF_SESSION(session));

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(user_table),
    G_CALLBACK(inf_adopted_session_add_user_cb),
    session
  );

  /* This calls the close vfunc if the session is running, in which we
   * free the local users. */
  G_OBJECT_CLASS(parent_class)->dispose(object);

  g_assert(priv->local_users == NULL);

  if(priv->algorithm != NULL)
  {
    g_object_unref(G_OBJECT(priv->algorithm));
    priv->algorithm = NULL;
  }

  if(priv->io != NULL)
  {
    g_object_unref(G_OBJECT(priv->io));
    priv->io = NULL;
  }
}

static void
inf_adopted_session_finalize(GObject* object)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;

  session = INF_ADOPTED_SESSION(object);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  /* Should have been freed in close, called by dispose */
  g_assert(priv->local_users == NULL);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_adopted_session_set_property(GObject* object,
                                 guint prop_id,
                                 const GValue* value,
                                 GParamSpec* pspec)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;

  session = INF_ADOPTED_SESSION(object);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_IO:
    g_assert(priv->io == NULL); /* construct only */
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_ALGORITHM:
    /* read only */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_session_get_property(GObject* object,
                                 guint prop_id,
                                 GValue* value,
                                 GParamSpec* pspec)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;

  session = INF_ADOPTED_SESSION(object);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, G_OBJECT(priv->io));
    break;
  case PROP_ALGORITHM:
    g_value_set_object(value, G_OBJECT(priv->algorithm));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * VFunc implementations.
 */

static void
inf_adopted_session_to_xml_sync_foreach_user_func(InfUser* user,
                                                  gpointer user_data)
{
  InfAdoptedRequestLog* log;
  InfAdoptedSessionToXmlSyncForeachData* data;
  guint i;
  guint end;
  xmlNodePtr xml;
  InfAdoptedRequest* request;

  g_assert(INF_ADOPTED_IS_USER(user));

  data = (InfAdoptedSessionToXmlSyncForeachData*)user_data;
  log = inf_adopted_user_get_request_log(INF_ADOPTED_USER(user));
  end = inf_adopted_request_log_get_end(log);

  for(i = inf_adopted_request_log_get_begin(log); i < end; ++ i)
  {
    request = inf_adopted_request_log_get_request(log, i);

    xml = xmlNewChild(
      data->parent_xml,
      NULL,
      (const xmlChar*)"sync-request",
      NULL
    );

    inf_adopted_session_request_to_xml(data->session, request, xml, TRUE);
    xmlAddChild(data->parent_xml, xml);
  }
}

static void
inf_adopted_session_to_xml_sync(InfSession* session,
                                xmlNodePtr parent)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedSessionToXmlSyncForeachData foreach_data;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  g_assert(priv->algorithm != NULL);

  INF_SESSION_CLASS(parent_class)->to_xml_sync(session, parent);

  foreach_data.session = INF_ADOPTED_SESSION(session);
  foreach_data.parent_xml = parent;

  inf_user_table_foreach_user(
    inf_session_get_user_table(session),
    inf_adopted_session_to_xml_sync_foreach_user_func,
    &foreach_data
  );
}

static gboolean
inf_adopted_session_process_xml_sync(InfSession* session,
                                     InfXmlConnection* connection,
                                     const xmlNodePtr xml,
                                     GError** error)
{
  InfAdoptedRequest* request;
  InfAdoptedUser* user;
  InfAdoptedRequestLog* log;

  if(strcmp((const char*)xml->name, "sync-request") == 0)
  {
    request = inf_adopted_session_xml_to_request(
      INF_ADOPTED_SESSION(session),
      xml,
      TRUE,
      error
    );

    if(request == NULL) return FALSE;

    user = INF_ADOPTED_USER(
      inf_user_table_lookup_user_by_id(
        inf_session_get_user_table(session),
        inf_adopted_request_get_user_id(request)
      )
    );

    log = inf_adopted_user_get_request_log(user);
    if(inf_adopted_session_validate_request(log, request, error) == FALSE)
      return FALSE;

    inf_adopted_request_log_add_request(log, request);
    return TRUE;
  }

  return INF_SESSION_CLASS(parent_class)->process_xml_sync(
    session,
    connection, 
    xml,
    error
  );
}

static gboolean
inf_adopted_session_process_xml_run(InfSession* session,
                                    InfXmlConnection* connection,
                                    const xmlNodePtr xml,
                                    GError** error)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedRequest* request;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  if(strcmp((const char*)xml->name, "request") == 0)
  {
    /* TODO: Check user! */

    request = inf_adopted_session_xml_to_request(
      INF_ADOPTED_SESSION(session),
      xml,
      FALSE,
      error
    );

    if(request == NULL) return FALSE;

    inf_adopted_algorithm_receive_request(priv->algorithm, request);
    g_object_unref(G_OBJECT(request));

    /* Requests can always be forwarded since user is given. */
    return TRUE;
  }

  return INF_SESSION_CLASS(parent_class)->process_xml_run(
    session,
    connection,
    xml,
    error
  );
}

static GArray*
inf_adopted_session_get_xml_user_props(InfSession* session,
                                       InfXmlConnection* conn,
                                       const xmlNodePtr xml)
{
  GArray* array;
  GParameter* parameter;
  InfAdoptedStateVector* vector;
  xmlChar* time;

  array = INF_SESSION_CLASS(parent_class)->get_xml_user_props(
    session,
    conn,
    xml
  );

  /* Vector time */
  time = inf_xml_util_get_attribute(xml, "time");
  if(time != NULL)
  {
    vector = inf_adopted_state_vector_from_string((const gchar*)time, NULL);
    xmlFree(time);

    /* TODO: Error reporting for get_xml_user_props */
    if(vector != NULL)
    {
      parameter = inf_session_get_user_property(array, "vector");
      g_value_init(&parameter->value, INF_ADOPTED_TYPE_STATE_VECTOR);
      g_value_take_boxed(&parameter->value, vector);
    }
  }

  /* log-begin is not in the  spec */
#if 0
  /* Initial request log, only if ID is also given */
  id_param = inf_session_lookup_user_property(
    (const GParameter*)array->data,
    array->len,
    "id"
  );

  if(id_param != NULL &&
     inf_xml_util_get_attribute_uint(xml, "log-begin", &log_begin, NULL))
  {
    log = inf_adopted_request_log_new(
      g_value_get_uint(&id_param->value),
      log_begin
    );

    parameter = inf_session_get_user_property(array, "request-log");
    g_value_init(&parameter->value, INF_ADOPTED_TYPE_REQUEST_LOG);
    g_value_take_object(&parameter->value, log);
  }
#endif

  return array;
}

static void
inf_adopted_session_set_xml_user_props(InfSession* session,
                                       const GParameter* params,
                                       guint n_params,
                                       xmlNodePtr xml)
{
  const GParameter* time;
  InfAdoptedStateVector* vector;
  gchar* time_string;

  INF_SESSION_CLASS(parent_class)->set_xml_user_props(
    session,
    params,
    n_params,
    xml
  );

  time = inf_session_lookup_user_property(params, n_params, "vector");
  if(time != NULL)
  {
    vector = (InfAdoptedStateVector*)g_value_get_boxed(&time->value);
    time_string = inf_adopted_state_vector_to_string(vector);
    inf_xml_util_set_attribute(xml, "time", time_string);
    g_free(time_string);
  }

  /* log-begin is not in the spec */
#if 0
  log = inf_session_lookup_user_property(params, n_params, "request-log");
  if(log != NULL)
  {
    log_begin = inf_adopted_request_log_get_begin(
      INF_ADOPTED_REQUEST_LOG(g_value_get_object(&log->value))
    );

    inf_xml_util_set_attribute_uint(xml, "log-begin", log_begin);
  }
#endif
}

static gboolean
inf_adopted_session_validate_user_props(InfSession* session,
                                        const GParameter* params,
                                        guint n_params,
                                        InfUser* exclude,
                                        GError** error)
{
  const GParameter* time;
  gboolean result;

  result = INF_SESSION_CLASS(parent_class)->validate_user_props(
    session,
    params,
    n_params,
    exclude,
    error
  );

  if(result == FALSE) return FALSE;

  time = inf_session_lookup_user_property(params, n_params, "vector");
  if(time == NULL)
  {
    g_set_error(
      error,
      inf_adopted_session_error_quark,
      INF_ADOPTED_SESSION_ERROR_MISSING_STATE_VECTOR,
      "'time' attribute in user message is missing"
    );

    return FALSE;
  }

  return TRUE;
}

static void
inf_adopted_session_close(InfSession* session)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedSessionLocalUser* local;
  GSList* item;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = (InfAdoptedSessionLocalUser*)item->data;
    inf_adopted_state_vector_free(local->last_send_vector);
    g_slice_free(InfAdoptedSessionLocalUser, local);
  }

  g_slist_free(priv->local_users);
  priv->local_users = NULL;

  /* Local user info is no longer required */
  INF_SESSION_CLASS(parent_class)->close(session);
}

static void
inf_adopted_session_synchronization_complete(InfSession* session,
                                             InfXmlConnection* connection)
{
  InfAdoptedSessionPrivate* priv;
  InfSessionStatus status;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  g_object_get(G_OBJECT(session), "status", &status, NULL);

  if(status == INF_SESSION_SYNCHRONIZING)
  {
    /* Create adOPTed algorithm upon successful synchronization */
    g_assert(priv->algorithm == NULL);
    priv->algorithm = inf_adopted_algorithm_new(
      inf_session_get_user_table(session),
      inf_session_get_buffer(session)
    );

    g_object_notify(G_OBJECT(session), "algorithm");
  }

  INF_SESSION_CLASS(parent_class)->synchronization_complete(
    session,
    connection
  );
}

/*
 * Gype registration.
 */

static void
inf_adopted_session_class_init(gpointer g_class,
                               gpointer class_data)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;
  InfAdoptedSessionClass* adopted_session_class;

  object_class = G_OBJECT_CLASS(g_class);
  session_class = INF_SESSION_CLASS(g_class);
  adopted_session_class = INF_ADOPTED_SESSION_CLASS(g_class);

  parent_class = INF_SESSION_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAdoptedSessionPrivate));

  object_class->constructor = inf_adopted_session_constructor;
  object_class->dispose = inf_adopted_session_dispose;
  object_class->finalize = inf_adopted_session_finalize;
  object_class->set_property = inf_adopted_session_set_property;
  object_class->get_property = inf_adopted_session_get_property;

  session_class->to_xml_sync = inf_adopted_session_to_xml_sync;
  session_class->process_xml_sync = inf_adopted_session_process_xml_sync;
  session_class->process_xml_run = inf_adopted_session_process_xml_run;
  session_class->get_xml_user_props = inf_adopted_session_get_xml_user_props;
  session_class->set_xml_user_props = inf_adopted_session_set_xml_user_props;
  session_class->validate_user_props =
    inf_adopted_session_validate_user_props;

  session_class->close = inf_adopted_session_close;
  
  session_class->synchronization_complete =
    inf_adopted_session_synchronization_complete;

  adopted_session_class->xml_to_operation = NULL;
  adopted_session_class->operation_to_xml = NULL;

  inf_adopted_session_error_quark = g_quark_from_static_string(
    "INF_ADOPTED_SESSION_ERROR"
  );

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "The IO object used for timeouts",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_ALGORITHM,
    g_param_spec_object(
      "algorithm",
      "Algorithm",
      "The adOPTed algorithm used for translating incoming requests",
      INF_ADOPTED_TYPE_ALGORITHM,
      G_PARAM_READABLE
    )
  );
}

GType
inf_adopted_session_get_type(void)
{
  static GType session_type = 0;

  if(!session_type)
  {
    static const GTypeInfo session_type_info = {
      sizeof(InfAdoptedSessionClass),   /* class_size */
      NULL,                             /* base_init */
      NULL,                             /* base_finalize */
      inf_adopted_session_class_init,   /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      sizeof(InfAdoptedSession),        /* instance_size */
      0,                                /* n_preallocs */
      inf_adopted_session_init,         /* instance_init */
      NULL                              /* value_table */
    };

    session_type = g_type_register_static(
      INF_TYPE_SESSION,
      "InfAdoptedSession",
      &session_type_info,
      0
    );
  }

  return session_type;
}

/*
 * Public API.
 */

/**
 * inf_adopted_session_get_io:
 * @session: A #InfAdoptedSession.
 *
 * Returns the #InfIo object of @session.
 *
 * Return Value: A #InfIo.
 **/
InfIo*
inf_adopted_session_get_io(InfAdoptedSession* session)
{
  g_return_val_if_fail(INF_ADOPTED_IS_SESSION(session), NULL);
  return INF_ADOPTED_SESSION_PRIVATE(session)->io;
}

/**
 * inf_adopted_session_get_algorithm:
 * @session: A #InfAdoptedSession.
 *
 * Returns the #InfAlgorithm object of @session. Returns %NULL if @session
 * has status %INF_SESSION_SYNCHRONIZING because there the algorithm object
 * is not yet created before successful synchronization.
 *
 * Return Value: A #InfAdoptedAlgorithm, or %NULL.
 **/
InfAdoptedAlgorithm*
inf_adopted_session_get_algorithm(InfAdoptedSession* session)
{
  g_return_val_if_fail(INF_ADOPTED_IS_SESSION(session), NULL);
  return INF_ADOPTED_SESSION_PRIVATE(session)->algorithm;
}

/**
 * inf_adopted_session_broadcast_request:
 * @session: A #InfAdoptedSession.
 * @request: A #InfAdoptedRequest obtained from @session's algorithm.
 *
 * Sends a request to all subscribed connections. The request should originate
 * from a call to inf_adopted_algorithm_generate_request_noexec(),
 * inf_adopted_algorithm_generate_request(),
 * inf_adopted_algorithm_generate_undo() or
 * inf_adopted_algorithm_generate_redo() with @session's #InfAdoptedAlgorithm.
 **/
void
inf_adopted_session_broadcast_request(InfAdoptedSession* session,
                                      InfAdoptedRequest* request)
{
  xmlNodePtr xml;
  
  g_return_if_fail(INF_ADOPTED_IS_SESSION(session));
  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));

  xml = xmlNewNode(NULL, (const xmlChar*)"request");
  inf_adopted_session_request_to_xml(session, request, xml, FALSE);

  inf_session_send_to_subscriptions(INF_SESSION(session), NULL, xml);
}

/**
 * inf_adopted_session_undo:
 * @session: A #InfAdoptedSession.
 * @user: A local #InfAdoptedUser.
 *
 * This is a shortcut for creating an undo request and broadcasting it.
 **/
void
inf_adopted_session_undo(InfAdoptedSession* session,
                         InfAdoptedUser* user)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedRequest* request;

  g_return_if_fail(INF_ADOPTED_IS_SESSION(session));
  g_return_if_fail(INF_ADOPTED_IS_USER(user));

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  request = inf_adopted_algorithm_generate_undo(priv->algorithm, user);
  inf_adopted_session_broadcast_request(session, request);
  g_object_unref(request);  
}

/**
 * inf_adopted_session_redo:
 * @session: A #InfAdoptedSession.
 * @user: A local #InfAdoptedUser.
 *
 * This is a shortcut for creating a redo request and broadcasting it.
 **/
void
inf_adopted_session_redo(InfAdoptedSession* session,
                         InfAdoptedUser* user)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedRequest* request;

  g_return_if_fail(INF_ADOPTED_IS_SESSION(session));
  g_return_if_fail(INF_ADOPTED_IS_USER(user));

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  request = inf_adopted_algorithm_generate_redo(priv->algorithm, user);
  inf_adopted_session_broadcast_request(session, request);
  g_object_unref(request);
}

/* vim:set et sw=2 ts=2: */
