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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinfinity/adopted/inf-adopted-request-log.h>

#include <string.h> /* For (g_)memmove */

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

typedef struct _InfAdoptedRequestLogEntry InfAdoptedRequestLogEntry;
struct _InfAdoptedRequestLogEntry {
  InfAdoptedRequest* request;
  InfAdoptedRequestLogEntry* original;

  InfAdoptedRequestLogEntry* next_associated;
  InfAdoptedRequestLogEntry* prev_associated;
};

typedef struct _InfAdoptedRequestLogPrivate InfAdoptedRequestLogPrivate;
struct _InfAdoptedRequestLogPrivate {
  guint user_id;
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
  PROP_USER_ID,

  PROP_BEGIN,
  PROP_END,

  PROP_NEXT_UNDO,
  PROP_NEXT_REDO
};

#define INF_ADOPTED_REQUEST_LOG_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_REQUEST_LOG, InfAdoptedRequestLogPrivate))
#define INF_ADOPTED_REQUEST_LOG_PRIVATE(obj)     ((InfAdoptedRequestLogPrivate*)(obj)->priv)

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
  log->priv = INF_ADOPTED_REQUEST_LOG_GET_PRIVATE(log);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  priv->user_id = 0;

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

/**
 * inf_adopted_request_log_new:
 * @user_id: The ID of the #InfAdoptedUser to create a request log for. The
 * request log only contains requests of that particular user.
 *
 * Return Value: A new #InfAdoptedRequestLog.
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
 * Return Value: The log's user ID.
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
 * Return Value: The first index in the log.
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
 * Return Value: The index of the next request in the log.
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

/**
 * inf_adopted_request_add_request:
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
  InfAdoptedRequestLogEntry* entry;
  InfAdoptedRequestLogEntry* old_entries;
  guint i;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log));
  g_return_if_fail(request != NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_return_if_fail(inf_adopted_request_get_user_id(request) == priv->user_id);

  g_return_if_fail(
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

  g_object_thaw_notify(G_OBJECT(log));
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
 * Return Value: The next associated request of @request, or %NULL.
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
 * Return Value: The original request of @request. This function never
* returns %NULL.
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
    entry =  priv->entries + priv->offset + n - priv->begin;
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
 * Return Value: The next request to be undone, or %NULL.
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
 * Return Value: The next request to be redone, or %NULL.
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
 * @n: Index of the first request in a set of related requests.
 *
 * Returns the newest request in @log that is related to @n<!-- -->th request
 * in log. requests are considered related when they are enclosed by a
 * do/undo, an undo/redo or a redo/undo pair.
 *
 * In other words, the "upper related" request of a given request A is the
 * first request newer than A so that all requests before the "upper related"
 * request can be removed without any remaining request in the log still
 * refering to a removed one.
 *
 * Note that the sets of related requests within a request log are
 * disjoint.
 *
 * <note><para>
 * This function only works if request is the oldest request of a
 * set of related requests. This could be changed in later versions.
 * </para></note>
 *
 * Return Value: The newest request in @log being related to @request.
 **/
InfAdoptedRequest*
inf_adopted_request_log_upper_related(InfAdoptedRequestLog* log,
                                      guint n)
{
  InfAdoptedRequestLogPrivate* priv;
  
  InfAdoptedRequestLogEntry* newest_related;
  InfAdoptedRequestLogEntry* current;
  
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log), NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

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
