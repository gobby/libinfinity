/* infinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
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

#ifndef __INF_ADOPTED_REQUEST_LOG_H__
#define __INF_ADOPTED_REQUEST_LOG_H__

#include <libinfinity/adopted/inf-adopted-request.h>

#include <glib-object.h>

/* TODO: can-undo / can-redo tracking */

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_REQUEST_LOG                 (inf_adopted_request_log_get_type())
#define INF_ADOPTED_REQUEST_LOG(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_REQUEST_LOG, InfAdoptedRequestLog))
#define INF_ADOPTED_REQUEST_LOG_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_ADOPTED_TYPE_REQUEST_LOG, InfAdoptedRequestLogClass))
#define INF_ADOPTED_IS_REQUEST_LOG(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_REQUEST_LOG))
#define INF_ADOPTED_IS_REQUEST_LOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_ADOPTED_TYPE_REQUEST_LOG))
#define INF_ADOPTED_REQUEST_LOG_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_ADOPTED_TYPE_REQUEST_LOG, InfAdoptedRequestLogClass))

typedef struct _InfAdoptedRequestLog InfAdoptedRequestLog;
typedef struct _InfAdoptedRequestLogClass InfAdoptedRequestLogClass;

struct _InfAdoptedRequestLogClass {
  GObjectClass parent_class;
};

struct _InfAdoptedRequestLog {
  GObject parent;
};

GType
inf_adopted_request_log_get_type(void) G_GNUC_CONST;

InfAdoptedRequestLog*
inf_adopted_request_log_new(guint user_id);

guint
inf_adopted_request_log_get_user_id(InfAdoptedRequestLog* log);

guint
inf_adopted_request_log_get_begin(InfAdoptedRequestLog* log);

guint
inf_adopted_request_log_get_end(InfAdoptedRequestLog* log);

InfAdoptedRequest*
inf_adopted_request_log_get_request(InfAdoptedRequestLog* log,
                                    guint n);

void
inf_adopted_request_log_add_request(InfAdoptedRequestLog* log,
                                    InfAdoptedRequest* request);

void
inf_adopted_request_log_remove_requests(InfAdoptedRequestLog* log,
                                        guint up_to);

InfAdoptedRequest*
inf_adopted_request_log_next_associated(InfAdoptedRequestLog* log,
                                        InfAdoptedRequest* request);

InfAdoptedRequest*
inf_adopted_request_log_prev_associated(InfAdoptedRequestLog* log,
                                        InfAdoptedRequest* request);

InfAdoptedRequest*
inf_adopted_request_log_original_request(InfAdoptedRequestLog* log,
                                         InfAdoptedRequest* request);

InfAdoptedRequest*
inf_adopted_request_log_next_undo(InfAdoptedRequestLog* log);

InfAdoptedRequest*
inf_adopted_request_log_next_redo(InfAdoptedRequestLog* log);

InfAdoptedRequest*
inf_adopted_request_log_upper_related(InfAdoptedRequestLog* log,
                                      InfAdoptedRequest* request);

G_END_DECLS

#endif /* __INF_ADOPTED_REQUEST_LOG_H__ */

/* vim:set et sw=2 ts=2: */
