/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-native-socket
 * @title: InfNativeSocket
 * @short_description: Uniform access to the BSD socket API
 * @include: libinfinity/common/inf-native-socket.h
 * @stability: Unstable
 *
 * This module provides a common way to access the BSD socket API. While
 * the API is mostly equivalent on Linux and Windows, there are some subtle
 * differences for which uniform identifiers are provided.
 *
 * Before #InfNativeSocket can be used, on Windows WinSock2 must be
 * initialized. You can either do this manually, or call inf_init() which
 * does it for you.
 */

#include <libinfinity/common/inf-native-socket.h>

#include <string.h>

#ifndef G_OS_WIN32
# include <sys/types.h>
# include <sys/socket.h>
#endif

#include "config.h"

#ifndef G_OS_WIN32
# ifdef HAVE_MSG_NOSIGNAL
const int INF_NATIVE_SOCKET_SENDRECV_FLAGS = MSG_NOSIGNAL;
# else
const int INF_NATIVE_SOCKET_SENDRECV_FLAGS = 0;
# endif
#endif

/**
 * inf_native_socket_error_quark:
 *
 * Returns a #GQuark representing the INF_NATIVE_SOCKET_ERROR domain.
 *
 * Returns: A #GQuark representing the INF_NATIVE_SOCKET_ERROR domain.
 */
GQuark
inf_native_socket_error_quark(void)
{
  return g_quark_from_static_string("INF_NATIVE_SOCKET_ERROR");
}

/**
 * inf_native_socket_make_error:
 * @code: An error code obtained with #INF_NATIVE_SOCKET_LAST_ERROR
 * @error: Location to store error information.
 *
 * Converts the platform-dependent error code @code into a #GError. The
 * #GError will contain the same numerical value and a platform-dependent
 * human-readable error message.
 */
void
inf_native_socket_make_error(int code,
                             GError** error)
{
#ifdef G_OS_WIN32
  gchar* error_message;
  error_message = g_win32_error_message(code);

  g_set_error_literal(
    error,
    inf_native_socket_error_quark(),
    code,
    error_message
  );

  g_free(error_message);
#else
  g_set_error_literal(
    error,
    inf_native_socket_error_quark(),
    code,
    strerror(code)
  );
#endif
}

/* vim:set et sw=2 ts=2: */
