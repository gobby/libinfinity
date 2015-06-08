/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFC_PROGRESS_REQUEST_H__
#define __INFC_PROGRESS_REQUEST_H__

#include <libinfinity/client/infc-request.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_PROGRESS_REQUEST                 (infc_progress_request_get_type())
#define INFC_PROGRESS_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_PROGRESS_REQUEST, InfcProgressRequest))
#define INFC_PROGRESS_REQUEST_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_PROGRESS_REQUEST, InfcProgressRequestClass))
#define INFC_IS_PROGRESS_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_PROGRESS_REQUEST))
#define INFC_IS_PROGRESS_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_PROGRESS_REQUEST))
#define INFC_PROGRESS_REQUEST_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_PROGRESS_REQUEST, InfcProgressRequestClass))

typedef struct _InfcProgressRequest InfcProgressRequest;
typedef struct _InfcProgressRequestClass InfcProgressRequestClass;

/**
 * InfcProgressRequestClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfcProgressRequestClass {
  /*< private >*/
  InfcRequestClass parent_class;
};

/**
 * InfcProgressRequest:
 *
 * #InfcProgressRequest is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfcProgressRequest {
  /*< private >*/
  InfcRequest parent;
};

GType
infc_progress_request_get_type(void) G_GNUC_CONST;

void
infc_progress_request_initiated(InfcProgressRequest* request,
                                guint total);

gboolean
infc_progress_request_get_initiated(InfcProgressRequest* request);

void
infc_progress_request_progress(InfcProgressRequest* request);

G_END_DECLS

#endif /* __INFC_PROGRESS_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
