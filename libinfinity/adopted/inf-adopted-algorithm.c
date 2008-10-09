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
 * you can create own reequests by the functions
 * inf_adopted_algorithm_generate_request(),
 * inf_adopted_algorithm_generate_request_noexec(),
 * inf_adopted_algorithm_generate_undo() and
 * inf_adopted_algorithm_generate_redo(). Remote requests can be applied via
 * inf_adopted_algorithm_receive_request(). This class does not take care of
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
 * and Rul Gunzenhäuser (http://portal.acm.org/citation.cfm?id=240305). Don't
 * even try to understand (the interesting part) of this code without having
 * read it.
 *
 * "Reducing the Problems of Group Undo" by Matthias Ressel and Rul
 * Gunzenhäuser (http://portal.acm.org/citation.cfm?doid=320297.320312)
 * might also be worth a read to (better) understand how local group undo
 * is achieved.
 */

/* TODO: Do only cleanup if too much entries in cache? */

typedef struct _InfAdoptedAlgorithmLocalUser InfAdoptedAlgorithmLocalUser;
struct _InfAdoptedAlgorithmLocalUser {
  InfAdoptedUser* user;
  gboolean can_undo;
  gboolean can_redo;
};

typedef struct _InfAdoptedAlgorithmCacheCleanupData
  InfAdoptedAlgorithmCacheCleanupData;
struct _InfAdoptedAlgorithmCacheCleanupData {
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedStateVector* lcp;
  GSList* rem_keys;
};

typedef struct _InfAdoptedAlgorithmRequestKey InfAdoptedAlgorithmRequestKey;
struct _InfAdoptedAlgorithmRequestKey {
  /* Not a copy, directly points to vector of the keyed request */
  InfAdoptedStateVector* vector;
  guint user_id;
};

typedef struct _InfAdoptedAlgorithmPrivate InfAdoptedAlgorithmPrivate;
struct _InfAdoptedAlgorithmPrivate {
  /* request log policy */
  guint max_total_log_size;

  InfAdoptedStateVector* current;
  InfUserTable* user_table;
  InfBuffer* buffer;
  /* doubly-linked so we can easily remove an element from the middle */
  GList* queue;

  /* Users in user table. We need to iterate over them very often, so we
   * keep them as array here. */
  InfAdoptedUser** users_begin;
  InfAdoptedUser** users_end;

  /* Request cache */
  /* TODO: Consider using a hash table here... given a good hash function it
   * could perhaps speedup lookup. */
  GTree* cache;

  GSList* local_users;
};

enum {
  PROP_0,

  /* construct only */
  PROP_USER_TABLE,
  PROP_BUFFER,
  PROP_MAX_TOTAL_LOG_SIZE,
  
  /* read/only */
  PROP_CURRENT_STATE
};

enum {
  CAN_UNDO_CHANGED,
  CAN_REDO_CHANGED,
  EXECUTE_REQUEST,
  APPLY_REQUEST,

  LAST_SIGNAL
};

#define INF_ADOPTED_ALGORITHM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_ALGORITHM, InfAdoptedAlgorithmPrivate))
#define INF_ADOPTED_ALGORITHM_PRIVATE(obj)     ((InfAdoptedAlgorithmPrivate*)(obj)->priv)

static GObjectClass* parent_class;
static guint algorithm_signals[LAST_SIGNAL];

static int
inf_adopted_algorithm_request_key_cmp(gconstpointer a,
                                      gconstpointer b,
                                      gpointer user_data)
{
  const InfAdoptedAlgorithmRequestKey* key_a;
  const InfAdoptedAlgorithmRequestKey* key_b;

  key_a = (const InfAdoptedAlgorithmRequestKey*)a;
  key_b = (const InfAdoptedAlgorithmRequestKey*)b;

  if(key_a->user_id < key_b->user_id)
    return -1;
  if(key_a->user_id > key_b->user_id)
    return 1;

  return inf_adopted_state_vector_compare(key_a->vector, key_b->vector);
}

static void
inf_adopted_algorithm_request_key_free(gpointer key)
{
  g_slice_free(InfAdoptedAlgorithmRequestKey, key);
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

static gboolean
inf_adopted_algorithm_cleanup_cache_traverse_func(gpointer key,
                                                  gpointer value,
                                                  gpointer data)
{
  InfAdoptedAlgorithmRequestKey* request_key;
  InfAdoptedAlgorithmCacheCleanupData* cleanup_data;
  InfAdoptedAlgorithmPrivate* priv;
  guint vdiff;

  request_key = (InfAdoptedAlgorithmRequestKey*)key;
  cleanup_data = (InfAdoptedAlgorithmCacheCleanupData*)data;
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(cleanup_data->algorithm);

  g_assert(priv->max_total_log_size != G_MAXUINT);

  /* TODO: Save vdiff-to-zero of lcp in cache data and vdiff-to-zero in
   * request key. We could then get the final vdiff by just subtracting them,
   * saving an exepensive inf_adopted_state_vector_vdiff() call. */
  if(inf_adopted_state_vector_causally_before(request_key->vector,
                                              cleanup_data->lcp))
  {
    vdiff = inf_adopted_state_vector_vdiff(
      request_key->vector,
      cleanup_data->lcp
    );

    if(vdiff > priv->max_total_log_size)
    {
      /* Old enough to remove */
      cleanup_data->rem_keys =
        g_slist_prepend(cleanup_data->rem_keys, request_key);
    }
  }

  /* TODO: Return TRUE if this takes too long (>10 msecs or so)? */
  return FALSE;
}

/* TODO: This is "only" some kind of garbage collection that does not need
 * to be done after _every_ request received. */
static void
inf_adopted_algorithm_cleanup(InfAdoptedAlgorithm* algorithm)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedStateVector* temp;
  InfAdoptedUser** user;
  InfAdoptedRequestLog* log;
  InfAdoptedRequest* req;
  InfAdoptedStateVector* req_vec;
  InfAdoptedStateVector* low_vec;
  gboolean req_before_lcp;
  guint n;
  guint id;
  guint vdiff;

  InfAdoptedAlgorithmCacheCleanupData cleanup_data;
  GSList* item;

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

  cleanup_data.lcp = inf_adopted_state_vector_copy(priv->current);
  for(user = priv->users_begin; user != priv->users_end; ++ user)
  {
    if(inf_user_get_status(INF_USER(*user)) != INF_USER_UNAVAILABLE)
    {
      temp = inf_adopted_algorithm_least_common_predecessor(
        algorithm,
        cleanup_data.lcp,
        inf_adopted_user_get_vector(*user)
      );

      inf_adopted_state_vector_free(cleanup_data.lcp);
      cleanup_data.lcp = temp;
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
        cleanup_data.lcp,
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

      vdiff = inf_adopted_state_vector_vdiff(low_vec, cleanup_data.lcp);

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

  cleanup_data.algorithm = algorithm;
  cleanup_data.rem_keys = NULL;

  /* Remove requests from cache. That's O(n log n) here in the worst case,
   * which is quite bad regarding that the cache can contain hundreds of
   * thousands of requests, or even more given a large enough
   * max total log size. However, that's a problem with the GTree API.
   * TODO: We could try out GHashTable instead which has
   * g_hash_table_foreach_remove() allowing this to be implemented in O(n).
   * We should still do good profiling, though. */
  /* TODO: We can also do this in an idle handler, and only process a couple
   * requests at a time. */
  g_tree_foreach(
    priv->cache,
    inf_adopted_algorithm_cleanup_cache_traverse_func,
    &cleanup_data
  );

  for(item = cleanup_data.rem_keys; item != NULL; item = g_slist_next(item))
    g_tree_remove(priv->cache, item->data);

  g_slist_free(cleanup_data.rem_keys);
  inf_adopted_state_vector_free(cleanup_data.lcp);
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
  gboolean result;
  guint n;
  
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  log = inf_adopted_user_get_request_log(component);
  current = v;
  g_assert(log != NULL);

  for(;;)
  {
    n = inf_adopted_state_vector_get(
      current,
      inf_user_get_id(INF_USER(component))
    );

    g_assert(n >= inf_adopted_request_log_get_begin(log));
    if(n == inf_adopted_request_log_get_begin(log)) return TRUE;

    request = inf_adopted_request_log_get_request(log, n - 1);
    type = inf_adopted_request_get_request_type(request);

    if(type == INF_ADOPTED_REQUEST_DO)
    {
      return inf_adopted_state_vector_causally_before_inc(
        inf_adopted_request_get_vector(request),
        v,
        inf_adopted_request_get_user_id(request)
      );
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
  InfAdoptedUser** user;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  g_assert(
    inf_adopted_state_vector_causally_before(v, priv->current) == TRUE
  );

  for(user = priv->users_begin; user != priv->users_end; ++ user)
    if(!inf_adopted_algorithm_is_component_reachable(algorithm, v, *user))
      return FALSE;
  
  return TRUE;
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

/* Required by inf_adopted_algorithm_transform_request() */
static InfAdoptedRequest*
inf_adopted_algorithm_translate_request(InfAdoptedAlgorithm* algorithm,
                                        InfAdoptedRequest* request,
                                        InfAdoptedStateVector* to);

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

      concurrency_id =
        inf_adopted_request_get_concurrency_id(lcs_request, lcs_against);
      
      g_object_unref(lcs_request);
      g_object_unref(lcs_against);
    }

    inf_adopted_state_vector_free(lcs);
  }

  result = inf_adopted_request_transform(
    request_at,
    against_at,
    concurrency_id
  );

  g_object_unref(request_at);
  g_object_unref(against_at);

  return result;
}

/* This should only ever be called by
 * inf_adopted_algorithm_translate_request() after it did the cache checks */
static InfAdoptedRequest*
inf_adopted_algorithm_translate_request_nocache(InfAdoptedAlgorithm* algorithm,
                                                InfAdoptedRequest* request,
                                                InfAdoptedStateVector* to)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedUser** user_it; /* user iterator */
  InfUser* user; /* points to current user (mostly *user_it) */
  guint user_id; /* Corresponding ID */
  InfAdoptedRequestLog* log; /* points to current log */
  InfAdoptedStateVector* vector; /* always points to request's vector */

  InfAdoptedRequest* associated;
  InfAdoptedRequest* translated;
  InfAdoptedRequest* result;
  /*InfAdoptedStateVector* v;*/
  gint by;
  guint n;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  user_id = inf_adopted_request_get_user_id(request);
  user = inf_user_table_lookup_user_by_id(priv->user_table, user_id);
  g_assert(user != NULL);

  log = inf_adopted_user_get_request_log(INF_ADOPTED_USER(user));

  g_assert(inf_adopted_state_vector_causally_before(to, priv->current));
  g_assert(
    inf_adopted_state_vector_causally_before(
      inf_adopted_request_get_vector(
        inf_adopted_request_log_original_request(log, request)
      ),
      to
    )
  );
  g_assert(inf_adopted_algorithm_is_reachable(algorithm, to) == TRUE);

  vector = inf_adopted_request_get_vector(request);
  /*v = inf_adopted_state_vector_copy(to);*/
  result = NULL;

  if(inf_adopted_request_get_request_type(request) != INF_ADOPTED_REQUEST_DO)
  {
    /* Try late mirror if this is not a do request */
    associated = inf_adopted_request_log_prev_associated(log, request);
    g_assert(associated != NULL);

    n = inf_adopted_state_vector_get(to, user_id);
    by = n - inf_adopted_state_vector_get(
      inf_adopted_request_get_vector(associated),
      user_id
    );

    inf_adopted_state_vector_add(to, user_id, -by);

    if(inf_adopted_algorithm_is_reachable(algorithm, to))
    {
      translated = inf_adopted_algorithm_translate_request(
        algorithm,
        associated,
        to
      );

      result = inf_adopted_request_mirror(translated, by);

      g_object_unref(translated);
    }

    /* Reset to for other routines to use */
    inf_adopted_state_vector_set(to, user_id, n);
    if(result) return result;
  }
  else
  {
    /* The request is a do request: We might already be done if we are
     * already at the state we are supposed to translate request to. */
    if(inf_adopted_state_vector_compare(vector, to) == 0)
    {
      g_object_ref(request);
      return request;
    }
  }

  for(user_it = priv->users_begin; user_it != priv->users_end; ++ user_it)
  {
    user = INF_USER(*user_it);
    user_id = inf_user_get_id(user);
    if(user_id == inf_adopted_request_get_user_id(request)) continue;

    n = inf_adopted_state_vector_get(to, user_id);
    log = inf_adopted_user_get_request_log(INF_ADOPTED_USER(user));

    g_assert(n >= inf_adopted_request_log_get_begin(log));
    if(n == inf_adopted_request_log_get_begin(log)) continue;

    /* Fold late, if possible */
    associated = inf_adopted_request_log_get_request(log, n - 1);
    if(inf_adopted_request_get_request_type(associated) !=
       INF_ADOPTED_REQUEST_DO)
    {
      associated = inf_adopted_request_log_prev_associated(log, associated);
      g_assert(associated != NULL);

      by = n - inf_adopted_state_vector_get(
        inf_adopted_request_get_vector(associated),
        user_id
      );

      inf_adopted_state_vector_add(to, user_id, -by);

      if(inf_adopted_algorithm_is_reachable(algorithm, to) &&
         inf_adopted_state_vector_causally_before(vector, to) == TRUE)
      {
        translated = inf_adopted_algorithm_translate_request(
          algorithm,
          request,
          to
        );

        result = inf_adopted_request_fold(
          translated,
          user_id,
          by
        );

        g_object_unref(translated);
      }

      /* Reset to for other routines to use */
      inf_adopted_state_vector_set(to, user_id, n);
      if(result) return result;
    }

    /* Transform into direction we are not going to fold later */
    if(inf_adopted_state_vector_get(vector, user_id) < n)
/*       inf_adopted_state_vector_get(to, user_id))*/
    {
      inf_adopted_state_vector_set(to, user_id, n - 1);
      if(inf_adopted_algorithm_is_reachable(algorithm, to))
      {
        associated = inf_adopted_request_log_get_request(log, n - 1);

        result = inf_adopted_algorithm_transform_request(
          algorithm,
          request,
          associated,
          to
        );
      }

      /* Reset to be reused */
      inf_adopted_state_vector_set(to, user_id, n);
      if(result) return result;
    }
  }

#if 0
  /* Last resort: Transform always */
  for(user_it = priv->users_begin; user_it != priv->users_end; ++ user_it)
  {
    user = INF_USER(*user_it);
    user_id = inf_user_get_id(user);
    if(user_id == inf_adopted_request_get_user_id(request)) continue;

    n = inf_adopted_state_vector_get(v, user_id);
    log = inf_adopted_user_get_request_log(INF_ADOPTED_USER(user));

    g_assert(n >= inf_adopted_request_log_get_begin(log));
    if(n == inf_adopted_request_log_get_begin(log)) continue;

    if(inf_adopted_state_vector_get(vector, user_id) <
       inf_adopted_state_vector_get(to, user_id))
    {
      inf_adopted_state_vector_set(v, user_id, n - 1);
      if(inf_adopted_algorithm_is_reachable(algorithm, v))
      {
        associated = inf_adopted_request_log_get_request(log, n - 1);

        result = inf_adopted_algorithm_transform_request(
          algorithm,
          request,
          associated,
          v
        );

        goto done;
      }
      else
      {
        /* Reset to be reused */
        inf_adopted_state_vector_set(v, user_id, n);
      }
    }
  }
#endif

  g_assert_not_reached();
  return NULL;
}

/* The result needs to be unref()ed. */
static InfAdoptedRequest*
inf_adopted_algorithm_translate_request(InfAdoptedAlgorithm* algorithm,
                                        InfAdoptedRequest* request,
                                        InfAdoptedStateVector* to)
{
  InfAdoptedAlgorithmPrivate* priv;
  guint user_id;

  InfAdoptedRequest* result;

  InfAdoptedAlgorithmRequestKey lookup_key;
  InfAdoptedAlgorithmRequestKey* insert_key;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  user_id = inf_adopted_request_get_user_id(request);

  /* If the request affects the buffer, then it might have been cached
   * earlier. */
  if(inf_adopted_request_affects_buffer(request))
  {
    lookup_key.vector = to;
    lookup_key.user_id = user_id;
    result = g_tree_lookup(priv->cache, &lookup_key);
    if(result != NULL)
    {
      g_object_ref(result);
      return result;
    }
  }

  result = inf_adopted_algorithm_translate_request_nocache(
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
  {
    insert_key = g_slice_new(InfAdoptedAlgorithmRequestKey);
    insert_key->vector = inf_adopted_request_get_vector(result);
    insert_key->user_id = user_id;
    g_assert(g_tree_lookup(priv->cache, insert_key) == NULL);
    g_tree_replace(priv->cache, insert_key, result);
    g_object_ref(result);
  }

  return result;
}

static void
inf_adopted_algorithm_init(GTypeInstance* instance,
                           gpointer g_class)
{
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;

  algorithm = INF_ADOPTED_ALGORITHM(instance);
  algorithm->priv = INF_ADOPTED_ALGORITHM_GET_PRIVATE(algorithm);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  priv->max_total_log_size = 2048;

  priv->current = inf_adopted_state_vector_new();
  priv->user_table = NULL;
  priv->buffer = NULL;
  priv->queue = NULL;

  /* Lookup by user, user is not refed because the request log holds a 
   * reference anyway. */
  priv->users_begin = NULL;
  priv->users_end = NULL;

  priv->cache = g_tree_new_full(
    inf_adopted_algorithm_request_key_cmp,
    NULL,
    inf_adopted_algorithm_request_key_free,
    g_object_unref
  );

  priv->local_users = NULL;
}

static void
inf_adopted_algorithm_constructor_foreach_user_func(InfUser* user,
                                                    gpointer user_data)
{
  InfAdoptedAlgorithm* algorithm;
  algorithm = INF_ADOPTED_ALGORITHM(user_data);

  g_assert(INF_ADOPTED_IS_USER(user));
  inf_adopted_algorithm_add_user(algorithm, INF_ADOPTED_USER(user));
}

static void
inf_adopted_algorithm_constructor_foreach_local_user_func(InfUser* user,
                                                          gpointer user_data)
{
  InfAdoptedAlgorithm* algorithm;
  algorithm = INF_ADOPTED_ALGORITHM(user_data);

  g_assert(INF_ADOPTED_IS_USER(user));
  inf_adopted_algorithm_add_local_user(algorithm, INF_ADOPTED_USER(user));
}

static GObject*
inf_adopted_algorithm_constructor(GType type,
                                  guint n_construct_properties,
                                  GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedAlgorithmPrivate* priv;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  algorithm = INF_ADOPTED_ALGORITHM(object);
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  /* Add initial users */
  inf_user_table_foreach_user(
    priv->user_table,
    inf_adopted_algorithm_constructor_foreach_user_func,
    algorithm
  );
  
  inf_user_table_foreach_local_user(
    priv->user_table,
    inf_adopted_algorithm_constructor_foreach_local_user_func,
    algorithm
  );

  return object;
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

  g_tree_destroy(priv->cache);
  priv->cache = NULL;

  g_free(priv->users_begin);

  if(priv->buffer != NULL)
  {
    g_object_unref(priv->buffer);
    priv->buffer = NULL;
  }

  if(priv->user_table != NULL)
  {
    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_adopted_algorithm_add_user_cb),
      algorithm
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_adopted_algorithm_add_local_user_cb),
      algorithm
    );

    g_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->user_table),
      G_CALLBACK(inf_adopted_algorithm_remove_local_user_cb),
      algorithm
    );

    g_object_unref(priv->user_table);
    priv->user_table = NULL;
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
    priv->buffer = INF_BUFFER(g_value_dup_object(value));
    break;
  case PROP_MAX_TOTAL_LOG_SIZE:
    priv->max_total_log_size = g_value_get_uint(value);
    break;
  case PROP_CURRENT_STATE:
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
inf_adopted_algorithm_execute_request(InfAdoptedAlgorithm* algorithm,
                                      InfAdoptedUser* user,
                                      InfAdoptedRequest* request,
                                      gboolean apply)
{
  InfAdoptedAlgorithmPrivate* priv;
  InfAdoptedRequestLog* log;
  InfAdoptedRequest* translated;
  InfAdoptedRequest* log_request;
  InfAdoptedOperation* operation;
  InfAdoptedOperation* reversible_operation;
  InfAdoptedOperationFlags flags;

  InfAdoptedRequest* original;
  InfAdoptedStateVector* v;

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  g_assert(
    inf_adopted_state_vector_causally_before(
      inf_adopted_request_get_vector(request),
      priv->current
    ) == TRUE
  );

  log = inf_adopted_user_get_request_log(user);

  /* Adjust vector time for Undo/Redo operations because they only depend on
   * their original operation. */
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
    g_object_ref(log_request);
  }

  translated = inf_adopted_algorithm_translate_request(
    algorithm,
    log_request,
    priv->current
  );

  if(inf_adopted_request_get_request_type(request) == INF_ADOPTED_REQUEST_DO)
  {
    g_assert(log_request == request);

    operation = inf_adopted_request_get_operation(request);
    flags = inf_adopted_operation_get_flags(operation);

    if( (flags & INF_ADOPTED_OPERATION_AFFECTS_BUFFER) != 0)
    {
      if(inf_adopted_operation_is_reversible(operation) == FALSE)
      {
        reversible_operation = inf_adopted_operation_make_reversible(
          operation,
          inf_adopted_request_get_operation(translated),
          priv->buffer
        );

        if(reversible_operation != NULL)
        {
          g_object_unref(log_request);

          log_request = inf_adopted_request_new_do(
            inf_adopted_request_get_vector(request),
            inf_adopted_request_get_user_id(request),
            reversible_operation
          );

          g_object_unref(reversible_operation);
        }
      }
    }
    else
    {
      /* Does not affect the buffer, so is not recorded in log */
      g_object_unref(log_request);
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
    g_object_unref(log_request);
  }

  if(apply == TRUE)
  {
    g_signal_emit(
      G_OBJECT(algorithm),
      algorithm_signals[APPLY_REQUEST],
      0,
      user,
      translated
    );
  }

  g_object_unref(translated);
}

static void
inf_adopted_algorithm_apply_request(InfAdoptedAlgorithm* algorithm,
                                    InfAdoptedUser* user,
                                    InfAdoptedRequest* request)
{
  InfAdoptedAlgorithmPrivate* priv;
  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  inf_adopted_operation_apply(
    inf_adopted_request_get_operation(request),
    user,
    priv->buffer
  );
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

  object_class->constructor = inf_adopted_algorithm_constructor;
  object_class->dispose = inf_adopted_algorithm_dispose;
  object_class->finalize = inf_adopted_algorithm_finalize;
  object_class->set_property = inf_adopted_algorithm_set_property;
  object_class->get_property = inf_adopted_algorithm_get_property;

  algorithm_class->can_undo_changed = inf_adopted_algorithm_can_undo_changed;
  algorithm_class->can_redo_changed = inf_adopted_algorithm_can_redo_changed;
  algorithm_class->execute_request = inf_adopted_algorithm_execute_request;
  algorithm_class->apply_request = inf_adopted_algorithm_apply_request;

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

  /**
   * InfAdoptedAlgorithm::can-undo-changed:
   * @algorithm: The #InfAdopedAlgorithm for which a user's
   * can-undo state changed.
   * @user: The #InfAdoptedUser whose can-undo state has changed.
   * @can_undo: Whether @user can issue an undo request in the current
   * state or not.
   *
   * This signal is emitted every time the can-undo state of a local user
   * in @algorithm's user table changed. The can-undo state defines whether
   * @user can generate an undo request
   * (via inf_adopted_algorithm_generate_undo()) in the current situation, see
   * also inf_adopted_algorithm_can_undo().
   */
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

  /**
   * InfAdoptedAlgorithm::can-redo-changed:
   * @algorithm: The #InfAdopedAlgorithm for which a user's
   * can-redo state changed.
   * @user: The #InfAdoptedUser whose can-redo state has changed.
   * @can_undo: Whether @user can issue a redo request in the current
   * state or not.
   *
   * This signal is emitted every time the can-redo state of a local user
   * in @algorithm's user table changed. The can-redo state defines whether
   * @user can generate a redo request
   * (via inf_adopted_algorithm_generate_redo()) in the current situation, see
   * also inf_adopted_algorithm_can_redo().
   */
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
  
  /**
   * InfAdoptedAlgorithm::execute-request:
   * @algorithm: The #InfAdoptedAlgorithm executing a request.
   * @user: The #InfAdoptedUser executing the request.
   * @request: The #InfAdoptedRequest being executed.
   * @apply: Whether the request will be applied after execution.
   *
   * This signal is emitted every time the algorithm executes a request.
   * @request is the request that @algorithm will execute. @request can
   * generally not be applied to the current state, and it might also be an
   * undo or redo request. The default handler of this signal computes the
   * operation that can be applied to the buffer, and applies it when @apply
   * is %TRUE by emitting #InfAdoptedAlgorithm::apply-request.
   */
  algorithm_signals[EXECUTE_REQUEST] = g_signal_new(
    "execute-request",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedAlgorithmClass, execute_request),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_OBJECT_BOOLEAN,
    G_TYPE_NONE,
    3,
    INF_ADOPTED_TYPE_USER,
    INF_ADOPTED_TYPE_REQUEST,
    G_TYPE_BOOLEAN
  );

  /**
   * InfAdoptedAlgorithm::apply-request:
   * @algorithm: The #InfAdopedAlgorithm applying a request.
   * @user: The #InfAdoptedUser applying the request.
   * @request: The #InfAdoptedRequest being applied.
   *
   * This signal is emitted every time the algorithm applies a request.
   *
   * Note a call to inf_adopted_algorithm_generate_request(),
   * inf_adopted_algorithm_generate_undo()
   * or inf_adopted_algorithm_generate_redo() always applies the generated
   * request. In contrast, inf_adopted_algorithm_receive_request() might not
   * apply the given request (if requests it depends upon have not yet
   * received) or might apply multiple request (if the provided request
   * fulfills the dependencies of queued requests).
   *
   * Note also that the signal is not emitted for every request processed by
   * #InfAdoptedAlgorithm since inf_adopted_algorthm_generate_request_noexec()
   * generates a request but does not apply it.
   */
  algorithm_signals[APPLY_REQUEST] = g_signal_new(
    "apply-request",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedAlgorithmClass, apply_request),
    NULL, NULL,
    inf_marshal_VOID__OBJECT_OBJECT,
    G_TYPE_NONE,
    2,
    INF_ADOPTED_TYPE_USER,
    INF_ADOPTED_TYPE_REQUEST
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

/**
 * inf_adopted_algorithm_new:
 * @user_table: The table of participating users.
 * @buffer: The buffer to apply operations to.
 *
 * Creates a #InfAdoptedAlgorithm.
 *
 * Return Value: A new #InfAdoptedAlgorithm.
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
 * inf_adopted_algorithm_new_full:
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
 * Return Value: A new #InfAdoptedAlgorithm.
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

/**
 * inf_adopted_algorithm_generate_request_noexec:
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

  g_signal_emit(
    G_OBJECT(algorithm),
    algorithm_signals[EXECUTE_REQUEST],
    0,
    user,
    request,
    FALSE
  );

  inf_adopted_algorithm_cleanup(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);

  return request;
}

/**
 * inf_adopted_algorithm_generate_request:
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

  g_signal_emit(
    G_OBJECT(algorithm),
    algorithm_signals[EXECUTE_REQUEST],
    0,
    user,
    request,
    TRUE
  );
  
  inf_adopted_algorithm_cleanup(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);
  
  return request;
}

/**
 * inf_adopted_algorithm_generate_undo:
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
  g_return_val_if_fail(inf_adopted_algorithm_can_undo(algorithm, user), NULL);

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  request = inf_adopted_request_new_undo(
    priv->current,
    inf_user_get_id(INF_USER(user))
  );

  g_signal_emit(
    G_OBJECT(algorithm),
    algorithm_signals[EXECUTE_REQUEST],
    0,
    user,
    request,
    TRUE
  );

  inf_adopted_algorithm_cleanup(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);
  
  return request;
}

/**
 * inf_adopted_algorithm_generate_redo:
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
  g_return_val_if_fail(inf_adopted_algorithm_can_redo(algorithm, user), NULL);

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);

  request = inf_adopted_request_new_redo(
    priv->current,
    inf_user_get_id(INF_USER(user))
  );

  g_signal_emit(
    G_OBJECT(algorithm),
    algorithm_signals[EXECUTE_REQUEST],
    0,
    user,
    request,
    TRUE
  );

  inf_adopted_algorithm_cleanup(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);
  
  return request;
}

/**
 * inf_adopted_algorithm_receive_request:
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

  GList* item;

  g_return_if_fail(INF_ADOPTED_IS_ALGORITHM(algorithm));
  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));

  priv = INF_ADOPTED_ALGORITHM_PRIVATE(algorithm);
  user_id = inf_adopted_request_get_user_id(request);
  user = inf_user_table_lookup_user_by_id(priv->user_table, user_id);
  g_return_if_fail(user != NULL);

  vector = inf_adopted_request_get_vector(request);
  user_vector = inf_adopted_user_get_vector(INF_ADOPTED_USER(user));
  g_return_if_fail( ((inf_user_get_flags(user)) & INF_USER_LOCAL) == 0);

  /* Update user vector if this is the newest request from that user. */
  if(inf_adopted_state_vector_causally_before(user_vector, vector))
  {
    /* Update remote user's vector: We know which requests the remote user
     * already has processed. */
    user_vector = inf_adopted_state_vector_copy(vector);
    if(inf_adopted_request_affects_buffer(request))
      inf_adopted_state_vector_add(user_vector, inf_user_get_id(user), 1);

    inf_adopted_user_set_vector(INF_ADOPTED_USER(user), user_vector);
  }

  /* TODO: Errorcheck that we can apply the request. That means: If it's a
   * DO request, check vector timestamps, if it's an undo or redo request,
   * then check can_undo/can_redo. This means this function needs to take a
   * GError**. */

  if(inf_adopted_state_vector_causally_before(vector, priv->current) == FALSE)
  {
    priv->queue = g_list_prepend(priv->queue, request);
    g_object_ref(G_OBJECT(request));
  }
  else
  {
    g_signal_emit(
      G_OBJECT(algorithm),
      algorithm_signals[EXECUTE_REQUEST],
      0,
      user,
      request,
      TRUE
    );

    /* process queued requests that might have become executable now. */
    /* TODO: Do this in an idle handler, to stay responsive. */
    do
    {
      for(item = priv->queue; item != NULL; item = g_list_next(item))
      {
        queued_request = INF_ADOPTED_REQUEST(item->data);
        vector = inf_adopted_request_get_vector(queued_request);
        if(inf_adopted_state_vector_causally_before(vector, priv->current))
        {
          user_id = inf_adopted_request_get_user_id(queued_request);
          user = inf_user_table_lookup_user_by_id(priv->user_table, user_id);
          g_assert(user != NULL);

          g_signal_emit(
            G_OBJECT(algorithm),
            algorithm_signals[EXECUTE_REQUEST],
            0,
            user,
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

  inf_adopted_algorithm_cleanup(algorithm);
  inf_adopted_algorithm_update_undo_redo(algorithm);
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
 * Return Value: %TRUE if Undo is possible, %FALSE otherwise.
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
 * Return Value: %TRUE if Redo is possible, %FALSE otherwise.
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
