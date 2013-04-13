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

#ifndef __INFC_CHAT_REQUEST_H__
#define __INFC_CHAT_REQUEST_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INFC_TYPE_CHAT_REQUEST                 (infc_chat_request_get_type())
#define INFC_CHAT_REQUEST(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFC_TYPE_CHAT_REQUEST, InfcChatRequest))
#define INFC_CHAT_REQUEST_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFC_TYPE_CHAT_REQUEST, InfcChatRequestClass))
#define INFC_IS_CHAT_REQUEST(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFC_TYPE_CHAT_REQUEST))
#define INFC_IS_CHAT_REQUEST_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFC_TYPE_CHAT_REQUEST))
#define INFC_CHAT_REQUEST_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFC_TYPE_CHAT_REQUEST, InfcChatRequestClass))

/**
 * InfcChatRequest:
 *
 * #InfcChatRequest is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfcChatRequest InfcChatRequest;
typedef struct _InfcChatRequestClass InfcChatRequestClass;

/**
 * InfcChatRequestClass:
 *
 * @finished: Default signal handler for the #InfcChatRequest::finished
 * signal.
 *
 * This structure contains default signal handlers for the
 * #InfcChatRequest class.
 */
struct _InfcChatRequestClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/

  /* Signals */
  void (*finished)(InfcChatRequest* request,
                   const GError* error);
};

struct _InfcChatRequest {
  GObject parent;
};

GType
infc_chat_request_get_type(void) G_GNUC_CONST;

void
infc_chat_request_finished(InfcChatRequest* request,
                           const GError* error);

G_END_DECLS

#endif /* __INFC_CHAT_REQUEST_H__ */

/* vim:set et sw=2 ts=2: */
