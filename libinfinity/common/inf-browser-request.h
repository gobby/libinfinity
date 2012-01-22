/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_BROWSER_REQUEST_H__
#define __INF_BROWSER_REQUEST_H__

#include <glib-object.h>
#include <libinfinity/common/inf-browser-iter.h>

G_BEGIN_DECLS

#define INF_TYPE_BROWSER_REQUEST                 (inf_browser_request_get_type())
#define INF_BROWSER_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_BROWSER_REQUEST, InfBrowserRequest))
#define INF_IS_BROWSER_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_BROWSER_REQUEST))
#define INF_BROWSER_REQUEST_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_BROWSER_REQUEST, InfBrowserRequestIface))

/**
 * InfBrowserRequest:
 *
 * #InfBrowserRequest is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfBrowserRequest InfBrowserRequest;
typedef struct _InfBrowserRequestIface InfBrowserRequestIface;

/**
 * InfBrowserRequestIface:
 * @finished: Default signal handler for the
 * #InfBrowserRequest::finished signal.
 *
 * Default signal handlers for the #InfBrowserRequest interface.
 */
struct _InfBrowserRequestIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/

  /* Signals */
  void (*finished)(InfBrowserRequest* request,
                   InfBrowserIter* iter,
                   const GError* error);
};

GType
inf_browser_request_get_type(void) G_GNUC_CONST;

void
inf_browser_request_finished(InfBrowserRequest* request,
                             InfBrowserIter* iter,
                             const GError* error);

G_END_DECLS

#endif /* __INF_BROWSER_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
