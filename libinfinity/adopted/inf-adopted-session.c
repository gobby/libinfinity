/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-adopted-session
 * @title: InfAdoptedSession
 * @short_description: Session handling concurrency control via the adOPTed
 * algorithm.
 * @include: libinfinity/adopted/inf-adopted-session.h
 * @see_also: #InfSession, #InfAdoptedAlgorithm
 * @stability: Unstable
 *
 * #InfAdoptedSession handles concurrency control with an #InfAdoptedAlgorithm
 * on top of a #InfSession. It takes care of sending all necessary information
 * to joining users, receives requests from the network (passing them to
 * #InfAdoptedAlgorithm) and transfers local requests to the other users. It
 * also makes sure to periodically send the state the local host is in to
 * other uses even if the local users are idle (which is required for others
 * to cleanup their request logs and request caches).
 */

/* TODO: warning if no update from a particular non-local user for some time */

#include <libinfinity/adopted/inf-adopted-session.h>
#include <libinfinity/adopted/inf-adopted-no-operation.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-signals.h>

#include <string.h>
#include <time.h>

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
  time_t noop_time; /* TODO: should be monotonic time */
};

typedef struct _InfAdoptedSessionPrivate InfAdoptedSessionPrivate;
struct _InfAdoptedSessionPrivate {
  InfIo* io;
  guint max_total_log_size;

  InfAdoptedAlgorithm* algorithm;
  GSList* local_users; /* having zero or one item in 99.9% of all cases */

  /* Timeout for sending noop with our current vector time */
  InfIoTimeout* noop_timeout;
  /* User to send the time for */
  InfAdoptedSessionLocalUser* next_noop_user;
  /* Buffer for requests that are not ready to be executed yet */
  GPtrArray* request_buffer;
};

enum {
  PROP_0,

  /* construct only */
  PROP_IO,
  PROP_MAX_TOTAL_LOG_SIZE,

  /* read only */
  PROP_ALGORITHM
};

enum {
  CHECK_REQUEST,

  LAST_SIGNAL
};

#define INF_ADOPTED_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_SESSION, InfAdoptedSessionPrivate))

static guint session_signals[LAST_SIGNAL];

static GQuark inf_adopted_session_error_quark;
/* TODO: This should perhaps be a property: */
static const int INF_ADOPTED_SESSION_NOOP_INTERVAL = 30;

G_DEFINE_TYPE_WITH_CODE(InfAdoptedSession, inf_adopted_session, INF_TYPE_SESSION,
  G_ADD_PRIVATE(InfAdoptedSession))

/*
 * Utility functions.
 */

static InfAdoptedSessionLocalUser*
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
      _("Request has index '%u', but index '%u' was expected"),
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
        g_set_error_literal(
          error,
          inf_adopted_session_error_quark,
          INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST,
          _("Undo received, but no previous request found")
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
        g_set_error_literal(
          error,
          inf_adopted_session_error_quark,
          INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST,
          _("Redo received, but no previous request found")
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

static InfAdoptedUser*
inf_adopted_session_user_from_request_xml(InfAdoptedSession* session,
                                          xmlNodePtr xml,
                                          GError** error)
{
  InfUserTable* user_table;
  InfUser* user;
  guint user_id;

  user_table = inf_session_get_user_table(INF_SESSION(session));

  if(!inf_xml_util_get_attribute_uint_required(xml, "user", &user_id, error))
    return FALSE;

  /* User ID 0 means no user */
  if(user_id == 0) return NULL;

  user = inf_user_table_lookup_user_by_id(user_table, user_id);

  if(user == NULL)
  {
    g_set_error(
      error,
      inf_adopted_session_error_quark,
      INF_ADOPTED_SESSION_ERROR_NO_SUCH_USER,
      _("No such user with user ID '%u'"),
      user_id
    );

    return NULL;
  }

  g_assert(INF_ADOPTED_IS_USER(user));
  return INF_ADOPTED_USER(user);
}

/*
 * Noop timer
 */

static void
inf_adopted_session_noop_timeout_func(gpointer user_data)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;
  InfAdoptedOperation* op;
  InfAdoptedRequest* request;

  session = INF_ADOPTED_SESSION(user_data);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  priv->noop_timeout = NULL;
  g_assert(priv->next_noop_user != NULL);

  op = INF_ADOPTED_OPERATION(inf_adopted_no_operation_new());

  request = inf_adopted_algorithm_generate_request(
    priv->algorithm,
    INF_ADOPTED_REQUEST_DO,
    priv->next_noop_user->user,
    op
  );

  g_object_unref(op);

  /* There is no need to actually execute the request, since it does not
   * do anything anyway. */

  /* This resets noop_time for this user, determines the next user for
   * which to generate a noop request and schedules the new timeout. */
  inf_adopted_session_broadcast_request(session, request);
  g_object_unref(request);
}

static InfAdoptedSessionLocalUser*
inf_adopted_session_find_next_noop_user(InfAdoptedSession* session)
{
  InfAdoptedSessionPrivate* priv;
  GSList* item;
  InfAdoptedSessionLocalUser* local;
  InfAdoptedSessionLocalUser* next_user;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  next_user = NULL;

  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = (InfAdoptedSessionLocalUser*)item->data;
    if(local->noop_time != 0)
      if(next_user == NULL || local->noop_time < next_user->noop_time)
        next_user = local;
  }

  return next_user;
}

