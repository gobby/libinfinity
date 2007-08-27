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

#ifndef __INF_IO_H__
#define __INF_IO_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_IO                 (inf_io_get_type())
#define INF_IO(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_IO, InfIo))
#define INF_IS_IO(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_IO))
#define INF_IO_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_IO, InfIoIface))

#define INF_TYPE_IO_EVENT           (inf_io_event_get_type())

typedef struct _InfIo InfIo;
typedef struct _InfIoIface InfIoIface;

typedef int InfNativeSocket;

typedef enum _InfIoEvent {
  INF_IO_INCOMING = 1 << 0,
  INF_IO_OUTGOING = 1 << 1,
  INF_IO_ERROR    = 1 << 2
} InfIoEvent;

typedef void(*InfIoFunc)(InfNativeSocket*, InfIoEvent, gpointer);
typedef void(*InfIoTimeoutFunc)(gpointer);

struct _InfIoIface {
  GTypeInterface parent;

  /* Virtual table */
  void (*watch)(InfIo* io,
                InfNativeSocket* socket,
                InfIoEvent events,
                InfIoFunc func,
                gpointer user_data);

  gpointer (*add_timeout)(InfIo* io,
                          guint msecs,
                          InfIoTimeoutFunc func,
                          gpointer user_data);

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
             gpointer user_data);

gpointer
inf_io_add_timeout(InfIo* io,
                   guint msecs,
                   InfIoTimeoutFunc func,
                   gpointer user_data);

void
inf_io_remove_timeout(InfIo* io,
                      gpointer timeout);

G_END_DECLS

#endif /* __INF_IO_H__ */

/* vim:set et sw=2 ts=2: */
