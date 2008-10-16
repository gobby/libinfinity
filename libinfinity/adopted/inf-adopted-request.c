/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/adopted/inf-adopted-request.h>

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

typedef struct _InfAdoptedRequestPrivate InfAdoptedRequestPrivate;
struct _InfAdoptedRequestPrivate {
  InfAdoptedRequestType type;
  InfAdoptedStateVector* vector;
  guint user_id;
  InfAdoptedOperation* operation;
};

enum {
  PROP_0,

  /* construct only */
  PROP_TYPE,
  PROP_VECTOR,
  PROP_USER_ID,
  PROP_OPERATION
};

#define INF_ADOPTED_REQUEST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_REQUEST, InfAdoptedRequestPrivate))
#define INF_ADOPTED_REQUEST_PRIVATE(obj)     ((InfAdoptedRequestPrivate*)(obj)->priv)

static GObjectClass* parent_class;

static void
inf_adopted_request_init(GTypeInstance* instance,
                         gpointer g_class)
{
  InfAdoptedRequest* request;
  InfAdoptedRequestPrivate* priv;

  request = INF_ADOPTED_REQUEST(instance);
  request->priv = INF_ADOPTED_REQUEST_GET_PRIVATE(request);
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

  priv->type = INF_ADOPTED_REQUEST_DO;
  priv->vector = NULL;
  priv->user_id = 0;
  priv->operation = NULL;
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

  G_OBJECT_CLASS(parent_class)->dispose(object);
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

  G_OBJECT_CLASS(parent_class)->finalize(object);
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
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_request_class_init(gpointer g_class,
                               gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAdoptedRequestPrivate));

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
}

GType
inf_adopted_request_type_get_type(void)
{
  static GType request_type_type = 0;

  if(!request_type_type)
  {
    static const GEnumValue request_type_type_values[] = {
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

    request_type_type = g_enum_register_static(
      "InfAdoptedRequestType",
      request_type_type_values
    );
  }

  return request_type_type;
}

GType
inf_adopted_request_get_type(void)
{
  static GType request_type = 0;

  if(!request_type)
  {
    static const GTypeInfo request_type_info = {
      sizeof(InfAdoptedRequestClass),   /* class_size */
      NULL,                             /* base_init */
      NULL,                             /* base_finalize */
      inf_adopted_request_class_init,   /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      sizeof(InfAdoptedRequest),        /* instance_size */
      0,                                /* n_preallocs */
      inf_adopted_request_init,         /* instance_init */
      NULL                              /* value_table */
    };

    request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfAdoptedRequest",
      &request_type_info,
      0
    );
  }

  return request_type;
}

/**
 * inf_adopted_request_new_do:
 * @vector: The vector time at which the request was made.
 * @user_id: The ID of the user that made the request.
 * @operation: The operation the user performed.
 *
 * Creates a new #InfAdoptedRequest with type %INF_ADOPTED_REQUEST_DO.
 *
 * Return Value: A new DO request.
 **/
InfAdoptedRequest*
inf_adopted_request_new_do(InfAdoptedStateVector* vector,
                           guint user_id,
                           InfAdoptedOperation* operation)
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
    NULL
  );

  return INF_ADOPTED_REQUEST(object);
}

/**
 * inf_adopted_request_new_undo:
 * @vector: The vector time at which the request was made.
 * @user_id: The ID of the user that made the request.
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
                             guint user_id)
{
  GObject* object;

  g_return_val_if_fail(vector != NULL, NULL);
  g_return_val_if_fail(user_id != 0, NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_REQUEST,
    "type", INF_ADOPTED_REQUEST_UNDO,
    "vector", vector,
    "user-id", user_id,
    NULL
  );
  
  return INF_ADOPTED_REQUEST(object);
}

/**
 * inf_adopted_request_new_redo:
 * @vector: The vector time at which the request was made.
 * @user_id: The ID of the user that made the request.
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
                             guint user_id)
{
  GObject* object;
  
  g_return_val_if_fail(vector != NULL, NULL);
  g_return_val_if_fail(user_id != 0, NULL);
  
  object = g_object_new(
    INF_ADOPTED_TYPE_REQUEST,
    "type", INF_ADOPTED_REQUEST_REDO,
    "vector", vector,
    "user-id", user_id,
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
      NULL
    );
  }

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
 * inf_adopted_request_need_concurrency_id:
 * @request: The request to transform.
 * @against: The request to transform against.
 *
 * Returns whether transforming @request against @against requires a
 * concurrency ID. You can still call inf_adopted_request_transform() with
 * a concurrency ID of %INF_ADOPTED_CONCURRENCY_NONE even if this function
 * returns %TRUE if you don't have another possibility to find a
 * concurrency ID in which case user IDs are used to determine which request
 * to transform.
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
 * inf_adopted_request_get_concurrency_id:
 * @request: The request to transform.
 * @against: The request to transform against.
 *
 * Returns a concurrency ID for transformation of @operation against @against.
 * It always returns %INF_ADOPTED_CONCURRENCY_NONE when
 * inf_adopted_request_need_concurrency_id() returns %TRUE (but that's not
 * necessarily true the other way around), since it is not possible to decide
 * which operation to transform without any additional information.
 *
 * However, the function can be called on the same requests in a previous
 * state. In some cases, a decision can be made based on these previous
 * requests. This can be used as a concurrency ID for a call to
 * inf_adopted_request_transform(). If this does not yield a decision, it is
 * still possible to call inf_adopted_request_transform() with
 * %INF_ADOPTED_CONCURRENCY_NONE as concurrency ID in which case an arbitrary
 * request will be transformed, based on the user IDs of the requests.
 *
 * Both requests must be of type %INF_ADOPTED_REQUEST_DO, and their state
 * vectors must be the same.
 *
 * Returns: A concurrency ID between @operation and @against. Can be
 * %INF_ADOPTED_CONCURRENCY_NONE in case no decision can be made.
 */