static void
inf_adopted_session_schedule_noop_timer(InfAdoptedSession* session)
{
  InfAdoptedSessionPrivate* priv;
  time_t current;
  time_t sched;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  if(priv->noop_timeout != NULL)
  {
    inf_io_remove_timeout(priv->io, priv->noop_timeout);
    priv->noop_timeout = NULL;
  }

  if(priv->next_noop_user != NULL)
  {
    current = time(NULL);
    sched =
      priv->next_noop_user->noop_time + INF_ADOPTED_SESSION_NOOP_INTERVAL;

    if(sched >= current)
      sched -= current;
    else
      sched = 0;

    priv->noop_timeout = inf_io_add_timeout(
      priv->io,
      sched * 1000,
      inf_adopted_session_noop_timeout_func,
      session,
      NULL
    );
  }
}

static void
inf_adopted_session_start_noop_timer(InfAdoptedSession* session,
                                     InfAdoptedSessionLocalUser* local)
{
  InfAdoptedSessionPrivate* priv;
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  g_assert(local->noop_time == 0);
  local->noop_time = time(NULL);

  if(priv->noop_timeout == NULL)
  {
    priv->next_noop_user = inf_adopted_session_find_next_noop_user(session);
    g_assert(priv->next_noop_user != NULL);

    inf_adopted_session_schedule_noop_timer(session);
  }
}

static void
inf_adopted_session_stop_noop_timer(InfAdoptedSession* session,
                                    InfAdoptedSessionLocalUser* local)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedSessionLocalUser* next_noop_user;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  if(local->noop_time > 0)
  {
    local->noop_time = 0;

    next_noop_user = inf_adopted_session_find_next_noop_user(session);
    if(next_noop_user != priv->next_noop_user)
    {
      priv->next_noop_user = next_noop_user;
      inf_adopted_session_schedule_noop_timer(session);
    }
  }
}

/* Breadcasts a request N times - makes only sense for undo and redo requests,
 * so that's the only thing we offer API for. */
static void
inf_adopted_session_broadcast_n_requests(InfAdoptedSession* session,
                                         InfAdoptedRequest* request,
                                         guint n)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedSessionClass* session_class;
  InfUserTable* user_table;
  guint user_id;
  InfUser* user;
  InfAdoptedSessionLocalUser* local;
  xmlNodePtr xml;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  session_class = INF_ADOPTED_SESSION_GET_CLASS(session);
  g_assert(session_class->request_to_xml != NULL);

  user_table = inf_session_get_user_table(INF_SESSION(session));
  user_id = inf_adopted_request_get_user_id(request);
  user = inf_user_table_lookup_user_by_id(user_table, user_id);
  g_assert(user != NULL);

  local = inf_adopted_session_lookup_local_user(
    session,
    INF_ADOPTED_USER(user)
  );
  g_assert(local != NULL);

  xml = xmlNewNode(NULL, (const xmlChar*)"request");

  session_class->request_to_xml(
    session,
    xml,
    request,
    local->last_send_vector,
    FALSE
  );

  if(n > 1) inf_xml_util_set_attribute_uint(xml, "num", n);
  inf_session_send_to_subscriptions(INF_SESSION(session), xml);

  inf_adopted_state_vector_free(local->last_send_vector);
  local->last_send_vector = inf_adopted_state_vector_copy(
    inf_adopted_request_get_vector(request)
  );

  /* Add this request to last send vector if it increases vector time
   * (-> affects buffer). */
  if(inf_adopted_request_affects_buffer(request) == TRUE)
    inf_adopted_state_vector_add(local->last_send_vector, user_id, n);

  inf_adopted_session_stop_noop_timer(session, local);
}

static gboolean
inf_adopted_session_process_request(InfAdoptedSession* session,
                                    InfAdoptedRequest* request,
                                    InfAdoptedUser* user,
                                    GError** error)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedStateVector* request_vector;
  InfAdoptedStateVector* current_vector;
  gboolean reject_request;
  GError* local_error;
  gboolean execute_result;

  xmlNodePtr reply_xml;
  gchar* request_str;
  gchar* current_str;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  request_vector = inf_adopted_request_get_vector(request);
  current_vector = inf_adopted_algorithm_get_current(priv->algorithm);

  if(inf_adopted_state_vector_causally_before(request_vector, current_vector))
  {
    g_signal_emit(
      G_OBJECT(session),
      session_signals[CHECK_REQUEST],
      0,
      request,
      user,
      &reject_request
    );

    local_error = NULL;
    
    if(reject_request)
    {
      g_set_error_literal(
        &local_error,
        inf_adopted_session_error_quark,
        INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST,
        _("The request was rejected via the API")
      );

      execute_result = FALSE;
    }
    else
    {
      execute_result = inf_adopted_algorithm_execute_request(
        priv->algorithm,
        request,
        TRUE,
        &local_error
      );
    }

    if(local_error != NULL)
    {
      /* Send a message back to where the request came from, to let them
       * know we couldn't handle this. Note that at the moment this is not
       * explicitly handled, but it can aid in debugging. */
      if(inf_user_get_connection(INF_USER(user)) != NULL)
      {
        /* Send a message back to where we got this request from, to inform
         * them that the request cannot be handled. */
        request_str = inf_adopted_state_vector_to_string(request_vector);
        current_str = inf_adopted_state_vector_to_string(current_vector);

        reply_xml = xmlNewNode(NULL, (const xmlChar*)"invalid-request");

        inf_xml_util_set_attribute(
          reply_xml,
          "request",
          request_str
        );

        inf_xml_util_set_attribute(
          reply_xml,
          "state",
          current_str
        );

        inf_xml_util_set_attribute_uint(
          reply_xml,
          "user",
          inf_user_get_id(INF_USER(user))
        );

        xmlNewChild(
          reply_xml,
          NULL,
          (const xmlChar*)"reason",
          (const xmlChar*)local_error->message
        );

        g_free(request_str);
        g_free(current_str);

        inf_communication_group_send_message(
          inf_session_get_subscription_group(INF_SESSION(session)),
          inf_user_get_connection(INF_USER(user)),
          reply_xml
        );
      }

      g_propagate_error(error, local_error);
    }

    return execute_result;
  }
  else
  {
    if(priv->request_buffer == NULL)
      priv->request_buffer = g_ptr_array_new();
    g_ptr_array_add(priv->request_buffer, request);
    g_object_ref(request);
    return TRUE;
  }
}

