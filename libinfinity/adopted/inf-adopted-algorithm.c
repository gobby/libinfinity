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

#include <libinfinity/adopted/inf-adopted-algorithm.h>
#include <libinfinity/inf-marshal.h>

/* This class implements the adOPTed algorithm as described in the paper
 * "An integrating, transformation-oriented approach to concurrency control
 * and undo in group editors" by Matthias Ressel, Doris Nitsche-Ruhland
 * and Rul Gunzenhäuser (http://portal.acm.org/citation.cfm?id=240305). Don't
 * even try to understand (the interesting part) of this code without having
 * read it.
 *
 * "Reducing the Problems of Group Undo" by Matthias Ressel and Rul
 * Gunzenhäuser (http://portal.acm.org/citation.cfm?doid=320297.320312)
 * might also be worth a read to (better) understand how local group undo
 * is achieved.
 */

typedef struct _InfAdoptedAlgorithmLocalUser InfAdoptedAlgorithmLocalUser;
struct _InfAdoptedAlgorithmLocalUser {
  InfAdoptedUser* user;
  gboolean can_undo;
  gboolean can_redo;
};

/* Scheduled removal of a group of related requests in a request log */
typedef struct _InfAdoptedAlgorithmLogRemoval InfAdoptedAlgorithmLogRemoval;
struct _InfAdoptedAlgorithmLogRemoval {
  InfAdoptedRequestLog* log;
  InfAdoptedRequest* upper; /* newest request of block being removed */
  GSList* blockers; /* Requests that block this removal */
};

typedef struct _InfAdoptedAlgorithmPrivate InfAdoptedAlgorithmPrivate;
struct _InfAdoptedAlgorithmPrivate {
  /* request log policy */
  guint max_total_log_size;

  InfAdoptedStateVector* current;
  InfBuffer* buffer;
  /* double-linked so we can easily remove an element from the middle */
  GList* queue;
  GHashTable* request_logs; /* TODO: This is not necessary anymore */

  /* TODO: Cache g_hash_table_get_keys(request_logs). Those are used
   * just everywhere. */

  GSList* local_users;
};

enum {
  PROP_0,

  /* construct only */
  PROP_BUFFER,
  PROP_MAX_TOTAL_LOG_SIZE
};

enum {
  CAN_UNDO_CHANGED,
  CAN_REDO_CHANGED,

  LAST_SIGNAL
};

#define INF_ADOPTED_ALGORITHM_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_ALGORITHM, InfAdoptedAlgorithmPrivate))

static GObjectClass* parent_class;
static guint algorithm_signals[LAST_SIGNAL];

/* Computes a vdiff between two vectors first and second with first <= second.
 * The vdiff is the sum of the differences of all vector components. */
/* TODO: Move this to state vector, possibly with a faster O(n)
 * implementation (This is O(n log n), at best) */
static guint
inf_adopted_algorithm_state_vector_vdiff(InfAdoptedAlgorithm* algorithm,
                                         InfAdoptedStateVector* first,
                                         InfAdoptedStateVector* second)
{
  InfAdoptedAlgorithmPrivate* priv;
  GList* keys;
  GList* item;
  guint result;
  guint id;

  g_assert(inf_adopted_state_vector_causally_before(first, second) == TRUE);

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  keys = g_hash_table_get_keys(priv->request_logs);
  result = 0;

  for(item = keys; item != NULL; item = g_list_next(item))
  {
    id = inf_user_get_id(INF_USER(item->data));
    result += (inf_adopted_state_vector_get(second, id) -
      inf_adopted_state_vector_get(first, id));
  }

  g_list_free(keys);
  return result;
}

/* Returns a new state vector v so that both first and second are causally
 * before v and so that there is no other state vector that is causally before
 * v which is also causally before first and second. */
/* TODO: Move this to state vector, possibly with a faster O(n)
 * implementation (This is O(n log n), at best) */
