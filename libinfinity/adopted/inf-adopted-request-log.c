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
 * SECTION:inf-adopted-request-log
 * @title: InfAdoptedRequestLog
 * @short_description: History of requests
 * @include: libinfinity/adopted/inf-adopted-request-log.h
 * @see_also: #InfAdoptedRequest, #InfAdoptedAlgorithm
 * @stability: Unstable
 *
 * #InfAdoptedRequestLog stores all requests by a particular user. These need
 * to be looked up by #InfAdoptedAlgorithm to perform transformations of
 * older requests to the current state. It also adds relations between the 
 * requests so that is easy to find the request that an Undo request undoes,
 * or the Undo request undoing a given request, if available.
 *
 * When requests are no longer needed, then they can also be removed again
 * from the log, however requests can only be removed so that remaining Undo
 * or Redo requests do not refer to some request that is about to be removed.
 */

#include <libinfinity/adopted/inf-adopted-request-log.h>

#include <string.h> /* For (g_)memmove */

typedef struct _InfAdoptedRequestLogCleanupCacheData
  InfAdoptedRequestLogCleanupCacheData;
struct _InfAdoptedRequestLogCleanupCacheData {
  guint user_id;
  guint up_to;
  GSList* requests_to_remove;
};

typedef struct _InfAdoptedRequestLogEntry InfAdoptedRequestLogEntry;
struct _InfAdoptedRequestLogEntry {
  InfAdoptedRequest* request;
  InfAdoptedRequestLogEntry* original;

  InfAdoptedRequestLogEntry* next_associated;
  InfAdoptedRequestLogEntry* prev_associated;

  InfAdoptedRequestLogEntry* lower_related;
  InfAdoptedRequestLogEntry* upper_related;
};

typedef struct _InfAdoptedRequestLogPrivate InfAdoptedRequestLogPrivate;
struct _InfAdoptedRequestLogPrivate {
  guint user_id;
  InfAdoptedRequestLogEntry* entries;
  GTree* cache;

  InfAdoptedRequestLogEntry* next_undo;
  InfAdoptedRequestLogEntry* next_redo;

  gsize offset;
  guint begin;
  guint end;
  gsize alloc;
};

enum {
  PROP_0,

  /* construct only */
  PROP_USER_ID,

  PROP_BEGIN,
  PROP_END,

  PROP_NEXT_UNDO,
  PROP_NEXT_REDO
};

enum {
  ADD_REQUEST,

  LAST_SIGNAL
};

#define INF_ADOPTED_REQUEST_LOG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_REQUEST_LOG, InfAdoptedRequestLogPrivate))
#define INF_ADOPTED_REQUEST_LOG_PRIVATE(obj)     ((InfAdoptedRequestLogPrivate*)(obj)->priv)

static const guint INF_ADOPTED_REQUEST_LOG_INC = 0x80;
static guint request_log_signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE(InfAdoptedRequestLog, inf_adopted_request_log, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfAdoptedRequestLog))

#ifdef INF_ADOPTED_REQUEST_LOG_CHECK_RELATED
static void
inf_adopted_request_log_verify_related(InfAdoptedRequestLog* log)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedRequestLogEntry* begin;
  InfAdoptedRequestLogEntry* end;
  InfAdoptedRequestLogEntry* current;

  InfAdoptedRequestLogEntry* lower_related;
  InfAdoptedRequestLogEntry* upper_related;

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  begin = priv->entries + priv->offset;
  end = begin + (priv->end - priv->begin);

  lower_related = NULL;
  upper_related = NULL;
  for(current = begin; current != end; ++current)
  {
    g_assert( (lower_related == NULL && upper_related == NULL) ||
              (lower_related != NULL && upper_related != NULL));

    if(lower_related == NULL)
    {
      g_assert(current->lower_related == current);
      g_assert(current->upper_related >= current);

      if(current->upper_related > current)
      {
        lower_related = current->lower_related;
        upper_related = current->upper_related;
      }
    }
    else
    {
      g_assert(current->lower_related == lower_related);
      g_assert(current->upper_related == upper_related);

      if(current == upper_related)
      {
        lower_related = NULL;
        upper_related = NULL;
      }
    }
  }
}
#else
# define inf_adopted_request_log_verify_related(log)
#endif

/*
 * Transformation cache
 */

