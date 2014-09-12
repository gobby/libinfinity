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
 * SECTION:inf-adopted-algorithm
 * @title: InfAdoptedAlgorithm
 * @short_description: adOPTed implementation
 * @include: libinfinity/adopted/inf-adopted-algorithm.h
 * @stability: Unstable
 * @see_also: #InfAdoptedSession
 *
 * #InfAdoptedAlgorithm implements the adOPTed algorithm for concurrency
 * control as described in the paper "An integrating, transformation-oriented
 * approach to concurrency control and undo in group editors" by Matthias
 * Ressel, Doris Nitsche-Ruhland and Rul Gunzenhäuser
 * (http://portal.acm.org/citation.cfm?id=240305).
 *
 * It is based on requests, represented by the #InfAdoptedRequest class. If
 * there is at least one local #InfUser in the algorithm's user table, then
 * you can create own requests with the
 * inf_adopted_algorithm_generate_request() function.
 * Remote requests can be applied via
 * inf_adopted_algorithm_execute_request(). This class does not take care of
 * transfering the generated requests to other users which is the scope of
 * #InfAdoptedSession.
 *
 * The implementation is not tied to text editing. It can handle any
 * operations implementing #InfAdoptedOperation as long as they define
 * sufficient transformation functions. The libinftext library provides
 * operations for text editing, see #InfTextInsertOperation and
 * #InfTextDeleteOperation.
 **/

/* This class implements the adOPTed algorithm as described in the paper
 * "An integrating, transformation-oriented approach to concurrency control
 * and undo in group editors" by Matthias Ressel, Doris Nitsche-Ruhland
 * and Rul Gunzenhäuser (http://portal.acm.org/citation.cfm?id=240305).
 *
 * "Reducing the Problems of Group Undo" by Matthias Ressel and Rul
 * Gunzenhäuser (http://portal.acm.org/citation.cfm?doid=320297.320312)
 * might also be worth a read to (better) understand how local group undo
 * is achieved.
 */

/* TODO: Do only cleanup if too much entries in cache? */

/* TODO: If users are not issuing any requests for some time, and we can be
 * sure that we do not need to transform any requests, then remove them from
 * the users array (users_begin, users_end). Readd users as soon as they
 * issue buffer-altering requests. This way we keep the asymptotic complexity
 * dynamically as O(active users^2). */

#include <libinfinity/adopted/inf-adopted-algorithm.h>
#include <libinfinity/inf-signals.h>
#include <libinfinity/inf-i18n.h>

typedef struct _InfAdoptedAlgorithmLocalUser InfAdoptedAlgorithmLocalUser;
struct _InfAdoptedAlgorithmLocalUser {
  InfAdoptedUser* user;
  gboolean can_undo;
  gboolean can_redo;
};

typedef struct _InfAdoptedAlgorithmPrivate InfAdoptedAlgorithmPrivate;
struct _InfAdoptedAlgorithmPrivate {
  /* request log policy */
  guint max_total_log_size;

  InfAdoptedStateVector* current;
  InfAdoptedStateVector* buffer_modified_time;

  InfAdoptedRequest* execute_request;

  InfUserTable* user_table;
  InfBuffer* buffer;

  /* Users in user table. We need to iterate over them very often, so we
   * keep them as array here. */
  InfAdoptedUser** users_begin;
  InfAdoptedUser** users_end;

  GSList* local_users;
};

enum {
  PROP_0,

  /* construct only */
  PROP_USER_TABLE,
  PROP_BUFFER,
  PROP_MAX_TOTAL_LOG_SIZE,
  
  /* read/only */
  PROP_CURRENT_STATE,
  PROP_BUFFER_MODIFIED_STATE
};

enum {
  CAN_UNDO_CHANGED,
  CAN_REDO_CHANGED,

  BEGIN_EXECUTE_REQUEST,
  END_EXECUTE_REQUEST,

  LAST_SIGNAL
};

#define INF_ADOPTED_ALGORITHM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_ALGORITHM, InfAdoptedAlgorithmPrivate))
#define INF_ADOPTED_ALGORITHM_PRIVATE(obj)     ((InfAdoptedAlgorithmPrivate*)(obj)->priv)

static guint algorithm_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(InfAdoptedAlgorithm, inf_adopted_algorithm, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfAdoptedAlgorithm))

static gint64
inf_adopted_algorithm_get_real_time()
{
  /* TODO: Replace by g_get_real_time() once we depend on glib >=2.28 */
  GTimeVal timeval;
  g_get_current_time(&timeval);
  return (gint64)timeval.tv_sec * 1000000 + timeval.tv_usec;
}

/* Returns a new state vector v so that both first and second are causally
 * before v and so that there is no other state vector with the same property
 * that is causally before v. */
/* TODO: Move this to state vector, possibly with a faster O(n)
 * implementation (This is O(n log n), at best) */
static InfAdoptedStateVector*
inf_adopted_algorithm_least_common_successor(InfAdoptedAlgorithm* algorithm,
                                             InfAdoptedStateVector* first,
                                             InfAdoptedStateVector* second)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedUser** user;
  InfAdoptedStateVector* result;
  guint id;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  result = inf_adopted_state_vector_new();

  for(user = priv->users_begin; user != priv->users_end; ++ user)
  {
    id = inf_user_get_id(INF_USER(*user));
    inf_adopted_state_vector_set(
      result,
      id,
      MAX(
        inf_adopted_state_vector_get(first, id),
        inf_adopted_state_vector_get(second, id)
      )
    );
  }

  g_assert(inf_adopted_state_vector_causally_before(first, result));
  g_assert(inf_adopted_state_vector_causally_before(second, result));
  return result;
}

/* Returns a new state vector v so that v is both causally before first and
 * second and so that there is no other state vector with the same property
 * so that v is causally before that vector. */
/* TODO: Move this to state vector, possibly with a faster O(n)
 * implementation (This is O(n log n), at best) */
/* TODO: A version modifying first instead of returning a new result,
 * use in inf_adopted_algorithm_cleanup(). */
static InfAdoptedStateVector*
inf_adopted_algorithm_least_common_predecessor(InfAdoptedAlgorithm* algorithm,
                                               InfAdoptedStateVector* first,
                                               InfAdoptedStateVector* second)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedUser** user;
  InfAdoptedStateVector* result;
  guint id;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  result = inf_adopted_state_vector_new();

  for(user = priv->users_begin; user != priv->users_end; ++ user)
  {
    id = inf_user_get_id(INF_USER(*user));
    inf_adopted_state_vector_set(
      result,
      id,
      MIN(
        inf_adopted_state_vector_get(first, id),
        inf_adopted_state_vector_get(second, id)
      )
    );
  }

  return result;
}

/* Checks whether the given request can be undone (or redone if it is an
 * undo request). In general, a user can perform an undo when
 * there is a request to undo in the request log. However, if there are too
 * much requests between it and the latest request (as determined by
 * max_total_log_size) we cannot issue an undo because others might already
 * have dropped that request from their request log (and therefore no longer
 * compute the Undo operation). */
static gboolean
inf_adopted_algorithm_can_undo_redo(InfAdoptedAlgorithm* algorithm,
                                    InfAdoptedUser* user,
                                    InfAdoptedRequest* request)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequestLog* log;
  guint diff;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  if(request != NULL)
  {
    if(priv->max_total_log_size != G_MAXUINT)
    {
      log = inf_adopted_user_get_request_log(user);
      request = inf_adopted_request_log_original_request(log, request);

      /* TODO: If other requests need to be undone or redone before request
       * can be undone or redone, then we need to include these in the
       * vdiff. */

      diff = inf_adopted_state_vector_vdiff(
        inf_adopted_request_get_vector(request),
        inf_adopted_user_get_vector(user)
      );

      if(diff >= priv->max_total_log_size)
        return FALSE;
      else
        return TRUE;
    }
    else
    {
      /* unlimited */
      return TRUE;
    }
  }
  else
  {
    /* no request to undo */
    return FALSE;
  }
}

/* Updates the can_undo and can_redo fields of the
 * InfAdoptedAlgorithmLocalUsers. */
