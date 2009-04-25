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
 * SECTION:inf-io
 * @title: InfIo
 * @short_description: Event loop abstraction
 * @include: libinfinity/common/inf-io.h
 * @see_also: #InfStandaloneIo
 * @stability: Unstable
 *
 * The #InfIo interface is used to schedule timeouts and to watch sockets for
 * events to occur. An actual implementation usually integrates this into the
 * application main loop, such as #GMainLoop. There is also a standalone
 * implementation, #InfStandaloneIo, that can directly be used as the
 * application's main loop.
 *
 * Every object in Libinfinity that needs to schedule timeouts or watches
 * sockets uses a InfIo to do so. This allows to use libinfinity with
 * different main event loops, not only Glib's one.
 **/

#include <libinfinity/common/inf-io.h>

GType
inf_io_event_get_type(void)
{
  static GType io_event_type = 0;

  if(!io_event_type)
  {
    static const GFlagsValue io_event_values[] = {
      {
        INF_IO_INCOMING,
        "INF_IO_INCOMING",
        "incoming"
      }, {
        INF_IO_OUTGOING,
        "INF_IO_OUTGOING",
        "outgoing"
      }, {
        INF_IO_ERROR,
        "INF_IO_ERROR",
        "error"
      }, {
        0,
        NULL,
        NULL
      }
    };

    io_event_type = g_flags_register_static(
      "InfIoEvent",
      io_event_values
    );
  }

  return io_event_type;
}

GType
inf_io_get_type(void)
{
  static GType io_type = 0;

  if(!io_type)
  {
    static const GTypeInfo io_info = {
      sizeof(InfIoIface),    /* class_size */
      NULL,                  /* base_init */
      NULL,                  /* base_finalize */
      NULL,                  /* class_init */
      NULL,                  /* class_finalize */
      NULL,                  /* class_data */
      0,                     /* instance_size */
      0,                     /* n_preallocs */
      NULL,                  /* instance_init */
      NULL                   /* value_table */
    };

    io_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfIo",
      &io_info,
      0
    );

    g_type_interface_add_prerequisite(io_type, G_TYPE_OBJECT);
  }

  return io_type;
}

/**
 * inf_io_watch:
 * @io: A #InfIo.
 * @socket: The socket to watch.
 * @events: Events to watch for.
 * @func: Function to be called when one of the events occurs.
 * @user_data: Extra data to pass to @func.
 * @notify: A #GDestroyNotify that is called when @user_data is no longer
 * needed, or %NULL.
 *
 * Monitors the given socket for activity and calls @func if one of the
 * events specified in @events occurs.
 **/
void
inf_io_watch(InfIo* io,
             InfNativeSocket* socket,
             InfIoEvent events,
             InfIoFunc func,
             gpointer user_data,
             GDestroyNotify notify)
{
  InfIoIface* iface;

  g_return_if_fail(INF_IS_IO(io));
  g_return_if_fail(socket != NULL);
  g_return_if_fail(events == 0 || func != NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_if_fail(iface->watch != NULL);

  iface->watch(io, socket, events, func, user_data, notify);
}

/**
 * inf_io_add_timeout:
 * @io: A #InfIo.
 * @msecs: Number of milliseconds after which the timeout should be elapsed.
 * @func: Function to be called when the timeout elapsed.
 * @user_data: Extra data to pass to @func.
 * @notify: A #GDestroyNotify that is called when @user_data is no longer
 * needed, or %NULL.
 *
 * Calls @func after at least @msecs milliseconds have elapsed. The timeout
 * is removed after it has elapsed.
 *
 * Return Value: A timeout handle that can be used to remove the timeout.
 **/
gpointer
inf_io_add_timeout(InfIo* io,
                   guint msecs,
                   InfIoTimeoutFunc func,
                   gpointer user_data,
                   GDestroyNotify notify)
{
  InfIoIface* iface;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(func != NULL, NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_val_if_fail(iface->add_timeout != NULL, NULL);

  return iface->add_timeout(io, msecs, func, user_data, notify);
}

/**
 * inf_io_remove_timeout:
 * @io: A #InfIo.
 * @timeout: A timeout handle obtained from inf_io_add_timeout().
 *
 * Removes the given timeout.
 **/
void
inf_io_remove_timeout(InfIo* io,
                      gpointer timeout)
{
  InfIoIface* iface;

  g_return_if_fail(INF_IS_IO(io));
  g_return_if_fail(timeout != NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_if_fail(iface->remove_timeout != NULL);

  iface->remove_timeout(io, timeout);
}

/* vim:set et sw=2 ts=2: */