static InfAdoptedStateVector*
inf_adopted_algorithm_least_common_successor(InfAdoptedAlgorithm* algorithm,
                                             InfAdoptedStateVector* first,
                                             InfAdoptedStateVector* second)
{
  InfAdoptedAlgorithmPrivate* priv;
  GList* keys;
  GList* item;
  InfAdoptedStateVector* result;
  guint id;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  keys = g_hash_table_get_keys(priv->request_logs);
  result = inf_adopted_state_vector_new();

  for(item = keys; item != NULL; item = g_list_next(item))
  {
    id = inf_user_get_id(INF_USER(item->data));
    inf_adopted_state_vector_set(
      result,
      id,
      MAX(
        inf_adopted_state_vector_get(first, id),
        inf_adopted_state_vector_get(second, id)
      )
    );
  }

  g_list_free(keys);
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
                                    InfAdoptedRequestLog* log,
                                    InfAdoptedRequest* request)
{
  InfAdoptedAlgorithmPrivate* priv;
  guint diff;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  if(request != NULL)
  {
    if(priv->max_total_log_size > 0)
    {
      request = inf_adopted_request_log_original_request(log, request);

      diff = inf_adopted_algorithm_state_vector_vdiff(
        algorithm,
        inf_adopted_request_get_vector(request),
        priv->current
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

static void
inf_adopted_algorithm_find_blockers(InfAdoptedAlgorithm* algorithm,
                                    GSList* removals)
{
  InfAdoptedAlgorithmPrivate* priv;
  GList* keys;
  GList* key_item;
  GSList* rem_item;

  InfAdoptedAlgorithmLogRemoval* removal;
  InfAdoptedRequestLog* log;
  guint user_id;
  guint upper_comp; /* TODO: Cache this in InfAdoptedAlgorithmLogRemoval? */
  guint begin;
  guint end;
  guint query;

  InfAdoptedRequest* candidate;
  InfAdoptedStateVector* vector;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  keys = g_hash_table_get_keys(priv->request_logs);
 
  for(rem_item = removals;
      rem_item != NULL;
      rem_item = g_slist_next(rem_item))
  {
    removal = rem_item->data;
    user_id = inf_adopted_request_get_user_id(removal->upper);

    /* TODO: InfAdoptedRequest should have a method to find that component,
     * it is really often used in InfAdoptedAlgorithm. */
    upper_comp = inf_adopted_state_vector_get(
      inf_adopted_request_get_vector(removal->upper),
      user_id
    );

    /* Check potential blockers */
    for(key_item = keys; key_item != NULL; key_item = g_list_next(key_item))
    {
      log = g_hash_table_lookup(priv->request_logs, key_item->data);
      g_assert(log != NULL);

      begin = inf_adopted_request_log_get_begin(log);
      end = inf_adopted_request_log_get_end(log);

      /* Binary search to find request with upper_comp in
       * user direction, or the first request below. These are requests that
       * still need the request(s) being removed when undone or redone. */
      vector = NULL;
      while(begin < end)
      {
        /* Note this never tries to access the request at 'end'
         * (which does not exist in the log). */
        query = (begin + end)/2;
        candidate = inf_adopted_request_log_get_request(log, query);
        vector = inf_adopted_request_get_vector(candidate);

        if(inf_adopted_state_vector_get(vector, user_id) <= upper_comp)
          begin = MAX(query, begin + 1);
        else
          end = MIN(query, end - 1);
      }

      /* The search above yields the first request with a component
       * greater than upper_comp. Note begin == end. */
      if(vector != NULL && begin > inf_adopted_request_log_get_begin(log))
      {
        -- begin;
        candidate = inf_adopted_request_log_get_request(log, begin);
        vector = inf_adopted_request_get_vector(candidate);
        g_assert(inf_adopted_state_vector_get(vector, user_id) <= upper_comp);

       /* TODO: If the found candidate cannot be un/redone because
        * it is too old (vdiff > max_total_log_size), it is not a blocker. */

        removal->blockers = g_slist_prepend(removal->blockers, candidate);
      }
    }
  }

  g_list_free(keys);
}

/* Creates a list of removals. All requests that are too old
 * (according to max_total_log_size) are recorded. */
static GSList*
inf_adopted_algorithm_create_removals(InfAdoptedAlgorithm* algorithm)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequestLog* log;
  InfAdoptedRequest* request;
  InfAdoptedAlgorithmLogRemoval* removal;
  GList* keys;
  GList* item;
  GList* item2;
  guint vdiff;
  guint min_vdiff;
  GSList* result;
  gboolean test;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  keys = g_hash_table_get_keys(priv->request_logs);
  result = NULL;

  for(item = keys; item != NULL; item = g_list_next(item))
  {
    log = g_hash_table_lookup(priv->request_logs, item->data);
    g_assert(log != NULL);

    /* No entry in log */
    if(inf_adopted_request_log_get_begin(log) ==
       inf_adopted_request_log_get_end(log))
    {
      continue;
    }

    request = inf_adopted_request_log_get_request(
      log,
      inf_adopted_request_log_get_begin(log)
    );

    /* Find vdiff from oldest request in log to every user's current state */
    min_vdiff = G_MAXUINT;
    for(item2 = keys; item2 != NULL; item2 = g_list_next(item2))
    {
      /* We should not have deleted a previous request in the log if this
       * does not hold. */
      /* TODO: Consider 3 users. The first makes a request. Then, the second
       * makes another request. The time of this request is 1:1;2:0;3:0.
       * However, the third user is still (known to be) at 1:0;2:0;3:0.
       * In this case this assertion does not hold. Probably, excess
       * components should be treated as 0 in the vdiff (since in the
       * worst case that component in the user's vector just climbs up to be
       * equal to the request's component. */
      /*g_assert(
        inf_adopted_state_vector_causally_before(
          inf_adopted_request_get_vector(request),
          inf_adopted_user_get_vector(INF_ADOPTED_USER(item2->data))
        ) == TRUE
      );*/

      test = inf_adopted_state_vector_causally_before(
        inf_adopted_request_get_vector(request),
        inf_adopted_user_get_vector(INF_ADOPTED_USER(item2->data))
      );

      if(test == TRUE)
      {
        vdiff = inf_adopted_algorithm_state_vector_vdiff(
          algorithm,
          inf_adopted_request_get_vector(request),
          inf_adopted_user_get_vector(INF_ADOPTED_USER(item2->data))
        );
      }
      else
      {
        vdiff = 0;
      }

      if(vdiff < min_vdiff)
        min_vdiff = vdiff;
    }

    /* Schedule for removal if too old */
    if(vdiff > priv->max_total_log_size)
    {
      removal = g_slice_new(InfAdoptedAlgorithmLogRemoval);
      removal->log = log;
      removal->upper = inf_adopted_request_log_upper_related(log, request);
      removal->blockers = NULL; /* No blockers yet */
      result = g_slist_prepend(result, removal);

      /* TODO: Also record next request, if any and old enough. */
      /* TODO: Detect blockers here. */
    }
  }

  g_list_free(keys);

  inf_adopted_algorithm_find_blockers(algorithm, result);
  return result;
}

static void
inf_adopted_algorithm_perform_removals(InfAdoptedAlgorithm* algorithm,
                                       GSList* removals)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedAlgorithmLogRemoval* removal;
  GSList* rem_item;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  for(rem_item = removals;
      rem_item != NULL;
      rem_item = g_slist_next(rem_item))
  {
    removal = rem_item->data;
    if(removal->blockers == NULL)
    {
      /* There are no blocking requests, remove from log */
      inf_adopted_request_log_remove_requests(
        removal->log,
        /* TODO: Cache that value in InfAdoptedAlgorithmLogRemoval? */
        inf_adopted_state_vector_get(
          inf_adopted_request_get_vector(removal->upper),
          inf_adopted_request_get_user_id(removal->upper)
        ) + 1
      );
    }

    /* TODO: Removal could also be performed if all blocking requests are
     * also removed. */
  }
}

/* TODO: This is "only" some kind of garbage collection that does not need
 * to be done after _every_ request received. */
static void
inf_adopted_algorithm_update_request_logs(InfAdoptedAlgorithm* algorithm)
{
  /* Procedure: */
  /* First step: Find groups of requests scheduled for removal */
  /* Second step: Foreach group: Find requests that block removal */
  /* Third step: Remove unblocked groups, and those groups that are only
   * blocked by requests which can also be removed. */

  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedAlgorithmLogRemoval* removal;
  GSList* removals;
  GSList* item;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  removals = inf_adopted_algorithm_create_removals(algorithm);
  inf_adopted_algorithm_perform_removals(algorithm, removals);

  for(item = removals; item != NULL; item = g_slist_next(item))
  {
    removal = item->data;

    g_slist_free(removal->blockers);
    g_slice_free(InfAdoptedAlgorithmLogRemoval, removal);
  }
  
  g_slist_free(removals);
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
    log = g_hash_table_lookup(priv->request_logs, local->user);
    g_assert(log != NULL);

    can_undo = inf_adopted_algorithm_can_undo_redo(
      algorithm,
      log,
      inf_adopted_request_log_next_undo(log)
    );

    can_redo = inf_adopted_algorithm_can_undo_redo(
      algorithm,
      log,
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

/* Required by inf_adopted_algorithm_user_notify_cb */
static void
inf_adopted_algorithm_local_user_free(InfAdoptedAlgorithm* algorithm,
                                      InfAdoptedAlgorithmLocalUser* local);

static void
inf_adopted_algorithm_user_notify_cb(GObject* object,
                                     GParamSpec* pspec,
                                     gpointer user_data)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedUser* user;
  InfAdoptedAlgorithmLocalUser* local;
  InfAdoptedRequestLog* log;
  GSList* item;

  algorithm = INF_ADOPTED_ALGORITHM(user_data);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  user = INF_ADOPTED_USER(object);

  /* Find existing local user */
  for(item = priv->local_users; item != NULL; item = g_slist_next(item))
    if(((InfAdoptedAlgorithmLocalUser*)item->data)->user == user)
      break;

  /* Update priv->local_users list. Unavailable users do not count. */
  if((inf_user_get_flags(INF_USER(user)) & INF_USER_LOCAL) != 0 &&
     inf_user_get_status(INF_USER(user)) != INF_USER_UNAVAILABLE)
  {
    log = g_hash_table_lookup(priv->request_logs, user);
    g_assert(log != NULL);

    if(item == NULL)
    {
      /* TODO: Set vector time of local user to current? This is currently
       * done by InfAdoptedSession. */

      local = g_slice_new(InfAdoptedAlgorithmLocalUser);
      local->user = user;

      local->can_undo = inf_adopted_algorithm_can_undo_redo(
        algorithm,
        log,
        inf_adopted_request_log_next_undo(log)
      );

      local->can_redo = inf_adopted_algorithm_can_undo_redo(
        algorithm,
        log,
        inf_adopted_request_log_next_redo(log)
      );

      priv->local_users = g_slist_prepend(priv->local_users, local);
    }
  }
  else
  {
    if(item != NULL)
    {
      inf_adopted_algorithm_local_user_free(algorithm, item->data);
    }
  }
}

static void
inf_adopted_algorithm_local_user_free(InfAdoptedAlgorithm* algorithm,
                                      InfAdoptedAlgorithmLocalUser* local)
{
  InfAdoptedAlgorithmPrivate* priv;
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(local->user),
    G_CALLBACK(inf_adopted_algorithm_user_notify_cb),
    algorithm
  );

  priv->local_users = g_slist_remove(priv->local_users, local);
  g_slice_free(InfAdoptedAlgorithmLocalUser, local);
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

/* TODO: Move this into request log? */
static gboolean
inf_adopted_algorithm_is_component_reachable(InfAdoptedAlgorithm* algorithm,
                                             InfAdoptedStateVector* v,
                                             InfAdoptedUser* component)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequestLog* log;
  InfAdoptedRequest* request;
  InfAdoptedRequestType type;
  InfAdoptedStateVector* current;
  InfAdoptedStateVector* w;
  gboolean result;
  guint n;
  
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  log = g_hash_table_lookup(priv->request_logs, component);
  current = v;
  g_assert(log != NULL);

  for(;;)
  {
    n = inf_adopted_state_vector_get(
      current,
      inf_user_get_id(INF_USER(component))
    );

    if(n == 0) return TRUE;

    request = inf_adopted_request_log_get_request(log, n - 1);
    type = inf_adopted_request_get_request_type(request);

    if(type == INF_ADOPTED_REQUEST_DO)
    {
      /* TODO: Can we also use inf_adopted_request_get_vector(request)
       * directly? Tests still seem to pass. */
      w = inf_adopted_state_vector_copy(
        inf_adopted_request_get_vector(request)
      );

      inf_adopted_state_vector_add(
        w,
        inf_adopted_request_get_user_id(request),
        1
      );

      result = inf_adopted_state_vector_causally_before(w, v);

      inf_adopted_state_vector_free(w);
      return result;
    }
    else
    {
      current = inf_adopted_request_get_vector(
        inf_adopted_request_log_prev_associated(log, request)
      );
    }
  }
}

static gboolean
inf_adopted_algorithm_is_reachable(InfAdoptedAlgorithm* algorithm,
                                   InfAdoptedStateVector* v)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedUser* user;
  GList* keys;
  GList* item;
  gboolean result;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  g_assert(
    inf_adopted_state_vector_causally_before(v, priv->current) == TRUE
  );

  /* TODO: Perhaps the keys should be given to this function so it does not
   * need to allocate them every time when called more than once. */
  keys = g_hash_table_get_keys(priv->request_logs);
  for(item = keys; item != NULL; item = g_list_next(item))
  {
    user = INF_ADOPTED_USER(item->data);
    result = inf_adopted_algorithm_is_component_reachable(algorithm, v, user);

    if(result == FALSE)
    {
      g_list_free(keys);
      return FALSE;
    }
  }
  
  g_list_free(keys);
  return TRUE;
}

static gboolean
inf_adopted_algorithm_translate_find_func(gpointer key,
                                          gpointer value,
                                          gpointer user_data)
{
  if(inf_user_get_id(INF_USER(key)) == GPOINTER_TO_UINT(user_data))
    return TRUE;

  return FALSE;
}

/* This function may change request, but it may also return a new request,
 * depending on what is necessary to do the translation. In any case, the
 * function adds a reference to the return value (which may or may not
 * be request) which you have to unref when you are finished with it */
/* TODO: Split this into multiple subroutines */
static InfAdoptedRequest*
inf_adopted_algorithm_translate_request(InfAdoptedAlgorithm* algorithm,
                                        InfAdoptedRequest* request,
                                        InfAdoptedStateVector* to)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfUser* user; /* points to current user */
  guint user_id; /* Corresponding ID */
  InfAdoptedRequestLog* log; /* points to current log */
  InfAdoptedStateVector* vector; /* always points to request's vector */
  InfAdoptedStateVector* original_vector;

  InfAdoptedRequest* associated; /* TODO: Rename this as it is not only used for associated requests */
  InfAdoptedRequest* against; /* Transformation partner */
  InfAdoptedRequest* original;
  InfAdoptedRequest* result;
  InfAdoptedStateVector* v;

  InfAdoptedStateVector* lcs;
  InfAdoptedRequest* lcs_result;
  InfAdoptedRequest* lcs_against;
  

  GList* keys;
  GList* item;
  guint n;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  user = NULL;
  user_id = inf_adopted_request_get_user_id(request);

  /* TODO: Replace that hash table by a simply linked list, or possibly
   * a hash table from user ID to request log and/or user. */
  log = g_hash_table_find(
    priv->request_logs,
    inf_adopted_algorithm_translate_find_func,
    GUINT_TO_POINTER(user_id)
  );

  g_assert(log != NULL);

  original = inf_adopted_request_log_original_request(log, request);
  original_vector = inf_adopted_request_get_vector(original);

  g_assert(inf_adopted_state_vector_causally_before(to, priv->current));
  g_assert(inf_adopted_state_vector_causally_before(original_vector, to));
  g_assert(inf_adopted_algorithm_is_reachable(algorithm, to) == TRUE);

  vector = inf_adopted_request_get_vector(request);
  v = inf_adopted_state_vector_copy(to);
  keys = NULL;

  if(inf_adopted_request_get_request_type(request) != INF_ADOPTED_REQUEST_DO)
  {
    /* Try late mirror if this is not a do request */
    associated = inf_adopted_request_log_prev_associated(log, request);
    g_assert(associated != NULL);

    inf_adopted_state_vector_set(
      v,
      user_id,
      inf_adopted_state_vector_get(
        inf_adopted_request_get_vector(associated),
        user_id
      )
    );

    if(inf_adopted_algorithm_is_reachable(algorithm, v))
    {
      /* We cannot modify (=call translate_request on) associated as such
       * because it is in a request log. We could perhaps save the copy if
       * associated.v() is equal to v, and therefore does not need further
       * transformation. */
      associated = inf_adopted_request_copy(associated);

      result = inf_adopted_algorithm_translate_request(
        algorithm,
        associated,
        v
      );

      g_object_unref(G_OBJECT(associated));

      inf_adopted_request_mirror(
        result,
        inf_adopted_state_vector_get(to, user_id) -
          inf_adopted_state_vector_get(v, user_id)
      );

      goto done;
    }
    else
    {
      /* Reset v for other routines to use */
      inf_adopted_state_vector_set(
        v,
        user_id,
        inf_adopted_state_vector_get(to, user_id)
      );
    }
  }
  else
  {
    /* The request is a do request: We might already be done if we are
     * already at the state we are supposed to translate request to. */
    if(inf_adopted_state_vector_compare(vector, to) == 0)
    {
      g_object_ref(G_OBJECT(request));
      result = request;
      goto done;
    }
  }

  /* TODO: It is stupid to allocate these keys for every recursive
   * translate_request call. We should do it once in execute_request and
   * pass to translate_request. */
  keys = g_hash_table_get_keys(priv->request_logs);

  for(item = keys; item != NULL; item = g_list_next(item))
  {
    user = INF_USER(item->data);
    user_id = inf_user_get_id(user);
    if(user_id == inf_adopted_request_get_user_id(request)) continue;

    n = inf_adopted_state_vector_get(v, user_id);
    if(n == 0) continue;

    log = g_hash_table_lookup(priv->request_logs, user);
    g_assert(log != NULL);

    /* Fold late, if possible */
    associated = inf_adopted_request_log_get_request(log, n - 1);
    if(inf_adopted_request_get_request_type(associated) !=
       INF_ADOPTED_REQUEST_DO)
    {
      associated = inf_adopted_request_log_prev_associated(log, associated);
      g_assert(associated != NULL);

      inf_adopted_state_vector_set(
        v,
        user_id,
        inf_adopted_state_vector_get(
          inf_adopted_request_get_vector(associated),
          user_id
        )
      );

      if(inf_adopted_algorithm_is_reachable(algorithm, v) &&
         inf_adopted_state_vector_causally_before(vector, v) == TRUE)
      {
        result = inf_adopted_algorithm_translate_request(
          algorithm,
          request,
          v
        );

        inf_adopted_request_fold(
          result,
          user_id,
          inf_adopted_state_vector_get(to, user_id) -
            inf_adopted_state_vector_get(v, user_id)
        );

        goto done;
      }
      else
      {
        /* Reset v to be reused */
        inf_adopted_state_vector_set(
          v,
          user_id,
          inf_adopted_state_vector_get(to, user_id)
        );
      }
    }
    /* Transform into direction we are not going to fold later */
    else if(inf_adopted_state_vector_get(/*original_*/vector, user_id) <
            inf_adopted_state_vector_get(to, user_id))
    {
      inf_adopted_state_vector_set(v, user_id, n - 1);
      if(inf_adopted_algorithm_is_reachable(algorithm, v))
      {
        /* TODO: We already have this from line 857 */
        associated = inf_adopted_request_log_get_request(log, n - 1);
        associated = inf_adopted_request_copy(associated);

        g_assert(inf_adopted_state_vector_causally_before(vector, v));

        g_assert(
          inf_adopted_state_vector_causally_before(
            inf_adopted_request_get_vector(associated),
            v
          )
        );

        /* Find least common successor and transform both requests
         * through that point. */
        lcs = inf_adopted_algorithm_least_common_successor(
          algorithm,
          inf_adopted_request_get_vector(request),
          inf_adopted_request_get_vector(associated)
        );

        g_assert(inf_adopted_state_vector_causally_before(lcs, v) == TRUE);

        lcs_against = inf_adopted_algorithm_translate_request(
          algorithm,
          associated,
          lcs
        );

        lcs_result = inf_adopted_algorithm_translate_request(
          algorithm,
          request,
          lcs
        );

        inf_adopted_state_vector_free(lcs);
        g_object_unref(G_OBJECT(associated));

        against = inf_adopted_algorithm_translate_request(
          algorithm,
          lcs_against,
          v
        );

        g_object_unref(G_OBJECT(lcs_against));

        result = inf_adopted_algorithm_translate_request(
          algorithm,
          lcs_result,
          v
        );

        g_object_unref(G_OBJECT(lcs_result));

        inf_adopted_request_transform(result, against);
        g_object_unref(G_OBJECT(against));

        goto done;
      }
      else
      {
        /* Reset to be reused */
        inf_adopted_state_vector_set(v, user_id, n);
      }
    }
  }

  /* Last resort: Transform always */
  for(item = keys; item != NULL; item = g_list_next(item))
  {
    user = INF_USER(item->data);
    user_id = inf_user_get_id(user);
    if(user_id == inf_adopted_request_get_user_id(request)) continue;

    n = inf_adopted_state_vector_get(v, user_id);
    if(n == 0) continue;

    log = g_hash_table_lookup(priv->request_logs, user);
    g_assert(log != NULL);

    if(inf_adopted_state_vector_get(/*original_*/vector, user_id) <
       inf_adopted_state_vector_get(to, user_id))
    {
      inf_adopted_state_vector_set(v, user_id, n - 1);
      if(inf_adopted_algorithm_is_reachable(algorithm, v))
      {
        associated = inf_adopted_request_log_get_request(log, n - 1);
        associated = inf_adopted_request_copy(associated);

        g_assert(inf_adopted_state_vector_causally_before(vector, v));

        g_assert(
          inf_adopted_state_vector_causally_before(
            inf_adopted_request_get_vector(associated),
            v
          )
        );

        /* Find least common successor and transform both requests
         * through that point. */
        lcs = inf_adopted_algorithm_least_common_successor(
          algorithm,
          inf_adopted_request_get_vector(request),
          inf_adopted_request_get_vector(associated)
        );

        g_assert(inf_adopted_state_vector_causally_before(lcs, v) == TRUE);

        lcs_against = inf_adopted_algorithm_translate_request(
          algorithm,
          associated,
          lcs
        );

        lcs_result = inf_adopted_algorithm_translate_request(
          algorithm,
          request,
          lcs
        );

        inf_adopted_state_vector_free(lcs);
        g_object_unref(G_OBJECT(associated));

        against = inf_adopted_algorithm_translate_request(
          algorithm,
          lcs_against,
          v
        );

        g_object_unref(G_OBJECT(lcs_against));

        result = inf_adopted_algorithm_translate_request(
          algorithm,
          lcs_result,
          v
        );

        g_object_unref(G_OBJECT(lcs_result));

        inf_adopted_request_transform(result, against);
        g_object_unref(G_OBJECT(against));

        goto done;
      }
      else
      {
        /* Reset to be reused */
        inf_adopted_state_vector_set(v, user_id, n);
      }
    }
  }

  g_assert_not_reached();

done:
  g_list_free(keys);
  inf_adopted_state_vector_free(v);
  return result;
}