static void
inf_adopted_algorithm_update_undo_redo(InfAdoptedAlgorithm* algorithm)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedAlgorithmLocalUser* local;
  InfAdoptedRequestLog* log;
  GSList* item;
  gboolean can_undo;
  gboolean can_redo;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = item->data;
    log = inf_adopted_user_get_request_log(local->user);

    can_undo = inf_adopted_algorithm_can_undo_redo(
      algorithm,
      local->user,
      inf_adopted_request_log_next_undo(log)
    );

    can_redo = inf_adopted_algorithm_can_undo_redo(
      algorithm,
      local->user,
      inf_adopted_request_log_next_redo(log)
    );

    if(local->can_undo != can_undo)
    {
      g_signal_emit(
        G_OBJECT(algorithm),
        algorithm_signals[CAN_UNDO_CHANGED],
        0,
        local->user,
        can_undo
      );
    }

    if(local->can_redo != can_redo)
    {
      g_signal_emit(
        G_OBJECT(algorithm),
        algorithm_signals[CAN_REDO_CHANGED],
        0,
        local->user,
        can_redo
      );
    }
  }
}

static InfAdoptedAlgorithmLocalUser*
inf_adopted_algorithm_find_local_user(InfAdoptedAlgorithm* algorithm,
                                      InfAdoptedUser* user)
{
  InfAdoptedAlgorithmPrivate* priv;
  GSList* item;
  InfAdoptedAlgorithmLocalUser* local;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = (InfAdoptedAlgorithmLocalUser*)item->data;
    if(local->user == user)
      return local;
  }

  return NULL;
}

static void
inf_adopted_algorithm_local_user_free(InfAdoptedAlgorithm* algorithm,
                                      InfAdoptedAlgorithmLocalUser* local)
{
  InfAdoptedAlgorithmPrivate* priv;
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  priv->local_users = g_slist_remove(priv->local_users, local);
  g_slice_free(InfAdoptedAlgorithmLocalUser, local);
}

static void
inf_adopted_algorithm_add_user(InfAdoptedAlgorithm* algorithm,
                               InfAdoptedUser* user)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequestLog* log;
  InfAdoptedStateVector* time;
  guint user_count;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  log = inf_adopted_user_get_request_log(user);
  time = inf_adopted_user_get_vector(user);

  inf_adopted_state_vector_set(
    priv->current,
    inf_user_get_id(INF_USER(user)),
    inf_adopted_state_vector_get(time, inf_user_get_id(INF_USER(user)))
  );

  user_count = (priv->users_end - priv->users_begin) + 1;
  priv->users_begin =
    g_realloc(priv->users_begin, sizeof(InfAdoptedUser*) * user_count);
  priv->users_end = priv->users_begin + user_count;
  priv->users_begin[user_count - 1] = user;
}

static void
inf_adopted_algorithm_add_local_user(InfAdoptedAlgorithm* algorithm,
                                     InfAdoptedUser* user)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedAlgorithmLocalUser* local;
  InfAdoptedRequestLog* log;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  local = g_slice_new(InfAdoptedAlgorithmLocalUser);
  local->user = user;
  log = inf_adopted_user_get_request_log(user);

  local->can_undo = inf_adopted_algorithm_can_undo_redo(
    algorithm,
    user,
    inf_adopted_request_log_next_undo(log)
  );

  local->can_redo = inf_adopted_algorithm_can_undo_redo(
    algorithm,
    user,
    inf_adopted_request_log_next_redo(log)
  );

  priv->local_users = g_slist_prepend(priv->local_users, local);
}

static void
inf_adopted_algorithm_add_user_cb(InfUserTable* user_table,
                                  InfUser* user,
                                  gpointer user_data)
{
  InfAdoptedAlgorithm* algorithm;
  algorithm = INF_ADOPTED_ALGORITHM(user_data);

  g_assert(INF_ADOPTED_IS_USER(user));
  inf_adopted_algorithm_add_user(algorithm, INF_ADOPTED_USER(user));
}

static void
inf_adopted_algorithm_add_local_user_cb(InfUserTable* user_table,
                                        InfUser* user,
                                        gpointer user_data)
{
  InfAdoptedAlgorithm* algorithm;
  algorithm = INF_ADOPTED_ALGORITHM(user_data);

  g_assert(INF_ADOPTED_IS_USER(user));
  inf_adopted_algorithm_add_local_user(algorithm, INF_ADOPTED_USER(user));
}

static void
inf_adopted_algorithm_remove_local_user_cb(InfUserTable* user_table,
                                           InfUser* user,
                                           gpointer user_data)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedAlgorithmLocalUser* local;

  algorithm = INF_ADOPTED_ALGORITHM(user_data);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  local =
    inf_adopted_algorithm_find_local_user(algorithm, INF_ADOPTED_USER(user));

  g_assert(local != NULL);
  inf_adopted_algorithm_local_user_free(algorithm, local);
}

/* Checks whether two states are equivalent, meaning one can be reached from
 * the other just by folding. */
static gboolean
inf_adopted_algorithm_buffer_states_equivalent(InfAdoptedAlgorithm* algorithm,
                                               InfAdoptedStateVector* first,
                                               InfAdoptedStateVector* second)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedUser** user_it;
  InfAdoptedUser* user;
  InfAdoptedRequest* request;
  InfAdoptedRequestLog* log;

  guint user_id;
  guint first_n;
  guint second_n;

  g_assert(inf_adopted_state_vector_causally_before(first, second));

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  for(user_it = priv->users_begin; user_it != priv->users_end; ++ user_it)
  {
    user = *user_it;
    user_id = inf_user_get_id(INF_USER(user));
    log = inf_adopted_user_get_request_log(user);

    first_n = inf_adopted_state_vector_get(first, user_id);
    second_n = inf_adopted_state_vector_get(second, user_id);

    /* TODO: This algorithm can probably be optimized by moving it into 
     * request log. */
    while(second_n > first_n)
    {
      /* If we dropped too much state, then we can't say whether the two
       * states are equivalent. Assume they aren't. */
      if(second_n <= inf_adopted_request_log_get_begin(log))
        return FALSE;
      request = inf_adopted_request_log_get_request(log, second_n - 1);

      if(inf_adopted_request_get_request_type(request) ==
         INF_ADOPTED_REQUEST_DO)
      {
        return FALSE;
      }
      else
      {
        request = inf_adopted_request_log_prev_associated(log, request);

        second_n = inf_adopted_state_vector_get(
          inf_adopted_request_get_vector(request),
          user_id
        );
      }
    }

    if(second_n < first_n)
      return FALSE;
  }

  return TRUE;
}

static void
inf_adopted_algorithm_buffer_notify_modified_cb(GObject* object,
                                                GParamSpec* pspec,
                                                gpointer user_data)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;
  gboolean equivalent;

  algorithm = INF_ADOPTED_ALGORITHM(user_data);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  if(inf_buffer_get_modified(INF_BUFFER(object)))
  {
    if(priv->buffer_modified_time != NULL)
    {
      /* If the current state is equivalent to the one the buffer has changed
       * the modified flag the last time, then the modified flag actually
       * changed. Otherwise, we got notified without the flag actually
       * changing. This should not happen normally, but we would lose
       * information about when the buffer has been non-modified the last
       * time otherwise. */
      equivalent = inf_adopted_algorithm_buffer_states_equivalent(
        algorithm,
        priv->buffer_modified_time,
        priv->current
      );

      if(equivalent == TRUE)
      {
        inf_adopted_state_vector_free(priv->buffer_modified_time);
        priv->buffer_modified_time = NULL;
      }
    }
  }
  else
  {
    if(priv->buffer_modified_time != NULL)
      inf_adopted_state_vector_free(priv->buffer_modified_time);

    /* Buffer is not modified anymore */
    priv->buffer_modified_time = inf_adopted_state_vector_copy(priv->current);
  }
}

static void
inf_adopted_algorithm_update_local_user_times(InfAdoptedAlgorithm* algorithm)
{
  /* TODO: I don't think we even need this because we could treat local
   * users implicitely as in-sync with priv->current. It would make some loops
   * a bit more complex, perhaps.
   *
   * Alternative: Let the local users just point to priv->current. */

  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedAlgorithmLocalUser* local;
  GSList* item;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  
  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
  {
    local = item->data;

    inf_adopted_user_set_vector(
      local->user,
      inf_adopted_state_vector_copy(priv->current)
    );
  }
}

