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
  /* Unknown error domain */
  INF_REQUEST_ERROR_UNKNOWN_DOMAIN,
  /* Server reply could not be processed */
  INF_REQUEST_ERROR_REPLY_UNPROCESSED,
  /* Server reply had invalid sequence number set */
  INF_REQUEST_ERROR_INVALID_SEQ,
  /* A required attribute was not set */
  INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
  /* An attribute expected to contain a number contained something else,
   * or an overflow occured */
  INF_REQUEST_ERROR_INVALID_NUMBER,

  INF_REQUEST_ERROR_FAILED
} InfRequestError;

typedef enum _InfUserJoinError {
  /* The requested name is already in use by another user */
  INF_USER_JOIN_ERROR_NAME_IN_USE,
  /* An id attribute was provided, but the server assigns it */
  INF_USER_JOIN_ERROR_ID_PROVIDED,
  /* User is not present, in case of a user rejoin */
  INF_USER_JOIN_ERROR_NO_SUCH_USER,
  /* A status attribute was provided, but the status is always
   * 'available' after a user-join. */
  INF_USER_JOIN_ERROR_STATUS_PROVIDED,

  INF_USER_JOIN_ERROR_FAILED
} InfUserJoinError;

typedef enum _InfUserStatusChangeError {
  /* There is no user with the given ID */
  INF_USER_STATUS_CHANGE_ERROR_NO_SUCH_USER,
  /* The user did not join from the connection the request comes from */
  INF_USER_STATUS_CHANGE_ERROR_NOT_JOINED,
  /* An invalid status was given */
  INF_USER_STATUS_CHANGE_ERROR_INVALID_STATUS,

  INF_USER_STATUS_CHANGE_ERROR_FAILED
} InfUserStatusChangeError;

typedef enum _InfDirectoryError {
  /* A node with this name exists already */
  INF_DIRECTORY_ERROR_NODE_EXISTS,
  /* The node referred to does not exist */
  INF_DIRECTORY_ERROR_NO_SUCH_NODE,
  /* The node referred to is not a subdirectory */
  INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY,
  /* The node referred to is not a note */
  INF_DIRECTORY_ERROR_NOT_A_NOTE,
  /* TODO: Make an own error domain out of the explore errors */
  /* The given subdirectory has already been explored */
  INF_DIRECTORY_ERROR_ALREADY_EXPLORED,
  /* There is no plugin that covers the given type */
  INF_DIRECTORY_ERROR_TYPE_UNKNOWN,
  /* The server sends more explored children that previously announced */
  INF_DIRECTORY_ERROR_TOO_MUCH_CHILDREN,
  /* The server sent explore-end before having sent all children */
  INF_DIRECTORY_ERROR_TOO_FEW_CHILDREN,
  /* The client is already subscribed to the session */
  INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED,
  /* Session does not support the network of the requesting connection */
  INF_DIRECTORY_ERROR_NETWORK_UNSUPPORTED,
  /* Session uses unsupported communication method */
  INF_DIRECTORY_ERROR_METHOD_UNSUPPORTED,
  /* Got unexpected XML message */
  INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE,

  INF_DIRECTORY_ERROR_FAILED
} InfDirectoryError;

GQuark
inf_request_error_quark(void);

const gchar*
inf_request_strerror(InfRequestError code);

GQuark
inf_user_join_error_quark(void);

const gchar*
inf_user_join_strerror(InfUserJoinError code);

GQuark
inf_user_status_change_error_quark(void);

const gchar*
inf_user_status_change_strerror(InfUserStatusChangeError code);

GQuark
inf_directory_error_quark(void);

const gchar*
inf_directory_strerror(InfDirectoryError code);

G_END_DECLS

#endif /* __INF_ERROR_H__ */

/* vim:set et sw=2 ts=2: */
