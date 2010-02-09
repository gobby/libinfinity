/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_ERROR_H__
#define __INF_ERROR_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* These are error codes do not occur directly in common/, but that
 * may be reported and/or need to be understood by both client and server. */

/**
 * InfRequestError:
 * @INF_REQUEST_ERROR_UNKNOWN_DOMAIN: The server sent &lt;request-failed/&gt;
 * with an unknown error domain.
 * @INF_REQUEST_ERROR_REPLY_UNPROCESSED: An error occured while processing the
 * server reply for a request.
 * @INF_REQUEST_ERROR_INVALID_SEQ: The server sent an invalid sequence number
 * in a reply to a request.
 * @INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE: A request did not contain a XML
 * attribute that is required to complete the request.
 * @INF_REQUEST_ERROR_INVALID_NUMBER: A number in a request was invalid.
 * Either it was not numerical, or it produced an underflow or an overflow.
 * @INF_REQUEST_ERROR_FAILED: Generic error code when no further reason of
 * failure is known.
 *
 * These are general request errors that all #InfcRequest<!-- -->s can
 * fail with. Specific requests may also fail with more errors, such as
 * #InfDirectoryError.
 */
typedef enum _InfRequestError {
  INF_REQUEST_ERROR_UNKNOWN_DOMAIN,
  INF_REQUEST_ERROR_REPLY_UNPROCESSED,
  INF_REQUEST_ERROR_INVALID_SEQ,
  INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
  INF_REQUEST_ERROR_INVALID_NUMBER,

  INF_REQUEST_ERROR_FAILED
} InfRequestError;

/**
 * InfUserError:
 * @INF_USER_ERROR_NAME_IN_USE: The requested name is already in use by
 * another user.
 * @INF_USER_ERROR_ID_PROVIDED: The client provided a user ID field in a
 * user join request, but it's the server's job to find an ID.
 * @INF_USER_ERROR_NO_SUCH_USER: A request referred to a user ID that no user
 * is associated with.
 * @INF_USER_ERROR_STATUS_UNAVAILABLE: The initial user status was given as
 * unavailable during user join or rejoin.
 * @INF_USER_ERROR_NOT_JOINED: The user did not join from the connection the
 * request comes from. For example, a user status change can only be requested
 * from the same connection that joined the user.
 * @INF_USER_ERROR_INVALID_STATUS: An invalid status was used in a XML
 * request. Allowed status values are "unavailable", "inactive" or "active".
 * @INF_USER_ERROR_FAILED: Generic error code when no further reason of
 * failure is known.
 *
 * These are errors related to users. They may occur during user join or
 * during processing a user-related message, such as a user status change
 * message in an #InfSession.
 */
typedef enum _InfUserError {
  INF_USER_ERROR_NAME_IN_USE,
  INF_USER_ERROR_ID_PROVIDED,
  INF_USER_ERROR_NO_SUCH_USER,
  INF_USER_ERROR_STATUS_UNAVAILABLE,
  INF_USER_ERROR_NOT_JOINED,
  INF_USER_ERROR_INVALID_STATUS,

  INF_USER_ERROR_FAILED
} InfUserError;

/**
 * InfDirectoryError:
 * @INF_DIRECTORY_ERROR_NO_WELCOME_MESSAGE: A client did not receive the
 * directory's initial welcome message.
 * @INF_DIRECTORY_ERROR_VERSION_MISMATCH: The server and client use
 * different versions of the protocol.
 * @INF_DIRECTORY_ERROR_NODE_EXISTS: A node with the given name exists
 * already in that subdirectory (in response to node creation requests).
 * @INF_DIRECTORY_ERROR_INVALID_NAME: A node with an invalid name was
 * attempted to be created.
 * @INF_DIRECTORY_ERROR_NO_SUCH_NODE: The node refered to in a request does
 * not exist in the directory (anymore).
 * @INF_DIRECTORY_ERROR_NO_SUCH_SUBSCRIPTION_REQUEST: A &lt;subscribe-ack&gt;
 * or &lt;subscribe-nack&gt; has been received without a previous request.
 * @INF_DIRECTORY_ERROR_CHAT_DISABLED: A client attempted to subscribe to a
 * server's chat, but the chat is disabled on the server side.
 * @INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY: The node refered to in a request
 * is not a subdirectory node, but the requested operation requires one.
 * @INF_DIRECTORY_ERROR_NOT_A_NOTE: The node refered to in a request is not
 * a note (non-subdirectory) node, but the requested operations requires one.
 * @INF_DIRECTORY_ERROR_ROOT_NODE_REMOVE_ATTEMPT: A &lt;remove-node&gt;
 * request attempted to remove a directory's root node, which is not allowed.
 * @INF_DIRECTORY_ERROR_ALREADY_EXPLORED: The node given in an exploration
 * request has already been explored earlier.
 * @INF_DIRECTORY_ERROR_TOO_MUCH_CHILDREN: Exploration yields more children
 * than announced at the beginning of the exploration.
 * @INF_DIRECTORY_ERROR_TOO_FEW_CHILDREN: Exploration yields less children
 * than announced at the beginning of the exploration.
 * @INF_DIRECTORY_ERROR_TYPE_UNKNOWN: The type of a note is not known.
 * @INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED: The connection already subscribed
 * to the node refered to, but the requested operation requires it to be
 * unsubscribed.
 * @INF_DIRECTORY_ERROR_UNSUBSCRIBED: The connection is not subscribed to the
 * node refered to, but the requested operation requires it to be subscribed.
 * @INF_DIRECTORY_ERROR_NETWORK_UNSUPPORTED: The server does not support the
 * network of the incoming connection for the requested operation. For
 * example, subscribing to a session might require a network that has a
 * peer-to-peer communication method, but there is no implementation of that
 * method for the connection's network.
 * @INF_DIRECTORY_ERROR_METHOD_UNSUPPORTED: The server requested a
 * communaction method for subscription or synchronization that is not
 * supported by the client.
 * @INF_DIRECTORY_ERROR_UNEXPECTED_SYNC_IN: A client received a
 * &lt;sync-in/&gt; without having requested one. The client has no data to
 * sync to the server.
 * @INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE: A message that is not understood
 * was received.
 * @INF_DIRECTORY_ERROR_FAILED: Generic error code when no further reason of
 * failure is known.
 *
 * These are errors related to the directory of documents. These errors can
 * be reason why requests created by #InfcBrowser fail.
 */
