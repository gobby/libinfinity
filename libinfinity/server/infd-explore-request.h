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

#ifndef __INFD_EXPLORE_REQUEST_H__
#define __INFD_EXPLORE_REQUEST_H__

#include <libinfinity/server/infd-node-request.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_EXPLORE_REQUEST                 (infd_explore_request_get_type())
#define INFD_EXPLORE_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_EXPLORE_REQUEST, InfdExploreRequest))
#define INFD_EXPLORE_REQUEST_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_EXPLORE_REQUEST, InfdExploreRequestClass))
#define INFD_IS_EXPLORE_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_EXPLORE_REQUEST))
#define INFD_IS_EXPLORE_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_EXPLORE_REQUEST))
#define INFD_EXPLORE_REQUEST_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_EXPLORE_REQUEST, InfdExploreRequestClass))

typedef struct _InfdExploreRequest InfdExploreRequest;
typedef struct _InfdExploreRequestClass InfdExploreRequestClass;

/**
 * InfdExploreRequestClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfdExploreRequestClass {
  /*< private >*/
  InfdNodeRequestClass parent_class;
};

/**
 * InfdExploreRequest:
 *
 * #InfdExploreRequest is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfdExploreRequest {
  /*< private >*/
  InfdNodeRequest parent;
};

GType
infd_explore_request_get_type(void) G_GNUC_CONST;

void
infd_explore_request_initiated(InfdExploreRequest* request,
                               guint total);

void
infd_explore_request_progress(InfdExploreRequest* request);

G_END_DECLS

#endif /* __INFD_EXPLORE_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
