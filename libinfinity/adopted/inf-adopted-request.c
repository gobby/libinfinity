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
 * SECTION:inf-adopted-request
 * @title: InfAdoptedRequest
 * @short_description: Request processed by #InfAdoptedAlgorithm.
 * @include: libinfinity/adopted/inf-adopted-request.h
 * @see_also: #InfAdoptedAlgorithm
 * @stability: Unstable
 *
 * An #InfAdoptedRequest is basically an #InfAdoptedOperation with some
 * metadata used by #InfAdoptedAlgorithm to determine which operations to
 * transform against each other. If the type of the request is
 * %INF_ADOPTED_REQUEST_DO, then it contains the operation to perform,
 * otherwise it does not because the request does not know the operation, it
 * is computed by #InfAdoptedAlgorithm when required. A #InfAdoptedRequest
 * also contains the state in which the operation can be applied to the
 * buffer and the user ID of the #InfAdoptedUser having generated the request.
 */

#include <libinfinity/adopted/inf-adopted-request.h>
#include <libinfinity/inf-define-enum.h>

static const GEnumValue inf_adopted_request_type_values[] = {
  {
    INF_ADOPTED_REQUEST_DO,
    "INF_ADOPTED_REQUEST_DO",
    "do",
  }, {
    INF_ADOPTED_REQUEST_UNDO,
    "INF_ADOPTED_REQUEST_UNDO",
    "undo",
  }, {
    INF_ADOPTED_REQUEST_REDO,
    "INF_ADOPTED_REQUEST_REDO",
    "redo"
  }, {
    0,
    NULL,
    NULL
  }
};

typedef struct _InfAdoptedRequestPrivate InfAdoptedRequestPrivate;
struct _InfAdoptedRequestPrivate {
  InfAdoptedRequestType type;
  InfAdoptedStateVector* vector;
  guint user_id;
  InfAdoptedOperation* operation;
  gint64 received;
  gint64 executed;
};

enum {
  PROP_0,

  /* construct only */
  PROP_TYPE,
  PROP_VECTOR,
  PROP_USER_ID,
  PROP_OPERATION,
  PROP_RECEIVED,

  /* read/write */
  PROP_EXECUTED
};

#define INF_ADOPTED_REQUEST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_REQUEST, InfAdoptedRequestPrivate))
#define INF_ADOPTED_REQUEST_PRIVATE(obj)     ((InfAdoptedRequestPrivate*)(obj)->priv)

INF_DEFINE_ENUM_TYPE(InfAdoptedRequestType, inf_adopted_request_type, inf_adopted_request_type_values)
G_DEFINE_TYPE_WITH_CODE(InfAdoptedRequest, inf_adopted_request, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfAdoptedRequest))

static void
inf_adopted_request_init(InfAdoptedRequest* request)
{
  InfAdoptedRequestPrivate* priv;
  request->priv = INF_ADOPTED_REQUEST_GET_PRIVATE(request);
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

  priv->type = INF_ADOPTED_REQUEST_DO;
  priv->vector = NULL;
  priv->user_id = 0;
  priv->operation = NULL;

  priv->received = 0;
  priv->executed = 0;
}

static void
inf_adopted_request_dispose(GObject* object)
{
  InfAdoptedRequest* request;
  InfAdoptedRequestPrivate* priv;

  request = INF_ADOPTED_REQUEST(object);
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

  if(priv->operation != NULL)
  {
    g_object_unref(G_OBJECT(priv->operation));
    priv->operation = NULL;
  }

  G_OBJECT_CLASS(inf_adopted_request_parent_class)->dispose(object);
}

static void
inf_adopted_request_finalize(GObject* object)
{
  InfAdoptedRequest* request;
  InfAdoptedRequestPrivate* priv;

  request = INF_ADOPTED_REQUEST(object);
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

 if(priv->vector != NULL)
  inf_adopted_state_vector_free(priv->vector);

  G_OBJECT_CLASS(inf_adopted_request_parent_class)->finalize(object);
}

