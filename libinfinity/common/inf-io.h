/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_IO_H__
#define __INF_IO_H__

#include <glib-object.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#endif

G_BEGIN_DECLS

#define INF_TYPE_IO                 (inf_io_get_type())
#define INF_IO(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_IO, InfIo))
#define INF_IS_IO(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_IO))
#define INF_IO_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_IO, InfIoIface))

#define INF_TYPE_IO_EVENT           (inf_io_event_get_type())

/**
 * InfIo:
 *
 * #InfIo is an opaque data type. You should only access it via the public
 * API functions.
 */
typedef struct _InfIo InfIo;
typedef struct _InfIoIface InfIoIface;

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

/**
 * InfIoEvent:
 * @INF_IO_INCOMING: Data can be read from the socket without blocking, or
 * the connection has been closed (which is the case when recv() returns 0).
 * @INF_IO_OUTGOING: Data can be sent without blocking.
 * @INF_IO_ERROR: An error with the socket occured, or the connection has
 * been closed. Use getsockopt() to read do %SO_ERROR option to find out what
 * the problem is.
 *
 * This enumeration specifies events that can be watched.
 */
typedef enum _InfIoEvent {
  INF_IO_INCOMING = 1 << 0,
  INF_IO_OUTGOING = 1 << 1,
  INF_IO_ERROR    = 1 << 2
} InfIoEvent;

/**
 * InfIoFunc:
 * @socket: The socket on which an event occured.
 * @event: A bitmask of the events that occured.
 * @user_data: User-defined data specified in inf_io_watch().
 *
 * Callback function that is called when an event occurs on a watched socket.
 */
typedef void(*InfIoFunc)(InfNativeSocket* socket,
                         InfIoEvent event,
                         gpointer user_data);

/**
 * InfIoTimeoutFunc:
 * @user_data: User-defined data specified in inf_io_add_timeout().
 *
 * Callback function that is called when a timeout has elapsed.
 */
typedef void(*InfIoTimeoutFunc)(gpointer user_data);

/**
 * InfIoIface:
 * @watch: Changes the events the given socket is watched for. If @events is
 * 0, removes the watch for @socket.
 * @add_timeout: Schedules @func to be called at least @msecs milliseconds
 * in the future.
 * @remove_timeout: Removes a scheduled timeout again. The timeout is
 * removed automatically when it has elapsed, so there is no need to call
 * this function in that case.
 *
 * The virtual methods of #InfIo. These allow to set up socket watches and
 * timeouts.
 */
struct _InfIoIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/
  void (*watch)(InfIo* io,
                InfNativeSocket* socket,
                InfIoEvent events,
                InfIoFunc func,
                gpointer user_data,
                GDestroyNotify notify);

  gpointer (*add_timeout)(InfIo* io,
                          guint msecs,
                          InfIoTimeoutFunc func,
                          gpointer user_data,
                          GDestroyNotify notify);

  void (*remove_timeout)(InfIo* io,
                         gpointer timeout);
};

GType
inf_io_event_get_type(void) G_GNUC_CONST;

GType
inf_io_get_type(void) G_GNUC_CONST;

void
inf_io_watch(InfIo* io,
             InfNativeSocket* socket,
             InfIoEvent events,
             InfIoFunc func,
             gpointer user_data,
             GDestroyNotify notify);

gpointer
inf_io_add_timeout(InfIo* io,
                   guint msecs,
                   InfIoTimeoutFunc func,
                   gpointer user_data,
                   GDestroyNotify notify);

void
inf_io_remove_timeout(InfIo* io,
                      gpointer timeout);

G_END_DECLS

#endif /* __INF_IO_H__ */

/* vim:set et sw=2 ts=2: */
