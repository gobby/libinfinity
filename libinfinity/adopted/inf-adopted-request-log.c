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

#include <libinfinity/adopted/inf-adopted-request-log.h>

#include <string.h> /* (g_)memmove */

typedef struct _InfAdoptedRequestLogEntry InfAdoptedRequestLogEntry;
struct _InfAdoptedRequestLogEntry {
  InfAdoptedRequest* request;
  InfAdoptedRequestLogEntry* original;

  InfAdoptedRequestLogEntry* next_associated;
  InfAdoptedRequestLogEntry* prev_associated;
};

typedef struct _InfAdoptedRequestLogPrivate InfAdoptedRequestLogPrivate;
struct _InfAdoptedRequestLogPrivate {
  InfAdoptedUser* user;
  InfAdoptedRequestLogEntry* entries;

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
  PROP_USER,

  PROP_BEGIN,
  PROP_END,

  PROP_NEXT_UNDO,
  PROP_NEXT_REDO
};

#define INF_ADOPTED_REQUEST_LOG_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_REQUEST_LOG, InfAdoptedRequestLogPrivate))

static GObjectClass* parent_class;
static const guint INF_ADOPTED_REQUEST_LOG_INC = 0x80;

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

  entry = priv->entries + priv->offset + priv->end - 1;

  while(entry >= priv->entries + priv->offset)
  {
    switch(inf_adopted_request_get_request_type(entry->request))
    {
    case INF_ADOPTED_REQUEST_DO:
      g_assert(type != INF_ADOPTED_REQUEST_REDO);
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

/* Only used in an assertion. Verifies that the removed entries do not
 * reference non-removed entries with their next_associated / prev_associated
 * fields. This would, in turn, mean, that a non-removed entry would point
 * to an entry that is about to be removed now. In effect, only whole
 * do/undo/redo/undo/redo/etc. chains can be removed but not be splitted
 * in between. */
static gboolean
inf_adopted_request_log_is_related(InfAdoptedRequestLog* log,
                                   guint up_to)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedRequestLogEntry* entry;
  guint index;
  guint i;

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  
  for(i = priv->offset; i < priv->offset + (up_to - priv->begin); ++ i)
  {
    entry = &priv->entries[i];

    /* Note that the only problematic field is next_associated because
     * the other point behind entry. */
    if(entry->next_associated != NULL)
    {
      /* Index of the associated entry within the priv->entries array,
       * with priv->offset as origin */
      index = entry->next_associated - (priv->entries + priv->offset);

      /* There is a relation if this points to something that is not
       * going to be removed */
      if(priv->begin + index >= up_to)
        return TRUE;
    }
  }

  return FALSE;
}

static void
inf_adopted_request_log_init(GTypeInstance* instance,
                             gpointer g_class)
{
  InfAdoptedRequestLog* log;
  InfAdoptedRequestLogPrivate* priv;

  log = INF_ADOPTED_REQUEST_LOG(instance);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  priv->user = NULL;

  priv->alloc = INF_ADOPTED_REQUEST_LOG_INC;
  priv->entries = g_malloc(priv->alloc * sizeof(InfAdoptedRequestLogEntry));
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

  for(i = priv->offset; i < priv->offset + (priv->end - priv->begin); ++ i)
    g_object_unref(G_OBJECT(priv->entries[i].request));

  if(priv->user != NULL)
  {
    g_object_unref(G_OBJECT(priv->user));
    priv->user = NULL;
  }

  priv->begin = 0;
  priv->end = 0;
  priv->offset = 0;

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_adopted_request_log_finalize(GObject* object)
{
  InfAdoptedRequestLog* log;
  InfAdoptedRequestLogPrivate* priv;

  log = INF_ADOPTED_REQUEST_LOG(object);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_free(priv->entries);

  G_OBJECT_CLASS(parent_class)->finalize(object);
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
  case PROP_USER:
    g_assert(priv->user == NULL); /* construct only */
    priv->user = INF_ADOPTED_USER(g_value_dup_object(value));
    break;
  case PROP_BEGIN:
  case PROP_END:
  case PROP_NEXT_UNDO:
  case PROP_NEXT_REDO:
    /* These are read only; fallthrough */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
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
  case PROP_USER:
    g_value_set_object(value, G_OBJECT(priv->user));
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
inf_adopted_request_log_class_init(gpointer g_class,
                                       gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAdoptedRequestLogPrivate));

  object_class->dispose = inf_adopted_request_log_dispose;
  object_class->finalize = inf_adopted_request_log_finalize;
  object_class->set_property = inf_adopted_request_log_set_property;
  object_class->get_property = inf_adopted_request_log_get_property;

  g_object_class_install_property(
    object_class,
    PROP_USER,
    g_param_spec_object(
      "user",
      "User",
      "The user whose requests the log contains",
      INF_ADOPTED_TYPE_USER,
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
      G_PARAM_READABLE
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
}

GType
inf_adopted_request_log_get_type(void)
{
  static GType request_log_type = 0;

  if(!request_log_type)
  {
    static const GTypeInfo request_log_type_info = {
      sizeof(InfAdoptedRequestLogClass),    /* class_size */
      NULL,                                 /* base_init */
      NULL,                                 /* base_finalize */
      inf_adopted_request_log_class_init,   /* class_init */
      NULL,                                 /* class_finalize */
      NULL,                                 /* class_data */
      sizeof(InfAdoptedRequestLog),         /* instance_size */
      0,                                    /* n_preallocs */
      inf_adopted_request_log_init,         /* instance_init */
      NULL                                  /* value_table */
    };

    request_log_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfAdoptedRequestLog",
      &request_log_type_info,
      0
    );
  }

  return request_log_type;
}

/** inf_adopted_request_log_new:
 *
 * @user: The #InfAdoptedUser to create a request log for. The request log
 * only contains requests of that particular user.
 *
 * Return Value: A new #InfAdoptedRequestLog.
 **/
InfAdoptedRequestLog*
inf_adopted_request_log_new(InfAdoptedUser* user)
{
  GObject* object;

  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);

  object = g_object_new(INF_ADOPTED_TYPE_REQUEST_LOG, "user", user, NULL);
  return INF_ADOPTED_REQUEST_LOG(object);
}

/** inf_adopted_request_log_get_user:
 *
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the user whose requests @log contains.
 *
 * Return Value: The log's user.
 **/
InfAdoptedUser*
inf_adopted_request_log_get_user(InfAdoptedRequestLog* log)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  return INF_ADOPTED_REQUEST_LOG_PRIVATE(log)->user;
}

