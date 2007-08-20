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

#include <libinfinity/adopted/inf-adopted-request.h>

typedef struct _InfAdoptedRequestPrivate InfAdoptedRequestPrivate;
struct _InfAdoptedRequestPrivate {
  InfAdoptedRequestType type;
  InfAdoptedStateVector* vector;
  InfAdoptedUser* user;
  InfAdoptedOperation* operation;
};

enum {
  PROP_0,

  /* construct only */
  PROP_TYPE,
  PROP_VECTOR,
  PROP_USER,
  PROP_OPERATION
};

#define INF_ADOPTED_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_REQUEST, InfAdoptedRequestPrivate))

static GObjectClass* parent_class;

static void
inf_adopted_request_init(GTypeInstance* instance,
                         gpointer g_class)
{
  InfAdoptedRequest* request;
  InfAdoptedRequestPrivate* priv;

  request = INF_ADOPTED_REQUEST(instance);
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);

  priv->type = INF_ADOPTED_REQUEST_DO;
  priv->vector = NULL;
  priv->user = NULL;
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

  if(priv->user != NULL)
  {
    g_object_unref(G_OBJECT(priv->user));
    priv->user = NULL;
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
  case PROP_USER:
    g_assert(priv->user == NULL); /* construct only */

    /* don't ref, we expect the user to exist as long the request exists
     * (which it does if you use InfAdoptedAlgorithm for all adopted
     * operations). */
    priv->user = INF_ADOPTED_USER(g_value_get_object(value));
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
  case PROP_USER:
    g_value_set_object(value, G_OBJECT(priv->user));
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
    PROP_USER,
    g_param_spec_object(
      "user",
      "User",
      "The user that made the request",
      INF_ADOPTED_TYPE_USER,
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

/** inf_adopted_request_new_do:
 *
 * @vector: The vector time at which the request was made.
 * @user: The user that made the request.
 * @operation: The operation the user performed.
 *
 * Creates a new #InfAdoptedRequest with type %INF_ADOPTED_REQUEST_DO.
 *
 * Return Value: A new DO request.
 **/
InfAdoptedRequest*
inf_adopted_request_new_do(InfAdoptedStateVector* vector,
                           InfAdoptedUser* user,
                           InfAdoptedOperation* operation)
{
  GObject* object;

  g_return_val_if_fail(vector != NULL, NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_REQUEST,
    "type", INF_ADOPTED_REQUEST_DO,
    "vector", vector,
    "user", user,
    "operation", operation,
    NULL
  );

  return INF_ADOPTED_REQUEST(object);
}

/** inf_adopted_request_new_undo:
 *
 * @vector: The vector time at which the request was made.
 * @user: The user that made the request.
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
                             InfAdoptedUser* user)
{
  GObject* object;

  g_return_val_if_fail(vector != NULL, NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_REQUEST,
    "type", INF_ADOPTED_REQUEST_UNDO,
    "vector", vector,
    "user", user,
    NULL
  );
  
  return INF_ADOPTED_REQUEST(object);
}

/** inf_adopted_request_new_redo:
 *
 * @vector: The vector time at which the request was made.
 * @user: The user that made the request.
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
                             InfAdoptedUser* user)
{
  GObject* object;
  
  g_return_val_if_fail(vector != NULL, NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);
  
  object = g_object_new(
    INF_ADOPTED_TYPE_REQUEST,
    "type", INF_ADOPTED_REQUEST_REDO,
    "vector", vector,
    "user", user,
    NULL
  );
  
  return INF_ADOPTED_REQUEST(object);
}

/** inf_adopted_request_copy:
 *
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
      "user", priv->user,
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
      "user", priv->user,
      NULL
    );
  }

  return INF_ADOPTED_REQUEST(object);
}

/** inf_adopted_request_get_request_type:
 *
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

/** inf_adopted_request_get_vector:
 *
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

/** inf_adopted_request_get_user:
 *
 * @request: A #InfAdoptedRequest.
 *
 * Returns the user that issued @request.
 *
 * Return Value: The request's user.
 **/
InfAdoptedUser*
inf_adopted_request_get_user(InfAdoptedRequest* request)
{
  g_return_val_if_fail(INF_ADOPTED_IS_USER(request), NULL);
  return INF_ADOPTED_REQUEST_PRIVATE(request)->user;
}

/** inf_adopted_request_get_operation:
 *
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

/** inf_adopted_request_transform:
 *
 * @request: The request to transform.
 * @against: The request to transform against.
 *
 * Transforms the operation of @request against the operation of @against.
 * Both requests must be of type %INF_ADOPTED_REQUEST_DO, and their state
 * vectors must be the same.
 **/
void
inf_adopted_request_transform(InfAdoptedRequest* request,
                              InfAdoptedRequest* against)
{
  InfAdoptedRequestPrivate* request_priv;
  InfAdoptedRequestPrivate* against_priv;
  InfAdoptedOperation* new_operation;
  
  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));
  g_return_if_fail(INF_ADOPTED_IS_REQUEST(against));
  
  request_priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  against_priv = INF_ADOPTED_REQUEST_PRIVATE(against);
  
  g_return_if_fail(request_priv->type == INF_ADOPTED_REQUEST_DO);
  g_return_if_fail(against_priv->type == INF_ADOPTED_REQUEST_DO);
  
  g_return_if_fail(
    inf_adopted_state_vector_compare(
      request_priv->vector,
      against_priv->vector
    ) == 0
  );

  new_operation = inf_adopted_operation_transform(
    request_priv->operation,
    against_priv->operation
  );

  g_object_unref(G_OBJECT(request_priv->operation));
  request_priv->operation = new_operation;

  inf_adopted_state_vector_add(
    request_priv->vector,
    INF_USER(against_priv->user),
    1
  );

  g_object_notify(G_OBJECT(request), "operation");
  g_object_notify(G_OBJECT(request), "vector");
}

/** inf_adopted_request_mirror:
 *
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
 **/
void
inf_adopted_request_mirror(InfAdoptedRequest* request,
                           guint by)
{
  InfAdoptedRequestPrivate* priv;
  InfAdoptedOperation* new_operation;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));
  g_return_if_fail(by % 2 == 1);
  
  priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->type == INF_ADOPTED_REQUEST_DO);
  g_return_if_fail(inf_adopted_operation_is_reversible(priv->operation));

  new_operation = inf_adopted_operation_revert(priv->operation);
  g_object_unref(G_OBJECT(priv->operation));
  priv->operation = new_operation;

  inf_adopted_state_vector_add(priv->vector, INF_USER(priv->user), by);

  g_object_notify(G_OBJECT(request), "operation");
  g_object_notify(G_OBJECT(request), "vector");

}

/** inf_adopted_request_fold:
 *
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
 **/
void
inf_adopted_request_fold(InfAdoptedRequest* request,
                         InfAdoptedUser* into,
                         guint by)
{
  InfAdoptedRequestPrivate* priv;

  g_return_if_fail(INF_ADOPTED_IS_REQUEST(request));
  g_return_if_fail(INF_ADOPTED_IS_USER(into));
  g_return_if_fail(by % 2 == 0);

  priv = INF_ADOPTED_REQUEST_PRIVATE(request);
  g_return_if_fail(priv->user != into);

  inf_adopted_state_vector_add(priv->vector, INF_USER(into), by);
  g_object_notify(G_OBJECT(request), "vector");
}