static void
inf_adopted_request_set_property(GObject* object,
                                 guint prop_id,
                                 const GValue* value,
                                 GParamSpec* pspec)
{
  InfAdoptedRequest* request;
  InfAdoptedRequestPrivate* priv;

  request = INF_ADOPTED_REQUEST(object);
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    priv->type = g_value_get_enum(value);
    break;
  case PROP_VECTOR:
    g_assert(priv->vector == NULL); /* construct only */
    priv->vector = g_value_dup_boxed(value);
    break;
  case PROP_USER_ID:
    g_assert(priv->user_id == 0); /* construct only */
    g_assert(g_value_get_uint(value) != 0); /* 0 is invalid ID */
    priv->user_id = g_value_get_uint(value);
    break;
  case PROP_OPERATION:
    g_assert(priv->operation == NULL); /* construct only */
    priv->operation = INF_ADOPTED_OPERATION(g_value_dup_object(value));
    break;
  case PROP_RECEIVED:
    g_assert(priv->received == 0); /* construct only */
    priv->received = g_value_get_int64(value);
    break;
  case PROP_EXECUTED:
    priv->executed = g_value_get_int64(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_request_get_property(GObject* object,
                                 guint prop_id,
                                 GValue* value,
                                 GParamSpec* pspec)
{
  InfAdoptedRequest* request;
  InfAdoptedRequestPrivate* priv;

  request = INF_ADOPTED_REQUEST(object);
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_value_set_enum(value, priv->type);
    break;
  case PROP_VECTOR:
    g_value_set_boxed(value, priv->vector);
    break;
  case PROP_USER_ID:
    g_value_set_uint(value, priv->user_id);
    break;
  case PROP_OPERATION:
    g_value_set_object(value, G_OBJECT(priv->operation));
    break;
  case PROP_RECEIVED:
    g_value_set_int64(value, priv->received);
    break;
  case PROP_EXECUTED:
    g_value_set_int64(value, priv->executed);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_request_class_init(InfAdoptedRequestClass* request_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(request_class);

  object_class->dispose = inf_adopted_request_dispose;
  object_class->finalize = inf_adopted_request_finalize;
  object_class->set_property = inf_adopted_request_set_property;
  object_class->get_property = inf_adopted_request_get_property;

  g_object_class_install_property(
    object_class,
    PROP_TYPE,
    g_param_spec_enum(
      "type",
      "Type",
      "The type of the operation",
      INF_ADOPTED_TYPE_REQUEST_TYPE,
      INF_ADOPTED_REQUEST_DO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_VECTOR,
    g_param_spec_boxed(
      "vector",
      "Vector",
      "The vector time at which the request was made",
      INF_ADOPTED_TYPE_STATE_VECTOR,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_USER_ID,
    g_param_spec_uint(
      "user-id",
      "User ID",
      "The ID of the user that made the request",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_OPERATION,
    g_param_spec_object(
      "operation",
      "Operation",
      "The operation of the request",
      INF_ADOPTED_TYPE_OPERATION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_RECEIVED,
    g_param_spec_int64(
      "received",
      "Received",
      "Time the request was received, in microseconds",
      G_MININT64,
      G_MAXINT64,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_EXECUTED,
    g_param_spec_int64(
      "executed",
      "Executed",
      "Time the request was executed, in microseconds",
      G_MININT64,
      G_MAXINT64,
      0,
      G_PARAM_READWRITE
    )
  );
}

/**
 * inf_adopted_request_new_do:
 * @vector: The vector time at which the request was made.
 * @user_id: The ID of the user that made the request.
 * @operation: The operation the user performed.
 * @received: Time the request was received, in microseconds since the epoch.
 *
 * Creates a new #InfAdoptedRequest with type %INF_ADOPTED_REQUEST_DO.
 *
 * Return Value: A new DO request.
 **/
InfAdoptedRequest*
inf_adopted_request_new_do(InfAdoptedStateVector* vector,
                           guint user_id,
                           InfAdoptedOperation* operation,
                           gint64 received)
{
  GObject* object;

  g_return_val_if_fail(vector != NULL, NULL);
  g_return_val_if_fail(user_id != 0, NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_REQUEST,
    "type", INF_ADOPTED_REQUEST_DO,
    "vector", vector,
    "user-id", user_id,
    "operation", operation,
    "received", received,
    NULL
  );

  return INF_ADOPTED_REQUEST(object);
}

/**
 * inf_adopted_request_new_undo:
 * @vector: The vector time at which the request was made.
 * @user_id: The ID of the user that made the request.
 * @received: Time the request was received, in microseconds since the epoch.
 *
 * Creates a new #InfAdoptedRequest with type %INF_ADOPTED_REQUEST_UNDO.
 * The operation performed is implicitely defined by reverting the operation
 * of the associated DO or REDO request, but must still be computed by
 * #InfAdoptedAlgorithm.
 *
 * Return Value: A new UNDO request.
 **/
InfAdoptedRequest*
inf_adopted_request_new_undo(InfAdoptedStateVector* vector,
                             guint user_id,
                             gint64 received)
{
  GObject* object;

  g_return_val_if_fail(vector != NULL, NULL);
  g_return_val_if_fail(user_id != 0, NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_REQUEST,
    "type", INF_ADOPTED_REQUEST_UNDO,
    "vector", vector,
    "user-id", user_id,
    "received", received,
    NULL
  );
  
  return INF_ADOPTED_REQUEST(object);
}

/**
 * inf_adopted_request_new_redo:
 * @vector: The vector time at which the request was made.
 * @user_id: The ID of the user that made the request.
 * @received: Time the request was received, in microseconds since the epoch.
 *
 * Creates a new #InfAdoptedRequest with type %INF_ADOPTED_REQUEST_REDO. The
 * operation performed is implicitely defined by reverting the operation of
 * the associated UNDO request, but must still be computed by
 * #InfAdoptedAlgorithm.
 *
 * Return Value: A new REDO request.
 **/
InfAdoptedRequest*
inf_adopted_request_new_redo(InfAdoptedStateVector* vector,
                             guint user_id,
                             gint64 received)
{
  GObject* object;
  
  g_return_val_if_fail(vector != NULL, NULL);
  g_return_val_if_fail(user_id != 0, NULL);
  
  object = g_object_new(
    INF_ADOPTED_TYPE_REQUEST,
    "type", INF_ADOPTED_REQUEST_REDO,
    "vector", vector,
    "user-id", user_id,
    "received", received,
    NULL
  );
  
  return INF_ADOPTED_REQUEST(object);
}

/**
 * inf_adopted_request_copy:
 * @request: The #InfAdoptedRequest to copy.
 *
 * Creates a copy of @request with an initial reference count of 1.
 *
 * Return Value: A new #InfAdoptedRequest.
 **/
InfAdoptedRequest*
inf_adopted_request_copy(InfAdoptedRequest* request)
{
  InfAdoptedRequestPrivate* priv;
  InfAdoptedRequestPrivate* new_priv;
  GObject* object;
  
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

  if(priv->type == INF_ADOPTED_REQUEST_DO)
  {
    object = g_object_new(
      INF_ADOPTED_TYPE_REQUEST,
      "type", priv->type,
      "vector", priv->vector,
      "user-id", priv->user_id,
      "operation", priv->operation,
      "received", priv->received,
      NULL
    );
  }
  else
  {
    object = g_object_new(
      INF_ADOPTED_TYPE_REQUEST,
      "type", priv->type,
      "vector", priv->vector,
      "user-id", priv->user_id,
      "received", priv->received,
      NULL
    );
  }

  new_priv = INF_ADOPTED_REQUEST_PRIVATE(INF_ADOPTED_REQUEST(object));
  new_priv->executed = priv->executed;
  return INF_ADOPTED_REQUEST(object);
}

/**
 * inf_adopted_request_get_request_type:
 * @request: A #InfAdoptedRequest.
 *
 * Returns the request type of @request.
 *
 * Return Value: The type of @request.
 **/
InfAdoptedRequestType
inf_adopted_request_get_request_type(InfAdoptedRequest* request)
{
  g_return_val_if_fail(
    INF_ADOPTED_IS_REQUEST(request),
    INF_ADOPTED_REQUEST_DO
  );

  return INF_ADOPTED_REQUEST_PRIVATE(request)->type;
}

/**
 * inf_adopted_request_get_vector:
 * @request: A #InfAdoptedRequest.
 *
 * Returns the vector time the request was made i.e. its operation can be
 * applied to the buffer.
 *
 * Return Value: The state vector of @request. The returned value should
 * not be freed, it is owned by the #InfAdoptedRequest.
 **/
InfAdoptedStateVector*
inf_adopted_request_get_vector(InfAdoptedRequest* request)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);
  return INF_ADOPTED_REQUEST_PRIVATE(request)->vector;
}

/**
 * inf_adopted_request_get_user_id:
 * @request: A #InfAdoptedRequest.
 *
 * Returns the user ID of the user that issued @request.
 *
 * Return Value: The request's user ID.
 **/
guint
inf_adopted_request_get_user_id(InfAdoptedRequest* request)
{
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), 0);
  return INF_ADOPTED_REQUEST_PRIVATE(request)->user_id;
}

/**
 * inf_adopted_request_get_operation:
 * @request: A #InfAdoptedRequest.
 *
 * Returns the operation carried by the request. This can only be called if
 * the request's type is %INF_ADOPTED_REQUEST_DO.
 *
 * Return Value: The request's operation.
 **/
InfAdoptedOperation*
inf_adopted_request_get_operation(InfAdoptedRequest* request)
{
  InfAdoptedRequestPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);

  priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  g_return_val_if_fail(priv->operation != NULL, NULL);

  return priv->operation;
}

/**
 * inf_adopted_request_get_index:
 * @request: A #InfAdoptedRequest.
 *
 * Returns the vector time component of the request's own users. This
 * corresponds to the request index by that user.
 *
 * Returns: The vector time component of the request's own user.
 */
guint
inf_adopted_request_get_index(InfAdoptedRequest* request)
{
  InfAdoptedRequestPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), 0);

  priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  return inf_adopted_state_vector_get(priv->vector, priv->user_id);
}

/**
 * inf_adopted_request_get_receive_time:
 * @request: A #InfAdoptedRequest.
 *
 * Returns the time when the request was received, or, if it's a local
 * request, generated. The time is given in microseconds since January 1,
 * 1970.
 *
 * Returns: Time when the request was received.
 */
gint64
inf_adopted_request_get_receive_time(InfAdoptedRequest* request)
{
  InfAdoptedRequestPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), 0);

  priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  return priv->received;
}