/** inf_adopted_request_log_get_begin:
 *
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the first index (i.e. the index of the oldest request) in the log.
 *
 * Return Value: The first index in the log.
 **/
guint
inf_adopted_request_log_get_begin(InfAdoptedRequestLog* log)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), 0);
  return INF_ADOPTED_REQUEST_LOG_PRIVATE(log)->begin;
}

/** inf_adopted_request_log_get_end:
 *
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the index a newly inserted request would have (i.e. one past the
 * index of the newest request in the log). This ensures that
 * inf_adopted_request_log_get_end() - inf_adopted_request_log_get_begin()
 * reveals the number of requests in the log.
 *
 * Return Value: The index of the next request in the log.
 **/
guint
inf_adopted_request_log_get_end(InfAdoptedRequestLog* log)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), 0);
  return INF_ADOPTED_REQUEST_LOG_PRIVATE(log)->end;
}

/** inf_adopted_request_log_get_request:
 *
 * @log: A #InfAdoptedRequestLog.
 * @n: The index of a request contained in @log.
 *
 * Returns the request with the given index. Such a request must exist in
 * @log.
 *
 * Return Value: A #InfAdoptedRequest. The request is owned by the request
 * log, you do not need to free it.
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

/** inf_adopted_request_add_request:
 *
 * @log: A #InfAdoptedRequestLog.
 * @request: A #InfAdoptedRequest.
 *
 * Inserts @request into @log. The component represented by the log's user
 * of the request's state vector must match the end index of @log. Also, the
 * user that issued @request must be the same user as the one this request log
 * belongs to.
 *
 * This function takes ownership of @request.
 **/