/* We can cache requests if:
 * a) they are reversible. If they are not, they will be made reversible
 * later (see inf_adopted_algorithm_execute_request), but the algorithm relies
 * on cached requests being reversible.
 * b) they affect the buffer. If they do not, then they will not be used for
 * later transformations anyway.
 */
static gboolean
inf_adopted_algorithm_can_cache(InfAdoptedRequest* request)
{
  InfAdoptedOperation* operation;
  InfAdoptedOperationFlags flags;

#define INF_ADOPTED_OPERATION_CACHABLE \
  (INF_ADOPTED_OPERATION_AFFECTS_BUFFER | INF_ADOPTED_OPERATION_REVERSIBLE)

  if(inf_adopted_request_get_request_type(request) != INF_ADOPTED_REQUEST_DO)
    return TRUE;

  operation = inf_adopted_request_get_operation(request);
  flags = inf_adopted_operation_get_flags(operation) &
    INF_ADOPTED_OPERATION_CACHABLE;

  return flags == INF_ADOPTED_OPERATION_CACHABLE;
}

/* Translates two requests to state at and then transforms them against each
 * other. The result needs to be unref()ed. */
static InfAdoptedRequest*
inf_adopted_algorithm_transform_request(InfAdoptedAlgorithm* algorithm,
                                        InfAdoptedRequest* request,
                                        InfAdoptedRequest* against,
                                        InfAdoptedStateVector* at)
{
  InfAdoptedRequest* request_at;
  InfAdoptedRequest* against_at;
  InfAdoptedConcurrencyId concurrency_id;
  InfAdoptedStateVector* lcs;
  InfAdoptedRequest* lcs_against;
  InfAdoptedRequest* lcs_request;
  InfAdoptedRequest* result;

  g_assert(
    inf_adopted_state_vector_causally_before(
      inf_adopted_request_get_vector(request),
      at
    )
  );
  g_assert(
    inf_adopted_state_vector_causally_before(
      inf_adopted_request_get_vector(against),
      at
    )
  );

  against_at = inf_adopted_algorithm_translate_request(
    algorithm,
    against,
    at
  );

  request_at = inf_adopted_algorithm_translate_request(
    algorithm,
    request,
    at
  );

  concurrency_id = INF_ADOPTED_CONCURRENCY_NONE;
  if(inf_adopted_request_need_concurrency_id(request_at, against_at) == TRUE)
  {
    lcs = inf_adopted_algorithm_least_common_successor(
      algorithm,
      inf_adopted_request_get_vector(request),
      inf_adopted_request_get_vector(against)
    );

    g_assert(inf_adopted_state_vector_causally_before(lcs, at));

    if(inf_adopted_state_vector_compare(lcs, at) != 0)
    {
      lcs_against = inf_adopted_algorithm_translate_request(
        algorithm,
        against,
        lcs
      );

      lcs_request = inf_adopted_algorithm_translate_request(
        algorithm,
        request,
        lcs
      );
    }
    else
    {
      lcs_against = against_at;
      lcs_request = request_at;

      g_object_ref(lcs_against);
      g_object_ref(lcs_request);
    }

    inf_adopted_state_vector_free(lcs);
  }
  else
  {
    lcs_against = NULL;
    lcs_request = NULL;
  }

  result = inf_adopted_request_transform(
    request_at,
    against_at,
    lcs_request,
    lcs_against
  );

  if(lcs_request != NULL)
    g_object_unref(lcs_request);
  if(lcs_against != NULL)
    g_object_unref(lcs_against);

  g_object_unref(request_at);
  g_object_unref(against_at);

  return result;
}

static InfAdoptedRequest*
inf_adopted_algorithm_translate_request_forward(InfAdoptedAlgorithm* algorithm,
                                                InfAdoptedRequest* request,
                                                InfAdoptedStateVector* to)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedUser** user_it;
  InfAdoptedUser* user;
  InfAdoptedRequestLog* log;
  guint user_id;

  InfAdoptedRequest* cur_req;
  InfAdoptedRequest* next_req;
  InfAdoptedStateVector* vector;

  InfAdoptedRequest* index;
  InfAdoptedRequest* associated;
  InfAdoptedRequest* translated;
  InfAdoptedStateVector* associated_vector;
  guint from_n;
  guint to_n;
  guint associated_index;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  cur_req = request;
  vector = inf_adopted_request_get_vector(cur_req);
  g_object_ref(cur_req);

  while(inf_adopted_state_vector_compare(vector, to) != 0)
  {
    next_req = NULL;

    g_assert(inf_adopted_state_vector_causally_before(vector, to) == TRUE);
    for(user_it = priv->users_begin; user_it != priv->users_end; ++user_it)
    {
      user = *user_it;
      user_id = inf_user_get_id(INF_USER(user));

      if(user_id == inf_adopted_request_get_user_id(cur_req)) continue;

      from_n = inf_adopted_state_vector_get(vector, user_id);
      to_n = inf_adopted_state_vector_get(to, user_id);
      g_assert(from_n <= to_n);

      if(from_n == to_n) continue;

      log = inf_adopted_user_get_request_log(user);
      g_assert(from_n >= inf_adopted_request_log_get_begin(log));
      g_assert(to_n <= inf_adopted_request_log_get_end(log));

      index = inf_adopted_request_log_get_request(log, from_n);
      associated = inf_adopted_request_log_next_associated(log, index);
      if(associated != NULL &&
         inf_adopted_request_get_index(associated) < to_n)
      {
        next_req = inf_adopted_request_fold(
          cur_req,
          user_id,
          inf_adopted_request_get_index(associated) - from_n + 1
        );

        break;
      }
      else
      {
        /* Cannot fold, so transform, if possible. */
        associated = inf_adopted_request_log_original_request(log, index);
        associated_vector = inf_adopted_request_get_vector(associated);
        if(inf_adopted_state_vector_causally_before(associated_vector, vector))
        {
          translated = inf_adopted_algorithm_translate_request(
            algorithm,
            associated,
            vector
          );

          next_req = inf_adopted_algorithm_transform_request(
            algorithm,
            cur_req,
            translated,
            vector
          );

          g_object_unref(translated);
          break;
        }
      }
    }

    /* Late Mirror, only if no transformations or folds possible */
    if(next_req == NULL)
    {
      user_id = inf_adopted_request_get_user_id(cur_req);
      user = INF_ADOPTED_USER(
        inf_user_table_lookup_user_by_id(priv->user_table, user_id)
      );

      log = inf_adopted_user_get_request_log(user);
      from_n = inf_adopted_request_get_index(cur_req);
      to_n = inf_adopted_state_vector_get(to, user_id);
      index = inf_adopted_request_log_get_request(log, from_n);
      associated = inf_adopted_request_log_next_associated(log, index);

      /* The last request might not be in the request log yet, so fetch
       * the index from the log endpoint. */
      if(associated == NULL)
      {
        if(inf_adopted_request_get_request_type(index) == INF_ADOPTED_REQUEST_UNDO)
        {
          if(inf_adopted_request_log_next_redo(log) == index)
            associated_index = to_n;
          else
            associated_index = G_MAXUINT;
        }
        else
        {
          if(inf_adopted_request_log_next_undo(log) == index)
            associated_index = to_n;
          else
            associated_index = G_MAXUINT;
        }
      }
      else
      {
        associated_index = inf_adopted_request_get_index(associated);
      }

      if(associated_index != G_MAXUINT && associated_index <= to_n)
      {
        next_req = inf_adopted_request_mirror(
          cur_req,
          associated_index - from_n
        );
      }
    }

    /* If next_req == NULL, to is not reachable in state space */
    g_assert(next_req != NULL);

    g_object_unref(cur_req);
    cur_req = next_req;
    vector = inf_adopted_request_get_vector(cur_req);
  }

  return cur_req;
}

static void
inf_adopted_algorithm_log_request(InfAdoptedAlgorithm* algorithm,
                                  InfAdoptedUser* user,
                                  InfAdoptedRequest* request)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequestLog* log;
