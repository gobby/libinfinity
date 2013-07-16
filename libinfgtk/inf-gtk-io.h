/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_GTK_IO_H__
#define __INF_GTK_IO_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_GTK_TYPE_IO                 (inf_gtk_io_get_type())
#define INF_GTK_IO(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_GTK_TYPE_IO, InfGtkIo))
#define INF_GTK_IO_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_GTK_TYPE_IO, InfGtkIoClass))
#define INF_GTK_IS_IO(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_GTK_TYPE_IO))
#define INF_GTK_IS_IO_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_GTK_TYPE_IO))
#define INF_GTK_IO_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_GTK_TYPE_IO, InfGtkIoClass))

typedef struct _InfGtkIo InfGtkIo;
typedef struct _InfGtkIoClass InfGtkIoClass;

struct _InfGtkIoClass {
  GObjectClass parent_class;
};

struct _InfGtkIo {
  GObject parent;
};

GType
inf_gtk_io_get_type(void) G_GNUC_CONST;

InfGtkIo*
inf_gtk_io_new(void);

G_END_DECLS

#endif /* __INF_GTK_IO_H__ */

/* vim:set et sw=2 ts=2: */
