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

#ifndef __INF_ADOPTED_SESSION_H__
#define __INF_ADOPTED_SESSION_H__

#include <libinfinity/adopted/inf-adopted-algorithm.h>
#include <libinfinity/adopted/inf-adopted-operation.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-io.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_SESSION                 (inf_adopted_session_get_type())
#define INF_ADOPTED_SESSION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_SESSION, InfAdoptedSession))
#define INF_ADOPTED_SESSION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_ADOPTED_TYPE_SESSION, InfAdoptedSessionClass))
#define INF_ADOPTED_IS_SESSION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_SESSION))
#define INF_ADOPTED_IS_SESSION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_ADOPTED_TYPE_SESSION))
#define INF_ADOPTED_SESSION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_ADOPTED_TYPE_SESSION, InfAdoptedSessionClass))

#define INF_ADOPTED_TYPE_SESSION_STATUS          (inf_adopted_session_status_get_type())

typedef struct _InfAdoptedSession InfAdoptedSession;
typedef struct _InfAdoptedSessionClass InfAdoptedSessionClass;

typedef enum _InfAdoptedSessionError {
  INF_ADOPTED_SESSION_ERROR_INVALID_TYPE,
  INF_ADOPTED_SESSION_ERROR_NO_SUCH_USER,
  INF_ADOPTED_SESSION_ERROR_MISSING_OPERATION,
  INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST,

  INF_ADOPTED_SESSION_ERROR_MISSING_STATE_VECTOR,
  
  INF_ADOPTED_SESSION_ERROR_FAILED
} InfAdoptedSessionError;

struct _InfAdoptedSessionClass {
  InfSessionClass parent_class;

  /* Virtual table */

  xmlNodePtr(*operation_to_xml)(InfAdoptedSession* session,
                                InfAdoptedOperation* operation,
                                gboolean for_sync);

  InfAdoptedOperation*(*xml_to_operation)(InfAdoptedSession* session,
                                          InfAdoptedUser* user,
                                          xmlNodePtr xml,
                                          gboolean for_sync,
                                          GError** error);
};

struct _InfAdoptedSession {
  InfSession parent;
};

GType
inf_adopted_session_get_type(void);

InfIo*
inf_adopted_session_get_io(InfAdoptedSession* session);

InfAdoptedAlgorithm*
inf_adopted_session_get_algorithm(InfAdoptedSession* session);

void
inf_adopted_session_broadcast_request(InfAdoptedSession* session,
                                      InfAdoptedRequest* request);

G_END_DECLS

#endif /* __INF_ADOPTED_SESSION_H__ */

/* vim:set et sw=2 ts=2: */