static void
inf_adopted_session_process_buffered_requests(InfAdoptedSession* session)
{
  InfAdoptedSessionPrivate* priv;
  InfUserTable* user_table;
  InfAdoptedStateVector* current;

  guint i;
  InfAdoptedRequest* request;
  InfAdoptedStateVector* vector;

  guint user_id;
  InfUser* user;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  if(priv->request_buffer != NULL)
  {
    user_table = inf_session_get_user_table(INF_SESSION(session));
    current = inf_adopted_algorithm_get_current(priv->algorithm);
    for(i = 0; i < priv->request_buffer->len; ++i)
    {
      request =
        INF_ADOPTED_REQUEST(g_ptr_array_index(priv->request_buffer, i));
      vector = inf_adopted_request_get_vector(request);

      if(inf_adopted_state_vector_causally_before(vector, current))
      {
        g_ptr_array_remove_index_fast(priv->request_buffer, i);

        user_id = inf_adopted_request_get_user_id(request);
        user = inf_user_table_lookup_user_by_id(user_table, user_id);
        g_assert(INF_ADOPTED_IS_USER(user));

        /* Note that there is no error handling here, since the buffered
         * requests are not related to the request which has currently been
         * received. In order to handle a failure here, the
         * InfAdoptedAlgorithm::end-execute-request signal should be used. */
        inf_adopted_session_process_request(
          session,
          request,
          INF_ADOPTED_USER(user),
          NULL
        );

        g_object_unref(request);
        return inf_adopted_session_process_buffered_requests(session);
      }
    }
  }
}

/*
 * Signal handlers
 */

static void
inf_adopted_session_local_user_added(InfAdoptedSession* session,
                                     InfAdoptedUser* user)
{
  InfAdoptedSessionPrivate* priv;
  InfSessionStatus status;
  InfAdoptedSessionLocalUser* local;
  InfAdoptedStateVector* current_state;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  status = inf_session_get_status(INF_SESSION(session));

  /* Cannot be local while synchronizing */
  g_assert(status == INF_SESSION_RUNNING);

  local = g_slice_new(InfAdoptedSessionLocalUser);
  local->user = user;

  local->last_send_vector = inf_adopted_state_vector_copy(
    inf_adopted_user_get_vector(user)
  );

  /* Set current vector for local user, this is kept up-to-date by
   * InfAdoptedAlgorithm. TODO: Also do this in InfAdoptedAlgorithm? */
  inf_adopted_user_set_vector(
    user,
    inf_adopted_state_vector_copy(
      inf_adopted_algorithm_get_current(priv->algorithm)
    )
  );

  local->noop_time = 0;

  priv->local_users = g_slist_prepend(priv->local_users, local);

  /* Start noop timer if user is not up to date */
  current_state = inf_adopted_algorithm_get_current(priv->algorithm);
  if(inf_adopted_state_vector_compare(current_state, local->last_send_vector))
    inf_adopted_session_start_noop_timer(session, local);
}

static void
inf_adopted_session_remove_local_user_cb(InfUserTable* user_table,
                                         InfUser* user,
                                         gpointer user_data)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;
  InfAdoptedSessionLocalUser* local;

  session = INF_ADOPTED_SESSION(user_data);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  local = inf_adopted_session_lookup_local_user(
    session,
    INF_ADOPTED_USER(user)
  );
  g_assert(local != NULL);

  inf_adopted_session_stop_noop_timer(session, local);
  inf_adopted_state_vector_free(local->last_send_vector);
  priv->local_users = g_slist_remove(priv->local_users, local);
  g_slice_free(InfAdoptedSessionLocalUser, local);
}

static void
inf_adopted_session_add_local_user_cb(InfUserTable* user_table,
                                      InfUser* user,
                                      gpointer user_data)
{
  g_assert(INF_ADOPTED_IS_USER(user));

  inf_adopted_session_local_user_added(
    INF_ADOPTED_SESSION(user_data),
    INF_ADOPTED_USER(user)
  );
}

static void
inf_adopted_session_constructed_foreach_local_user_func(InfUser* user,
                                                        gpointer user_data)
{
  g_assert(INF_ADOPTED_IS_USER(user));

  inf_adopted_session_local_user_added(
    INF_ADOPTED_SESSION(user_data),
    INF_ADOPTED_USER(user)
  );
}

static void
inf_adopted_session_end_execute_request_cb(InfAdoptedAlgorithm* algorithm,
                                          InfAdoptedUser* user,
                                          InfAdoptedRequest* request,
                                          InfAdoptedRequest* translated,
                                          const GError* error,
                                          gpointer user_data)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;
  GSList* item;
  InfAdoptedSessionLocalUser* local;
  guint id;
  InfAdoptedOperation* operation;

  session = INF_ADOPTED_SESSION(user_data);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  if(translated != NULL)
  {
    if(inf_adopted_request_affects_buffer(translated))
    {
      id = inf_adopted_request_get_user_id(translated);

      /* A request has been executed, meaning we are no longer up to date. Send
       * a noop in some time, so that others know what we already processed. */
      for(item = priv->local_users; item != NULL; item = g_slist_next(item))
      {
        local = (InfAdoptedSessionLocalUser*)item->data;
        if(local->noop_time == 0)
          /* Except we issued the request ourselves, of course. */
          if(inf_user_get_id(INF_USER(local->user)) != id)
            inf_adopted_session_start_noop_timer(session, local);
      }
    }

    /* Mark inactive users active if they do something */
    /* Note: This behaviour is implicitly performed by both client and server,
     * and requires no further network traffic. However, users explictely have
     * to be set inactive. */
    operation = inf_adopted_request_get_operation(translated);
    if(!INF_ADOPTED_IS_NO_OPERATION(operation))
    {
      /* TODO: We should offer a virtual function to flush all requests for
       * local users, either here or even in InfSession via a vfunc, so that
       * we don't accidentally make local users active by a delayed request. */
      if(inf_user_get_status(INF_USER(user)) == INF_USER_INACTIVE)
        g_object_set(G_OBJECT(user), "status", INF_USER_ACTIVE, NULL);
    }
  }
}

