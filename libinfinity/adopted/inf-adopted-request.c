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
    request_type = g_boxed_type_register_static(
      "InfAdoptedRequest",
      (GBoxedCopyFunc)inf_adopted_request_copy,
      (GBoxedFreeFunc)inf_adopted_request_free
    );
  }

  return request_type;
}

/** inf_adopted_request_new:
 *
 * @type: The type of the request to be created.
 * @vector: The vector time at which the request's operation can be applied.
 * @user: The user that issued the request.
 * @operation: The operation performed by the request.
 *
 * Creates a new #InfAdoptedRequest. If @type is not %INF_ADOPTED_REQUEST_DO,
 * then operation must be %NULL because the operation is already defined
 * implicitely by the reverse operation of the corresponding request's
 * operation.
 *
 * This function takes ownership of @vector. It does not reference @user, so
 * be sure to keep a reference to @user around (note that this is guaranteed
 * as long as you are using #InfAdoptedAlgorithm).
 *
 * Return Value: A new #InfAdoptedRequest.
 **/
InfAdoptedRequest*
inf_adopted_request_new(InfAdoptedRequestType type,
                        InfAdoptedStateVector* vector,
                        InfAdoptedUser* user,
                        InfAdoptedOperation* operation)
{
  InfAdoptedRequest* request;

  g_return_val_if_fail(vector != NULL, NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_USER(user), NULL);

  g_return_val_if_fail(
    (type == INF_ADOPTED_REQUEST_DO && INF_ADOPTED_IS_OPERATION(operation)) ||
      (type != INF_ADOPTED_REQUEST_DO && operation == NULL),
    NULL
  );

  request = g_slice_new(InfAdoptedRequest);
  request->type = type;
  request->vector = vector;
  request->user = user;
  request->operation = operation;

  if(operation != NULL)
    g_object_ref(G_OBJECT(operation));

  return request;
}

/** inf_adopted_request_copy:
 *
 * @request: The #InfAdoptedRequest to copy.
 *
 * Returns a copy of @request. Note that the operation is not actually copied,
 * but operations are supposed to be immutable (if not, it is their fault), so
 * they cannot be changed anyway.
 *
 * Return Value: A copy of @request.
 **/
InfAdoptedRequest*
inf_adopted_request_copy(InfAdoptedRequest* request)
{
  InfAdoptedRequest* new_request;

  g_return_val_if_fail(request != NULL, NULL);

  new_request = g_slice_new(InfAdoptedRequest);
  new_request->type = request->type;
  new_request->vector = inf_adopted_state_vector_copy(request->vector);
  new_request->user = request->user;
  new_request->operation = request->operation;

  if(new_request->operation != NULL)
    g_object_ref(G_OBJECT(new_request->operation));

  return new_request;
}

/** inf_adopted_request_free:
 *
 * @request: A #InfAdoptedRequest.
 *
 * Frees a request  allocated by inf_adopted_request_new() or
 * inf_adopted_request_copy().
 **/
void
inf_adopted_request_free(InfAdoptedRequest* request)
{
  g_return_if_fail(request != NULL);

  if(request->operation != NULL)
    g_object_unref(G_OBJECT(request->operation));

  inf_adopted_state_vector_free(request->vector);
  g_slice_free(InfAdoptedRequest, request);
}
