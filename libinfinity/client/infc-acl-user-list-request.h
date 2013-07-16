/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFC_ACL_USER_LIST_REQUEST_H__
#define __INFC_ACL_USER_LIST_REQUEST_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_ACL_USER_LIST_REQUEST                 (infc_acl_user_list_request_get_type())
#define INFC_ACL_USER_LIST_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_ACL_USER_LIST_REQUEST, InfcAclUserListRequest))
#define INFC_ACL_USER_LIST_REQUEST_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_ACL_USER_LIST_REQUEST, InfcAclUserListRequestClass))
#define INFC_IS_ACL_USER_LIST_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_ACL_USER_LIST_REQUEST))
#define INFC_IS_ACL_USER_LIST_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_ACL_USER_LIST_REQUEST))
#define INFC_ACL_USER_LIST_REQUEST_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_ACL_USER_LIST_REQUEST, InfcAclUserListRequestClass))

/**
 * InfcAclUserListRequest:
 *
 * #InfcAclUserListRequest is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfcAclUserListRequest InfcAclUserListRequest;
typedef struct _InfcAclUserListRequestClass InfcAclUserListRequestClass;

/**
 * InfcAclUserListRequestClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfcAclUserListRequestClass {
  /*< private >*/
  GObjectClass parent_class;
};

struct _InfcAclUserListRequest {
  GObject parent;
};

GType
infc_acl_user_list_request_get_type(void) G_GNUC_CONST;

void
infc_acl_user_list_request_initiated(InfcAclUserListRequest* request,
                                     guint total);

gboolean
infc_acl_user_list_request_get_initiated(InfcAclUserListRequest* request);

void
infc_acl_user_list_request_progress(InfcAclUserListRequest* request);

G_END_DECLS

#endif /* __INFC_ACL_USER_LIST_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
