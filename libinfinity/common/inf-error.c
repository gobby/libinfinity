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

/**
 * SECTION:inf-error
 * @short_description: Common error codes
 * @include: libinfinity/common/inf-error.h
 * @stability: Unstable
 *
 * This section defines some common error codes that are used on both client
 * and server side in infinote, and maps these to #GError<!-- -->s.
 **/

#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#include <gsasl.h>

#include <unistd.h> /* Get ssize_t on MSVC, required by gnutls.h */
#include <gnutls/gnutls.h>

/* TODO: Cache GQuarks */

/**
 * inf_request_error_quark:
 *
 * Error domain for request errors. Errors in this domain will be from the
 * #InfRequestError enumeration. See #GError for information on error domains.
 *
 * Returns: A GQuark.
 */
GQuark
inf_request_error_quark(void)
{
  return g_quark_from_static_string("INF_REQUEST_ERROR");
}

/**
 * inf_request_strerror:
 * @code: An error code from the #InfRequestError enumeration.
 *
 * Returns a human-readable string for the given error code.
 *
 * Returns: A static string that must not be freed.
 */
const gchar*
inf_request_strerror(InfRequestError code)
{
  switch(code)
  {
  case INF_REQUEST_ERROR_UNKNOWN_DOMAIN:
    return _("Received error from an unknown domain");
  case INF_REQUEST_ERROR_REPLY_UNPROCESSED:
    return _("Failed to process server reply");
  case INF_REQUEST_ERROR_INVALID_SEQ:
    return _("Server reply contains invalid sequence number");
  case INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE:
    return _("A required attribute was not set in request");
  case INF_REQUEST_ERROR_INVALID_NUMBER:
    return _("An attribute contained an invalid number");
  case INF_REQUEST_ERROR_FAILED:
    return _("An unknown request error occured");
  default:
    return _("An error with unknown error code occured");
  }
}

/**
 * inf_user_error_quark:
 *
 * Error domain for user-related errors. Errors in this domain will be from
 * the #InfUserError enumeration. See #GError for information on error
 * domains.
 *
 * Returns: A GQuark.
 */
GQuark
inf_user_error_quark(void)
{
  return g_quark_from_static_string("INF_USER_ERROR");
}

/**
 * inf_user_strerror:
 * @code: An error code from the #InfUserError enumeration.
 *
 * Returns a human-readable string for the given error code.
 *
 * Returns: A static string that must not be freed.
 */
const gchar*
inf_user_strerror(InfUserError code)
{
  switch(code)
  {
  case INF_USER_ERROR_NAME_IN_USE:
    return _("Name is already in use");
  case INF_USER_ERROR_ID_PROVIDED:
    return _("'id' attribute provided in request");
  case INF_USER_ERROR_NO_SUCH_USER:
    return _("There is no user with the given ID");
  case INF_USER_ERROR_STATUS_UNAVAILABLE:
    return _("'status' attribute is 'unavailable' in join or rejoin request");
  case INF_USER_ERROR_NOT_JOINED:
    return _("User did not join via this connection");
  case INF_USER_ERROR_INVALID_STATUS:
    return _("'status' attribute has invalid value");
  case INF_USER_ERROR_FAILED:
    return _("An unknown user error occured");
  default:
    return _("An error with unknown error code occured");
  }
}

/**
 * inf_directory_error_quark:
 *
 * Error domain for directory errors. Errors in this domain will be from the
 * #InfDirectoryError enumeration. See #GError for information on error
 * domains.
 *
 * Returns: A GQuark.
 */
GQuark
inf_directory_error_quark(void)
{
  return g_quark_from_static_string("INF_DIRECTORY_ERROR");
}

/**
 * inf_directory_strerror:
 * @code: An error code from the #InfDirectoryError enumeration.
 *
 * Returns a human-readable string for the given error code.
 *
 * Returns: A static string that must not be freed.
 */
