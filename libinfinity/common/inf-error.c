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
    return "Synchronization is still in progress";
  case INF_REQUEST_ERROR_UNEXPECTED_MESSAGE:
    return "Message was not understood";
  case INF_REQUEST_ERROR_UNKNOWN_DOMAIN:
    return "Received error from an unknown domain";
  case INF_REQUEST_ERROR_REPLY_UNPROCESSED:
    return "Failed to process server reply";
  case INF_REQUEST_ERROR_INVALID_SEQ:
    return "Server reply contains invalid sequence number";
  case INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE:
    return "A required attribute was not set in request";
  case INF_REQUEST_ERROR_INVALID_NUMBER:
    return "An attribute contained an invalid number";
  case INF_REQUEST_ERROR_FAILED:
    return "An unknown request error occured";
  default:
    return "An error with unknown error code occured";
  }
}

GQuark
inf_user_join_error_quark(void)
{
  return g_quark_from_static_string("INF_USER_JOIN_ERROR");
}

const gchar*
inf_user_join_strerror(InfUserJoinError code)
{
  switch(code)
  {
  case INF_USER_JOIN_ERROR_NAME_IN_USE:
    return "Name is already in use";
  case INF_USER_JOIN_ERROR_ID_PROVIDED:
    return "'id' attribute provided in request";
  case INF_USER_JOIN_ERROR_NO_SUCH_USER:
    return "There is no user with the given ID";
  case INF_USER_JOIN_ERROR_STATUS_PROVIDED:
    return "'status' attribute provided in request";
  case INF_USER_JOIN_ERROR_FAILED:
    return "An unknown user join error occured";
  default:
    return "An error with unknown error code occured";
  }
}

GQuark
inf_user_leave_error_quark(void)
{
  return g_quark_from_static_string("INF_USER_LEAVE_ERROR");
}

const gchar*
inf_user_leave_strerror(InfUserLeaveError code)
{
  switch(code)
  {
  case INF_USER_LEAVE_ERROR_NO_SUCH_USER:
    return "There is no user with the given ID";
  case INF_USER_LEAVE_ERROR_NOT_JOINED:
    return "User did not join via this connection";
  case INF_USER_LEAVE_ERROR_FAILED:
    return "An unknown user leave error occured";
  default:
    return "An error with unknown error code occured";
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
    return "A node with this name exists already";
  case INF_DIRECTORY_ERROR_NO_SUCH_NODE:
    return "Node does not exist";
  case INF_DIRECTORY_ERROR_NOT_A_SUBDIRECTORY:
    return "Node is not a subdirectory";
  case INF_DIRECTORY_ERROR_NOT_A_NOTE:
    return "Node is not a note";
  case INF_DIRECTORY_ERROR_ALREADY_EXPLORED:
    return "Subdirectory has already been explored";
  case INF_DIRECTORY_ERROR_TYPE_UNKNOWN:
    return "Note type is not supported";
  case INF_DIRECTORY_ERROR_TOO_MUCH_CHILDREN:
    return "Server sent more explored nodes then announced";
  case INF_DIRECTORY_ERROR_TOO_FEW_CHILDREN:
    return "Server sent not as much explored nodes as announced";
  case INF_DIRECTORY_ERROR_UNEXPECTED_MESSAGE:
    return "Unexpected XML message";
  case INF_DIRECTORY_ERROR_FAILED:
    return "An unknown directory error has occured";
  default:
    return "An error with unknown code has occured";
  }
}