/*  InfAdoptedStateVector* user_vector;
  InfAdoptedStateVector* request_vector;*/
  guint user_id;
  gboolean equivalent;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  log = inf_adopted_user_get_request_log(user);
/*  user_vector = inf_adopted_user_get_vector(user);
  request_vector = inf_adopted_request_get_vector(request);*/
  user_id = inf_user_get_id(INF_USER(user));

  g_assert(inf_adopted_request_get_user_id(request) == user_id);

  /* Now update our local state. There are only updates required if the
   * request is buffer-altering. */
  if(inf_adopted_request_affects_buffer(request))
  {
    /* First, add to request log */
    inf_adopted_request_log_add_request(log, request);
    /* Update current document state */
    inf_adopted_state_vector_add(priv->current, user_id, 1);
    /* Update local user times */
    inf_adopted_algorithm_update_local_user_times(algorithm);

    /* Unset the modified flag of the buffer if the state is equivalent
     * (reachable only by folding, i.e. skipping undo/redo pairs) to the
     * known state when the buffer was not considered modified. */
    if(priv->buffer_modified_time != NULL)
    {
      equivalent = inf_adopted_algorithm_buffer_states_equivalent(
        algorithm,
        priv->buffer_modified_time,
        priv->current
      );

      if(equivalent == TRUE)
      {
        inf_buffer_set_modified(priv->buffer, FALSE);
        inf_adopted_state_vector_free(priv->buffer_modified_time);
        priv->buffer_modified_time =
          inf_adopted_state_vector_copy(priv->current);
      }
      else
      {
        /* The buffer does this automatically when applying an operation: */
        /*inf_buffer_set_modified(priv->buffer, TRUE);*/
      }
    }
    else
    {
      /* When the modified flag is set to false, then we create the
       * buffer_modified_time, so when it is unset, the flag needs to be set.
       * Otherwise, we didn't get notified correctly. */
      g_assert(inf_buffer_get_modified(priv->buffer) == TRUE);
    }
  }
}

static InfAdoptedRequest*
inf_adopted_algorithm_apply_request(InfAdoptedAlgorithm* algorithm,
                                    InfAdoptedUser* user,
                                    InfAdoptedRequest* request,
                                    InfAdoptedRequest* translated,
                                    GError** error)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedOperation* reversible_operation;
  InfAdoptedRequest* log_request;

  GError* local_error;
  gchar* request_str;
  gchar* translated_str;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  /* TODO: Assert that the  user ID
   * for request and translated are the same and that translated can be
   * applied in the current state, and that translated is a DO request. */

  local_error = NULL;
  log_request = NULL;

  /* Apply the operation to the buffer. If this originated from a DO request,
   * make the operation reversible before adding it to the request log. */
  if(inf_adopted_request_get_request_type(request) == INF_ADOPTED_REQUEST_DO)
  {
    /* Make the operation reversible */
    reversible_operation = inf_adopted_operation_apply_transformed(
      inf_adopted_request_get_operation(request),
      inf_adopted_request_get_operation(translated),
      user,
      priv->buffer,
      &local_error
    );

    if(local_error == NULL)
    {
      g_assert(reversible_operation != NULL);

      /* It can happen that we could not make the operation reversible, or
       * that it was reversible already in the first place, in which case we
       * get the same as the original operation from apply_transformed. */
      if(reversible_operation == inf_adopted_request_get_operation(request))
      {
        log_request = request;
        g_object_ref(log_request);
        g_object_unref(reversible_operation);
      }
      else
      {
        /* Create the log request from the reversible operation */
        log_request = inf_adopted_request_new_do(
          inf_adopted_request_get_vector(request),
          inf_adopted_request_get_user_id(request),
          reversible_operation,
          inf_adopted_request_get_receive_time(request)
        );

        inf_adopted_request_set_execute_time(
          log_request,
          inf_adopted_request_get_execute_time(request)
        );

        g_object_unref(reversible_operation);
      }
    }
  }
  else
  {
    inf_adopted_operation_apply(
      inf_adopted_request_get_operation(translated),
      user,
      priv->buffer,
      &local_error
    );

    if(local_error == NULL)
    {
      log_request = request;
      g_object_ref(log_request);
    }
  }

  if(local_error != NULL)
  {
    g_assert(log_request == NULL);

    request_str = inf_adopted_state_vector_to_string(
      inf_adopted_request_get_vector(request)
    );

    translated_str = inf_adopted_state_vector_to_string(
      inf_adopted_request_get_vector(translated)
    );

    g_propagate_prefixed_error(
      error,
      local_error,
      _("Failed to apply request \"%s\" from user \"%s\" at state \"%s\": "),
      request_str,
      inf_user_get_name(INF_USER(user)),
      translated_str
    );

    g_free(request_str);
    g_free(translated_str);
    return NULL;
  }

  g_assert(log_request != NULL);
  return log_request;
}

static void
inf_adopted_algorithm_init(InfAdoptedAlgorithm* algorithm)
{
  InfAdoptedAlgorithmPrivate* priv;

  algorithm->priv = INF_ADOPTED_ALGORITHM_GET_PRIVATE(algorithm);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  priv->max_total_log_size = 2048;
  priv->execute_request = NULL;

  priv->current = inf_adopted_state_vector_new();
  priv->buffer_modified_time = NULL;
  priv->user_table = NULL;
  priv->buffer = NULL;

  /* Lookup by user, user is not refed because the request log holds a 
   * reference anyway. */
  priv->users_begin = NULL;
  priv->users_end = NULL;

  priv->local_users = NULL;
}

static void
inf_adopted_algorithm_constructed_foreach_user_func(InfUser* user,
                                                    gpointer user_data)
{
  InfAdoptedAlgorithm* algorithm;
  algorithm = INF_ADOPTED_ALGORITHM(user_data);

  g_assert(INF_ADOPTED_IS_USER(user));
  inf_adopted_algorithm_add_user(algorithm, INF_ADOPTED_USER(user));
}

static void
inf_adopted_algorithm_constructed_foreach_local_user_func(InfUser* user,
                                                          gpointer user_data)
{
  InfAdoptedAlgorithm* algorithm;
  algorithm = INF_ADOPTED_ALGORITHM(user_data);

  g_assert(INF_ADOPTED_IS_USER(user));
  inf_adopted_algorithm_add_local_user(algorithm, INF_ADOPTED_USER(user));
}

static void
inf_adopted_algorithm_constructed(GObject* object)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;
  gboolean modified;

  G_OBJECT_CLASS(inf_adopted_algorithm_parent_class)->constructed(object);

  algorithm = INF_ADOPTED_ALGORITHM(object);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  /* Add initial users */
  inf_user_table_foreach_user(
    priv->user_table,
    inf_adopted_algorithm_constructed_foreach_user_func,
    algorithm
  );
  
  inf_user_table_foreach_local_user(
    priv->user_table,
    inf_adopted_algorithm_constructed_foreach_local_user_func,
    algorithm
  );

  g_object_get(G_OBJECT(priv->buffer), "modified", &modified, NULL);
  if(modified == FALSE)
    priv->buffer_modified_time = inf_adopted_state_vector_copy(priv->current);
}

static void
inf_adopted_algorithm_dispose(GObject* object)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;
  GList* item;

  algorithm = INF_ADOPTED_ALGORITHM(object);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  while(priv->local_users != NULL)
    inf_adopted_algorithm_local_user_free(algorithm, priv->local_users->data);

  g_free(priv->users_begin);

  if(priv->buffer != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_adopted_algorithm_buffer_notify_modified_cb),
      algorithm
    );

    g_object_unref(priv->buffer);
    priv->buffer = NULL;
  }

  if(priv->buffer_modified_time != NULL)
    inf_adopted_state_vector_free(priv->buffer_modified_time);

  if(priv->user_table != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_adopted_algorithm_add_user_cb),
      algorithm
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_adopted_algorithm_add_local_user_cb),
      algorithm
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_adopted_algorithm_remove_local_user_cb),
      algorithm
    );

    g_object_unref(priv->user_table);
    priv->user_table = NULL;
  }

  G_OBJECT_CLASS(inf_adopted_algorithm_parent_class)->dispose(object);
}