static void
inf_adopted_algorithm_execute_request(InfAdoptedAlgorithm* algorithm,
                                      InfAdoptedRequest* request,
                                      gboolean apply)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequestLog* log;
  InfAdoptedRequest* temp;
  InfAdoptedRequest* translated;
  InfAdoptedRequest* log_request;
  InfAdoptedOperation* operation;
  InfAdoptedOperation* reversible_operation;
  InfAdoptedOperationFlags flags;

  InfAdoptedRequest* original;
  InfAdoptedStateVector* v;

  GList* keys;
  GList* item;
  InfAdoptedUser* user;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  g_assert(
    inf_adopted_state_vector_causally_before(
      inf_adopted_request_get_vector(request),
      priv->current
    ) == TRUE
  );

  /* TODO: Replace that hash table by a simply linked list, or possibly
   * a hash table from user ID to request log and/or user. */
  log = g_hash_table_find(
    priv->request_logs,
    inf_adopted_algorithm_translate_find_func,
    GUINT_TO_POINTER(inf_adopted_request_get_user_id(request))
  );

  g_assert(log != NULL);

  /* TODO: We can save some copies here since translate_request does another
  * copy in some circumstances (temp.type != DO). */
  if(inf_adopted_request_get_request_type(request) != INF_ADOPTED_REQUEST_DO)
  {
    original = inf_adopted_request_log_original_request(log, request);

    v = inf_adopted_state_vector_copy(
      inf_adopted_request_get_vector(original)
    );

    inf_adopted_state_vector_set(
      v,
      inf_adopted_request_get_user_id(request),
      inf_adopted_state_vector_get(
        inf_adopted_request_get_vector(request),
        inf_adopted_request_get_user_id(request)
      )
    );

    switch(inf_adopted_request_get_request_type(request))
    {
    case INF_ADOPTED_REQUEST_UNDO:
      log_request = inf_adopted_request_new_undo(
        v,
        inf_adopted_request_get_user_id(request)
      );

      break;
    case INF_ADOPTED_REQUEST_REDO:
      log_request = inf_adopted_request_new_redo(
        v,
        inf_adopted_request_get_user_id(request)
      );

      break;
    default:
      g_assert_not_reached();
      break;
    }

    inf_adopted_state_vector_free(v);
  }
  else
  {
    log_request = request;
  }

  /* Adjust vector time for Undo/Redo operations because they only depend on
   * their original operation. */
  temp = inf_adopted_request_copy(log_request);

  translated = inf_adopted_algorithm_translate_request(
    algorithm,
    temp,
    priv->current
  );

  g_object_unref(G_OBJECT(temp));

  /* TODO: Replace that hash table by a simply linked list, or possibly
    * a hash table from user ID to request log and/or user. */
  user = NULL;
  keys = g_hash_table_get_keys(priv->request_logs);
  for(item = keys; item != NULL; item = g_list_next(item))
  {
    if(inf_user_get_id(INF_USER(item->data)) ==
       inf_adopted_request_get_user_id(request))
    {
      user = INF_ADOPTED_USER(item->data);
      break;
    }
  }

  g_assert(user != NULL);
  g_list_free(keys);

  if(inf_adopted_request_get_request_type(request) == INF_ADOPTED_REQUEST_DO)
  {
    operation = inf_adopted_request_get_operation(request);
    flags = inf_adopted_operation_get_flags(operation);

    if( (flags & INF_ADOPTED_OPERATION_AFFECTS_BUFFER) != 0)
    {
      /* log_request is not newly allocated here */
      log_request = request;

      if(inf_adopted_operation_is_reversible(operation) == FALSE)
      {
        reversible_operation = inf_adopted_operation_make_reversible(
          operation,
          inf_adopted_request_get_operation(translated),
          priv->buffer
        );

        if(reversible_operation != NULL)
        {
          log_request = inf_adopted_request_new_do(
            inf_adopted_request_get_vector(request),
            inf_adopted_request_get_user_id(request),
            reversible_operation
          );

          g_object_unref(G_OBJECT(reversible_operation));
        }
      }
    }
    else
    {
      /* Does not affect the buffer, so is not recorded in log */
      log_request = NULL;
    }
  }

  if(log_request != NULL)
  {
    inf_adopted_request_log_add_request(log, log_request);

    inf_adopted_state_vector_add(
      priv->current,
      inf_adopted_request_get_user_id(request),
      1
    );

    inf_adopted_algorithm_update_local_user_times(algorithm);

    /* If log request has been newly created, additional unref */
    if(log_request != request)
      g_object_unref(G_OBJECT(log_request));
  }

  if(apply == TRUE)
  {
    inf_adopted_operation_apply(
      inf_adopted_request_get_operation(translated),
      user,
      priv->buffer
    );
  }

  g_object_unref(G_OBJECT(translated));
}

