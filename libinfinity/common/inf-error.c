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

#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-i18n.h>

#include <gnutls/gnutls.h>

/* TODO: Cache GQuarks */

GQuark
inf_request_error_quark(void)
{
  return g_quark_from_static_string("INF_REQUEST_ERROR");
}

const gchar*
inf_request_strerror(InfRequestError code)
{
  switch(code)
  {
  case INF_REQUEST_ERROR_SYNCHRONIZING:
    return _("Synchronization is still in progress");
  case INF_REQUEST_ERROR_UNEXPECTED_MESSAGE:
    return _("Message was not understood");
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

GQuark
inf_user_error_quark(void)
{
  return g_quark_from_static_string("INF_USER_ERROR");
}

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

GQuark
inf_directory_error_quark(void)
{
  return g_quark_from_static_string("INF_DIRECTORY_ERROR");
}

const gchar*
inf_directory_strerror(InfDirectoryError code)
{
  switch(code)
  {
  case INF_DIRECTORY_ERROR_NODE_EXISTS:
    return _("A node with this name exists already");
  case INF_DIRECTORY_ERROR_NO_SUCH_NODE:
    return _("Node does not exist");
  case INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY:
    return _("Node is not a subdirectory");
  case INF_DIRECTORY_ERROR_NOT_A_NOTE:
    return _("Node is not a note");
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

GQuark
inf_gnutls_error_quark(void)
{
  return g_quark_from_static_string("INF_GNUTLS_ERROR");
}

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

/* vim:set et sw=2 ts=2: */
