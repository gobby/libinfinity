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

#ifndef __INF_ERROR_H__
#define __INF_ERROR_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* These are error codes do not occur directly in libinfinity, but that
 * may be reported and/or need to be understood by both client and server. */

typedef enum _InfRequestError {
  /* Synchronization is still in progress. */
  INF_REQUEST_ERROR_SYNCHRONIZING,
  /* Received an unexpected message */
  INF_REQUEST_ERROR_UNEXPECTED_MESSAGE,

  INF_REQUEST_ERROR_FAILED
} InfRequestError;

typedef enum _InfUserJoinError {
  /* The requested name is already in use by another user */
  INF_USER_JOIN_ERROR_NAME_IN_USE,
  /* The request does not include a name attribute */
  INF_USER_JOIN_ERROR_NAME_MISSING,
  /* An id attribute was provided, but the server assigns it */
  INF_USER_JOIN_ERROR_ID_PROVIDED,
  /* A status attribute was provided, but the status is always
   * 'available' after a user-join. */
  INF_USER_JOIN_ERROR_STATUS_PROVIDED,

  INF_USER_JOIN_ERROR_FAILED
} InfUserJoinError;

typedef enum _InfUserLeaveError {
  /* The request does not include an id attribute */
  INF_USER_LEAVE_ERROR_ID_NOT_PRESENT,
  /* There is no user with the given ID */
  INF_USER_LEAVE_ERROR_NO_SUCH_USER,
  /* The user did not join from the connection the request comes from */
  INF_USER_LEAVE_ERROR_NOT_JOINED,

  INF_USER_LEAVE_ERROR_FAILED
} InfUserLeaveError;

GQuark
inf_request_error_quark(void);

const gchar*
inf_request_strerror(InfRequestError code);

GQuark
inf_user_join_error_quark(void);

const gchar*
inf_user_join_strerror(InfUserJoinError code);

GQuark
inf_user_leave_error_quark(void);

const gchar*
inf_user_leave_strerror(InfUserLeaveError code);

G_END_DECLS

#endif /* __INF_ERROR_H__ */