const gchar*
inf_directory_strerror(InfDirectoryError code)
{
  switch(code)
  {
  case INF_DIRECTORY_ERROR_NO_WELCOME_MESSAGE:
    return _("Server did not send an initial welcome message");
  case INF_DIRECTORY_ERROR_VERSION_MISMATCH:
    return _("The server and client use different protocol versions");
  case INF_DIRECTORY_ERROR_NODE_EXISTS:
    return _("A node with this name exists already");
  case INF_DIRECTORY_ERROR_INVALID_NAME:
    return _("Invalid node name");
  case INF_DIRECTORY_ERROR_NO_SUCH_NODE:
    return _("Node does not exist");
  case INF_DIRECTORY_ERROR_NO_SUCH_SUBSCRIPTION_REQUEST:
    return _("No previous subscription request present");
  case INF_DIRECTORY_ERROR_CHAT_DISABLED:
    return _("The chat is disabled on the server side");
  case INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY:
    return _("Node is not a subdirectory");
  case INF_DIRECTORY_ERROR_NOT_A_NOTE:
    return _("Node is not a note");
  case INF_DIRECTORY_ERROR_ROOT_NODE_REMOVE_ATTEMPT:
    return _("The root node cannot be removed");
  case INF_DIRECTORY_ERROR_ALREADY_EXPLORED:
    return _("Subdirectory has already been explored");
  case INF_DIRECTORY_ERROR_TYPE_UNKNOWN:
    return _("Note type is not supported");
  case INF_DIRECTORY_ERROR_ALREADY_SUBSCRIBED:
    return _("Connection is already subscribed to this session");
  case INF_DIRECTORY_ERROR_UNSUBSCRIBED:
    return _("The requesting connection is not subscribed to the session");
  case INF_DIRECTORY_ERROR_TOO_MUCH_CHILDREN:
    return _("Server sent more explored nodes then announced");
  case INF_DIRECTORY_ERROR_TOO_FEW_CHILDREN:
    return _("Server sent not as much explored nodes as announced");
  case INF_DIRECTORY_ERROR_NETWORK_UNSUPPORTED:
    return _("The session does not support the network through which the "
             "connection attempt is being made.");
  case INF_DIRECTORY_ERROR_METHOD_UNSUPPORTED:
    return _("The session uses an unsupported communication method");
  case INF_DIRECTORY_ERROR_UNEXPECTED_SYNC_IN:
    return _("Received sync-in message without having requested a sync-in");
  case INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE:
    return _("Unexpected XML message");
  case INF_DIRECTORY_ERROR_FAILED:
    return _("An unknown directory error has occured");
  default:
    return _("An error with unknown code has occured");
  }
}

/**
 * inf_authentication_detail_error_quark:
 *
 * Error domain for further information on authentication errors. Errors in
 * this domain will be from the #InfAuthenticationDetailError enumeration.
 * See #GError for information on error domains.
 *
 * Returns: A GQuark.
 */
GQuark
inf_authentication_detail_error_quark(void)
{
  return g_quark_from_static_string("INF_AUTHENTICATION_DETAIL_ERROR");
}


/**
 * inf_authentication_detail_strerror:
 * @code: An error code from the #InfAuthenticationDetailError enumeration.
 *
 * Returns a human-readable string for the given error code.
 *
 * Returns: A static string that must not be freed.
 */
const gchar*
inf_authentication_detail_strerror(InfAuthenticationDetailError code)
{
  switch(code)
  {
  case INF_AUTHENTICATION_DETAIL_ERROR_AUTHENTICATION_FAILED:
    return _("User did not provide valid credentials.");
  case INF_AUTHENTICATION_DETAIL_ERROR_USER_NOT_AUTHORIZED:
    return _("User is not permitted to connect to this server.");
  case INF_AUTHENTICATION_DETAIL_ERROR_SERVER_ERROR:
    return _("An error cocured while checking user permissions.");
  default:
    return _("An error with unknown code has occured");
  }
}

/**
 * inf_gnutls_error_quark:
 *
 * Error domain for GnuTLS errors. Errors in this domain will be GnuTLS error
 * codes. See #GError for information on error domains.
 *
 * Returns: A GQuark.
 */
GQuark
inf_gnutls_error_quark(void)
{
  return g_quark_from_static_string("INF_GNUTLS_ERROR");
}

/**
 * inf_gnutls_set_error:
 * @error: Location to store the error, or %NULL.
 * @error_code: A GnuTLS error code.
 *
 * Sets a #GError from a GnuTLS error code. If @error is %NULL, does nothing.
 */
void
inf_gnutls_set_error(GError** error,
                     int error_code)
{
  if(error != NULL)
  {
    *error = g_error_new_literal(
      inf_gnutls_error_quark(),
      error_code,
      gnutls_strerror(error_code)
    );
  }
}

/**
 * inf_gsasl_error_quark:
 *
 * Error domain for GNU SASL errors. Errors in this domain will be GNU SASL
 * error codes. See #GError for information on error domains.
 *
 * Returns: A GQuark.
 */
GQuark
inf_gsasl_error_quark(void)
{
  return g_quark_from_static_string("INF_GSASL_ERROR");
}

/**
 * inf_gsasl_set_error:
 * @error: Location to store the error, or %NULL.
 * @error_code: A GNU SASL error code.
 *
 * Sets a #GError from a GNU SASL error code. If @error is %NULL, does nothing.
 */
void
inf_gsasl_set_error(GError** error,
                     int error_code)
{
  if(error != NULL)
  {
    *error = g_error_new_literal(
      inf_gsasl_error_quark(),
      error_code,
      gsasl_strerror(error_code)
    );
  }
}

/* vim:set et sw=2 ts=2: */