static int
inf_adopted_request_log_cache_key_cmp(gconstpointer a,
                                      gconstpointer b,
                                      gpointer user_data)
{
  InfAdoptedRequestLog* log;
  InfAdoptedRequestLogPrivate* priv;
  const InfAdoptedStateVector* key_a;
  const InfAdoptedStateVector* key_b;
  guint n_a, n_b;

  key_a = (const InfAdoptedStateVector*)a;
  key_b = (const InfAdoptedStateVector*)b;

  log = (InfAdoptedRequestLog*)user_data;
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  /* Primary, order by component of user ID -- this makes cleanup very
   * efficient. */
  n_a = inf_adopted_state_vector_get(key_a, priv->user_id);
  n_b = inf_adopted_state_vector_get(key_b, priv->user_id);

  if(n_a < n_b)
    return -1;
  if(n_a > n_b)
    return 1;

  /* If user ID component is equal, go by other components. */
  return inf_adopted_state_vector_compare(key_a, key_b);
}

static gboolean
inf_adopted_request_log_remove_requests_cache_foreach_func(gpointer key,
                                                           gpointer value,
                                                           gpointer user_data)
{
  InfAdoptedStateVector* vector;
  InfAdoptedRequestLogCleanupCacheData* data;

  vector = (InfAdoptedStateVector*)key;
  data = (InfAdoptedRequestLogCleanupCacheData*)user_data;

  /* Remove all requests which are a cached translation of one of the requests
   * that have been removed, i.e. have a user component smaller than up_to. */
  if(inf_adopted_state_vector_get(vector, data->user_id) < data->up_to)
  {
    data->requests_to_remove =
      g_slist_prepend(data->requests_to_remove, vector);
    return FALSE;
  }
  else
  {
    /* Stop traversal. The tree is traversed in sorted order, and the primary
     * sort criteria of the tree is the vector component of the user. So if
     * we have reached this point, all other elements in the tree will have
     * a higher user component and are not scheduled for removal. */
    return TRUE;
  }
}

/*
 * Associated and Related requests
 */

/* Find the request that is undone if the next request was an undo request
 * (to be cached in priv->next_undo). Similar if type is REDO. */
static InfAdoptedRequestLogEntry*
inf_adopted_request_log_find_associated(InfAdoptedRequestLog* log,
                                        InfAdoptedRequestType type)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedRequestLogEntry* entry;

  g_assert(type != INF_ADOPTED_REQUEST_DO);
  
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  entry = priv->entries + priv->offset + (priv->end - priv->begin) - 1;

  while(entry >= priv->entries + priv->offset)
  {
    switch(inf_adopted_request_get_request_type(entry->request))
    {
    case INF_ADOPTED_REQUEST_DO:
      /* There is no Undo to Redo */
      if(type == INF_ADOPTED_REQUEST_REDO)
        return NULL;

      return entry;
    case INF_ADOPTED_REQUEST_UNDO:
      if(type == INF_ADOPTED_REQUEST_UNDO)
      {
        g_assert(entry->prev_associated != NULL);
        entry = entry->prev_associated - 1;
      }
      else
      {
        return entry;
      }

      break;
    case INF_ADOPTED_REQUEST_REDO:
      if(type == INF_ADOPTED_REQUEST_REDO)
      {
        g_assert(entry->prev_associated != NULL);
        entry = entry->prev_associated - 1;
      }
      else
      {
        return entry;
      }

      break;
    default:
      g_assert_not_reached();
      break;
    }
  }
  
  return NULL;
}

/*
 * GObject overrides
 */

static void
inf_adopted_request_log_init(InfAdoptedRequestLog* log)
{
  InfAdoptedRequestLogPrivate* priv;
  log->priv = INF_ADOPTED_REQUEST_LOG_GET_PRIVATE(log);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  priv->user_id = 0;

  priv->alloc = INF_ADOPTED_REQUEST_LOG_INC;
  priv->entries = g_malloc(priv->alloc * sizeof(InfAdoptedRequestLogEntry));
  priv->cache = NULL;
  priv->begin = 0;
  priv->end = 0;
  priv->offset = 0;

  priv->next_undo = NULL;
  priv->next_redo = NULL;
}

static void
inf_adopted_request_log_dispose(GObject* object)
{
  InfAdoptedRequestLog* log;
  InfAdoptedRequestLogPrivate* priv;
  guint i;

  log = INF_ADOPTED_REQUEST_LOG(object);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  if(priv->cache != NULL)
  {
    g_tree_destroy(priv->cache);
    priv->cache = NULL;
  }

  for(i = priv->offset; i < priv->offset + (priv->end - priv->begin); ++ i)
    g_object_unref(G_OBJECT(priv->entries[i].request));

  priv->begin = 0;
  priv->end = 0;
  priv->offset = 0;

  G_OBJECT_CLASS(inf_adopted_request_log_parent_class)->dispose(object);
}

static void
inf_adopted_request_log_finalize(GObject* object)
{
  InfAdoptedRequestLog* log;
  InfAdoptedRequestLogPrivate* priv;

  log = INF_ADOPTED_REQUEST_LOG(object);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_free(priv->entries);

  G_OBJECT_CLASS(inf_adopted_request_log_parent_class)->finalize(object);
}

