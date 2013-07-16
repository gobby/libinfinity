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

#ifndef __INFD_NODE_REQUEST_H__
#define __INFD_NODE_REQUEST_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_NODE_REQUEST                 (infd_node_request_get_type())
#define INFD_NODE_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_NODE_REQUEST, InfdNodeRequest))
#define INFD_NODE_REQUEST_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_NODE_REQUEST, InfdNodeRequestClass))
#define INFD_IS_NODE_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_NODE_REQUEST))
#define INFD_IS_NODE_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_NODE_REQUEST))
#define INFD_NODE_REQUEST_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_NODE_REQUEST, InfdNodeRequestClass))

/**
 * InfdNodeRequest:
 *
 * #InfdNodeRequest is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfdNodeRequest InfdNodeRequest;
typedef struct _InfdNodeRequestClass InfdNodeRequestClass;

/**
 * InfdNodeRequestClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfdNodeRequestClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
};

struct _InfdNodeRequest {
  GObject parent;
};

GType
infd_node_request_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __INFD_NODE_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