static void
inf_adopted_algorithm_init(GTypeInstance* instance,
                           gpointer g_class)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;

  algorithm = INF_ADOPTED_ALGORITHM(instance);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  priv->max_total_log_size = 2048;

  priv->current = inf_adopted_state_vector_new();
  priv->buffer = NULL;
  priv->queue = NULL;

  /* Lookup by user, user is not refed because the request log holds a 
   * reference anyway. */
  priv->request_logs = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)g_object_unref
  );

  priv->local_users = NULL;
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

  for(item = priv->queue; item != NULL; item = g_list_next(item))
    g_object_unref(G_OBJECT(item->data));
  g_list_free(priv->queue);
  priv->queue = NULL;

  g_hash_table_remove_all(priv->request_logs);

  if(priv->buffer != NULL)
  {
    g_object_unref(G_OBJECT(priv->buffer));
    priv->buffer = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_adopted_algorithm_finalize(GObject* object)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;

  algorithm = INF_ADOPTED_ALGORITHM(object);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  g_hash_table_unref(priv->request_logs);
  inf_adopted_state_vector_free(priv->current);

  G_OBJECT_CLASS(parent_class)->finalize(object);
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
  case PROP_BUFFER:
    g_assert(priv->buffer == NULL); /* construct only */
    priv->buffer = INF_BUFFER(g_value_dup_object(value));
    break;
  case PROP_MAX_TOTAL_LOG_SIZE:
    priv->max_total_log_size = g_value_get_uint(value);
    break;
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
  case PROP_BUFFER:
    g_value_set_object(value, G_OBJECT(priv->buffer));
    break;
  case PROP_MAX_TOTAL_LOG_SIZE:
    g_value_set_uint(value, priv->max_total_log_size);
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
inf_adopted_algorithm_class_init(gpointer g_class,
                                 gpointer class_data)
{
  GObjectClass* object_class;
  InfAdoptedAlgorithmClass* algorithm_class;

  object_class = G_OBJECT_CLASS(g_class);
  algorithm_class = INF_ADOPTED_ALGORITHM_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAdoptedAlgorithmPrivate));

  object_class->dispose = inf_adopted_algorithm_dispose;
  object_class->finalize = inf_adopted_algorithm_finalize;
  object_class->set_property = inf_adopted_algorithm_set_property;
  object_class->get_property = inf_adopted_algorithm_get_property;

  algorithm_class->can_undo_changed = inf_adopted_algorithm_can_undo_changed;
  algorithm_class->can_redo_changed = inf_adopted_algorithm_can_redo_changed;

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

  algorithm_signals[CAN_UNDO_CHANGED] = g_signal_new(
    "can-undo-changed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedAlgorithmClass, can_undo_changed),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_BOOLEAN,
    G_TYPE_NONE,
    2,
    INF_ADOPTED_TYPE_USER,
    G_TYPE_BOOLEAN
  );

  algorithm_signals[CAN_REDO_CHANGED] = g_signal_new(
    "can-redo-changed",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedAlgorithmClass, can_redo_changed),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_BOOLEAN,
    G_TYPE_NONE,
    2,
    INF_ADOPTED_TYPE_USER,
    G_TYPE_BOOLEAN
  );
}