static void
inf_adopted_request_log_set_property(GObject* object,
                                     guint prop_id,
                                     const GValue* value,
                                     GParamSpec* pspec)
{
  InfAdoptedRequestLog* log;
  InfAdoptedRequestLogPrivate* priv;

  log = INF_ADOPTED_REQUEST_LOG(object);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  switch(prop_id)
  {
  case PROP_USER_ID:
    g_assert(priv->user_id == 0); /* construct only */
    g_assert(g_value_get_uint(value) != 0); /* 0 is invalid user ID */
    priv->user_id = g_value_get_uint(value);
    break;
  case PROP_BEGIN:
    g_assert(priv->begin == 0); /* construct only */
    priv->begin = g_value_get_uint(value);
    priv->end = priv->begin;
    break;
  case PROP_END:
  case PROP_NEXT_UNDO:
  case PROP_NEXT_REDO:
    /* These are read only; fallthrough */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_request_log_get_property(GObject* object,
                                     guint prop_id,
                                     GValue* value,
                                     GParamSpec* pspec)
{
  InfAdoptedRequestLog* log;
  InfAdoptedRequestLogPrivate* priv;

  log = INF_ADOPTED_REQUEST_LOG(object);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  switch(prop_id)
  {
  case PROP_USER_ID:
    g_value_set_uint(value, priv->user_id);
    break;
  case PROP_BEGIN:
    g_value_set_uint(value, priv->begin);
    break;
  case PROP_END:
    g_value_set_uint(value, priv->end);
    break;
  case PROP_NEXT_UNDO:
    if(priv->next_undo != NULL)
      g_value_set_object(value, G_OBJECT(priv->next_undo->request));
    else
      g_value_set_object(value, NULL);
    
    break;
  case PROP_NEXT_REDO:
    if(priv->next_redo != NULL)
      g_value_set_object(value, G_OBJECT(priv->next_redo->request));
    else
      g_value_set_object(value, NULL);

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_request_log_add_request_handler(InfAdoptedRequestLog* log,
                                            InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedRequestLogEntry* entry;
  InfAdoptedRequestLogEntry* old_entries;
  InfAdoptedRequestLogEntry* current;
  guint i;

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_assert(inf_adopted_request_get_user_id(request) == priv->user_id);

  g_assert(
    priv->begin == priv->end ||
    inf_adopted_state_vector_get(
      inf_adopted_request_get_vector(request),
      priv->user_id
    ) == priv->end
  );

  if(priv->offset + (priv->end - priv->begin) == priv->alloc)
  {
    if(priv->offset > 0)
    {
      g_memmove(
        priv->entries,
        priv->entries + priv->offset,
        (priv->end - priv->begin) * sizeof(InfAdoptedRequestLogEntry)
      );

      /* We now corrupted several pointers in our entries because of
       * moving memory around. */
      for(i = 0; i < priv->end - priv->begin; ++ i)
      {
        g_assert(priv->entries[i].original != NULL);
        priv->entries[i].original -= priv->offset;

        if(priv->entries[i].next_associated != NULL)
          priv->entries[i].next_associated -= priv->offset;
        if(priv->entries[i].prev_associated != NULL)
          priv->entries[i].prev_associated -= priv->offset;
        priv->entries[i].lower_related -= priv->offset;
        priv->entries[i].upper_related -= priv->offset;
      }

      if(priv->next_undo != NULL) priv->next_undo -= priv->offset;
      if(priv->next_redo != NULL) priv->next_redo -= priv->offset;

      priv->offset = 0;
    }
    else
    {
      old_entries = priv->entries;
      priv->alloc += INF_ADOPTED_REQUEST_LOG_INC;

      priv->entries = g_realloc(
        priv->entries,
        priv->alloc * sizeof(InfAdoptedRequestLogEntry)
      );

      if(priv->entries != old_entries)
      {
        /* The realloc above could have invalidated several pointers */
        for(i = 0; i < priv->end - priv->begin; ++ i)
        {
          g_assert(priv->entries[i].original != NULL);
          priv->entries[i].original =
            priv->entries + (priv->entries[i].original - old_entries);

          if(priv->entries[i].next_associated != NULL)
          {
            priv->entries[i].next_associated = priv->entries +
              (priv->entries[i].next_associated - old_entries);
          }

          if(priv->entries[i].prev_associated != NULL)
          {
            priv->entries[i].prev_associated = priv->entries +
              (priv->entries[i].prev_associated - old_entries);
          }

          priv->entries[i].lower_related = priv->entries +
            (priv->entries[i].lower_related - old_entries);
          priv->entries[i].upper_related = priv->entries +
            (priv->entries[i].upper_related - old_entries);
        }

        if(priv->next_undo != NULL)
          priv->next_undo = priv->entries + (priv->next_undo - old_entries);
        if(priv->next_redo != NULL)
          priv->next_redo = priv->entries + (priv->next_redo - old_entries);
      }
    }
  }

  g_object_freeze_notify(G_OBJECT(log));

  if(priv->begin == priv->end)
  {
    priv->begin = inf_adopted_state_vector_get(
      inf_adopted_request_get_vector(request),
      priv->user_id
    );

    priv->end = priv->begin;
  }

  entry = &priv->entries[priv->offset + (priv->end - priv->begin)];
  ++ priv->end;

  g_object_notify(G_OBJECT(log), "end");

  entry->request = request;
  g_object_ref(G_OBJECT(request));

  switch(inf_adopted_request_get_request_type(request))
  {
  case INF_ADOPTED_REQUEST_DO:
    entry->original = entry;
    entry->next_associated = NULL;
    entry->prev_associated = NULL;
    entry->lower_related = entry;
    entry->upper_related = entry;
    priv->next_undo = entry;
    g_object_notify(G_OBJECT(log), "next-undo");

    if(priv->next_redo != NULL)
    {
      priv->next_redo = NULL;
      g_object_notify(G_OBJECT(log), "next-redo");
    }

    break;
  case INF_ADOPTED_REQUEST_UNDO:
    g_assert(priv->next_undo != NULL);

    entry->next_associated = NULL;
    entry->prev_associated = priv->next_undo;

    entry->prev_associated->next_associated = entry;
    entry->original = entry->prev_associated->original;

    entry->lower_related = entry->original->lower_related;
    entry->upper_related = entry;
    for(current = entry->lower_related; current != entry; ++current)
    {
      current->lower_related = entry->lower_related;
      current->upper_related = entry;
    }

    priv->next_undo =
      inf_adopted_request_log_find_associated(log, INF_ADOPTED_REQUEST_UNDO);
    g_object_notify(G_OBJECT(log), "next-undo");

    priv->next_redo = entry;
    g_object_notify(G_OBJECT(log), "next-redo");

    g_assert(priv->next_undo == NULL ||
             inf_adopted_request_get_request_type(priv->next_undo->request) ==
             INF_ADOPTED_REQUEST_DO ||
             inf_adopted_request_get_request_type(priv->next_undo->request) ==
             INF_ADOPTED_REQUEST_REDO);

    break;
  case INF_ADOPTED_REQUEST_REDO:
    g_assert(priv->next_redo != NULL);

    entry->next_associated = NULL;
    entry->prev_associated = priv->next_redo;

    entry->prev_associated->next_associated = entry;
    entry->original = entry->prev_associated->original;

    entry->lower_related = entry->original->lower_related;
    entry->upper_related = entry;
    for(current = entry->lower_related; current != entry; ++current)
    {
      current->lower_related = entry->lower_related;
      current->upper_related = entry;
    }

    priv->next_undo = entry;
    g_object_notify(G_OBJECT(log), "next-undo");

    priv->next_redo =
      inf_adopted_request_log_find_associated(log, INF_ADOPTED_REQUEST_REDO);
    g_object_notify(G_OBJECT(log), "next-redo");

    g_assert(priv->next_redo == NULL ||
             inf_adopted_request_get_request_type(priv->next_redo->request) ==
             INF_ADOPTED_REQUEST_UNDO);

    break;
  default:
    g_assert_not_reached();
    break;
  }

  inf_adopted_request_log_verify_related(log);
  g_object_thaw_notify(G_OBJECT(log));
}

static void
inf_adopted_request_log_class_init(
  InfAdoptedRequestLogClass* request_log_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(request_log_class);

  object_class->dispose = inf_adopted_request_log_dispose;
  object_class->finalize = inf_adopted_request_log_finalize;
  object_class->set_property = inf_adopted_request_log_set_property;
  object_class->get_property = inf_adopted_request_log_get_property;
  request_log_class->add_request =
    inf_adopted_request_log_add_request_handler;

  g_object_class_install_property(
    object_class,
    PROP_USER_ID,
    g_param_spec_uint(
      "user-id",
      "User ID",
      "The ID of the user whose requests the log contains",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_BEGIN,
    g_param_spec_uint(
      "begin",
      "Begin",
      "The first index contained in the log",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_END,
    g_param_spec_uint(
      "end",
      "End",
      "The index of the next request that is inserted into the log",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READABLE
    )
  );
  
  g_object_class_install_property(
    object_class,
    PROP_NEXT_UNDO,
    g_param_spec_object(
      "next-undo",
      "Next undo",
      "The request that is undone when the user issues an undo request now",
      INF_ADOPTED_TYPE_REQUEST,
      G_PARAM_READABLE
    )
  );
  
  g_object_class_install_property(
    object_class,
    PROP_NEXT_REDO,
    g_param_spec_object(
      "next-redo",
      "Next redo",
      "The request that is redone when the user issues a redo request new",
      INF_ADOPTED_TYPE_REQUEST,
      G_PARAM_READABLE
    )
  );

  /**
   * InfAdoptedRequestLog::add-request:
   * @log: The #InfAdoptedRequestLog to which a new request is added.
   * @request: The new request being added.
   *
   * This signal is emitted whenever a new request is added to the request log
   * via inf_adopted_request_log_add_request().
   */
  request_log_signals[ADD_REQUEST] = g_signal_new(
    "add-request",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedRequestLogClass, add_request),
    NULL, NULL,
    g_cclosure_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INF_ADOPTED_TYPE_REQUEST
  );
}

/*
 * Public API
 */

/**
 * inf_adopted_request_log_new: (constructor)
 * @user_id: The ID of the #InfAdoptedUser to create a request log for. The
 * request log only contains requests of that particular user.
 *
 * Creates a new #InfAdoptedRequestLog for the user with the given ID.
 *
 * Returns: (transfer full): A new #InfAdoptedRequestLog.
 **/
InfAdoptedRequestLog*
inf_adopted_request_log_new(guint user_id)
{
  GObject* object;

  g_return_val_if_fail(user_id != 0, NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_REQUEST_LOG,
    "user-id", user_id,
    NULL
  );

  return INF_ADOPTED_REQUEST_LOG(object);
}

/**
 * inf_adopted_request_log_get_user_id:
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the ID of the user whose requests @log contains.
 *
 * Returns: The log's user ID.
 **/
guint
inf_adopted_request_log_get_user_id(InfAdoptedRequestLog* log)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), 0);
  return INF_ADOPTED_REQUEST_LOG_PRIVATE(log)->user_id;
}

/**
 * inf_adopted_request_log_get_begin:
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the first index (i.e. the index of the oldest request) in the log.
 *
 * Returns: The first index in the log.
 **/
guint
inf_adopted_request_log_get_begin(InfAdoptedRequestLog* log)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), 0);
  return INF_ADOPTED_REQUEST_LOG_PRIVATE(log)->begin;
}

