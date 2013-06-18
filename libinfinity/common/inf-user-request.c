/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-user-request
 * @title: InfUserRequest
 * @short_description: Asynchronous user request
 * @include: libinfinity/common/inf-user-request.h
 * @see_also: #InfRequest, #InfSessionProxy
 * @stability: Unstable
 *
 * #InfUserRequest represents a request that has been made via the
 * #InfSessionProxy API to join a user, i.e. inf_session_proxy_join_user().
 * In general this is an asynchronous operation since a client might have to
 * wait for a response from an infinote server. The #InfUserRequest class can
 * be used to be notified when the request finishes.
 */

#include <libinfinity/common/inf-user-request.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/inf-marshal.h>

enum {
  FINISHED,

  LAST_SIGNAL
};

static guint user_request_signals[LAST_SIGNAL];

static void
inf_user_request_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    /**
     * InfUserRequest::finished:
     * @browser: The #InfUserRequest which finished.
     * @user: A #InfUser for which the request was made.
     * request.
     * @error: Error information in case the request failed, or %NULL
     * otherwise.
     *
     * This signal is emitted when the request finishes. If it finishes
     * successfully, @error will be %NULL and @user points to the affected
     * user. For a user join request this is the newly joined user. If the
     * request failed @error will be non-%NULL and @user can be %NULL.
     */
    user_request_signals[FINISHED] = g_signal_new(
      "finished",
      INF_TYPE_USER_REQUEST,
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfUserRequestIface, finished),
      NULL, NULL,
      inf_marshal_VOID__OBJECT_POINTER,
      G_TYPE_NONE,
      2,
      INF_TYPE_USER,
      G_TYPE_POINTER /* GError* */
    );

    initialized = TRUE;
  }
}

GType
inf_user_request_get_type(void)
{
  static GType user_request_type = 0;

  if(!user_request_type)
  {
    static const GTypeInfo user_request_info = {
      sizeof(InfUserRequestIface),     /* class_size */
      inf_user_request_base_init,      /* base_init */
      NULL,                            /* base_finalize */
      NULL,                            /* class_init */
      NULL,                            /* class_finalize */
      NULL,                            /* class_data */
      0,                               /* instance_size */
      0,                               /* n_preallocs */
      NULL,                            /* instance_init */
      NULL                             /* value_table */
    };

    user_request_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfUserRequest",
      &user_request_info,
      0
    );

    g_type_interface_add_prerequisite(user_request_type, INF_TYPE_REQUEST);
  }

  return user_request_type;
}

/**
 * inf_user_request_finished:
 * @request: A #InfUserRequest.
 * @user: A #InfUser for which the request was made.
 * @error: A #GError containing error information in case the request failed,
 * or %NULL otherwise.
 *
 * This function emits the #InfUserRequest::finished signal on @request.
 * It is meant to be used by interface implementations only.
 */
void
inf_user_request_finished(InfUserRequest* request,
                          InfUser* user,
                          const GError* error)
{
  g_return_if_fail(INF_IS_USER_REQUEST(request));

  g_signal_emit(
    request,
    user_request_signals[FINISHED],
    0,
    user,
    error
  );
}

/* vim:set et sw=2 ts=2: */