void
inf_adopted_request_log_add_request(InfAdoptedRequestLog* log,
                                    InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedRequestLogEntry* entry;
  guint i;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log));
  g_return_if_fail(request != NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_return_if_fail(inf_adopted_request_get_user(request) == priv->user);

  g_return_if_fail(
    inf_adopted_state_vector_get(
      inf_adopted_request_get_vector(request),
      INF_USER(priv->user)
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
      }

      if(priv->next_undo != NULL) priv->next_undo -= priv->offset;
      if(priv->next_redo != NULL) priv->next_redo -= priv->offset;

      priv->offset = 0;
    }
    else
    {
      priv->alloc += INF_ADOPTED_REQUEST_LOG_INC;

      priv->entries = g_realloc(
        priv->entries,
        priv->alloc * sizeof(InfAdoptedRequestLogEntry)
      );
    }
  }

  entry = &priv->entries[priv->offset + (priv->end - priv->begin)];
  ++ priv->end;

  entry->request = request;

  switch(inf_adopted_request_get_request_type(request))
  {
  case INF_ADOPTED_REQUEST_DO:
    entry->original = entry;
    entry->next_associated = NULL;
    entry->prev_associated = NULL;
    priv->next_undo = entry;
    priv->next_redo = NULL;
    break;
  case INF_ADOPTED_REQUEST_UNDO:
    g_assert(priv->next_undo != NULL);

    entry->next_associated = NULL;
    entry->prev_associated = priv->next_undo;

    entry->prev_associated->next_associated = entry;
    entry->original = entry->prev_associated->original;

    priv->next_undo =
      inf_adopted_request_log_find_associated(log, INF_ADOPTED_REQUEST_UNDO);
    priv->next_redo = entry;

    break;
  case INF_ADOPTED_REQUEST_REDO:
    g_assert(priv->next_redo != NULL);

    entry->next_associated = NULL;
    entry->prev_associated = priv->next_redo;

    entry->prev_associated->next_associated = entry;
    entry->original = entry->prev_associated->original;

    priv->next_undo = entry;
    priv->next_redo =
      inf_adopted_request_log_find_associated(log, INF_ADOPTED_REQUEST_REDO);

    break;
  default:
    g_assert_not_reached();
    break;
  }

  /* TODO: Only notify if they really changed */
  g_object_notify(G_OBJECT(log), "next-undo");
  g_object_notify(G_OBJECT(log), "next-redo");
  g_object_notify(G_OBJECT(log), "end");
}

/** inf_adopted_request_log_remove_requests:
 *
 * @log: A #InfAdoptedRequestLog.
 * @up_to: The index of the first request not to remove.
 *
 * Removes all requests with index lower than @up_to.
 **/
void
inf_adopted_request_log_remove_requests(InfAdoptedRequestLog* log,
                                        guint up_to)
{
  InfAdoptedRequestLogPrivate* priv;
  guint i;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log));
  g_return_if_fail(inf_adopted_request_log_is_related(log, up_to) == FALSE);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_return_if_fail(up_to >= priv->begin && up_to <= priv->end);

  for(i = priv->offset; i < priv->offset + (up_to - priv->begin); ++ i)
    g_object_unref(G_OBJECT(priv->entries[i].request));

  g_object_freeze_notify(G_OBJECT(log));

  /* If the next undo/redo request has been removed, there cannot be
   * a new next undo/redo request, because the next undo is already the
   * newest one in the log */
  if(priv->next_undo != NULL)
  {
    if(priv->next_undo < priv->entries + priv->offset + up_to)
    {
      priv->next_undo = NULL;
      g_object_notify(G_OBJECT(log), "next-undo");
    }
  }

  if(priv->next_redo != NULL)
  {
    if(priv->next_redo < priv->entries + priv->offset + up_to)
    {
      priv->next_redo = NULL;
      g_object_notify(G_OBJECT(log), "next-redo");
    }
  }

  priv->offset += (up_to - priv->begin);
  priv->begin = up_to;

  g_object_notify(G_OBJECT(log), "begin");
  g_object_thaw_notify(G_OBJECT(log));
}

