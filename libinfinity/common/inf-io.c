/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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
 *
 * #InfIo is guaranteed to be thread-safe. All functions can be called from
 * any thread at any time. However, all callback functions are always called
 * from the same thread (normally the one running the main loop).
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
 * inf_io_add_watch:
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
 *
 * Returns: A #InfIoWatch that can be used to update or remove the watch.
 **/
InfIoWatch*
inf_io_add_watch(InfIo* io,
                 InfNativeSocket* socket,
                 InfIoEvent events,
                 InfIoWatchFunc func,
                 gpointer user_data,
                 GDestroyNotify notify)
{
  InfIoIface* iface;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(socket != NULL, NULL);
  g_return_val_if_fail(func != NULL, NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_val_if_fail(iface->add_watch != NULL, NULL);

  return iface->add_watch(io, socket, events, func, user_data, notify);
}

/**
 * inf_io_update_watch:
 * @io: A #InfIo.
 * @watch: The watch to update, as returned by inf_io_add_watch().
 * @events: The new events to watch for.
 *
 * Changes the events that the socket bound to @watch is being watched for.
 * The callback of @watch will only be called if one of the newly watched for
 * events occurs.
 */
void
inf_io_update_watch(InfIo* io,
                    InfIoWatch* watch,
                    InfIoEvent events)
{
  InfIoIface* iface;

  g_return_if_fail(INF_IS_IO(io));
  g_return_if_fail(watch != NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_if_fail(iface->update_watch != NULL);

  iface->update_watch(io, watch, events);
}

/**
 * inf_io_remove_watch:
 * @io: A #InfIo.
 * @watch: The watch to remove, as returned by inf_io_add_watch().
 *
 * Removes @watch from @io and releases all resources allocated for the watch.
 * Events are no longer looked for on the socket.
 */
void
inf_io_remove_watch(InfIo* io,
                    InfIoWatch* watch)
{
  InfIoIface* iface;

  g_return_if_fail(INF_IS_IO(io));
  g_return_if_fail(watch != NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_if_fail(iface->remove_watch != NULL);

  iface->remove_watch(io, watch);
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
 * Returns: A timeout handle that can be used to remove the timeout.
 **/
InfIoTimeout*
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
                      InfIoTimeout* timeout)
{
  InfIoIface* iface;

  g_return_if_fail(INF_IS_IO(io));
  g_return_if_fail(timeout != NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_if_fail(iface->remove_timeout != NULL);

  iface->remove_timeout(io, timeout);
}

/**
 * inf_io_add_dispatch:
 * @io: A #InfIo.
 * @func: Function to be called when the function is dispatched.
 * @user_data: Extra data to pass to @func.
 * @notify: A #GDestroyNotify that is called when @user_data is no longer
 * needed, or %NULL.
 *
 * Schedules @func to be called by the thread @io runs in. This function can
 * be used from a different thread to communicate to @io's thread.
 *
 * Returns: A dispatch handle that can be used to stop the dispatched function
 * from being called as long as it has not yet been called.
 **/
InfIoDispatch*
inf_io_add_dispatch(InfIo* io,
                    InfIoDispatchFunc func,
                    gpointer user_data,
                    GDestroyNotify notify)
{
  InfIoIface* iface;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(func != NULL, NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_val_if_fail(iface->add_dispatch != NULL, NULL);

  return iface->add_dispatch(io, func, user_data, notify);
}

/**
 * inf_io_remove_dispatch:
 * @io: A #InfIo.
 * @dispatch: A dispatch handle obtained from inf_io_add_dispatch().
 *
 * Removes the given dispatch from @io so that it is not called.
 **/
void
inf_io_remove_dispatch(InfIo* io,
                       InfIoDispatch* dispatch)
{
  InfIoIface* iface;

  g_return_if_fail(INF_IS_IO(io));
  g_return_if_fail(dispatch != NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_if_fail(iface->remove_dispatch != NULL);

  iface->remove_dispatch(io, dispatch);
}

/* vim:set et sw=2 ts=2: */
