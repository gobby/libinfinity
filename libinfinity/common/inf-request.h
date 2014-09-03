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

#ifndef __INF_REQUEST_H__
#define __INF_REQUEST_H__

#include <libinfinity/common/inf-certificate-chain.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_REQUEST                 (inf_request_get_type())
#define INF_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_REQUEST, InfRequest))
#define INF_IS_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_REQUEST))
#define INF_REQUEST_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_REQUEST, InfRequestInterface))

/**
 * InfRequestResult:
 *
 * #InfRequestResult is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfRequestResult InfRequestResult;

/**
 * InfRequest:
 *
 * #InfRequest is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfRequest InfRequest;
typedef struct _InfRequestInterface InfRequestInterface;

/**
 * InfRequestInterface:
 * @finished: Default signal handler of the #InfRequest::finished signal.
 * @is_local: Virtual function to check whether the request is local or
 * remote.
 *
 * Virtual functions of the #InfRequest interface.
 */
struct _InfRequestInterface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/
  void (*finished)(InfRequest* request,
                   const InfRequestResult* result,
                   const GError* error);

  gboolean (*is_local)(InfRequest* request);
};

/**
 * InfRequestFunc:
 * @request: The #InfRequest that emits the signal.
 * @result: A #InfRequestResult which contains the result of the request.
 * @error: Error information in case the request failed, or %NULL
 * otherwise.
 * @user_data: Additional data set when the signal handler was connected.
 *
 * Signature of a signal handler for the #InfRequest::finished signal.
 */
typedef void(*InfRequestFunc)(InfRequest* request,
                              const InfRequestResult* result,
                              const GError* error,
                              gpointer user_data);

GType
inf_request_get_type(void) G_GNUC_CONST;

void
inf_request_fail(InfRequest* request,
                 const GError* error);

void
inf_request_finish(InfRequest* request,
                   InfRequestResult* result);

gboolean
inf_request_is_local(InfRequest* request);

G_END_DECLS

#endif /* __INF_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