static void
inf_adopted_algorithm_finalize(GObject* object)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;

  algorithm = INF_ADOPTED_ALGORITHM(object);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  inf_adopted_state_vector_free(priv->current);

  G_OBJECT_CLASS(inf_adopted_algorithm_parent_class)->finalize(object);
}

static void
inf_adopted_algorithm_set_property(GObject* object,
                                   guint prop_id,
                                   const GValue* value,
                                   GParamSpec* pspec)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;

  algorithm = INF_ADOPTED_ALGORITHM(object);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  switch(prop_id)
  {
  case PROP_USER_TABLE:
    g_assert(priv->user_table == NULL); /* construct/only */
    priv->user_table = INF_USER_TABLE(g_value_dup_object(value));
    
    g_signal_connect(
      G_OBJECT(priv->user_table),
      "add-user",
      G_CALLBACK(inf_adopted_algorithm_add_user_cb),
      algorithm
    );

    g_signal_connect(
      G_OBJECT(priv->user_table),
      "add-local-user",
      G_CALLBACK(inf_adopted_algorithm_add_local_user_cb),
      algorithm
    );

    g_signal_connect(
      G_OBJECT(priv->user_table),
      "remove-local-user",
      G_CALLBACK(inf_adopted_algorithm_remove_local_user_cb),
      algorithm
    );

    break;
  case PROP_BUFFER:
    g_assert(priv->buffer == NULL); /* construct only */
    g_assert(priv->buffer_modified_time == NULL);

    priv->buffer = INF_BUFFER(g_value_dup_object(value));

    g_signal_connect(
      G_OBJECT(priv->buffer),
      "notify::modified",
      G_CALLBACK(inf_adopted_algorithm_buffer_notify_modified_cb),
      algorithm
    );

    break;
  case PROP_MAX_TOTAL_LOG_SIZE:
    priv->max_total_log_size = g_value_get_uint(value);
    break;
  case PROP_CURRENT_STATE:
  case PROP_BUFFER_MODIFIED_STATE:
    /* read/only */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_algorithm_get_property(GObject* object,
                                   guint prop_id,
                                   GValue* value,
                                   GParamSpec* pspec)
{
  InfAdoptedAlgorithm* log;
  InfAdoptedAlgorithmPrivate* priv;

  log = INF_ADOPTED_ALGORITHM(object);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(log);

  switch(prop_id)
  {
  case PROP_USER_TABLE:
    g_value_set_object(value, G_OBJECT(priv->user_table));
    break;
  case PROP_BUFFER:
    g_value_set_object(value, G_OBJECT(priv->buffer));
    break;
  case PROP_MAX_TOTAL_LOG_SIZE:
    g_value_set_uint(value, priv->max_total_log_size);
    break;
  case PROP_CURRENT_STATE:
    g_value_set_boxed(value, priv->current);
    break;
  case PROP_BUFFER_MODIFIED_STATE:
    g_value_set_boxed(value, priv->buffer_modified_time);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_algorithm_can_undo_changed(InfAdoptedAlgorithm* algorithm,
                                       InfAdoptedUser* user,
                                       gboolean can_undo)
{
  InfAdoptedAlgorithmPrivate* priv;
  GSList* item;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
    if( ((InfAdoptedAlgorithmLocalUser*)item->data)->user == user)
      ((InfAdoptedAlgorithmLocalUser*)item->data)->can_undo = can_undo;
}

static void
inf_adopted_algorithm_can_redo_changed(InfAdoptedAlgorithm* algorithm,
                                       InfAdoptedUser* user,
                                       gboolean can_redo)
{
  InfAdoptedAlgorithmPrivate* priv;
  GSList* item;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
    if( ((InfAdoptedAlgorithmLocalUser*)item->data)->user == user)
      ((InfAdoptedAlgorithmLocalUser*)item->data)->can_redo = can_redo;
}

static void
inf_adopted_algorithm_class_init(InfAdoptedAlgorithmClass* algorithm_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(algorithm_class);

  object_class->constructed = inf_adopted_algorithm_constructed;
  object_class->dispose = inf_adopted_algorithm_dispose;
  object_class->finalize = inf_adopted_algorithm_finalize;
  object_class->set_property = inf_adopted_algorithm_set_property;
  object_class->get_property = inf_adopted_algorithm_get_property;

  algorithm_class->can_undo_changed = inf_adopted_algorithm_can_undo_changed;
  algorithm_class->can_redo_changed = inf_adopted_algorithm_can_redo_changed;
  algorithm_class->begin_execute_request = NULL;
  algorithm_class->end_execute_request = NULL;

  g_object_class_install_property(
    object_class,
    PROP_USER_TABLE,
    g_param_spec_object(
      "user-table",
      "User table",
      "The user table",
      INF_TYPE_USER_TABLE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_BUFFER,
    g_param_spec_object(
      "buffer",
      "Buffer",
      "The buffer to apply operations to",
      INF_TYPE_BUFFER,
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
    PROP_CURRENT_STATE,
    g_param_spec_boxed(
      "current-state",
      "Current state",
      "The state vector describing the current document state",
      INF_ADOPTED_TYPE_STATE_VECTOR,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_BUFFER_MODIFIED_STATE,
    g_param_spec_boxed(
      "buffer-modified-state",
      "Buffer modified state",
      "The state in which the buffer is considered not being modified",
      INF_ADOPTED_TYPE_STATE_VECTOR,
      G_PARAM_READABLE
    )
  );

  /**
   * InfAdoptedAlgorithm::can-undo-changed:
   * @algorithm: The #InfAdoptedAlgorithm for which a user's
   * can-undo state changed.
   * @user: The #InfAdoptedUser whose can-undo state has changed.
   * @can_undo: Whether @user can issue an undo request in the current
   * state or not.
   *
   * This signal is emitted every time the can-undo state of a local user
   * in @algorithm's user table changed. The can-undo state defines whether
   * @user can generate an undo request
   * (via inf_adopted_algorithm_generate_request()) in the current situation, see
   * also inf_adopted_algorithm_can_undo().
   */
  algorithm_signals[CAN_UNDO_CHANGED] = g_signal_new(
    "can-undo-changed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedAlgorithmClass, can_undo_changed),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_ADOPTED_TYPE_USER,
    G_TYPE_BOOLEAN
  );

  /**
   * InfAdoptedAlgorithm::can-redo-changed:
   * @algorithm: The #InfAdoptedAlgorithm for which a user's
   * can-redo state changed.
   * @user: The #InfAdoptedUser whose can-redo state has changed.
   * @can_undo: Whether @user can issue a redo request in the current
   * state or not.
   *
   * This signal is emitted every time the can-redo state of a local user
   * in @algorithm's user table changed. The can-redo state defines whether
   * @user can generate a redo request
   * (via inf_adopted_algorithm_generate_request()) in the current situation, see
   * also inf_adopted_algorithm_can_redo().
   */
  algorithm_signals[CAN_REDO_CHANGED] = g_signal_new(
    "can-redo-changed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedAlgorithmClass, can_redo_changed),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_ADOPTED_TYPE_USER,
    G_TYPE_BOOLEAN
  );
  
  /**
   * InfAdoptedAlgorithm::begin-execute-request:
   * @algorithm: The #InfAdoptedAlgorithm executing a request.
   * @user: The #InfAdoptedUser executing the request.
   * @request: The #InfAdoptedRequest being executed.
   *
   * This signal is emitted every time the algorithm executes a request, i.e.
   * transforms it such that it can be applied to the current state, resolves
   * undo/redo operations and applies the resulting operation to the buffer.
   */
  algorithm_signals[BEGIN_EXECUTE_REQUEST] = g_signal_new(
    "begin-execute-request",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedAlgorithmClass, begin_execute_request),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    INF_ADOPTED_TYPE_USER,
    INF_ADOPTED_TYPE_REQUEST
  );

  /**
   * InfAdoptedAlgorithm::end-execute-request:
   * @algorithm: The #InfAdoptedAlgorithm executing a request.
   * @user: The #InfAdoptedUser executing the request.
   * @request: The #InfAdoptedRequest that was executed.
   * @translated: The result of the request transformation, or %NULL.
   * @error: The error that occurred during execution, or %NULL.
   *
   * This signal is emitted after a request has been executed. The @request
   * parameter is not necessarily the same as the one in the corresponding
   * emission of #InfAdoptedAlgorithm::begin-execute-request, however its
   * effect on the buffer is the same. The difference is that the request in
   * this signal might be reversible while the request in the
   * #InfAdoptedAlgorithm::begin-execute-request emission might not be
   * reversible. The algorithm can make some requests reversible during
   * their execution.
   *
   * The @translated request is the result of the transformation, i.e. it is
   * always a %INF_ADOPTED_REQUEST_DO type request and its state vector
   * corresponds to the current state. It has already been applied on the
   * buffer by the algorithm. If @request is of type %INF_ADOPTED_REQUEST_UNDO
   * or %INF_ADOPTED_REQUEST_REDO then @translated represents the operation
   * that actually was performed on the buffer to undo or redo the effect of
   * a previous request.
   *
   * It can happen that an error occurs during execution. Usually this is due
   * to invalid input, such as a request that cannot be transformed to the
   * current state, a %INF_ADOPTED_REQUEST_UNDO request which has no
   * corresponding %INF_ADOPTED_REQUEST_DO or %INF_ADOPTED_REQUEST_REDO
   * request, or a request that ends up in an invalid operation (e.g.
   * inserting text behind the end of the document). If such an error occurs
   * then @request is the same as the one in the
   * %InfAdoptedAlgorithm::begin-execute-request emission, @translated may or
   * may not be %NULL and @error contains information on the error occured.
   */
  algorithm_signals[END_EXECUTE_REQUEST] = g_signal_new(
    "end-execute-request",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedAlgorithmClass, end_execute_request),
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    4,
    INF_ADOPTED_TYPE_USER,
    INF_ADOPTED_TYPE_REQUEST,
    INF_ADOPTED_TYPE_REQUEST,
    G_TYPE_POINTER /* GError* */
  );
}

/**
 * inf_adopted_algorithm_new: (constructor)
 * @user_table: The table of participating users.
 * @buffer: The buffer to apply operations to.
 *
 * Creates a #InfAdoptedAlgorithm.
 *
 * Returns: (transfer full): A new #InfAdoptedAlgorithm.
 **/
InfAdoptedAlgorithm*
inf_adopted_algorithm_new(InfUserTable* user_table,
                          InfBuffer* buffer)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_BUFFER(buffer), NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_ALGORITHM,
    "user-table", user_table,
    "buffer", buffer,
    NULL
  );

  return INF_ADOPTED_ALGORITHM(object);
}

/**
 * inf_adopted_algorithm_new_full: (constructor)
 * @user_table: The table of participating users.
 * @buffer: The buffer to apply operations to.
 * @max_total_log_size: The maxmimum number of operations to keep in all
 * user's request logs.
 *
 * Note that it is possible that request logs need to grow a bit larger than
 * @max_total_log_size in high-latency situations or when a user does not send
 * status updates frequently. However, when all requests have been
 * processed by all users, the sum of all requests in the logs is guaranteed
 * to be lower or equal to this value.
 *
 * Set to %G_MAXUINT to disable limitation. In theory, this would allow
 * everyone to undo every operation up to the first one ever made. In practise,
 * this issues a huge amount of data that needs to be synchronized on user
 * join and is too expensive to compute anyway.
 *
 * The default value is 2048.
 *
 * Returns: (transfer full): A new #InfAdoptedAlgorithm.
 **/
InfAdoptedAlgorithm*
inf_adopted_algorithm_new_full(InfUserTable* user_table,
                               InfBuffer* buffer,
                               guint max_total_log_size)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_BUFFER(buffer), NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_ALGORITHM,
    "user-table", user_table,
    "buffer", buffer,
    "max-total-log-size", max_total_log_size,
    NULL
  );

  return INF_ADOPTED_ALGORITHM(object);
}