typedef enum _InfDirectoryError {
  INF_DIRECTORY_ERROR_NO_WELCOME_MESSAGE,
  INF_DIRECTORY_ERROR_VERSION_MISMATCH,
  INF_DIRECTORY_ERROR_NODE_EXISTS,
  INF_DIRECTORY_ERROR_INVALID_NAME,
  INF_DIRECTORY_ERROR_NO_SUCH_NODE,
  INF_DIRECTORY_ERROR_NO_SUCH_SUBSCRIPTION_REQUEST,
  INF_DIRECTORY_ERROR_CHAT_DISABLED,
  INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY,
  INF_DIRECTORY_ERROR_NOT_A_NOTE,
  INF_DIRECTORY_ERROR_ROOT_NODE_REMOVE_ATTEMPT,
  INF_DIRECTORY_ERROR_ALREADY_EXPLORED,
  INF_DIRECTORY_ERROR_TOO_MUCH_CHILDREN,
  INF_DIRECTORY_ERROR_TOO_FEW_CHILDREN,
  INF_DIRECTORY_ERROR_TYPE_UNKNOWN,
  INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED,
  INF_DIRECTORY_ERROR_UNSUBSCRIBED,
  INF_DIRECTORY_ERROR_NETWORK_UNSUPPORTED,
  INF_DIRECTORY_ERROR_METHOD_UNSUPPORTED,
  INF_DIRECTORY_ERROR_UNEXPECTED_SYNC_IN,
  INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE,
  INF_DIRECTORY_ERROR_NO_STORAGE,

  INF_DIRECTORY_ERROR_FAILED
} InfDirectoryError;

/**
 * InfAuthenticationDetailError:
 * @INF_AUTHENTICATION_DETAIL_ERROR_AUTHENTICATION_FAILED: User did not provide
 * valid credentials.
 * @INF_AUTHENTICATION_DETAIL_ERROR_USER_NOT_AUTHORIZED: User is not permitted
 * to connect to this server.
 * @INF_AUTHENTICATION_DETAIL_ERROR_TRY_AGAIN: Authentication was temporarily
 * interrupted on the server side.
 * @INF_AUTHENTICATION_DETAIL_ERROR_SERVER_ERROR: An error occured while checking
 * user permissions.
 */
typedef enum _InfAuthenticationDetailError {
  INF_AUTHENTICATION_DETAIL_ERROR_AUTHENTICATION_FAILED,
  INF_AUTHENTICATION_DETAIL_ERROR_USER_NOT_AUTHORIZED,
  INF_AUTHENTICATION_DETAIL_ERROR_TRY_AGAIN,
  INF_AUTHENTICATION_DETAIL_ERROR_SERVER_ERROR
} InfAuthenticationDetailError;

GQuark
inf_request_error_quark(void);

const gchar*
inf_request_strerror(InfRequestError code);

GQuark
inf_user_error_quark(void);

const gchar*
inf_user_strerror(InfUserError code);

GQuark
inf_directory_error_quark(void);

const gchar*
inf_directory_strerror(InfDirectoryError code);

const gchar*
inf_authentication_detail_strerror(InfAuthenticationDetailError code);

GQuark
inf_authentication_detail_error_quark(void);

GQuark
inf_gnutls_error_quark(void);

void
inf_gnutls_set_error(GError** error,
                     int error_code);

GQuark
inf_gsasl_error_quark(void);

void
inf_gsasl_set_error(GError** error,
                     int error_code);

G_END_DECLS

#endif /* __INF_ERROR_H__ */

/* vim:set et sw=2 ts=2: */
