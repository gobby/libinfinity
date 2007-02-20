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

#include <infinity/inf-stroke.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_BUFFER                 (inf_buffer_get_type())
#define INF_BUFFER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TYPE_BUFFER, InfBuffer))
#define INF_BUFFER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TYPE_BUFFER, InfBufferClass))
#define INF_IS_BUFFER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TYPE_BUFFER))
#define INF_IS_BUFFER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TYPE_BUFFER))
#define INF_BUFFER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TYPE_BUFFER, InfBufferClass))

typedef struct _InfBuffer InfBuffer;
typedef struct _InfBufferClass InfBufferClass;

struct _InfBufferClass {
  GObjectClass parent_class;

  /* Signals */
  void(*stroke_add)(InfBuffer*, InfStroke*);
  void(*stroke_remove)(InfBuffer*, InfStroke*);

  void(*stroke_move)(InfBuffer*, InfStroke*, gdouble, gdouble);
};

struct _InfBuffer {
  GObject parent;
};

GType
inf_buffer_get_type(void) G_GNUC_CONST;

InfBuffer*
inf_buffer_new(void);

void
inf_buffer_add_stroke(InfBuffer* buffer,
                      InfStroke* stroke);

void
inf_buffer_remove_stroke(InfBuffer* buffer,
                         InfStroke* stroke);

void
inf_buffer_move_stroke(InfBuffer* buffer,
                       InfStroke* stroke,
                       gdouble by_x,
                       gdouble by_y);

InfStroke*
inf_buffer_get_stroke_by_id(InfBuffer* buffer,
                            guint id);

G_END_DECLS

#endif /* __INF_BUFFER_H__ */