/**
 * inf_adopted_request_get_execute_time:
 * @request: A #InfAdoptedRequest.
 *
 * Returns the time when the request was executed by an #InfAdoptedAlgorithm,
 * see the #InfAdoptedAlgorithm::execute-request signal. The time is given in
 * microseconds since January 1, 1970. If the request was not yet executed,
 * the function returns 0.
 *
 * Returns: The time when the function was executed, or 0.
 */
gint64
inf_adopted_request_get_execute_time(InfAdoptedRequest* request)
{
  InfAdoptedRequestPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), 0);

  priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  return priv->executed;
}

/**
 * inf_adopted_request_set_execute_time:
 * @request: A #InfAdoptedRequest.
 * @time: A time in microseconds since January 1, 1970.
 *
 * Sets the time when @request was executed. Usually this is called by
 * #InfAdoptedAlgorithm when it executes a request, i.e. translates it to the
 * current state of the document.
 */
void
inf_adopted_request_set_execute_time(InfAdoptedRequest* request,
                                     gint64 time)
{
  InfAdoptedRequestPrivate* priv;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));

  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

  if(priv->executed != time)
  {
    priv->executed = time;
    g_object_notify(G_OBJECT(request), "executed");
  }
}

/**
 * inf_adopted_request_need_concurrency_id:
 * @request: The request to transform.
 * @against: The request to transform against.
 *
 * Returns whether transforming @request against @against requires a
 * concurrency ID. If this function returns %TRUE, you must provide the
 * @request_lcs and @against_lcs parameters when calling
 * inf_adopted_request_transform().
 *
 * Both request need to be of type %INF_ADOPTED_REQUEST_DO, and their state
 * vectors must be the same.
 *
 * Returns: Whether transformation of @request against @against requires a
 * concurrency ID.
 */