/*
 * Helper functions
 */

static void
inf_adopted_session_create_algorithm(InfAdoptedSession* session)
{
  InfAdoptedSessionPrivate* priv;
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  g_assert(priv->algorithm == NULL);

  g_assert(
    inf_session_get_status(INF_SESSION(session)) == INF_SESSION_RUNNING
  );

  priv->algorithm = inf_adopted_algorithm_new_full(
    inf_session_get_user_table(INF_SESSION(session)),
    inf_session_get_buffer(INF_SESSION(session)),
    priv->max_total_log_size
  );

  g_signal_connect(
    G_OBJECT(priv->algorithm),
    "end-execute-request",
    G_CALLBACK(inf_adopted_session_end_execute_request_cb),
    session
  );

  g_object_notify(G_OBJECT(session), "algorithm");
}

/*
 * GObject overrides.
 */

static void
inf_adopted_session_init(InfAdoptedSession* session)
{
  InfAdoptedSessionPrivate* priv;
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  priv->io = NULL;
  priv->max_total_log_size = 2048;
  priv->algorithm = NULL;
  priv->local_users = NULL;
  priv->noop_timeout = NULL;
  priv->next_noop_user = NULL;
  priv->request_buffer = NULL;
}

static void
inf_adopted_session_constructed(GObject* object)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;
  InfSessionStatus status;
  InfUserTable* user_table;

  G_OBJECT_CLASS(inf_adopted_session_parent_class)->constructed(object);

  session = INF_ADOPTED_SESSION(object);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  g_assert(priv->io != NULL);
  g_object_get(G_OBJECT(session), "status", &status, NULL);

  user_table = inf_session_get_user_table(INF_SESSION(session));

  g_signal_connect(
    G_OBJECT(user_table),
    "add-local-user",
    G_CALLBACK(inf_adopted_session_add_local_user_cb),
    session
  );

  g_signal_connect(
    G_OBJECT(user_table),
    "remove-local-user",
    G_CALLBACK(inf_adopted_session_remove_local_user_cb),
    session
  );

  switch(status)
  {
  case INF_SESSION_PRESYNC:
  case INF_SESSION_SYNCHRONIZING:
    /* algorithm is created during initial synchronization when parameters
     * like initial vector time, max total log size etc. are known. */
    break;
  case INF_SESSION_RUNNING:
    g_assert(inf_session_get_buffer(INF_SESSION(session)) != NULL);
    inf_adopted_session_create_algorithm(session);

    break;
  case INF_SESSION_CLOSED:
    /* Session should not be initially closed */
  default:
    g_assert_not_reached();
    break;
  }

  /* Add initial local users. Note that this requires the algorithm to exist, 
   * though in synchronizing state no local users can exist. */
  inf_user_table_foreach_local_user(
    user_table,
    inf_adopted_session_constructed_foreach_local_user_func,
    session
  );
}

