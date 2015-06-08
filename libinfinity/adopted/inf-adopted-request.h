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

#ifndef __INF_ADOPTED_REQUEST_H__
#define __INF_ADOPTED_REQUEST_H__

#include <libinfinity/adopted/inf-adopted-operation.h>
#include <libinfinity/adopted/inf-adopted-state-vector.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_REQUEST                 (inf_adopted_request_get_type())
#define INF_ADOPTED_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_REQUEST, InfAdoptedRequest))
#define INF_ADOPTED_REQUEST_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_ADOPTED_TYPE_REQUEST, InfAdoptedRequestClass))
#define INF_ADOPTED_IS_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_REQUEST))
#define INF_ADOPTED_IS_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_ADOPTED_TYPE_REQUEST))
#define INF_ADOPTED_REQUEST_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_ADOPTED_TYPE_REQUEST, InfAdoptedRequestClass))

#define INF_ADOPTED_TYPE_REQUEST_TYPE            (inf_adopted_request_type_get_type())

typedef struct _InfAdoptedRequest InfAdoptedRequest;
typedef struct _InfAdoptedRequestClass InfAdoptedRequestClass;

/**
 * InfAdoptedRequestClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfAdoptedRequestClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfAdoptedRequest:
 *
 * #InfAdoptedRequest is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfAdoptedRequest {
  /*< private >*/
  GObject parent;
  gpointer priv;
};

/**
 * InfAdoptedRequestType:
 * @INF_ADOPTED_REQUEST_DO: A request that performs an operation.
 * @INF_ADOPTED_REQUEST_UNDO: A request that undoes a previously applied
 * request.
 * @INF_ADOPTED_REQUEST_REDO: A request that redoes a previously undone
 * request.
 *
 * Possible types for an #InfAdoptedRequest.
 */
typedef enum _InfAdoptedRequestType {
  INF_ADOPTED_REQUEST_DO,
  INF_ADOPTED_REQUEST_UNDO,
  INF_ADOPTED_REQUEST_REDO
} InfAdoptedRequestType;

GType
inf_adopted_request_type_get_type(void) G_GNUC_CONST;

GType
inf_adopted_request_get_type(void) G_GNUC_CONST;

InfAdoptedRequest*
inf_adopted_request_new_do(InfAdoptedStateVector* vector,
                           guint user_id,
                           InfAdoptedOperation* operation,
                           gint64 received);

InfAdoptedRequest*
inf_adopted_request_new_undo(InfAdoptedStateVector* vector,
                             guint user_id,
                             gint64 received);

InfAdoptedRequest*
inf_adopted_request_new_redo(InfAdoptedStateVector* vector,
                             guint user_id,
                             gint64 received);

InfAdoptedRequest*
inf_adopted_request_copy(InfAdoptedRequest* request);

InfAdoptedRequestType
inf_adopted_request_get_request_type(InfAdoptedRequest* request);

InfAdoptedStateVector*
inf_adopted_request_get_vector(InfAdoptedRequest* request);

guint
inf_adopted_request_get_user_id(InfAdoptedRequest* request);

InfAdoptedOperation*
inf_adopted_request_get_operation(InfAdoptedRequest* request);

guint
inf_adopted_request_get_index(InfAdoptedRequest* request);

gint64
inf_adopted_request_get_receive_time(InfAdoptedRequest* request);

gint64
inf_adopted_request_get_execute_time(InfAdoptedRequest* request);

void
inf_adopted_request_set_execute_time(InfAdoptedRequest* request,
                                     gint64 time);

gboolean
inf_adopted_request_need_concurrency_id(InfAdoptedRequest* request,
                                        InfAdoptedRequest* against);

InfAdoptedRequest*
inf_adopted_request_transform(InfAdoptedRequest* request,
                              InfAdoptedRequest* against,
                              InfAdoptedRequest* request_lcs,
                              InfAdoptedRequest* against_lcs);

InfAdoptedRequest*
inf_adopted_request_mirror(InfAdoptedRequest* request,
                           guint by);

InfAdoptedRequest*
inf_adopted_request_fold(InfAdoptedRequest* request,
                         guint into,
                         guint by);

gboolean
inf_adopted_request_affects_buffer(InfAdoptedRequest* request);

G_END_DECLS

#endif /* __INF_ADOPTED_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