gboolean
inf_adopted_request_need_concurrency_id(InfAdoptedRequest* request,
                                        InfAdoptedRequest* against)
{
  InfAdoptedRequestPrivate* request_priv;
  InfAdoptedRequestPrivate* against_priv;
  
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), FALSE);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(against), FALSE);
  
  request_priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  against_priv = INF_ADOPTED_REQUEST_PRIVATE(against);
  
  g_return_val_if_fail(request_priv->type == INF_ADOPTED_REQUEST_DO, FALSE);
  g_return_val_if_fail(against_priv->type == INF_ADOPTED_REQUEST_DO, FALSE);
  g_return_val_if_fail(request_priv->user_id != against_priv->user_id, FALSE);
  
  g_return_val_if_fail(
    inf_adopted_state_vector_compare(
      request_priv->vector,
      against_priv->vector
    ) == 0,
    FALSE
  );

  return inf_adopted_operation_need_concurrency_id(
    request_priv->operation,
    against_priv->operation
  );
}

/**
 * inf_adopted_request_transform:
 * @request: The request to transform.
 * @against: The request to transform against.
 * @request_lcs: The request to transform in a previous state, or %NULL.
 * @against_lcs: The request to transform against in a previous state, or
 * %NULL.
 *
 * Transforms the operation of @request against the operation of @against.
 * Both requests must be of type %INF_ADOPTED_REQUEST_DO, and their state
 * vectors must be the same.
 *
 * If the function inf_adopted_request_need_concurrency_id() returns %TRUE,
 * @request_lcs and @against_lcs must not be %NULL.
 *
 * Returns: A new #InfAdoptedRequest, the result of the transformation.
 **/
