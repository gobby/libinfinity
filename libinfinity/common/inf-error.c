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
  case INF_USER_JOIN_ERROR_NAME_MISSING:
    return "'name' attribute in request missing";
  case INF_USER_JOIN_ERROR_ID_PROVIDED:
    return "'id' attribute provided in request";
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
  case INF_USER_LEAVE_ERROR_ID_NOT_PRESENT:
    return "'id' attribute in request missing";
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
