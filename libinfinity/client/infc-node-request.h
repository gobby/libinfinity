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

#ifndef __INFC_NODE_REQUEST_H__
#define __INFC_NODE_REQUEST_H__

#include <libinfinity/client/infc-browser-iter.h>
#include <libinfinity/client/infc-request.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_NODE_REQUEST                 (infc_node_request_get_type())
#define INFC_NODE_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_NODE_REQUEST, InfcNodeRequest))
#define INFC_NODE_REQUEST_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_NODE_REQUEST, InfcNodeRequestClass))
#define INFC_IS_NODE_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_NODE_REQUEST))
#define INFC_IS_NODE_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_NODE_REQUEST))
#define INFC_NODE_REQUEST_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_NODE_REQUEST, InfcNodeRequestClass))

typedef struct _InfcNodeRequest InfcNodeRequest;
typedef struct _InfcNodeRequestClass InfcNodeRequestClass;

struct _InfcNodeRequestClass {
  InfcRequestClass parent_class;

  /* Signals */
  void (*finished)(InfcNodeRequest* node_request,
                   const InfcBrowserIter* iter);
};

struct _InfcNodeRequest {
  InfcRequest parent;
};

GType
infc_node_request_get_type(void) G_GNUC_CONST;

void
infc_node_request_finished(InfcNodeRequest* request,
                           const InfcBrowserIter* iter);

G_END_DECLS

#endif /* __INFC_NODE_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