InfAdoptedRequest*
inf_adopted_request_transform(InfAdoptedRequest* request,
                              InfAdoptedRequest* against,
                              InfAdoptedRequest* request_lcs,
                              InfAdoptedRequest* against_lcs)
{
  InfAdoptedRequestPrivate* request_priv;
  InfAdoptedRequestPrivate* against_priv;
  InfAdoptedRequestPrivate* request_lcs_priv;
  InfAdoptedRequestPrivate* against_lcs_priv;
  InfAdoptedRequestPrivate* new_priv;
  InfAdoptedOperation* new_operation;
  InfAdoptedStateVector* new_vector;
  InfAdoptedRequest* new_request;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(against), NULL);

  request_priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  against_priv = INF_ADOPTED_REQUEST_PRIVATE(against);
  
  if(request_lcs != NULL)
    request_lcs_priv = INF_ADOPTED_REQUEST_PRIVATE(request_lcs);
  if(against_lcs != NULL)
    against_lcs_priv = INF_ADOPTED_REQUEST_PRIVATE(against_lcs);

  g_return_val_if_fail(request_priv->type == INF_ADOPTED_REQUEST_DO, NULL);
  g_return_val_if_fail(against_priv->type == INF_ADOPTED_REQUEST_DO, NULL);
  g_return_val_if_fail(
    request_lcs == NULL || request_lcs_priv->type == INF_ADOPTED_REQUEST_DO,
    NULL
  );
  g_return_val_if_fail(
    against_lcs == NULL || against_lcs_priv->type == INF_ADOPTED_REQUEST_DO,
    NULL
  );

  g_return_val_if_fail(request_priv->user_id != against_priv->user_id, NULL);
  g_return_val_if_fail(
    request_lcs == NULL || request_priv->user_id == request_lcs_priv->user_id,
    NULL
  );
  g_return_val_if_fail(
    against_lcs == NULL || against_priv->user_id == against_lcs_priv->user_id,
    NULL
  );

  g_return_val_if_fail(
    request_lcs == NULL ||
    inf_adopted_state_vector_causally_before(
      request_lcs_priv->vector,
      request_priv->vector
    ),
    NULL
  );

  g_return_val_if_fail(
    against_lcs == NULL ||
    inf_adopted_state_vector_causally_before(
      against_lcs_priv->vector,
      against_priv->vector
    ),
    NULL
  );

  g_return_val_if_fail(
    inf_adopted_state_vector_compare(
      request_priv->vector,
      against_priv->vector
    ) == 0, NULL
  );

  g_return_val_if_fail(
    inf_adopted_operation_need_concurrency_id(
      request_priv->operation,
      against_priv->operation
    ) == FALSE || (request_lcs != NULL && against_lcs != NULL),
    NULL
  );

  if(request_priv->user_id > against_priv->user_id)
  {
    new_operation = inf_adopted_operation_transform(
      request_priv->operation,
      against_priv->operation,
      request_lcs == NULL ? NULL : request_lcs_priv->operation,
      against_lcs == NULL ? NULL : against_lcs_priv->operation,
      INF_ADOPTED_CONCURRENCY_OTHER
    );
  }
  else
  {
    new_operation = inf_adopted_operation_transform(
      request_priv->operation,
      against_priv->operation,
      request_lcs == NULL ? NULL : request_lcs_priv->operation,
      against_lcs == NULL ? NULL : against_lcs_priv->operation,
      INF_ADOPTED_CONCURRENCY_SELF
    );
  }

  new_vector = inf_adopted_state_vector_copy(request_priv->vector);
  inf_adopted_state_vector_add(new_vector, against_priv->user_id, 1);

  new_request = inf_adopted_request_new_do(
    new_vector,
    request_priv->user_id,
    new_operation,
    request_priv->received
  );

  new_priv = INF_ADOPTED_REQUEST_PRIVATE(new_request);
  new_priv->executed = request_priv->executed;

  g_object_unref(new_operation);
  inf_adopted_state_vector_free(new_vector);
  return new_request;
}

