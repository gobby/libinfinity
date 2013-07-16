/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFC_NODE_REQUEST_H__
#define __INFC_NODE_REQUEST_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_NODE_REQUEST                 (infc_node_request_get_type())
#define INFC_NODE_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_NODE_REQUEST, InfcNodeRequest))
#define INFC_NODE_REQUEST_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_NODE_REQUEST, InfcNodeRequestClass))
#define INFC_IS_NODE_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_NODE_REQUEST))
#define INFC_IS_NODE_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_NODE_REQUEST))
#define INFC_NODE_REQUEST_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_NODE_REQUEST, InfcNodeRequestClass))

/**
 * InfcNodeRequest:
 *
 * #InfcNodeRequest is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfcNodeRequest InfcNodeRequest;
typedef struct _InfcNodeRequestClass InfcNodeRequestClass;

/**
 * InfcNodeRequestClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfcNodeRequestClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
};

struct _InfcNodeRequest {
  GObject parent;
};

GType
infc_node_request_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __INFC_NODE_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