static void
inf_adopted_session_dispose(GObject* object)
{
  InfAdoptedSession* session;
  InfAdoptedSessionPrivate* priv;
  InfUserTable* user_table;
  guint i;

  session = INF_ADOPTED_SESSION(object);
  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  user_table = inf_session_get_user_table(INF_SESSION(session));

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(user_table),
    G_CALLBACK(inf_adopted_session_add_local_user_cb),
    session
  );

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(user_table),
    G_CALLBACK(inf_adopted_session_remove_local_user_cb),
    session
  );

  if(priv->noop_timeout != NULL)
  {
    inf_io_remove_timeout(priv->io, priv->noop_timeout);
    priv->noop_timeout = NULL;
  }

  /* This calls the close vfunc if the session is running, in which we
   * free the local users. */
  G_OBJECT_CLASS(inf_adopted_session_parent_class)->dispose(object);

  g_assert(priv->local_users == NULL);

  if(priv->request_buffer != NULL)
  {
    for(i = 0; i < priv->request_buffer->len; ++i)
      g_object_unref(g_ptr_array_index(priv->request_buffer, i));
    g_ptr_array_free(priv->request_buffer, TRUE);
    priv->request_buffer = NULL;
  }

  if(priv->algorithm != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->algorithm),
      G_CALLBACK(inf_adopted_session_end_execute_request_cb),
      session
    );

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

  G_OBJECT_CLASS(inf_adopted_session_parent_class)->finalize(object);
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
  case PROP_MAX_TOTAL_LOG_SIZE:
    priv->max_total_log_size = g_value_get_uint(value);
    break;
  case PROP_ALGORITHM:
    /* read only */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
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
  case PROP_MAX_TOTAL_LOG_SIZE:
    g_value_set_uint(value, priv->max_total_log_size);
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
  InfAdoptedSessionClass* session_class;
  guint i;
  guint end;
  xmlNodePtr xml;
  InfAdoptedRequest* request;

  g_assert(INF_ADOPTED_IS_USER(user));

  data = (InfAdoptedSessionToXmlSyncForeachData*)user_data;
  log = inf_adopted_user_get_request_log(INF_ADOPTED_USER(user));
  end = inf_adopted_request_log_get_end(log);
  session_class = INF_ADOPTED_SESSION_GET_CLASS(data->session);
  g_assert(session_class->request_to_xml != NULL);

  for(i = inf_adopted_request_log_get_begin(log); i < end; ++ i)
  {
    request = inf_adopted_request_log_get_request(log, i);

    xml = xmlNewChild(
      data->parent_xml,
      NULL,
      (const xmlChar*)"sync-request",
      NULL
    );

    /* TODO: Diff to previous request? */
    session_class->request_to_xml(data->session, xml, request, NULL, TRUE);
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

  INF_SESSION_CLASS(inf_adopted_session_parent_class)->to_xml_sync(
    session,
    parent
  );

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
  InfAdoptedSessionClass* session_class;
  InfAdoptedRequest* request;
  InfAdoptedUser* user;
  InfAdoptedRequestLog* log;
  InfSessionClass* parent_class;

  if(strcmp((const char*)xml->name, "sync-request") == 0)
  {
    session_class = INF_ADOPTED_SESSION_GET_CLASS(session);
    g_assert(session_class->xml_to_request != NULL);

    request = session_class->xml_to_request(
      INF_ADOPTED_SESSION(session),
      xml,
      NULL, /* TODO: Diff to previous request, if any. */
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
    {
      g_object_unref(request);
      return FALSE;
    }

    inf_adopted_request_log_add_request(log, request);
    g_object_unref(request);

    return TRUE;
  }

  parent_class = INF_SESSION_CLASS(inf_adopted_session_parent_class);
  return parent_class->process_xml_sync(session, connection, xml, error);
}

static InfCommunicationScope
inf_adopted_session_process_xml_run(InfSession* session,
                                    InfXmlConnection* connection,
                                    const xmlNodePtr xml,
                                    GError** error)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedSessionClass* session_class;
  InfAdoptedRequest* request;
  InfAdoptedUser* user;
  guint user_id;

  InfAdoptedStateVector* user_vector;
  InfAdoptedStateVector* request_vector;

  gboolean has_num;
  gboolean process_request;
  guint num;
  GError* local_error;
  InfAdoptedRequest* copy_req;
  guint i;

  gchar* request_str;
  gchar* user_str;

  InfSessionClass* parent_class;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  if(strcmp((const char*)xml->name, "request") == 0)
  {
    session_class = INF_ADOPTED_SESSION_GET_CLASS(session);
    g_assert(session_class->xml_to_request != NULL);

    user = inf_adopted_session_user_from_request_xml(
      INF_ADOPTED_SESSION(session),
      xml,
      error
    );

    if(user == NULL)
      return INF_COMMUNICATION_SCOPE_PTP;

    if(inf_user_get_status(INF_USER(user)) == INF_USER_UNAVAILABLE ||
       inf_user_get_connection(INF_USER(user)) != connection)
    {
      g_set_error_literal(
        error,
        inf_user_error_quark(),
        INF_USER_ERROR_NOT_JOINED,
        _("User did not join from this connection")
      );

      return INF_COMMUNICATION_SCOPE_PTP;
    }

    local_error = NULL;
    has_num = inf_xml_util_get_attribute_uint(xml, "num", &num, &local_error);
    if(local_error != NULL)
    {
      g_propagate_error(error, local_error);
      return INF_COMMUNICATION_SCOPE_PTP;
    }

    if(has_num == FALSE)
      num = 1;

    user_id = inf_user_get_id(INF_USER(user));
    user_vector = inf_adopted_user_get_vector(user);

    request = session_class->xml_to_request(
      INF_ADOPTED_SESSION(session),
      xml,
      user_vector,
      FALSE,
      error
    );

    if(request == NULL)
      return INF_COMMUNICATION_SCOPE_PTP;

    request_vector = inf_adopted_request_get_vector(request);

    if(!inf_adopted_state_vector_causally_before(user_vector, request_vector))
    {
      /* Note that this can actually not happen, since the request time is
       * transferred as a diff to the previous user time. If the absolute
       * time were transmitted this would need to be handled as an error. */
      g_assert_not_reached();
    }
    else if(inf_adopted_request_get_index(request) !=
            inf_adopted_state_vector_get(user_vector, user_id))
    {
      request_str = inf_adopted_state_vector_to_string(request_vector);
      user_str = inf_adopted_state_vector_to_string(user_vector);

      g_set_error(
        error,
        inf_adopted_session_error_quark,
        INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST,
        _("Request \"%s\" by user \"%s\" is not consecutive with respect to "
          "previously received request \"%s\""),
        request_str,
        inf_user_get_name(INF_USER(user)),
        user_str
      );

      g_free(request_str);
      g_free(user_str);
      g_object_unref(request);
      return INF_COMMUNICATION_SCOPE_PTP;
    }

    /* Update the user vector to the state of the request. */
    user_vector = inf_adopted_state_vector_copy(request_vector);
    /* Note that this function takes ownership of user_vector */
    inf_adopted_user_set_vector(INF_ADOPTED_USER(user), user_vector);

    /* Apply the request more than once if num >= 2 is given. This is mostly
     * used for multiple undos and redos, but is in general allowed for any
     * request. */
    for(i = 0; i < num; ++i)
    {
      if(i == 0)
      {
        copy_req = request;
        g_object_ref(copy_req);
      }
      else
      {
        copy_req = inf_adopted_request_copy(request);

        /* TODO: This is a bit of a hack since requests are normally
         * immutable. It avoids an additional vector copy here though. */
        inf_adopted_state_vector_add(
          inf_adopted_request_get_vector(copy_req),
          inf_user_get_id(INF_USER(user)),
          i
        );
      }

      process_request = inf_adopted_session_process_request(
        INF_ADOPTED_SESSION(session),
        copy_req,
        user,
        error
      );

      g_object_unref(copy_req);

      /* Update the user vector again, including the component of the processed request. */
      if(inf_adopted_request_affects_buffer(request))
      {
        user_vector = inf_adopted_state_vector_copy(
          inf_adopted_request_get_vector(copy_req)
        );

        inf_adopted_state_vector_add(user_vector, user_id, 1);
        /* Note that this function takes ownership of user_vector */
        inf_adopted_user_set_vector(INF_ADOPTED_USER(user), user_vector);
      }

      /* If an error occured then break here, and do not process the
       * subsequent requests -- they will likely fail as well. */
      if(process_request == FALSE)
        break;
    }

    g_object_unref(request);

    /* The processed request(s) might have caused some of the buffered
     * requests to become ready. */
    if(i > 0)
    {
      inf_adopted_session_process_buffered_requests(
        INF_ADOPTED_SESSION(session)
      );
    }

    /* Cleanup requests that are no longer used after
     * having processed everything */
    inf_adopted_algorithm_cleanup(
      inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session))
    );

    /* Requests can always be forwarded since user is given. Explicitly allow
     * forwarding if the request could not be applied... maybe others are more
     * lucky? In the worst case it will just fail for them as well. */
    return INF_COMMUNICATION_SCOPE_GROUP;
  }

  parent_class = INF_SESSION_CLASS(inf_adopted_session_parent_class);
  return parent_class->process_xml_run(session, connection, xml, error);
}