/**
 * inf_adopted_request_mirror:
 * @request: A #InfAdoptedRequest.
 * @by: The number of requests between the original and the mirrored
 * operation.
 *
 * Mirrors @request as described in "Reducing the Problems of Group Undo" by
 * Matthias Ressel and Rul Gunzenh&auml;user
 * (http://portal.acm.org/citation.cfm?doid=320297.320312).
 *
 * Note that @by is the total amount of requests between the original and
 * mirrored request, and thus equivalent to 2j-1 in the paper's definition.
 *
 * @request must be of type %INF_ADOPTED_REQUEST_DO and its operation must
 * be reversible.
 *
 * Returns: The mirrored request as a new #InfAdoptedRequest.
 **/
InfAdoptedRequest*
inf_adopted_request_mirror(InfAdoptedRequest* request,
                           guint by)
{
  InfAdoptedRequestPrivate* priv;
  InfAdoptedRequestPrivate* new_priv;
  InfAdoptedOperation* new_operation;
  InfAdoptedStateVector* new_vector;
  InfAdoptedRequest* new_request;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);
  g_return_val_if_fail(by % 2 == 1, NULL);
  
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  g_return_val_if_fail(priv->type == INF_ADOPTED_REQUEST_DO, NULL);
  g_return_val_if_fail(
    inf_adopted_operation_is_reversible(priv->operation),
    NULL
  );

  new_operation = inf_adopted_operation_revert(priv->operation);
  new_vector = inf_adopted_state_vector_copy(priv->vector);
  inf_adopted_state_vector_add(new_vector, priv->user_id, by);

  new_request = inf_adopted_request_new_do(
    new_vector,
    priv->user_id,
    new_operation,
    priv->received
  );

  new_priv = INF_ADOPTED_REQUEST_PRIVATE(new_request);
  new_priv->executed = priv->executed;

  g_object_unref(new_operation);
  inf_adopted_state_vector_free(new_vector);
  return new_request;
}

