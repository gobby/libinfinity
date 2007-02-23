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

#ifndef __INF_INK_BUFFER_H__
#define __INF_INK_BUFFER_H__

#include <libinfinity/inf-ink-stroke.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_INK_BUFFER                 (inf_ink_buffer_get_type())
#define INF_INK_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_INK_BUFFER, InfInkBuffer))
#define INF_IS_INK_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_INK_BUFFER))
#define INF_INK_BUFFER_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TYPE_INK_BUFFER, InfInkBufferIface))

typedef struct _InfInkBuffer InfInkBuffer;
typedef struct _InfInkBufferIface InfInkBufferIface;

struct _InfInkBufferIface {
  GTypeInterface parent;

  /* Virtual Table */
  InfInkStroke* (*get_stroke_by_id)(InfInkBuffer* buffer,
                                    guint id);

  /* Signals */
  void(*add_stroke)(InfInkBuffer* buffer,
                    InfInkStroke* stroke);

  void(*remove_stroke)(InfInkBuffer* buffer,
                       InfInkStroke* stroke);

  void(*move_stroke)(InfInkBuffer* buffer,
                     InfInkStroke* stroke,
                     gdouble by_x,
                     gdouble by_y);
};

GType
inf_ink_buffer_get_type(void) G_GNUC_CONST;

void
inf_ink_buffer_add_stroke(InfInkBuffer* buffer,
                          InfInkStroke* stroke);

void
inf_ink_buffer_remove_stroke(InfInkBuffer* buffer,
                             InfInkStroke* stroke);

void
inf_ink_buffer_move_stroke(InfInkBuffer* buffer,
                           InfInkStroke* stroke,
                           gdouble by_x,
                           gdouble by_y);

InfInkStroke*
inf_ink_buffer_get_stroke_by_id(InfInkBuffer* buffer,
                                guint id);

G_END_DECLS

#endif /* __INF_INK_BUFFER_H__ */
