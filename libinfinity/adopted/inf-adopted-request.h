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

#ifndef __INF_ADOPTED_REQUEST_H__
#define __INF_ADOPTED_REQUEST_H__

#include <libinfinity/adopted/inf-adopted-operation.h>
#include <libinfinity/adopted/inf-adopted-user.h>
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

struct _InfAdoptedRequestClass {
  GObjectClass parent_class;
};

struct _InfAdoptedRequest {
  GObject parent;
};

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
                           InfAdoptedUser* user,
                           InfAdoptedOperation* operation);

InfAdoptedRequest*
inf_adopted_request_new_undo(InfAdoptedStateVector* vector,
                             InfAdoptedUser* user);

InfAdoptedRequest*
inf_adopted_request_new_redo(InfAdoptedStateVector* vector,
                             InfAdoptedUser* user);

InfAdoptedRequest*
inf_adopted_request_copy(InfAdoptedRequest* request);

InfAdoptedRequestType
inf_adopted_request_get_request_type(InfAdoptedRequest* request);

InfAdoptedStateVector*
inf_adopted_request_get_vector(InfAdoptedRequest* request);

InfAdoptedUser*
inf_adopted_request_get_user(InfAdoptedRequest* request);

InfAdoptedOperation*
inf_adopted_request_get_operation(InfAdoptedRequest* request);

void
inf_adopted_request_transform(InfAdoptedRequest* request,
                              InfAdoptedRequest* against);

void
inf_adopted_request_mirror(InfAdoptedRequest* request,
                           guint by);

void
inf_adopted_request_fold(InfAdoptedRequest* request,
                         InfAdoptedUser* into,
                         guint by);

G_END_DECLS

#endif /* __INF_ADOPTED_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