/**
 * inf_adopted_request_fold:
 * @request: A #InfAdoptedRequest.
 * @into: The direction into which to fold.
 * @by: The number of operations between the original and the fold request.
 *
 * Folds @request as described in "Reducing the Problems of Group Undo" by
 * Matthias Ressel and Rul Gunzenh&auml;user
 * (http://portal.acm.org/citation.cfm?doid=320297.320312).
 *
 * Note that @by is the total amount of requests between the original and
 * the fold request, and thus equivalent to 2j in the paper's definition.
 *
 * @into must not be the same user as the one that issued @request.
 *
 * Returns: The folded request as a new #InfAdoptedRequest.
 **/
InfAdoptedRequest*
inf_adopted_request_fold(InfAdoptedRequest* request,
                         guint into,
                         guint by)
{
  InfAdoptedRequestPrivate* priv;
  InfAdoptedRequestPrivate* new_priv;
  InfAdoptedStateVector* new_vector;
  InfAdoptedRequest* new_request;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);
  g_return_val_if_fail(into != 0, NULL);
  g_return_val_if_fail(by % 2 == 0, NULL);

  priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  g_return_val_if_fail(priv->user_id != into, NULL);

  new_vector = inf_adopted_state_vector_copy(priv->vector);
  inf_adopted_state_vector_add(new_vector, into, by);

  if(priv->type == INF_ADOPTED_REQUEST_DO)
  {
    new_request = INF_ADOPTED_REQUEST(
      g_object_new(
        INF_ADOPTED_TYPE_REQUEST,
        "type", priv->type,
        "operation", priv->operation,
        "vector", new_vector,
        "user-id", priv->user_id,
        "received", priv->received,
        NULL
      )
    );
  }
  else
  {
    new_request = INF_ADOPTED_REQUEST(
      g_object_new(
        INF_ADOPTED_TYPE_REQUEST,
        "type", priv->type,
        "vector", new_vector,
        "user-id", priv->user_id,
        "received", priv->received,
        NULL
      )
    );
  }

  new_priv = INF_ADOPTED_REQUEST_PRIVATE(new_request);
  new_priv->executed = priv->executed;

  inf_adopted_state_vector_free(new_vector);
  return new_request;
}

/**
 * inf_adopted_request_affects_buffer:
 * @request: A #InfAdoptedRequest.
 *
 * Returns whether this request, when applied, changes the content of the
 * buffer. If this is a %INF_ADOPTED_REQUEST_UNDO or %INF_ADOPTED_REQUEST_REDO
 * request, than it always affects the buffer, because only requests that
 * affect the buffer can be undone or redone. If it is a
 * %INF_ADOPTED_REQUEST_DO request, than it returns whether its operation
 * has the %INF_ADOPTED_OPERATION_AFFECTS_BUFFER flag set.
 *
 * Returns: Whether @request affects the session's buffer.
 **/
gboolean
inf_adopted_request_affects_buffer(InfAdoptedRequest* request)
{
  InfAdoptedRequestPrivate* priv;

  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), FALSE);
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

  switch(priv->type)
  {
  case INF_ADOPTED_REQUEST_DO:
    if(inf_adopted_operation_get_flags(priv->operation) &
       INF_ADOPTED_OPERATION_AFFECTS_BUFFER)
    {
      return TRUE;
    }
    else
    {
      return FALSE;
    }
  case INF_ADOPTED_REQUEST_UNDO:
  case INF_ADOPTED_REQUEST_REDO:
    return TRUE;
  default:
    g_assert_not_reached();
    return FALSE;
  }
}

/* vim:set et sw=2 ts=2: */
