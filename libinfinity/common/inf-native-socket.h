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

#ifndef __INF_NATIVE_SOCKET_H__
#define __INF_NATIVE_SOCKET_H__

#include <glib.h>

#ifdef G_OS_WIN32
# include <winsock2.h>
#endif

G_BEGIN_DECLS

/**
 * InfNativeSocket:
 *
 * Native socket type on the target platform. This typedef is a simple #int
 * on Unix and a #SOCKET on Windows.
 */
#ifdef G_OS_WIN32
typedef SOCKET InfNativeSocket;
#else
typedef int InfNativeSocket;
#endif

#ifdef G_OS_WIN32
# define INF_NATIVE_SOCKET_SENDRECV_FLAGS 0
# define INF_NATIVE_SOCKET_LAST_ERROR     WSAGetLastError()
# define INF_NATIVE_SOCKET_EINTR          WSAEINTR
# define INF_NATIVE_SOCKET_EAGAIN         WSAEWOULDBLOCK
/* This is not a typo here. On Windows, connect() returns WSAEWOULDBLOCK on
 * a non-blocking socket. */
# define INF_NATIVE_SOCKET_EINPROGRESS    WSAEWOULDBLOCK
#else
extern const int INF_NATIVE_SOCKET_SENDRECV_FLAGS;
# define INF_NATIVE_SOCKET_LAST_ERROR     errno
# define INF_NATIVE_SOCKET_EINTR          EINTR
# define INF_NATIVE_SOCKET_EAGAIN         EAGAIN
# define INF_NATIVE_SOCKET_EINPROGRESS    EINPROGRESS
# define closesocket(s) close(s)
# define INVALID_SOCKET -1
#endif

GQuark
inf_native_socket_error_quark(void);

void
inf_native_socket_make_error(int code,
                             GError** error);

G_END_DECLS

#endif /* __INF_NATIVE_SOCKET_H__ */

/* vim:set et sw=2 ts=2: */