GType
inf_adopted_algorithm_get_type(void)
{
  static GType algorithm_type = 0;

  if(!algorithm_type)
  {
    static const GTypeInfo algorithm_type_info = {
      sizeof(InfAdoptedAlgorithmClass),   /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      inf_adopted_algorithm_class_init,   /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof(InfAdoptedAlgorithm),        /* instance_size */
      0,                                  /* n_preallocs */
      inf_adopted_algorithm_init,         /* instance_init */
      NULL                                /* value_table */
    };

    algorithm_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfAdoptedAlgorithm",
      &algorithm_type_info,
      0
    );
  }

  return algorithm_type;
}

/** inf_adopted_algorithm_new:
 *
 * @buffer: The buffer to apply operations to.
 *
 * Creates a #InfAdoptedAlgorithm.
 *
 * Return Value: A new #InfAdoptedAlgorithm.
 **/
InfAdoptedAlgorithm*
inf_adopted_algorithm_new(InfBuffer* buffer)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_BUFFER(buffer), NULL);

  object = g_object_new(INF_ADOPTED_TYPE_ALGORITHM, "buffer", buffer, NULL);
  return INF_ADOPTED_ALGORITHM(object);
}

/** inf_adopted_algorithm_new_full:
 *
 * @buffer: The buffer to apply operations to.
 * @max_total_log_size: The maxmimum number of operations to keep in all
 * user's request logs.
 *
 * Note that it is possible that request logs need to grow a bit larger than
 * @max_total_log_size in high-latency situations or when a user does not send
 * status updates frequently. However, when all requests have been
 * processed by all users, the sum of all requests in the logs is guaranteed
 * to be lower or equal to this value. (TODO: I am not sure this is right 
 * since DO requests have to be kept if their associated requests (if any) are
 * still needed for other user's transformation.
 *
 * Set to 0 to disable limitation. In theory, this would allow everyone to
 * undo every operation up to the first one ever made. In practise, this
 * issues a huge amount of data that needs to be synchronized on user
 * join and is too expensive to compute anyway.
 *
 * The default value is 2048.
 *
 * Return Value: A new #InfAdoptedAlgorithm.
 **/