/**
 * inf_adopted_request_log_get_end:
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the index a newly inserted request would have (i.e. one past the
 * index of the newest request in the log). This ensures that
 * inf_adopted_request_log_get_end() - inf_adopted_request_log_get_begin()
 * reveals the number of requests in the log.
 *
 * Returns: The index of the next request in the log.
 **/
guint
inf_adopted_request_log_get_end(InfAdoptedRequestLog* log)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), 0);
  return INF_ADOPTED_REQUEST_LOG_PRIVATE(log)->end;
}

/**
 * inf_adopted_request_log_is_empty:
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns whether @log is empty. A log is empty if it does not contain any
 * requsets.
 *
 * Returns: Whether @log is empty.
 */
gboolean
inf_adopted_request_log_is_empty(InfAdoptedRequestLog* log)
{
  InfAdoptedRequestLogPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), TRUE);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  if(priv->begin == priv->end)
    return TRUE;

  return FALSE;
}

/**
 * inf_adopted_request_log_set_begin:
 * @log: A #InfAdoptedRequestLog.
 * @n: The index of the first request to be added to the log.
 *
 * This function sets the index of the first log that will be added to @log.
 * For a new request log, this is set to 0. If you intend to insert a request
 * sequence into @log that does not start with 0, then you can call this
 * function with the desired start index, so that
 * inf_adopted_request_log_get_begin() and inf_adopted_request_log_get_end()
 * return the correct value.
 *
 * If you don't need inf_adopted_request_log_get_begin() or
 * inf_adopted_request_log_get_end() before adding the first request to the
 * log, then you don't need to call this function, since
 * inf_adopted_request_log_add_request() will do it implicitely based on the
 * request's vector time component for the request log's user.
 *
 * This function can only be called if the request log is empty, see
 * inf_adopted_request_log_is_empty().
 */