/**
 * inf_adopted_algorithm_get_current:
 * @algorithm: A #InfAdoptedAlgorithm.
 *
 * Returns the current vector time of @algorithm.
 *
 * Returns: (transfer none): A #InfAdoptedStateVector owned by @algorithm.
 **/
InfAdoptedStateVector*
inf_adopted_algorithm_get_current(InfAdoptedAlgorithm* algorithm)
{
  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), NULL);
  return INF_ADOPTED_ALGORITHM_PRIVATE(algorithm)->current;
}

/**
 * inf_adopted_algorithm_get_execute_request:
 * @algorithm: A #InfAdoptedAlgorithm.
 *
 * Returns whether the algorithm is currently transforming a request to the
 * current state and appling its state to the buffer. If it is the function
 * is returning the request that was received and is currently being
 * executed, other wise the function returns %NULL. Note that the request
 * execution is not re-entrant, i.e. two requests cannot be executed
 * concurrently at the same time, or recursively.
 *
 * Returns: (transfer none) (allow-none): The request that @algorithm is
 * currently processing, or %NULL. The return value must not be freed by
 * the caller.
 */
InfAdoptedRequest*
inf_adopted_algorithm_get_execute_request(InfAdoptedAlgorithm* algorithm)
{
  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), NULL);
  return INF_ADOPTED_ALGORITHM_PRIVATE(algorithm)->execute_request;
}

/**
 * inf_adopted_algorithm_generate_request:
 * @algorithm: A #InfAdoptedAlgorithm.
 * @type: The type of request to create.
 * @user: The user for which to create the request.
 * @operation: The operation to perform, or %NULL.
 *
 * Creates a new request that can be applied to the current document state.
 * The request is made by the given user. If operation is of type
 * %INF_ADOPTED_REQUEST_DO, then @operation specifies the operation to be
 * performed. Otherwise, @operation must be %NULL.
 *
 * To apply the effect of the request to the document, run
 * inf_adopted_algorithm_execute_request(). Note that even if the effect
 * is already applied to the document, the function must still be called
 * with the @apply parameter set to %FALSE, so that the algorithm knows that
 * the request has been applied.
 *
 * Returns: (transfer full): A new #InfAdoptedRequest. Free with
 * g_object_unref() when no longer needed.
 */
InfAdoptedRequest*
inf_adopted_algorithm_generate_request(InfAdoptedAlgorithm* algorithm,
                                       InfAdoptedRequestType type,
                                       InfAdoptedUser* user,
                                       InfAdoptedOperation* operation)
{
  InfAdoptedAlgorithmPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);

  g_return_val_if_fail(
    type != INF_ADOPTED_REQUEST_DO || INF_ADOPTED_IS_OPERATION(operation),
    NULL
  );

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  switch(type)
  {
  case INF_ADOPTED_REQUEST_DO:
    return inf_adopted_request_new_do(
      priv->current,
      inf_user_get_id(INF_USER(user)),
      operation,
      inf_adopted_algorithm_get_real_time()
    );
  case INF_ADOPTED_REQUEST_UNDO:
    return inf_adopted_request_new_undo(
      priv->current,
      inf_user_get_id(INF_USER(user)),
      inf_adopted_algorithm_get_real_time()
    );
  case INF_ADOPTED_REQUEST_REDO:
    return inf_adopted_request_new_redo(
      priv->current,
      inf_user_get_id(INF_USER(user)),
      inf_adopted_algorithm_get_real_time()
    );
  default:
    g_return_val_if_reached(NULL);
    return NULL;
  }
}

/**
 * inf_adopted_algorithm_translate_request:
 * @algorithm: A #InfAdoptedAlgorithm.
 * @request: A #InfAdoptedRequest.
 * @to: (transfer none): The state vector to translate @request to.
 *
 * Translates @request so that it can be applied to the document at state @to.
 * @request will not be modified but a new, translated request is returned
 * instead.
 *
 * There are several preconditions for this function to be called. @to must
 * be a reachable point in the state space. Also, requests can only be
 * translated in forward direction, so @request's vector time must be
 * causally before (see inf_adopted_state_vector_causally_before()) @to.
 *
 * Returns: (transfer full): A new or cached #InfAdoptedRequest. Free with
 * g_object_unref() when no longer needed.
 */