InfAdoptedAlgorithm*
inf_adopted_algorithm_new_full(InfBuffer* buffer,
                               guint max_total_log_size)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_BUFFER(buffer), NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_ALGORITHM,
    "buffer", buffer,
    "max-total-log-size", max_total_log_size,
    NULL
  );

  return INF_ADOPTED_ALGORITHM(object);
}

/** inf_adopted_algorithm_add_user:
 *
 * @algorithm: A #InfAdoptedAlgorithm.
 * @user: The #InfAdoptedUser to add to @algorithm.
 *
 * Adds a user to the algorithm so that it can process requests (or generate
 * some, if it is a local user) from that user.
 *
 * The latest request in @user's request log is assumed to be the last one
 * retrieved from that user and the new user's component in the current vector
 * time is set to the value from the same component in that request.
 **/
void
inf_adopted_algorithm_add_user(InfAdoptedAlgorithm* algorithm,
                               InfAdoptedUser* user)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequestLog* initial_log;
  InfAdoptedAlgorithmLocalUser* local;

  g_return_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm));
  g_return_if_fail(INF_ADOPTED_IS_USER(user));

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  g_return_if_fail(g_hash_table_lookup(priv->request_logs, user) == NULL);

  initial_log = inf_adopted_user_get_request_log(user);
  g_object_ref(G_OBJECT(initial_log));

  inf_adopted_state_vector_set(
    priv->current,
    inf_user_get_id(INF_USER(user)),
    inf_adopted_request_log_get_end(initial_log)
  );

  g_hash_table_insert(priv->request_logs, user, initial_log);
  
  if((inf_user_get_flags(INF_USER(user)) & INF_USER_LOCAL) != 0 &&
     inf_user_get_status(INF_USER(user)) != INF_USER_UNAVAILABLE)
  {
    local = g_slice_new(InfAdoptedAlgorithmLocalUser);
    local->user = user;

    local->can_undo = inf_adopted_algorithm_can_undo_redo(
      algorithm,
      initial_log,
      inf_adopted_request_log_next_undo(initial_log)
    );

    local->can_redo = inf_adopted_algorithm_can_undo_redo(
      algorithm,
      initial_log,
      inf_adopted_request_log_next_redo(initial_log)
    );

    priv->local_users = g_slist_prepend(priv->local_users, local);
  }

  g_signal_connect(
    G_OBJECT(user),
    "notify::status",
    G_CALLBACK(inf_adopted_algorithm_user_notify_cb),
    algorithm
  );
  
  g_signal_connect(
    G_OBJECT(user),
    "notify::flags",
    G_CALLBACK(inf_adopted_algorithm_user_notify_cb),
    algorithm
  );
}