static GArray*
inf_adopted_session_get_xml_user_props(InfSession* session,
                                       InfXmlConnection* conn,
                                       const xmlNodePtr xml)
{
  InfSessionClass* parent_class;
  GArray* array;
  GParameter* parameter;
  InfAdoptedStateVector* vector;
  xmlChar* time;

  parent_class = INF_SESSION_CLASS(inf_adopted_session_parent_class);
  array = parent_class->get_xml_user_props(session, conn, xml);

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

  return array;
}

static void
inf_adopted_session_set_xml_user_props(InfSession* session,
                                       const GParameter* params,
                                       guint n_params,
                                       xmlNodePtr xml)
{
  InfSessionClass* parent_class;
  InfAdoptedSessionPrivate* priv;
  const GParameter* time;
  InfAdoptedStateVector* vector;
  gchar* time_string;

  const GParameter* id_param;
  guint id;
  GSList* item;
  InfAdoptedSessionLocalUser* local_user;

  parent_class = INF_SESSION_CLASS(inf_adopted_session_parent_class);
  parent_class->set_xml_user_props(session, params, n_params, xml);

  priv = INF_ADOPTED_SESSION_PRIVATE(INF_ADOPTED_SESSION(session));

  time = inf_session_lookup_user_property(params, n_params, "vector");
  if(time != NULL)
  {
    /* If this is a local user, use last_send_vector instead of the user's
     * vector, so that subsequent differential updates are consistent. */
    vector = NULL;
    id_param = inf_session_lookup_user_property(params, n_params, "id");

    if(id_param != NULL)
    {
      id = g_value_get_uint(&id_param->value);
      for(item = priv->local_users; item != NULL; item = item->next)
      {
        local_user = (InfAdoptedSessionLocalUser*)item->data;
        if(inf_user_get_id(INF_USER(local_user->user)) == id)
        {
          vector = local_user->last_send_vector;
          break;
        }
      }
    }

    if(vector == NULL)
    {
      /* remote user, or a user join request */
      vector = (InfAdoptedStateVector*)g_value_get_boxed(&time->value);
    }

    time_string = inf_adopted_state_vector_to_string(vector);
    inf_xml_util_set_attribute(xml, "time", time_string);
    g_free(time_string);
  }
}