void
inf_adopted_request_log_set_begin(InfAdoptedRequestLog* log,
                                  guint n)
{
  InfAdoptedRequestLogPrivate* priv;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log));

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  g_return_if_fail(priv->begin == priv->end);

  if(priv->begin != n)
  {
    priv->begin = n;
    priv->end = n;

    g_object_notify(G_OBJECT(log), "begin");
    g_object_notify(G_OBJECT(log), "end");
  }
}

/**
 * inf_adopted_request_log_get_request:
 * @log: A #InfAdoptedRequestLog.
 * @n: The index of a request contained in @log.
 *
 * Returns the request with the given index. Such a request must exist in
 * @log.
 *
 * Returns: (transfer none): A #InfAdoptedRequest. The request is owned by
 * the request log, you do not need to free it.
 **/
InfAdoptedRequest*
inf_adopted_request_log_get_request(InfAdoptedRequestLog* log,
                                    guint n)
{
  InfAdoptedRequestLogPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  g_return_val_if_fail(n >= priv->begin && n < priv->end, NULL);

  return priv->entries[priv->offset + n - priv->begin].request;
}

/**
 * inf_adopted_request_log_add_request:
 * @log: A #InfAdoptedRequestLog.
 * @request: A #InfAdoptedRequest.
 *
 * Inserts @request into @log. The component represented by the log's user
 * of the request's state vector must match the end index of @log if @log
 * is not empty. Also, the user that issued @request must be the same user as
 * the one this request log belongs to.
 **/