/** inf_adopted_algorithm_get_current()
 *
 * @algorithm: A #InfAdoptedAlgorithm.
 *
 * Returns the current vector time of @algorithm.
 *
 * Return Value: A #InfAdoptedStateVector owned by @algorithm.
 **/
InfAdoptedStateVector*
inf_adopted_algorithm_get_current(InfAdoptedAlgorithm* algorithm)
{
  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), NULL);
  return INF_ADOPTED_ALGORITHM_PRIVATE(algorithm)->current;
}

/** inf_adopted_algorithm_generate_request_noexec:
 *
 * @algorithm: A #InfAdoptedAlgorithm.
 * @user: A local #InfAdoptedUser.
 * @operation: A #InfAdoptedOperation.
 *
 * Creates a #InfAdoptedRequest for the given operation, executed by @user.
 * The user needs to have the %INF_USER_LOCAL flag set.
 *
 * The operation is not applied to the buffer, so you are responsible that
 * the operation is applied before the next request is processed or generated.
 * This may be useful if you are applying multiple operations, but want
 * to only make a single request out of them to save bandwidth.
 *
 * Return Value: A #InfAdoptedRequest that needs to be transmitted to the
 * other non-local users.
 **/
InfAdoptedRequest*
inf_adopted_algorithm_generate_request_noexec(InfAdoptedAlgorithm* algorithm,
                                              InfAdoptedUser* user,
                                              InfAdoptedOperation* operation)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequest* request;

  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), NULL);
  g_return_val_if_fail(
    (inf_user_get_flags(INF_USER(user)) & INF_USER_LOCAL) != 0,
    NULL
  );

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  request = inf_adopted_request_new_do(
    priv->current,
    inf_user_get_id(INF_USER(user)),
    operation
  );

  inf_adopted_algorithm_execute_request(algorithm, request, FALSE);

  inf_adopted_algorithm_update_request_logs(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);

  return request;
}

/** inf_adopted_algorithm_generate_request:
 *
 * @algorithm: A #InfAdoptedAlgorithm.
 * @user: A local #InfAdoptedUser.
 * @operation: A #InfAdoptedOperation.
 *
 * Creates a #InfAdoptedRequest for the given operation, executed by @user.
 * The user needs to have the %INF_USER_LOCAL flag set. @operation is
 * applied to the buffer (by @user).
 *
 * Return Value: A #InfAdoptedRequest that needs to be transmitted to the
 * other non-local users.
 **/
