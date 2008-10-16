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

#ifndef __INFC_USER_REQUEST_H__
#define __INFC_USER_REQUEST_H__

#include <libinfinity/client/infc-request.h>
#include <libinfinity/common/inf-user.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_USER_REQUEST                 (infc_user_request_get_type())
#define INFC_USER_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_USER_REQUEST, InfcUserRequest))
#define INFC_USER_REQUEST_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_USER_REQUEST, InfcUserRequestClass))
#define INFC_IS_USER_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_USER_REQUEST))
#define INFC_IS_USER_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_USER_REQUEST))
#define INFC_USER_REQUEST_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_USER_REQUEST, InfcUserRequestClass))

typedef struct _InfcUserRequest InfcUserRequest;
typedef struct _InfcUserRequestClass InfcUserRequestClass;

struct _InfcUserRequestClass {
  InfcRequestClass parent_class;

  /* Signals */
  void (*finished)(InfcUserRequest* request,
                   InfUser* user);
};

struct _InfcUserRequest {
  InfcRequest parent;
};

GType
infc_user_request_get_type(void) G_GNUC_CONST;

void
infc_user_request_finished(InfcUserRequest* request,
                           InfUser* user);

G_END_DECLS

#endif /* __INFC_USER_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