InfAdoptedConcurrencyId
inf_adopted_request_get_concurrency_id(InfAdoptedRequest* request,
                                       InfAdoptedRequest* against)
{
  InfAdoptedRequestPrivate* request_priv;
  InfAdoptedRequestPrivate* against_priv;
  
  g_return_val_if_fail(
    INF_ADOPTED_IS_REQUEST(request),
    INF_ADOPTED_CONCURRENCY_NONE
  );
  g_return_val_if_fail(
    INF_ADOPTED_IS_REQUEST(against),
    INF_ADOPTED_CONCURRENCY_NONE
  );
  
  request_priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  against_priv = INF_ADOPTED_REQUEST_PRIVATE(against);
  
  g_return_val_if_fail(
    request_priv->type == INF_ADOPTED_REQUEST_DO,
    INF_ADOPTED_CONCURRENCY_NONE
  );
  g_return_val_if_fail(
    against_priv->type == INF_ADOPTED_REQUEST_DO,
    INF_ADOPTED_CONCURRENCY_NONE
  );
  g_return_val_if_fail(
    request_priv->user_id != against_priv->user_id,
    INF_ADOPTED_CONCURRENCY_NONE
  );
  
  g_return_val_if_fail(
    inf_adopted_state_vector_compare(
      request_priv->vector,
      against_priv->vector
    ) == 0,
    INF_ADOPTED_CONCURRENCY_NONE
  );

  return inf_adopted_operation_get_concurrency_id(
    request_priv->operation,
    against_priv->operation
  );
}

/**
 * inf_adopted_request_transform:
 * @request: The request to transform.
 * @against: The request to transform against.
 * @concurrency_id: A concurrency ID for the transformation.
 *
 * Transforms the operation of @request against the operation of @against.
 * Both requests must be of type %INF_ADOPTED_REQUEST_DO, and their state
 * vectors must be the same.
 *
 * @concurrency_id can be %INF_ADOPTED_CONCURRENCY_NONE even if the
 * transformation requires a concurrency ID (see
 * inf_adopted_request_need_concurrency_id()). In that case, it is assumed
 * that it does not matter which operation to transform, and user IDs are
 * used to determine a concurrency ID for the transformation.
 *
 * Returns: A new #InfAdoptedRequest, the result of the transformation.
 **/
InfAdoptedRequest*
inf_adopted_request_transform(InfAdoptedRequest* request,
                              InfAdoptedRequest* against,
                              InfAdoptedConcurrencyId concurrency_id)
{
  InfAdoptedRequestPrivate* request_priv;
  InfAdoptedRequestPrivate* against_priv;
  InfAdoptedOperation* new_operation;
  InfAdoptedStateVector* new_vector;
  InfAdoptedRequest* new_request;
  
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(request), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_REQUEST(against), NULL);
  
  request_priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  against_priv = INF_ADOPTED_REQUEST_PRIVATE(against);
  
  g_return_val_if_fail(request_priv->type == INF_ADOPTED_REQUEST_DO, NULL);
  g_return_val_if_fail(against_priv->type == INF_ADOPTED_REQUEST_DO, NULL);
  g_return_val_if_fail(request_priv->user_id != against_priv->user_id, NULL);
  
  g_return_val_if_fail(
    inf_adopted_state_vector_compare(
      request_priv->vector,
      against_priv->vector
    ) == 0, NULL
  );

  if(concurrency_id != INF_ADOPTED_CONCURRENCY_NONE)
  {
    new_operation = inf_adopted_operation_transform(
      request_priv->operation,
      against_priv->operation,
      concurrency_id
    );
  }
  else if(request_priv->user_id > against_priv->user_id)
  {
    new_operation = inf_adopted_operation_transform(
      request_priv->operation,
      against_priv->operation,
      INF_ADOPTED_CONCURRENCY_OTHER
    );
  }
  else
  {
    new_operation = inf_adopted_operation_transform(
      request_priv->operation,
      against_priv->operation,
      INF_ADOPTED_CONCURRENCY_SELF
    );
  }

  new_vector = inf_adopted_state_vector_copy(request_priv->vector);
  inf_adopted_state_vector_add(new_vector, against_priv->user_id, 1);

  new_request = inf_adopted_request_new_do(
    new_vector,
    request_priv->user_id,
    new_operation
  );

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
    new_operation
  );

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
        "user-id", priv->user_id
      )
    );
  }

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
