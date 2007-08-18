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

typedef struct _InfAdoptedRequestLogPrivate InfAdoptedRequestLogPrivate;
struct _InfAdoptedRequestLogPrivate {
  InfAdoptedUser* user;

  /* TODO: Also cache associated and original request, possibly in both
   * directions. */
  InfAdoptedRequest** data;

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
  PROP_END
};

#define INF_ADOPTED_REQUEST_LOG_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_REQUEST_LOG, InfAdoptedRequestLogPrivate))

static GObjectClass* parent_class;
static const guint INF_ADOPTED_REQUEST_LOG_INC = 0x80;

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
  priv->data = g_malloc(priv->alloc * sizeof(InfAdoptedRequestLog*));
  priv->begin = 0;
  priv->end = 0;
  priv->offset = 0;
}

static void
inf_adopted_request_log_dispose(GObject* object)
{
  InfAdoptedRequestLog* log;
  InfAdoptedRequestLogPrivate* priv;

  log = INF_ADOPTED_REQUEST_LOG(object);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  if(priv->user != NULL)
  {
    g_object_unref(G_OBJECT(priv->user));
    priv->user = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_adopted_request_log_finalize(GObject* object)
{
  InfAdoptedRequestLog* log;
  InfAdoptedRequestLogPrivate* priv;
  guint i;

  log = INF_ADOPTED_REQUEST_LOG(object);
  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  for(i = priv->offset; i < priv->offset + (priv->end - priv->begin); ++ i)
    inf_adopted_request_free(priv->data[i]);

  g_free(priv->data);

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

  return priv->data[priv->offset + n - priv->begin];
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

  g_return_if_fail(INF_ADOPTED_IS_REQUEST_LOG(log));
  g_return_if_fail(request != NULL);

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_return_if_fail(request->user == priv->user);

  g_return_if_fail(
    inf_adopted_state_vector_get(request->vector, INF_USER(priv->user)) ==
    priv->end
  );

  if(priv->offset + (priv->end - priv->begin) == priv->alloc)
  {
    if(priv->offset > 0)
    {
      g_memmove(
        priv->data,
	priv->data + priv->offset,
	(priv->end - priv->begin) * sizeof(InfAdoptedRequest*)
      );

      priv->offset = 0;
    }
    else
    {
      priv->alloc += INF_ADOPTED_REQUEST_LOG_INC;

      priv->data = g_realloc(
        priv->data,
        priv->alloc * sizeof(InfAdoptedRequest*)
      );        
    }
  }

  priv->data[priv->offset + (priv->end - priv->begin)] = request;
  ++ priv->end;

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

  priv = INF_ADOPTED_REQUEST_LOG_PRIVATE(log);

  g_return_if_fail(up_to >= priv->begin && up_to <= priv->end);

  for(i = priv->offset; i < priv->offset + (up_to - priv->begin); ++ i)
    inf_adopted_request_free(priv->data[i]);

  priv->offset += (up_to - priv->begin);
  priv->begin = up_to;

  g_object_notify(G_OBJECT(log), "begin");
}
