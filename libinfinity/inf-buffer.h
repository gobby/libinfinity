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

#ifndef __INF_BUFFER_H__
#define __INF_BUFFER_H__

#include <libinfinity/inf-user.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_BUFFER                 (inf_buffer_get_type())
#define INF_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_BUFFER, InfBuffer))
#define INF_IS_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_BUFFER))
#define INF_BUFFER_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_BUFFER, InfBufferIface))

typedef struct _InfBuffer InfBuffer;
typedef struct _InfBufferIface InfBufferIface;

struct _InfBufferIface {
  GTypeInterface parent;
};

GType
inf_buffer_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __INF_BUFFER_H__ */