/** inf_adopted_request_log_next_associated:
 *
 * @log: A #InfAdoptedRequestLog.
 * @request: A #InfAdoptedRequest contained in @log.
 *
 * If @request is of type %INF_ADOPTED_REQUEST_DO or
 * %INF_ADOPTED_REQUEST_REDO, this returns UNDO request that undoes this
 * request, if any. If @request is a %INF_ADOPTED_REQUEST UNDO request, this
 * returns a request that redoes @request, if any.
 *
 * Return Value: The next associated request of @request, or %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_next_associated(InfAdoptedRequestLog* log,
                                        InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedStateVector* vector;
  InfAdoptedUser* user;
  guint n;
  InfAdoptedRequestLogEntry* entry;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  vector = inf_adopted_request_get_vector(request);
  user = inf_adopted_request_get_user(request);
  n = inf_adopted_state_vector_get(vector, INF_USER(user));

  g_return_val_if_fail(priv->user == user, NULL);
  g_return_val_if_fail(n >= priv->begin && n < priv->end, NULL);

  entry =  priv->entries + priv->offset + n - priv->begin;
  if(entry->next_associated == NULL) return NULL;
  return entry->next_associated->request;
}

/** inf_adopted_request_log_prev_associated:
 *
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
 * in which case @request is treated as it if was the newest requset in @log.
 *
 * Return Value: The previous associated request of @request, or %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_prev_associated(InfAdoptedRequestLog* log,
                                        InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedStateVector* vector;
  InfAdoptedUser* user;
  guint n;
  InfAdoptedRequestLogEntry* entry;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  vector = inf_adopted_request_get_vector(request);
  user = inf_adopted_request_get_user(request);
  n = inf_adopted_state_vector_get(vector, INF_USER(user));

  g_return_val_if_fail(priv->user == user, NULL);
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

/** inf_adopted_request_log_original_request:
 *
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
 * in which case @request is treated as it if was the newest requset in @log.
 *
 * Return Value: The original request of @request. This function never
* returns %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_original_request(InfAdoptedRequestLog* log,
                                         InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedStateVector* vector;
  InfAdoptedUser* user;
  guint n;
  InfAdoptedRequestLogEntry* entry;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  vector = inf_adopted_request_get_vector(request);
  user = inf_adopted_request_get_user(request);
  n = inf_adopted_state_vector_get(vector, INF_USER(user));

  g_return_val_if_fail(priv->user == user, NULL);
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
    entry =  priv->entries + priv->offset + n - priv->begin;
    g_assert(entry->original != NULL);
    return entry->original->request;
  }
}

/** inf_adopted_request_log_next_undo:
 *
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the request that would be undone if a undo request was added to
 * the request log.
 *
 * Return Value: The next request to be undone, or %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_next_undo(InfAdoptedRequestLog* log)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  return INF_ADOPTED_REQUEST_LOG_PRIVATE(log)->next_undo->request;
}

/** inf_adopted_request_log_next_redo:
 *
 * @log: A #InfAdoptedRequestLog.
 *
 * Returns the request that would be redone if a redo request was added to
 * the request log.
 *
 * Return Value: The next request to be redone, or %NULL.
 **/
InfAdoptedRequest*
inf_adopted_request_log_next_redo(InfAdoptedRequestLog* log)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  return INF_ADOPTED_REQUEST_LOG_PRIVATE(log)->next_redo->request;
}

/** inf_adopted_request_log_upper_related:
 *
 * @log: A #InfAdoptedRequestLog.
 * @request: A #InfAdoptedRequest contained in @log.
 *
 * Returns the newest request in @log that is related to @request. Two
 * requests are considered related when they are enclosed by a do/undo,
 * an undo/redo or a redo/undo pair.
 *
 * TODO: This function only works if request is the oldest request of a
 * set of related requests.
 *
 * Return Value: The newest request in @log being related to @request.
 **/
InfAdoptedRequest*
inf_adopted_request_log_upper_related(InfAdoptedRequestLog* log,
                                      InfAdoptedRequest* request)
{
  InfAdoptedRequestLogPrivate* priv;
  InfAdoptedUser* user;
  InfAdoptedStateVector* vector;
  guint n;
  
  InfAdoptedRequestLogEntry* newest_related;
  InfAdoptedRequestLogEntry* current;
  
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);
  user = inf_adopted_request_get_user(request);

  g_return_val_if_fail(user == priv->user, NULL);

  vector = inf_adopted_request_get_vector(request);
  n = inf_adopted_state_vector_get(vector, INF_USER(user));

  newest_related = priv->entries + priv->offset + n - priv->begin;
  for(current = newest_related; current <= newest_related; ++ current)
  {
    if(current->next_associated != NULL &&
       current->next_associated > newest_related)
    {
      newest_related = current->next_associated;
    }
  }

  return newest_related->request;
}

/* vim:set et sw=2 ts=2: */
