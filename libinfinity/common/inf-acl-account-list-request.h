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

#ifndef __INF_ACL_ACCOUNT_LIST_REQUEST_H__
#define __INF_ACL_ACCOUNT_LIST_REQUEST_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_ACL_ACCOUNT_LIST_REQUEST                 (inf_acl_account_list_request_get_type())
#define INF_ACL_ACCOUNT_LIST_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_ACL_ACCOUNT_LIST_REQUEST, InfAclAccountListRequest))
#define INF_IS_ACL_ACCOUNT_LIST_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_ACL_ACCOUNT_LIST_REQUEST))
#define INF_ACL_ACCOUNT_LIST_REQUEST_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_ACL_ACCOUNT_LIST_REQUEST, InfAclAccountListRequestIface))

/**
 * InfAclAccountListRequest:
 *
 * #InfAclAccountListRequest is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfAclAccountListRequest InfAclAccountListRequest;
typedef struct _InfAclAccountListRequestIface InfAclAccountListRequestIface;

/**
 * InfAclAccountListRequestIface:
 * @finished: Default signal handler for the
 * #InfAclAccountListRequest::finished signal.
 *
 * Default signal handlers for the #InfAclAccountListRequest interface.
 */
struct _InfAclAccountListRequestIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/

  /* Signals */
  void (*finished)(InfAclAccountListRequest* request,
                   const GError* error);
};

/**
 * InfAclAccountListRequestFunc:
 * @request: The #InfAclAccountListRequest which has finished.
 * @error: The error which occurred, or %NULL.
 * @user_data: User data passed when the signal handler was connected.
 *
 * The signature of #InfAclAccountListRequest::finished signal handlers.
 */
typedef void(*InfAclAccountListRequestFunc)(InfAclAccountListRequest* request,
                                            const GError* error,
                                            gpointer user_data);

GType
inf_acl_account_list_request_get_type(void) G_GNUC_CONST;

void
inf_acl_account_list_request_finished(InfAclAccountListRequest* request,
                                      const GError* error);

G_END_DECLS

#endif /* __INF_ACL_ACCOUNT_LIST_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
