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

#ifndef __INF_BUFFER_H__
#define __INF_BUFFER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_BUFFER                 (inf_buffer_get_type())
#define INF_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_BUFFER, InfBuffer))
#define INF_IS_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_BUFFER))
#define INF_BUFFER_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_BUFFER, InfBufferIface))

typedef struct _InfBuffer InfBuffer;
typedef struct _InfBufferIface InfBufferIface;

/**
 * InfBufferIface:
 * @get_modified: Returns whether the buffer has been modified since the last
 * call to @set_modified set modified flag to %FALSE.
 * @set_modified: Set the current modified state of the buffer.
 *
 * The virtual methods of #InfBuffer.
 */
struct _InfBufferIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/
  gboolean (*get_modified)(InfBuffer* buffer);

  void (*set_modified)(InfBuffer* buffer,
                       gboolean modified);
};

/**
 * InfBuffer:
 *
 * #InfBuffer is an opaque data type. You should only access it
 * via the public API functions.
 */

GType
inf_buffer_get_type(void) G_GNUC_CONST;

gboolean
inf_buffer_get_modified(InfBuffer* buffer);

void
inf_buffer_set_modified(InfBuffer* buffer,
                        gboolean modified);

G_END_DECLS

#endif /* __INF_BUFFER_H__ */

/* vim:set et sw=2 ts=2: */