static gboolean
inf_adopted_session_validate_user_props(InfSession* session,
                                        const GParameter* params,
                                        guint n_params,
                                        InfUser* exclude,
                                        GError** error)
{
  InfSessionClass* parent_class;
  const GParameter* time;
  gboolean result;

  parent_class = INF_SESSION_CLASS(inf_adopted_session_parent_class);
  result = parent_class->validate_user_props(
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
    g_set_error_literal(
      error,
      inf_adopted_session_error_quark,
      INF_ADOPTED_SESSION_ERROR_MISSING_STATE_VECTOR,
      _("\"time\" attribute in user message is missing")
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

  /* Local user info is no longer required */
  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = (InfAdoptedSessionLocalUser*)item->data;
    inf_adopted_state_vector_free(local->last_send_vector);
    g_slice_free(InfAdoptedSessionLocalUser, local);
  }

  g_slist_free(priv->local_users);
  priv->local_users = NULL;

  INF_SESSION_CLASS(inf_adopted_session_parent_class)->close(session);
}

static void
inf_adopted_session_synchronization_complete_foreach_user_func(InfUser* user,
                                                               gpointer data)
{
  InfAdoptedRequestLog* log;
  log = inf_adopted_user_get_request_log(INF_ADOPTED_USER(user));

  /* Set begin index of empty request logs. Algorithm relies on
   * inf_adopted_request_log_get_begin() to return the index of the request
   * that will first be added to the request log. */
  if(inf_adopted_request_log_is_empty(log))
  {
    inf_adopted_request_log_set_begin(
      log,
      inf_adopted_state_vector_get(
        inf_adopted_user_get_vector(INF_ADOPTED_USER(user)),
        inf_user_get_id(user)
      )
    );
  }
}

static void
inf_adopted_session_synchronization_complete(InfSession* session,
                                             InfXmlConnection* connection)
{
  InfSessionClass* parent_class;
  InfAdoptedSessionPrivate* priv;
  InfSessionStatus status;

  priv = INF_ADOPTED_SESSION_PRIVATE(session);
  status = inf_session_get_status(session);

  g_object_freeze_notify(G_OBJECT(session));

  parent_class = INF_SESSION_CLASS(inf_adopted_session_parent_class);
  parent_class->synchronization_complete(session, connection);

  if(status == INF_SESSION_SYNCHRONIZING)
  {
    inf_user_table_foreach_user(
      inf_session_get_user_table(session),
      inf_adopted_session_synchronization_complete_foreach_user_func,
      NULL
    );

    /* Create adOPTed algorithm upon successful synchronization */
    g_assert(priv->algorithm == NULL);
    inf_adopted_session_create_algorithm(INF_ADOPTED_SESSION(session));
  }

  g_object_thaw_notify(G_OBJECT(session));
}

static gboolean
inf_adopted_session_check_request(InfAdoptedSession* session,
                                  InfAdoptedRequest* request,
                                  InfAdoptedUser* user)
{
  /* Accept all requests by default */
  return FALSE;
}

/*
 * Gype registration.
 */

static void
inf_adopted_session_class_init(InfAdoptedSessionClass* adopted_session_class)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;

  object_class = G_OBJECT_CLASS(adopted_session_class);
  session_class = INF_SESSION_CLASS(adopted_session_class);

  object_class->constructed = inf_adopted_session_constructed;
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

  adopted_session_class->xml_to_request = NULL;
  adopted_session_class->request_to_xml = NULL;
  adopted_session_class->check_request = inf_adopted_session_check_request;

  inf_adopted_session_error_quark = g_quark_from_static_string(
    "INF_ADOPTED_SESSION_ERROR"
  );

  /**
   * InfAdoptedSession::check-request:
   * @session: The #InfAdoptedSession which is about to process a request.
   * @request: The request to be processed.
   * @user: The user who issued the request.
   *
   * This signal is emitted whenever the session received a request from a
   * non-local user. It is used to decide whether the request should be
   * processed or not. Note that generally not processing a request results is
   * loss of synchronization, since other hosts might process the request.
   * Only if the same condition can be applied on all sites a request should
   * be rejected. Another possibility is to reject a request at a central host
   * before it gets distributed to all other clients. If there is one signal
   * handler returning %TRUE the request is rejected, i.e. only if all signal
   * handlers return %FALSE it is accepted.
   */
  session_signals[CHECK_REQUEST] = g_signal_new(
    "check-request",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedSessionClass, check_request),
    g_signal_accumulator_true_handled, NULL,
    NULL,
    G_TYPE_BOOLEAN,
    2,
    INF_ADOPTED_TYPE_REQUEST,
    INF_ADOPTED_TYPE_USER
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
    PROP_MAX_TOTAL_LOG_SIZE,
    g_param_spec_uint(
      "max-total-log-size",
      "Maxmimum total log size",
      "The maximum number of requests to keep in all user's logs",
      0,
      G_MAXUINT,
      2048,
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

/*
 * Public API.
 */

/**
 * inf_adopted_session_get_io:
 * @session: A #InfAdoptedSession.
 *
 * Returns the #InfIo object of @session.
 *
 * Returns: (transfer none): A #InfIo.
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
 * Returns the #InfAdoptedAlgorithm object of @session. Returns %NULL if
 * @session has status %INF_SESSION_PRESYNC or %INF_SESSION_SYNCHRONIZING
 * because there the algorithm object is not yet created before successful
 * synchronization.
 *
 * Returns: (transfer none): A #InfAdoptedAlgorithm, or %NULL.
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
 * from a call to inf_adopted_algorithm_generate_request(), with
 * @session's #InfAdoptedAlgorithm.
 **/
void
inf_adopted_session_broadcast_request(InfAdoptedSession* session,
                                      InfAdoptedRequest* request)
{
  g_return_if_fail(INF_ADOPTED_IS_SESSION(session));
  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));

  inf_adopted_session_broadcast_n_requests(session, request, 1);
}

/**
 * inf_adopted_session_undo:
 * @session: A #InfAdoptedSession.
 * @user: A local #InfAdoptedUser.
 * @n: The number of undo requests to issue.
 *
 * This is a shortcut for creating @n undo requests and broadcasting them.
 * If @n > 1 then this is also more efficient.
 **/
void
inf_adopted_session_undo(InfAdoptedSession* session,
                         InfAdoptedUser* user,
                         guint n)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedRequest* first_request;
  InfAdoptedRequest* request;
  guint i;
  gboolean result;

  g_return_if_fail(INF_ADOPTED_IS_SESSION(session));
  g_return_if_fail(INF_ADOPTED_IS_USER(user));
  g_return_if_fail(n >= 1);

  /* TODO: Check whether we can issue n undo requests before doing anything */

  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  first_request = NULL;
  for(i = 0; i < n; ++i)
  {
    request = inf_adopted_algorithm_generate_request(
      priv->algorithm,
      INF_ADOPTED_REQUEST_UNDO,
      user,
      NULL
    );

    result = inf_adopted_algorithm_execute_request(
      priv->algorithm,
      request,
      TRUE,
      NULL
    );

    /* This cannot fail if the input parameters have been checked before. */
    g_assert(result == TRUE);

    if(first_request == NULL)
      first_request = request;
    else
      g_object_unref(request);
  }

  inf_adopted_session_broadcast_n_requests(session, first_request, n);
  g_object_unref(first_request);
}

