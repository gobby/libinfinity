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

/** inf_io_watch:
 *
 * @io: A #InfIo.
 * @socket: The socket to watch.
 * @events: Events to watch for.
 * @func: Function to be called when one of the events occurs.
 * @user_data: Extra data to pass to @func.
 *
 * Monitors the given socket for activity and calls @func if one of the
 * events specified in @events occurs.
 **/
void
inf_io_watch(InfIo* io,
             InfNativeSocket* socket,
             InfIoEvent events,
             InfIoFunc func,
             gpointer user_data)
{
  InfIoIface* iface;

  g_return_if_fail(INF_IS_IO(io));
  g_return_if_fail(socket != NULL);
  g_return_if_fail(events == 0 || func != NULL);

  iface = INF_IO_GET_IFACE(io);
  g_return_if_fail(iface->watch != NULL);

  iface->watch(io, socket, events, func, user_data);
}

/* vim:set et sw=2 ts=2: */