void
inf_adopted_request_log_add_request(InfAdoptedRequestLog* log,
                                    InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log));
  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_return_if_fail(inf_adopted_request_get_user_id(request) == priv->user_id);

  g_return_if_fail(
    priv->begin == priv->end ||
    inf_adopted_state_vector_get(
      inf_adopted_request_get_vector(request),
      priv->user_id
    ) == priv->end
  );

  g_signal_emit(G_OBJECT(log), request_log_signals[ADD_REQUEST], 0, request);
}

/**
 * inf_adopted_request_log_remove_requests:
 * @log: A #InfAdoptedRequestLog.
 * @up_to: The index of the first request not to remove.
 *
 * Removes all requests with index lower than @up_to. This function only works
 * if the request before @up_to is an "upper related" request.
 * See inf_adopted_request_log_upper_related(). This condition guarantees
 * that remaining requests do not refer to removed ones.
 **/
void
inf_adopted_request_log_remove_requests(InfAdoptedRequestLog* log,
                                        guint up_to)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedRequestLogCleanupCacheData data;
  guint i;
  GSList* item;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log));

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_return_if_fail(up_to >= priv->begin && up_to <= priv->end);

  g_return_if_fail(
    up_to == priv->begin ||
    priv->entries[priv->offset + up_to - priv->begin - 1].upper_related ==
    &priv->entries[priv->offset + up_to - priv->begin - 1]
  );

  for(i = priv->offset; i < priv->offset + (up_to - priv->begin); ++i)
    g_object_unref(G_OBJECT(priv->entries[i].request));

  g_object_freeze_notify(G_OBJECT(log));

  /* If the next undo/redo request has been removed, there cannot be
   * a new next undo/redo request, because the next undo is already the
   * newest one in the log */
  if(priv->next_undo != NULL)
  {
    if(priv->next_undo < priv->entries + priv->offset + (up_to - priv->begin))
    {
      priv->next_undo = NULL;
      g_object_notify(G_OBJECT(log), "next-undo");
    }
  }

  if(priv->next_redo != NULL)
  {
    if(priv->next_redo < priv->entries + priv->offset + (up_to - priv->begin))
    {
      priv->next_redo = NULL;
      g_object_notify(G_OBJECT(log), "next-redo");
    }
  }

  priv->offset += (up_to - priv->begin);
  priv->begin = up_to;
  g_object_notify(G_OBJECT(log), "begin");

  if(priv->cache != NULL)
  {
    data.user_id = priv->user_id;
    data.up_to = up_to;
    data.requests_to_remove = NULL;

    g_tree_foreach(
      priv->cache,
      inf_adopted_request_log_remove_requests_cache_foreach_func,
      &data
    );
    
    for(item = data.requests_to_remove; item != NULL; item = item->next)
      g_tree_remove(priv->cache, (InfAdoptedStateVector*)item->data);
    g_slist_free(data.requests_to_remove);
  }

  inf_adopted_request_log_verify_related(log);
  g_object_thaw_notify(G_OBJECT(log));
}

