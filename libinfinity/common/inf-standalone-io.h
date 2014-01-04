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

#ifndef __INF_STANDALONE_IO_H__
#define __INF_STANDALONE_IO_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_STANDALONE_IO                 (inf_standalone_io_get_type())
#define INF_STANDALONE_IO(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_STANDALONE_IO, InfStandaloneIo))
#define INF_STANDALONE_IO_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_STANDALONE_IO, InfStandaloneIoClass))
#define INF_IS_STANDALONE_IO(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_STANDALONE_IO))
#define INF_IS_STANDALONE_IO_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_STANDALONE_IO))
#define INF_STANDALONE_IO_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_STANDALONE_IO, InfStandaloneIoClass))

typedef struct _InfStandaloneIo InfStandaloneIo;
typedef struct _InfStandaloneIoClass InfStandaloneIoClass;

/**
 * InfStandaloneIoClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfStandaloneIoClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfStandaloneIo:
 *
 * #InfStandaloneIo is an opaque data type. You should only access it via the
 * public API functions.
 */
struct _InfStandaloneIo {
  /*< private >*/
  GObject parent;
};

GType
inf_standalone_io_get_type(void) G_GNUC_CONST;

InfStandaloneIo*
inf_standalone_io_new(void);

void
inf_standalone_io_iteration(InfStandaloneIo* io);

void
inf_standalone_io_iteration_timeout(InfStandaloneIo* io,
                                    guint timeout);

void
inf_standalone_io_loop(InfStandaloneIo* io);

void
inf_standalone_io_loop_quit(InfStandaloneIo* io);

gboolean
inf_standalone_io_loop_running(InfStandaloneIo* io);

G_END_DECLS

#endif /* __INF_STANDALONE_IO_H__ */

/* vim:set et sw=2 ts=2: */