/**
 * inf_adopted_session_redo:
 * @session: A #InfAdoptedSession.
 * @user: A local #InfAdoptedUser.
 * @n: The number of redo requests to issue.
 *
 * This is a shortcut for creating @n redo requests and broadcasting them.
 * If @n > 1 then this is also more efficient.
 **/
void
inf_adopted_session_redo(InfAdoptedSession* session,
                         InfAdoptedUser* user,
                         guint n)
{
  InfAdoptedSessionPrivate* priv;
  InfAdoptedRequest* first_request;
  InfAdoptedRequest* request;
  guint i;
  gboolean result;

  /* TODO: Check whether we can issue n redo requests before doing anything */

  g_return_if_fail(INF_ADOPTED_IS_SESSION(session));
  g_return_if_fail(INF_ADOPTED_IS_USER(user));
  g_return_if_fail(n >= 1);

  priv = INF_ADOPTED_SESSION_PRIVATE(session);

  first_request = NULL;
  for(i = 0; i < n; ++i)
  {
    request = inf_adopted_algorithm_generate_request(
      priv->algorithm,
      INF_ADOPTED_REQUEST_REDO,
      user,
      NULL
    );

    result = inf_adopted_algorithm_execute_request(
      priv->algorithm,
      request,
      TRUE,
      NULL
    );

    /* This cannot fail if the input parameters have been checked before. */
    g_assert(result == TRUE);

    if(first_request == NULL)
      first_request = request;
    else
      g_object_unref(request);
  }

  inf_adopted_session_broadcast_n_requests(session, first_request, n);
  g_object_unref(first_request);
}

/**
 * inf_adopted_session_read_request_info:
 * @session: A #InfAdoptedSession.
 * @xml: The XML to read the data from.
 * @diff_vec: The reference vector of the time vector of the request, or %NULL.
 * @user: (out) (transfer none) (allow-none): Location to store the user of
 * the request, or %NULL.
 * @time: (out) (transfer full) (allow-none): Location to store the state the
 * request was made, or %NULL.
 * @operation: (out) (transfer none) (allow-none): Location to store the
 * operation of the request, or %NULL.
 * @error: Location to place an error, if any.
 *
 * This function reads common information such as the state vector the request
 * was made and the user that made the request from XML. It is most likely to
 * be used by implementations of the xml_to_request virtual function.
 *
 * Returns: %TRUE if the data could be read successfully, %FALSE if the XML
 * request does not contain valid request data, in which case @error is set.
 */
gboolean
inf_adopted_session_read_request_info(InfAdoptedSession* session,
                                      xmlNodePtr xml,
                                      InfAdoptedStateVector* diff_vec,
                                      InfAdoptedUser** user,
                                      InfAdoptedStateVector** time,
                                      xmlNodePtr* operation,
                                      GError** error)
{
  xmlChar* attr;
  xmlNodePtr child;

  if(user != NULL)
  {
    *user = inf_adopted_session_user_from_request_xml(session, xml, error);
    if(*user == NULL) return FALSE;
  }

  if(time != NULL)
  {
    attr = inf_xml_util_get_attribute_required(xml, "time", error);
    if(attr == NULL) return FALSE;

    if(diff_vec == NULL)
    {
      *time = inf_adopted_state_vector_from_string((const gchar*)attr, error);
    }
    else
    {
      *time = inf_adopted_state_vector_from_string_diff(
        (const gchar*)attr,
        diff_vec,
        error
      );
    }

    xmlFree(attr);
    if(*time == NULL) return FALSE;
  }

  if(operation != NULL)
  {
    /* Get first child element */
    child = xml->children;
    while(child != NULL && child->type != XML_ELEMENT_NODE)
      child = child->next;

    if(child == NULL)
    {
      g_set_error_literal(
        error,
        inf_adopted_session_error_quark,
        INF_ADOPTED_SESSION_ERROR_MISSING_OPERATION,
        _("Operation for request missing")
      );

      if(time) inf_adopted_state_vector_free(*time);
      return FALSE;
    }

    *operation = child;
  }
  
  return TRUE;
}

/**
 * inf_adopted_session_write_request_info:
 * @session: A #InfAdoptedSession.
 * @diff_vec: A reference state vector, or %NULL.
 * @request: The #InfAdoptedRequest whose info to write.
 * @xml: The XML node to write the data into.
 * @operation: An XML node representing the operation of the request, or
 * %NULL.
 *
 * This function writes common data from @request, such as the user that
 * issued the request and the state in which the request was made into @xml.
 * If @diff_vec is given, then the state is written as a diff to this vector,
 * see inf_adopted_state_vector_to_string_diff(). Deserializing this data
 * again (via inf_adopted_session_read_request_info()) requires the same
 * @diff_vec then.
 *
 * This function is most likely to be used by implementations of the
 * request_to_xml virtual function.
 */
void
inf_adopted_session_write_request_info(InfAdoptedSession* session,
                                       InfAdoptedRequest* request,
                                       InfAdoptedStateVector* diff_vec,
                                       xmlNodePtr xml,
                                       xmlNodePtr operation)
{
  InfAdoptedStateVector* vector;
  guint user_id;
  gchar* vec_str;

  vector = inf_adopted_request_get_vector(request);
  user_id = inf_adopted_request_get_user_id(request);

  inf_xml_util_set_attribute_uint(xml, "user", user_id);

  if(diff_vec == NULL)
    vec_str = inf_adopted_state_vector_to_string(vector);
  else
    vec_str = inf_adopted_state_vector_to_string_diff(vector, diff_vec);

  inf_xml_util_set_attribute(xml, "time", vec_str);
  g_free(vec_str);
  
  if(operation != NULL)
    xmlAddChild(xml, operation);
}

/* vim:set et sw=2 ts=2: */