/**
 * inf_adopted_request_log_next_associated:
 * @log: A #InfAdoptedRequestLog.
 * @request: A #InfAdoptedRequest contained in @log.
 *
 * If @request is of type %INF_ADOPTED_REQUEST_DO or
 * %INF_ADOPTED_REQUEST_REDO, this returns UNDO request that undoes this
 * request, if any. If @request is a %INF_ADOPTED_REQUEST UNDO request, this
 * returns a request that redoes @request, if any.
 *
 * Returns: (transfer none) (allow-none): The next associated request of
 * @request, or %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_next_associated(InfAdoptedRequestLog* log,
                                        InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedStateVector* vector;
  guint user_id;
  guint n;
  InfAdoptedRequestLogEntry* entry;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  vector = inf_adopted_request_get_vector(request);
  user_id = inf_adopted_request_get_user_id(request);
  n = inf_adopted_state_vector_get(vector, user_id);

  g_return_val_if_fail(priv->user_id == user_id, NULL);
  g_return_val_if_fail(n >= priv->begin && n < priv->end, NULL);

  entry =  priv->entries + priv->offset + n - priv->begin;
  if(entry->next_associated == NULL) return NULL;
  return entry->next_associated->request;
}

/**
 * inf_adopted_request_log_prev_associated:
 * @log: A #InfAdoptedRequestLog.
 * @request: A #InfAdoptedRequest.
 *
 * If @request is of type %INF_ADOPTED_REQUEST_REDO or, this returns the UNDO
 * request that is redone by @request, if @request is a
 * %INF_ADOPTED_REQUEST_UNDO request, this returns the request that is undone
 * by @request.
 *
 * @request must either be contained in @log or the vector time component
 * of its own user must be equivalent to inf_adopted_request_log_get_end(),
 * in which case @request is treated as it if was the newest request in @log.
 *
 * Returns: (transfer none) (allow-none): The previous associated request of
 * @request, or %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_prev_associated(InfAdoptedRequestLog* log,
                                        InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedStateVector* vector;
  guint user_id;
  guint n;
  InfAdoptedRequestLogEntry* entry;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  vector = inf_adopted_request_get_vector(request);
  user_id = inf_adopted_request_get_user_id(request);
  n = inf_adopted_state_vector_get(vector, user_id);

  g_return_val_if_fail(priv->user_id == user_id, NULL);
  g_return_val_if_fail(n >= priv->begin && n <= priv->end, NULL);

  if(n == priv->end)
  {
    switch(inf_adopted_request_get_request_type(request))
    {
    case INF_ADOPTED_REQUEST_DO:
      entry = NULL;
      break;
    case INF_ADOPTED_REQUEST_UNDO:
      entry = priv->next_undo;
      break;
    case INF_ADOPTED_REQUEST_REDO:
      entry = priv->next_redo;
      break;
    default:
      g_assert_not_reached();
      break;
    }

    if(entry != NULL)
      return entry->request;
    else
      return NULL;
  }
  else
  {
    entry =  priv->entries + priv->offset + n - priv->begin;
    if(entry->prev_associated == NULL) return NULL;
    return entry->prev_associated->request;
  }
}

/**
 * inf_adopted_request_log_original_request:
 * @log: A #InfAdoptedRequestLog.
 * @request: A #InfAdoptedRequest.
 *
 * Returns the most previous associated request for @request, that is,
 * the %INF_ADOPTED_REQUEST_DO request that @request undoes or redoes,
 * respectively. If @request itself is a %INF_ADOPTED_REQUEST_DO request,
 * @request itself is returned.
 *
 * @request must either be contained in @log or the vector time component
 * of its own user must be equivalent to inf_adopted_request_log_get_end(),
 * in which case @request is treated as it if was the newest request in @log.
 *
 * Returns: (transfer none): The original request of @request.
 * This function never returns %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_original_request(InfAdoptedRequestLog* log,
                                         InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedStateVector* vector;
  guint user_id;
  guint n;
  InfAdoptedRequestLogEntry* entry;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  vector = inf_adopted_request_get_vector(request);
  user_id = inf_adopted_request_get_user_id(request);
  n = inf_adopted_state_vector_get(vector, user_id);

  g_return_val_if_fail(priv->user_id == user_id, NULL);
  g_return_val_if_fail(n >= priv->begin && n <= priv->end, NULL);

  if(n == priv->end)
  {
    switch(inf_adopted_request_get_request_type(request))
    {
    case INF_ADOPTED_REQUEST_DO:
      entry = NULL;
      break;
    case INF_ADOPTED_REQUEST_UNDO:
      entry = priv->next_undo;
      break;
    case INF_ADOPTED_REQUEST_REDO:
      entry = priv->next_redo;
      break;
    default:
      g_assert_not_reached();
      break;
    }

    if(entry != NULL)
      return entry->original->request;
    else
      return request;
  }
  else
  {
    /* Note that this check would in principle not be needed, if request were
     * always contained in request log. However, it can happen that it is a
     * different request (for example, a helper request that does not affect
     * the buffer -- InfTextUndoGrouping uses such a request). In this case we
     * do not want to exchange the given request by the one which is in the
     * request log. */
    if(inf_adopted_request_get_request_type(request) == INF_ADOPTED_REQUEST_DO)
      return request;

    entry = priv->entries + priv->offset + n - priv->begin;
    g_assert(entry->original != NULL);
    return entry->original->request;
  }
}