InfAdoptedRequest*
inf_adopted_algorithm_translate_request(InfAdoptedAlgorithm* algorithm,
                                        InfAdoptedRequest* request,
                                        InfAdoptedStateVector* to)
{
  InfAdoptedAlgorithmPrivate* priv;
  guint user_id;
  InfUser* plain_user;
  InfAdoptedUser* user;
  InfAdoptedRequestLog* log;
  InfAdoptedRequest* result;

  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);
  g_return_val_if_fail(to != NULL, NULL);

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  user_id = inf_adopted_request_get_user_id(request);
  plain_user = inf_user_table_lookup_user_by_id(priv->user_table, user_id);

  /* Validity checks */
  g_return_val_if_fail(INF_ADOPTED_IS_USER(plain_user), NULL);
  user = INF_ADOPTED_USER(plain_user);
  log = inf_adopted_user_get_request_log(user);

  g_return_val_if_fail(
    inf_adopted_state_vector_causally_before(to, priv->current),
    NULL
  );

  g_return_val_if_fail(
    inf_adopted_state_vector_causally_before(
      inf_adopted_request_get_vector(
        inf_adopted_request_log_original_request(log, request)
      ),
      to
    ),
    NULL
  );

  /* If the request affects the buffer, then it might have been cached
   * earlier. */
  if(inf_adopted_request_affects_buffer(request))
  {
    result = inf_adopted_request_log_lookup_cached_request(log, to);
    if(result != NULL)
    {
      g_object_ref(result);
      return result;
    }
  }

  /* New algorithm */
  result = inf_adopted_algorithm_translate_request_forward(
    algorithm,
    request,
    to
  );

  g_assert(
    inf_adopted_state_vector_compare(
      inf_adopted_request_get_vector(result),
      to
    ) == 0
  );

  if(inf_adopted_algorithm_can_cache(result))
    inf_adopted_request_log_add_cached_request(log, result);
  return result;
}

/**
 * inf_adopted_algorithm_execute_request:
 * @algorithm: A #InfAdoptedAlgorithm.
 * @request: The request to execute.
 * @apply: Whether to apply the request to the buffer.
 * @error: Location to store error information, if any.
 *
 * This function transforms the given request such that it can be applied to
 * the current document state and then applies it the buffer and adds it to
 * the request log of the algorithm, so that it is used for future
 * transformations of other requests.
 *
 * If @apply is %FALSE then the request is not applied to the buffer. In this
 * case, it is assumed that the buffer is already modified, and that the
 * request is made as a result from the buffer modification. This also means
 * that the request must be applicable to the current document state, without
 * requiring transformation.
 *
 * In addition, the function emits the
 * #InfAdoptedAlgorithm::begin-execute-request and
 * #InfAdoptedAlgorithm::end-execute-request signals, and makes
 * inf_adopted_algorithm_get_execute_request() return @request during that
 * period.
 *
 * This allows other code to hook in before and after request processing. This
 * does not cause any loss of generality because this function is not
 * re-entrant anyway: it cannot work when used concurrently by multiple
 * threads nor in a recursive manner, because only when one request has been
 * added to the log the next request can be translated, since it might need
 * the previous request for the translation path and it needs to be translated
 * to a state where the effect of the previous request is included so that it
 * can consistently applied to the buffer.
 *
 * There are also runtime errors that can occur if @request execution fails.
 * In this case the function returns %FALSE and @error is set. Possible
 * reasons for this include @request being an %INF_ADOPTED_REQUEST_UNDO or
 * %INF_ADOPTED_REQUEST_REDO request without there being an operation to
 * undo or redo, or if the translated operation cannot be applied to the
 * buffer. This usually means that the input @request was invalid. However,
 * this is not considered a programmer error because typically requests are
 * received from untrusted input sources such as network connections.
 * Note that there cannot be any runtime errors if @apply is set to %FALSE.
 * In that case it is safe to call the function with %NULL error.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_adopted_algorithm_execute_request(InfAdoptedAlgorithm* algorithm,
                                      InfAdoptedRequest* request,
                                      gboolean apply,
                                      GError** error)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedUser* user;
  InfAdoptedRequestLog* log;

  InfAdoptedRequest* original;
  InfAdoptedRequest* translated;
  InfAdoptedRequest* log_request;

  GError* local_error;
  gchar* request_str;

  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), FALSE);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), FALSE);

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  g_return_val_if_fail(
    inf_adopted_state_vector_causally_before(
      inf_adopted_request_get_vector(request),
      priv->current
    ),
    FALSE
  );

  g_return_val_if_fail(
    apply == TRUE || (
      inf_adopted_state_vector_compare(
        inf_adopted_request_get_vector(request),
        priv->current
      ) == 0 && 
      inf_adopted_request_get_request_type(request) == INF_ADOPTED_REQUEST_DO
    ),
    FALSE
  );

  user = INF_ADOPTED_USER(
    inf_user_table_lookup_user_by_id(
      priv->user_table,
      inf_adopted_request_get_user_id(request)
    )
  );

  g_return_val_if_fail(user != NULL, FALSE);

  /* not re-entrant */
  g_return_val_if_fail(priv->execute_request == NULL, FALSE);
  priv->execute_request = request;

  inf_adopted_request_set_execute_time(
    request,
    inf_adopted_algorithm_get_real_time()
  );

  g_signal_emit(
    G_OBJECT(algorithm),
    algorithm_signals[BEGIN_EXECUTE_REQUEST],
    0,
    user,
    request
  );

  local_error = NULL;
  switch(inf_adopted_request_get_request_type(request))
  {
  case INF_ADOPTED_REQUEST_DO:
    /* nothing to check, DO requests can always be made */
    break;
  case INF_ADOPTED_REQUEST_UNDO:
    if(!inf_adopted_algorithm_can_undo(algorithm, user))
    {
      request_str = inf_adopted_state_vector_to_string(
        inf_adopted_request_get_vector(request)
      );

      g_set_error(
        &local_error,
        g_quark_from_static_string("INF_ADOPTED_ALGORITHM_ERROR"),
        INF_ADOPTED_ALGORITHM_ERROR_NO_UNDO,
        _("The request \"%s\" from user \"%s\" is an UNDO request but there "
          "is no request to be undone."),
        request_str,
        inf_user_get_name(INF_USER(user))
      );
      
      g_free(request_str);
    }

    break;
  case INF_ADOPTED_REQUEST_REDO:
    if(!inf_adopted_algorithm_can_redo(algorithm, user))
    {
      request_str = inf_adopted_state_vector_to_string(
        inf_adopted_request_get_vector(request)
      );

      g_set_error(
        &local_error,
        g_quark_from_static_string("INF_ADOPTED_ALGORITHM_ERROR"),
        INF_ADOPTED_ALGORITHM_ERROR_NO_REDO,
        _("The request \"%s\" from user \"%s\" is a REDO request but there "
          "is no request to be redone."),
        request_str,
        inf_user_get_name(INF_USER(user))
      );

      g_free(request_str);
    }

    break;
  default:
    g_assert_not_reached();
    break;
  }

  if(local_error != NULL)
  {
    g_signal_emit(
      G_OBJECT(algorithm),
      algorithm_signals[END_EXECUTE_REQUEST],
      0,
      user,
      request,
      NULL,
      local_error
    );

    priv->execute_request = NULL;
    g_propagate_error(error, local_error);
    return FALSE;
  }

  log = inf_adopted_user_get_request_log(user);
  original = inf_adopted_request_log_original_request(log, request);

  g_assert(
    inf_adopted_request_get_request_type(original) == INF_ADOPTED_REQUEST_DO
  );

  translated = inf_adopted_algorithm_translate_request(
    algorithm,
    original,
    priv->current
  );

  g_assert(
    inf_adopted_request_get_request_type(translated) == INF_ADOPTED_REQUEST_DO
  );

  inf_signal_handlers_block_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_adopted_algorithm_buffer_notify_modified_cb),
    algorithm
  );

  if(apply == TRUE)
  {
    log_request = inf_adopted_algorithm_apply_request(
      algorithm,
      user,
      request,
      translated,
      &local_error
    );

    if(local_error != NULL)
    {
      inf_signal_handlers_unblock_by_func(
        G_OBJECT(priv->buffer),
        G_CALLBACK(inf_adopted_algorithm_buffer_notify_modified_cb),
        algorithm
      );

      g_signal_emit(
        G_OBJECT(algorithm),
        algorithm_signals[END_EXECUTE_REQUEST],
        0,
        user,
        request,
        translated,
        local_error
      );

      priv->execute_request = NULL;
      g_object_unref(translated);

      g_propagate_error(error, local_error);
      return FALSE;
    }
  }
  else
  {
    log_request = request;
    g_object_ref(request);
  }

  inf_adopted_algorithm_log_request(
    algorithm,
    user,
    log_request
  );

  inf_signal_handlers_unblock_by_func(
    G_OBJECT(priv->buffer),
    G_CALLBACK(inf_adopted_algorithm_buffer_notify_modified_cb),
    algorithm
  );

  inf_adopted_algorithm_update_undo_redo(algorithm);

  g_signal_emit(
    G_OBJECT(algorithm),
    algorithm_signals[END_EXECUTE_REQUEST],
    0,
    user,
    log_request,
    translated,
    NULL
  );

  g_object_unref(translated);
  g_object_unref(log_request);

  priv->execute_request = NULL;
  return TRUE;
}

