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

typedef struct _InfAdoptedSession InfAdoptedSession;
typedef struct _InfAdoptedSessionClass InfAdoptedSessionClass;

/**
 * InfAdoptedSessionError:
 * @INF_ADOPTED_SESSION_ERROR_NO_SUCH_USER: The "user" field in a request
 * message does not contain a valid user ID.
 * @INF_ADOPTED_SESSION_ERROR_MISSING_OPERATION: A request message does not
 * contain an operation.
 * @INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST: A request in a synchronized
 * request log is invalid. Invalid means that it is not the request that
 * was issued after the previous request in the log, or that it is an Undo
 * or Redo request without a request to Undo or Redo, respectively.
 * @INF_ADOPTED_SESSION_ERROR_MISSING_STATE_VECTOR: A synchronized user does
 * not contain that the state that user currently is in.
 * @INF_ADOPTED_SESSION_ERROR_FAILED: No further specified error code.
 *
 * Error codes for #InfAdoptedSession. These only occur when invalid requests
 * are received from the network.
 */
typedef enum _InfAdoptedSessionError {
  INF_ADOPTED_SESSION_ERROR_NO_SUCH_USER,
  INF_ADOPTED_SESSION_ERROR_MISSING_OPERATION,
  INF_ADOPTED_SESSION_ERROR_INVALID_REQUEST,

  INF_ADOPTED_SESSION_ERROR_MISSING_STATE_VECTOR,
  
  INF_ADOPTED_SESSION_ERROR_FAILED
} InfAdoptedSessionError;

/**
 * InfAdoptedSessionClass:
 * @xml_to_request: Virtual function to deserialize an #InfAdoptedRequest
 * from XML. The implementation of this function can use
 * inf_adopted_session_read_request_info() to read the common info.
 * @request_to_xml: Virtual function to serialize an #InfAdoptedRequest
 * to XML. This function should add properties and children to the given XML
 * node. At might use inf_adopted_session_write_request_info() to write the
 * common info.
 *
 * Virtual functions for #InfAdoptedSession.
 */
struct _InfAdoptedSessionClass {
  /*< private >*/
  InfSessionClass parent_class;

  /* Virtual table */

  /*< public >*/
  InfAdoptedRequest*(*xml_to_request)(InfAdoptedSession* session,
                                      xmlNodePtr xml,
                                      InfAdoptedStateVector* diff_vec,
                                      gboolean for_sync,
                                      GError** error);

  void(*request_to_xml)(InfAdoptedSession* session,
                        xmlNodePtr xml,
                        InfAdoptedRequest* request,
                        InfAdoptedStateVector* diff_vec,
                        gboolean for_sync);
};

/**
 * InfAdoptedSession:
 *
 * #InfAdoptedSession is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfAdoptedSession {
  /*< private >*/
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

void
inf_adopted_session_undo(InfAdoptedSession* session,
                         InfAdoptedUser* user,
                         guint n);

void
inf_adopted_session_redo(InfAdoptedSession* session,
                         InfAdoptedUser* user,
                         guint n);

gboolean
inf_adopted_session_read_request_info(InfAdoptedSession* session,
                                      xmlNodePtr xml,
                                      InfAdoptedStateVector* diff_vec,
                                      InfAdoptedUser** user,
                                      InfAdoptedStateVector** time,
                                      xmlNodePtr* operation,
                                      GError** error);

void
inf_adopted_session_write_request_info(InfAdoptedSession* session,
                                       InfAdoptedRequest* request,
                                       InfAdoptedStateVector* diff_vec,
                                       xmlNodePtr xml,
                                       xmlNodePtr operation);

G_END_DECLS

#endif /* __INF_ADOPTED_SESSION_H__ */

/* vim:set et sw=2 ts=2: */