/**
 * inf_adopted_request_log_next_undo:
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the request that would be undone if a undo request was added to
 * the request log.
 *
 * Returns: (transfer none) (allow-none): The next request to be undone, or
 * %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_next_undo(InfAdoptedRequestLog* log)
{
  InfAdoptedRequestLogPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  if(priv->next_undo == NULL) return NULL;

  return priv->next_undo->request;
}

/**
 * inf_adopted_request_log_next_redo:
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the request that would be redone if a redo request was added to
 * the request log.
 *
 * Returns: (transfer none) (allow-none): The next request to be redone, or
 * %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_next_redo(InfAdoptedRequestLog* log)
{
  InfAdoptedRequestLogPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  if(priv->next_redo == NULL) return NULL;

  return priv->next_redo->request;
}

/**
 * inf_adopted_request_log_upper_related:
 * @log: A #InfAdoptedRequestLog.
 * @n: Index of a request in @log.
 *
 * Returns the newest request in @log that is related to @n<!-- -->th request
 * in log. Requests are considered related when they are enclosed by a
 * do/undo, an undo/redo or a redo/undo pair.
 *
 * Note that the sets of related requests within a request log are
 * disjoint.
 *
 * Returns: (transfer none): The newest request in @log being related to the
 * @n<!-- -->th request.
 **/
InfAdoptedRequest*
inf_adopted_request_log_upper_related(InfAdoptedRequestLog* log,
                                      guint n)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedRequestLogEntry* current;
  
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_return_val_if_fail(n >= priv->begin && n < priv->end, NULL);

  inf_adopted_request_log_verify_related(log);

  current = priv->entries + priv->offset + n - priv->begin;
  return current->upper_related->request;
}

/**
 * inf_adopted_request_log_lower_related:
 * @log: A #InfAdoptedRequestLog.
 * @n: Index of a request in @log.
 *
 * Returns the oldest request in @log that is related to @n<!-- -->th request
 * in log. Requests are considered related when they are enclosed by a
 * do/undo, an undo/redo or a redo/undo pair.
 *
 * Note that the sets of related requests within a request log are
 * disjoint.
 *
 * Returns: (transfer none): The oldest request in @log being related to the
 * @n<!-- -->th request.
 **/
InfAdoptedRequest*
inf_adopted_request_log_lower_related(InfAdoptedRequestLog* log,
                                      guint n)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedRequestLogEntry* current;
  
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_return_val_if_fail(n >= priv->begin && n < priv->end, NULL);

  inf_adopted_request_log_verify_related(log);

  current = priv->entries + priv->offset + n - priv->begin;
  return current->lower_related->request;
}

/**
 * inf_adopted_request_log_add_cached_request:
 * @log: A #InfAdoptedRequestLog.
 * @request: The #InfAdoptedRequest to add to the cache.
 *
 * #InfAdoptedRequestLog has a cache for translated requests built in. This
 * can be used to store requests that have been translated to another point
 * in the state space, and to efficiently look them up later. The advantage
 * of having this functionality within #InfAdoptedRequestLog is that when
 * requests are removed from the log the cache is automatically updated
 * accordingly.
 *
 * The data structure of the cache is optimized for quick lookup of entries
 * by the state vector and cleaning up entries in an efficient manner also
 * when the cache has grown very big.
 *
 * The request cache is mainly used by #InfAdoptedAlgorithm to efficiently
 * handle big transformations.
 *
 * This function adds a request to the cache of the request log.
 * @request must be a translated version of a request existing in @log.
 */
void
inf_adopted_request_log_add_cached_request(InfAdoptedRequestLog* log,
                                           InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedStateVector* vector;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log));
  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  g_return_if_fail(inf_adopted_request_get_user_id(request) == priv->user_id);

  vector = inf_adopted_request_get_vector(request);

  if(priv->cache == NULL)
  {
    priv->cache = g_tree_new_full(
      inf_adopted_request_log_cache_key_cmp,
      log,
      NULL,
      g_object_unref
    );
  }

  g_return_if_fail(g_tree_lookup(priv->cache, vector) == NULL);

  g_tree_insert(priv->cache, vector, request);
  g_object_ref(request);

  /* Implement lookup_cached_request */
  /* Use this implementation in InfAdoptedAlgorithm */
  /* Make the performance tests */
}

/**
 * inf_adopted_request_log_lookup_cached_request:
 * @log: A #InfAdoptedRequestLog.
 * @vec: The state vector at which to look up the request.
 *
 * Looks up the request at @vec from the cache of the request log. If the
 * queried request does not exist in the cache, the function returns %NULL.
 *
 * See inf_adopted_request_log_add_cached_request() for an explanation of
 * the request cache.
 *
 * Returns: (transfer none) (allow-none): The cached #InfAdoptedRequest
 * according to @vec, or %NULL.
 */
InfAdoptedRequest*
inf_adopted_request_log_lookup_cached_request(InfAdoptedRequestLog* log,
                                              InfAdoptedStateVector* vec)
{
  InfAdoptedRequestLogPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  g_return_val_if_fail(vec != NULL, NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  if(priv->cache == NULL) return NULL;

  return INF_ADOPTED_REQUEST(g_tree_lookup(priv->cache, vec));
}

/* vim:set et sw=2 ts=2: */