/**
 * inf_adopted_algorithm_cleanup:
 * @algorithm: A #InfAdoptedAlgorithm.
 *
 * Removes requests in all users request logs which are no longer needed. This
 * includes requests which cannot be undone or redone anymore due to the
 * constraints of the #InfAdoptedAlgorithm:max-total-log-size property, and
 * requests that every participant is guaranteed to have processed already.
 *
 * This function can be called after every executed request to keep memory use
 * to a minimum, or it can be called in regular intervals, or it can also be
 * omitted if the request history should be preserved.
 **/
void
inf_adopted_algorithm_cleanup(InfAdoptedAlgorithm* algorithm)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedStateVector* temp;
  InfAdoptedStateVector* lcp;
  InfAdoptedUser** user;
  InfAdoptedRequestLog* log;
  InfAdoptedRequest* req;
  InfAdoptedStateVector* req_vec;
  InfAdoptedStateVector* low_vec;
  gboolean req_before_lcp;
  guint n;
  guint id;
  guint vdiff;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  g_assert(priv->users_begin != priv->users_end);

  /* We don't do cleanup in case the total log size is G_MAXUINT, which
   * means we keep all requests without limit. */
  if(priv->max_total_log_size == G_MAXUINT)
    return;

  /* We remove every request whose "lower related" request has a greater
   * vdiff to the lcp then max-total-log-size from both request log and
   * the request cache. The lcp is a common state that _all_ sites are
   * guaranteed to have reached. Related requests not causally before lcp are
   * always kept, though. This should not happen if max-total-log-size is
   * reasonably high and the network latency reasonably low, but we can't
   * guarentee it does not happen. It just means we can't drop a request that
   * another site has not yet processed, although it is old enough.*/

  /* The "upper related" request of a request A is the next-newer request so
   * that all requests before the "upper related" request can be removed
   * without any remaining request in the log still refering to a removed
   * one. See also inf_adopted_request_log_upper_related(). */

  /* Note that we could be more intelligent here. It would be enough if the
   * oldest request of a set of related requests is old enough to be removed.
   * But we would need to make sure that the requests between the oldest and
   * the upper related are not required anymore. I am not sure whether there
   * are additional conditions. However, in the current case, some requests
   * are just kept a bit longer than necessary, in favor of simplicity. */

  lcp = inf_adopted_state_vector_copy(priv->current);
  for(user = priv->users_begin; user != priv->users_end; ++ user)
  {
    if(inf_user_get_status(INF_USER(*user)) != INF_USER_UNAVAILABLE)
    {
      temp = inf_adopted_algorithm_least_common_predecessor(
        algorithm,
        lcp,
        inf_adopted_user_get_vector(*user)
      );

      inf_adopted_state_vector_free(lcp);
      lcp = temp;
    }
  }

  for(user = priv->users_begin; user != priv->users_end; ++ user)
  {
    id = inf_user_get_id(INF_USER(*user));
    log = inf_adopted_user_get_request_log(*user);
    n = inf_adopted_request_log_get_begin(log);

    /* Remove all sets of related requests whose upper related request has
     * a large enough vdiff to lcp. */
    while(n < inf_adopted_request_log_get_end(log))
    {
      req = inf_adopted_request_log_upper_related(log, n);
      req_vec = inf_adopted_request_get_vector(req);

      /* We can only remove requests that are causally before lcp,
       * as explained above. We need to compare the target vector time of the
       * request, though, and not the source which is why we increase the
       * request's user's component by one. This is because of the fact that
       * the request needs to be available to reach its target vector time. */
      req_before_lcp = inf_adopted_state_vector_causally_before_inc(
        req_vec,
        lcp,
        id
      );

      if(!req_before_lcp)
        break;

      /* TODO: Experimentally, I try using the lower related for the vdiff
       * here. If it doesn't work out, then we will need to use the upper
       * related. Note that changing this requires changing the cleanup
       * tests, too. */
      low_vec = inf_adopted_request_get_vector(
        inf_adopted_request_log_get_request(log, n)
      );

      vdiff = inf_adopted_state_vector_vdiff(low_vec, lcp);

      /* TODO: Again, I experimentally changed <= to < here. If the vdiff is
       * equal to the log size, then nobody can do anything with the request
       * set anymore: Everybody already processed every request in the set
       * (otherwise, the causally_before_ check above would have failed), and
       * the user in question cannot Undo anymore since this would require one
       * too much request in the request log. Note again that changing this
       * requires changing the cleanup tests, too. */
      if(vdiff < priv->max_total_log_size)
        break;

      /* Check next set of related requests */
      n = inf_adopted_state_vector_get(req_vec, id) + 1;
    }

    inf_adopted_request_log_remove_requests(log, n);
  }

  inf_adopted_state_vector_free(lcp);
}

/**
 * inf_adopted_algorithm_can_undo:
 * @algorithm: A #InfAdoptedAlgorithm.
 * @user: A local #InfAdoptedUser.
 *
 * Returns whether @user can issue an undo request in the current state. Note
 * that if @user is non-local, then the result of this function does not
 * depend on the current state but on the state that we know @user is
 * guaranteed to have reached. This is because @user might still issue an
 * Undo request even if the max-total-log-size is already exceeded if @user
 * does not know yet that it is exceeded.
 *
 * Returns: %TRUE if Undo is possible, %FALSE otherwise.
 **/
gboolean
inf_adopted_algorithm_can_undo(InfAdoptedAlgorithm* algorithm,
                               InfAdoptedUser* user)
{
  InfAdoptedAlgorithmLocalUser* local;
  InfAdoptedRequestLog* log;

  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), FALSE);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), FALSE);

  local = inf_adopted_algorithm_find_local_user(algorithm, user);
  if(local != NULL)
  {
    return local->can_undo;
  }
  else
  {
    log = inf_adopted_user_get_request_log(user);

    return inf_adopted_algorithm_can_undo_redo(
      algorithm,
      user,
      inf_adopted_request_log_next_undo(log)
    );
  }
}

/**
 * inf_adopted_algorithm_can_redo:
 * @algorithm: A #InfAdoptedAlgorithm.
 * @user: A local #InfAdoptedUser.
 *
 * Returns whether @user can issue a redo request in the current state. Note
 * that if @user is non-local, then the result of this function does not
 * depend on the current state but on the state that we know @user is
 * guaranteed to have reached. This is because @user might still issue a
 * Redo request even if the max-total-log-size is already exceeded if @user
 * does not know yet that it is exceeded.
 *
 * Returns: %TRUE if Redo is possible, %FALSE otherwise.
 **/
gboolean
inf_adopted_algorithm_can_redo(InfAdoptedAlgorithm* algorithm,
                               InfAdoptedUser* user)
{
  InfAdoptedAlgorithmLocalUser* local;
  InfAdoptedRequestLog* log;

  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), FALSE);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), FALSE);

  local = inf_adopted_algorithm_find_local_user(algorithm, user);
  if(local != NULL)
  {
    return local->can_redo;
  }
  else
  {
    log = inf_adopted_user_get_request_log(user);

    return inf_adopted_algorithm_can_undo_redo(
      algorithm,
      user,
      inf_adopted_request_log_next_redo(log)
    );
  }
}

/* vim:set et sw=2 ts=2: */
