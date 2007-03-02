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

#include <libinfinity/inf-ink-buffer.h>
#include <libinfinity/inf-buffer.h>
#include <libinfinity/inf-marshal.h>

enum {
  ADD_STROKE,
  REMOVE_STROKE,
  MOVE_STROKE,

  LAST_SIGNAL
};

static guint ink_buffer_signals[LAST_SIGNAL];

static void
inf_ink_buffer_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;
  GObjectClass* object_class;

  object_class = G_OBJECT_CLASS(g_class);

  if(!initialized)
  {
    ink_buffer_signals[ADD_STROKE] = g_signal_new(
      "add-stroke",
      G_OBJECT_CLASS_TYPE(object_class),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfInkBufferIface, add_stroke),
      NULL, NULL,
      inf_marshal_VOID__BOXED,
      G_TYPE_NONE,
      1,
      INF_TYPE_INK_STROKE | G_SIGNAL_TYPE_STATIC_SCOPE
    );

    ink_buffer_signals[REMOVE_STROKE] = g_signal_new(
      "remove-stroke",
      G_OBJECT_CLASS_TYPE(object_class),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfInkBufferIface, remove_stroke),
      NULL, NULL,
      inf_marshal_VOID__BOXED,
      G_TYPE_NONE,
      1,
      INF_TYPE_INK_STROKE | G_SIGNAL_TYPE_STATIC_SCOPE
    );

    ink_buffer_signals[MOVE_STROKE] = g_signal_new(
      "move-stroke",
      G_OBJECT_CLASS_TYPE(object_class),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET(InfInkBufferIface, move_stroke),
      NULL, NULL,
      inf_marshal_VOID__BOXED_DOUBLE_DOUBLE,
      G_TYPE_NONE,
      3,
      INF_TYPE_INK_STROKE | G_SIGNAL_TYPE_STATIC_SCOPE,
      G_TYPE_DOUBLE,
      G_TYPE_DOUBLE
    );
  }
}

GType
inf_ink_buffer_get_type(void)
{
  static GType ink_buffer_type = 0;

  if(!ink_buffer_type)
  {
    static const GTypeInfo ink_buffer_info = {
      sizeof(InfInkBufferIface),     /* class_size */
      inf_ink_buffer_base_init,      /* base_init */
      NULL,                          /* base_finalize */
      NULL,                          /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      0,                             /* instance_size */
      0,                             /* n_preallocs */
      NULL,                          /* instance_init */
      NULL                           /* value_table */
    };

    ink_buffer_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfInkBuffer",
      &ink_buffer_info,
      0
    );

    g_type_interface_add_prerequisite(ink_buffer_type, INF_TYPE_BUFFER);
  }

  return ink_buffer_type;
}

/** inf_ink_buffer_add_stroke:
 *
 * @buffer: A #InfInkBuffer.
 * @stroke: A #InfInkStroke to add to @buffer.
 *
 * Adds @stroke to @buffer. If storke->id is 0, then the buffer chooses one
 * automatically. If it is set, make sure that the ID is not in use
 * already by another stroke in the buffe before calling this function. The
 * set ID is taken in this case.
 *
 * This function takes ownership of @stroke.
 **/
void
inf_ink_buffer_add_stroke(InfInkBuffer* buffer,
                          InfInkStroke* stroke)
{
  g_return_if_fail(INF_IS_INK_BUFFER(buffer));

  g_return_if_fail(
    inf_ink_buffer_get_stroke_by_id(buffer, stroke->id) != NULL
  );

  g_signal_emit(
    G_OBJECT(buffer),
    ink_buffer_signals[ADD_STROKE],
    0,
    stroke
  );
}

/** inf_ink_buffer_remove_stroke:
 *
 * @buffer: A #InfInkBuffer.
 * @stroke: A #InfInkStroke contained in @buffer.
 *
 * Removes @stroke from @buffer.
 **/
void
inf_ink_buffer_remove_stroke(InfInkBuffer* buffer,
                             InfInkStroke* stroke)
{
  g_return_if_fail(INF_IS_INK_BUFFER(buffer));

  g_return_if_fail(
    inf_ink_buffer_get_stroke_by_id(buffer, stroke->id) == stroke
  );

  g_object_ref(G_OBJECT(stroke));

  g_signal_emit(
    G_OBJECT(buffer),
    ink_buffer_signals[REMOVE_STROKE],
    0,
    stroke
  );

  g_object_unref(G_OBJECT(stroke));
}

/** inf_ink_buffer_move_stroke:
 *
 * @buffer: A #InfInkBuffer.
 * @stroke: A #InfInkStroke contained in @buffer.
 * @by_x: Relative movement on the X axis.
 * @by_y: Relative movement on the Y axis.
 *
 * Moves @stroke to another position within @buffer. Movement is performed
 * relatively to the current position of @stroke.
 **/
void
inf_ink_buffer_move_stroke(InfInkBuffer* buffer,
                           InfInkStroke* stroke,
                           gdouble by_x,
                           gdouble by_y)
{
  g_return_if_fail(INF_IS_INK_BUFFER(buffer));

  g_return_if_fail(
    inf_ink_buffer_get_stroke_by_id(buffer, stroke->id) == stroke
  );

  g_signal_emit(
    G_OBJECT(buffer),
    ink_buffer_signals[MOVE_STROKE],
    0,
    stroke,
    by_x,
    by_y
  );
}

/** inf_ink_buffer_get_stroke_by_id:
 *
 * @buffer: A #InfInkBuffer.
 * @id: ID to lookup.
 *
 * Looks up an #InfInkStroke contained in @buffer with the given ID.
 *
 * Return Value: An #InfInkStroke with matching ID, or %NULL.
 **/
InfInkStroke*
inf_ink_buffer_get_stroke_by_id(InfInkBuffer* buffer,
                                guint id)
{
  InfInkBufferIface* iface;

  g_return_val_if_fail(INF_IS_INK_BUFFER(buffer), NULL);

  iface = INF_INK_BUFFER_GET_IFACE(buffer);
  g_return_val_if_fail(iface->get_stroke_by_id != NULL, NULL);

  return iface->get_stroke_by_id(buffer, id);
}