InfAdoptedRequest*
inf_adopted_algorithm_generate_request(InfAdoptedAlgorithm* algorithm,
                                       InfAdoptedUser* user,
                                       InfAdoptedOperation* operation)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequest* request;
  
  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), NULL);
  g_return_val_if_fail(
    (inf_user_get_flags(INF_USER(user)) & INF_USER_LOCAL) != 0,
    NULL
  );

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  request = inf_adopted_request_new_do(
    priv->current,
    inf_user_get_id(INF_USER(user)),
    operation
  );

  inf_adopted_algorithm_execute_request(algorithm, request, TRUE);
  
  inf_adopted_algorithm_update_request_logs(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);
  
  return request;
}

/** inf_adopted_algorithm_generate_undo:
 *
 * @algorithm: A #InfAdoptedAlgorithm.
 * @user: A local #InfAdoptedUser.
 *
 * Creates a request of type %INF_ADOPTED_REQUEST_UNDO for the given
 * user and with the current vector time. The user needs to have the
 * %INF_USER_LOCAL flag set. It also applies the effect of the operation to
 * the buffer.
 *
 * Return Value: A #InfAdoptedRequest that needs to be transmitted to the
 * other non-local users.
 **/
InfAdoptedRequest*
inf_adopted_algorithm_generate_undo(InfAdoptedAlgorithm* algorithm,
                                    InfAdoptedUser* user)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequest* request;
  
  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);
  g_return_val_if_fail(
    (inf_user_get_flags(INF_USER(user)) & INF_USER_LOCAL) != 0,
    NULL
  );

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  request = inf_adopted_request_new_undo(
    priv->current,
    inf_user_get_id(INF_USER(user))
  );

  inf_adopted_algorithm_execute_request(algorithm, request, TRUE);
  
  inf_adopted_algorithm_update_request_logs(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);
  
  return request;
}

/** inf_adopted_algorithm_generate_redo:
 *
 * @algorithm: A #InfAdoptedAlgorithm.
 * @user: A local #InfAdoptedUser.
 *
 * Creates a request of type %INF_ADOPTED_REQUEST_REDO for the given
 * user and with the current vector time. The user needs to have the
 * %INF_USER_LOCAL flag set. It also applies the effect of the operation to
 * the buffer.
 *
 * Return Value: A #InfAdoptedRequest that needs to be transmitted to the
 * other non-local users.
 **/
InfAdoptedRequest*
inf_adopted_algorithm_generate_redo(InfAdoptedAlgorithm* algorithm,
                                    InfAdoptedUser* user)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequest* request;
  
  g_return_val_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);
  g_return_val_if_fail(
    (inf_user_get_flags(INF_USER(user)) & INF_USER_LOCAL) != 0,
    NULL
  );

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  request = inf_adopted_request_new_redo(
    priv->current,
    inf_user_get_id(INF_USER(user))
  );

  inf_adopted_algorithm_execute_request(algorithm, request, TRUE);

  inf_adopted_algorithm_update_request_logs(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);
  
  return request;
}

/** inf_adopted_algorithm_receive_request:
 *
 * @algorithm: A #InfAdoptedAlgorithm.
 * @request:  A #InfAdoptedRequest from a non-local user.
 *
 * This function processes a request received from a non-local user and
 * applies its operation to the buffer.
 **/
void
inf_adopted_algorithm_receive_request(InfAdoptedAlgorithm* algorithm,
                                      InfAdoptedRequest* request)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequest* queued_request;
  InfAdoptedStateVector* vector;
  InfAdoptedStateVector* user_vector;

  guint user_id;
  InfUser* user;

  GList* keys;
  GList* item;

  g_return_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm));
  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  user_id = inf_adopted_request_get_user_id(request);

  /* TODO: Replace by user table: */
  user = NULL;
  keys = g_hash_table_get_keys(priv->request_logs);

  for(item = keys; item != NULL; item = g_list_next(item))
  {
    if(inf_user_get_id(INF_USER(item->data)) == user_id)
    {
      user = INF_USER(item->data);
      break;
    }
  }

  g_list_free(keys);
  g_assert(user != NULL);

  vector = inf_adopted_request_get_vector(request);
  user_vector = inf_adopted_user_get_vector(INF_ADOPTED_USER(user));

  /* TODO: Why was this disabled? */
  g_return_if_fail( ((inf_user_get_flags(user)) & INF_USER_LOCAL) == 0);

  /* Update user vector if this is the newest request from that user. */
  if(inf_adopted_state_vector_causally_before(user_vector, vector))
  {
    /* Update remote user's vector: We know which requests the remote user
     * already has processed. */
    user_vector = inf_adopted_state_vector_copy(vector);
    inf_adopted_state_vector_add(user_vector, inf_user_get_id(user), 1);
    inf_adopted_user_set_vector(INF_ADOPTED_USER(user), user_vector);
  }
  
  if(inf_adopted_state_vector_causally_before(vector, priv->current) == FALSE)
  {
    priv->queue = g_list_prepend(priv->queue, request);
    g_object_ref(G_OBJECT(request));
  }
  else
  {
    inf_adopted_algorithm_execute_request(algorithm, request, TRUE);

    /* process queued requests that might have become executable now. */
    do
    {
      for(item = priv->queue; item != NULL; item = g_list_next(item))
      {
        queued_request = INF_ADOPTED_REQUEST(item->data);
        vector = inf_adopted_request_get_vector(queued_request);
        if(inf_adopted_state_vector_causally_before(vector, priv->current))
        {
          inf_adopted_algorithm_execute_request(
            algorithm,
            queued_request,
            TRUE
          );

          g_object_unref(G_OBJECT(queued_request));
          priv->queue = g_list_delete_link(priv->queue, item);

          break;
        }
      }
    } while(item != NULL);
  }

  inf_adopted_algorithm_update_request_logs(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);
}

/* vim:set et sw=2 ts=2: */
