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

#define INF_ADOPTED_TYPE_REQUEST            (inf_adopted_request_get_type())
#define INF_ADOPTED_TYPE_REQUEST_TYPE       (inf_adopted_request_type_get_type())

typedef enum _InfAdoptedRequestType {
  INF_ADOPTED_REQUEST_DO,
  INF_ADOPTED_REQUEST_UNDO,
  INF_ADOPTED_REQUEST_REDO
} InfAdoptedRequestType;

typedef struct _InfAdoptedRequest InfAdoptedRequest;
struct _InfAdoptedRequest {
  /* readonly */
  InfAdoptedRequestType type;
  /* readonly, not refed */
  InfAdoptedUser* user;
  /* read/write */
  InfAdoptedStateVector* vector;
  /* read/write */
  InfAdoptedOperation* operation;
};

GType
inf_adopted_request_type_get_type(void) G_GNUC_CONST;

GType
inf_adopted_request_get_type(void) G_GNUC_CONST;

InfAdoptedRequest*
inf_adopted_request_new(InfAdoptedRequestType type,
                        InfAdoptedStateVector* vector,
                        InfAdoptedUser* user,
                        InfAdoptedOperation* operation);

InfAdoptedRequest*
inf_adopted_request_copy(InfAdoptedRequest* request);

void
inf_adopted_request_free(InfAdoptedRequest* request);

G_END_DECLS

#endif /* __INF_ADOPTED_REQUEST_H__ */
