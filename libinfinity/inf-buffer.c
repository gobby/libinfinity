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

#include <libinfinity/inf-buffer.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfBufferPrivate InfBufferPrivate;
struct _InfBufferPrivate {
  guint id_counter; /* ID given to the next stroke */
  GHashTable* strokes; /* id->stroke */
};

enum {
  STROKE_ADD,
  STROKE_REMOVE,
  STROKE_MOVE,

  LAST_SIGNAL
};

#define INF_BUFFER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_BUFFER, InfBufferPrivate))

static GObjectClass* parent_class;
static guint buffer_signals[LAST_SIGNAL];

static void
inf_buffer_init(GTypeInstance* instance,
                gpointer g_class)
{
  InfBuffer* stream;
  InfBufferPrivate* priv;

  stream = INF_BUFFER(instance);
  priv = INF_BUFFER_PRIVATE(stream);

  priv->id_counter = 1;
  priv->strokes = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)inf_stroke_free
  );
}

static void
inf_buffer_finalize(GObject* object)
{
  InfBuffer* stream;
  InfBufferPrivate* priv;

  stream = INF_BUFFER(object);
  priv = INF_BUFFER_PRIVATE(stream);

  g_hash_table_destroy(priv->strokes);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_buffer_stroke_add(InfBuffer* buffer,
                      InfStroke* stroke)
{
  InfBufferPrivate* priv;
  priv = INF_BUFFER_PRIVATE(buffer);

  if(stroke->id == 0)
    stroke->id = priv->id_counter;

  g_assert(
    g_hash_table_lookup(priv->strokes, GUINT_TO_POINTER(stroke->id)) == NULL
  );

  g_hash_table_insert(priv->strokes, GUINT_TO_POINTER(stroke->id), stroke);

  if(stroke->id >= priv->id_counter)
    priv->id_counter = stroke->id + 1;
}

static void
inf_buffer_stroke_remove(InfBuffer* buffer,
                         InfStroke* stroke)
{
  InfBufferPrivate* priv;
  priv = INF_BUFFER_PRIVATE(buffer);

  g_assert(
    g_hash_table_lookup(priv->strokes, GUINT_TO_POINTER(stroke->id)) == stroke
  );

  g_hash_table_remove(priv->strokes, GUINT_TO_POINTER(stroke->id));
}

static void
inf_buffer_stroke_move(InfBuffer* buffer,
                       InfStroke* stroke,
                       gdouble by_x,
                       gdouble by_y)
{
  InfBufferPrivate* priv;
  priv = INF_BUFFER_PRIVATE(buffer);

  g_assert(
    g_hash_table_lookup(priv->strokes, GUINT_TO_POINTER(stroke->id)) == stroke
  );

  stroke->x += by_x;
  stroke->y += by_y;
}

static void
inf_buffer_class_init(gpointer g_class,
                      gpointer class_data)
{
  GObjectClass* object_class;
  InfBufferClass* buffer_class;

  object_class = G_OBJECT_CLASS(g_class);
  buffer_class = INF_BUFFER_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfBufferPrivate));

  object_class->finalize = inf_buffer_finalize;

  buffer_class->stroke_add = inf_buffer_stroke_add;
  buffer_class->stroke_remove = inf_buffer_stroke_remove;
  buffer_class->stroke_move = inf_buffer_stroke_move;

  buffer_signals[STROKE_ADD] = g_signal_new(
    "stroke-add",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBufferClass, stroke_add),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INF_TYPE_STROKE
  );

  /* Actually, I would like to remove the stroke from the hash table during
   * the RUN_LAST state and free the InfStroke in RUN_CLEANUP so that
   * people connecting with G_SIGNAL_CONNECT_AFTER still have a valid
   * stroke that is no longer in the hash table. However, for this to work it
   * would be required to find out the current emission state in the default
   * signal handler, so the object is also taken out from the hash table
   * in RUN_CLEANUP state. */
  buffer_signals[STROKE_REMOVE] = g_signal_new(
    "stroke-remove",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_CLEANUP,
    G_STRUCT_OFFSET(InfBufferClass, stroke_remove),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INF_TYPE_STROKE
  );

  buffer_signals[STROKE_MOVE] = g_signal_new(
    "stroke-move",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfBufferClass, stroke_move),
    NULL, NULL,
    inf_marshal_VOID__BOXED_DOUBLE_DOUBLE,
    G_TYPE_NONE,
    3,
    INF_TYPE_STROKE,
    G_TYPE_DOUBLE,
    G_TYPE_DOUBLE
  );
}

GType
inf_buffer_get_type(void)
{
  static GType buffer_type = 0;

  if(!buffer_type)
  {
    static const GTypeInfo buffer_type_info = {
      sizeof(InfBufferClass),  /* class_size */
      NULL,                    /* base_init */
      NULL,                    /* base_finalize */
      inf_buffer_class_init,   /* class_init */
      NULL,                    /* class_finalize */
      NULL,                    /* class_data */
      sizeof(InfBuffer),       /* instance_size */
      0,                       /* n_preallocs */
      inf_buffer_init,         /* instance_init */
      NULL                     /* value_table */
    };

    buffer_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfBuffer",
      &buffer_type_info,
      0
    );
  }

  return buffer_type;
}

/** inf_buffer_new:
 *
 * Creates a new buffer that contains #InfStroke objects.
 *
 * Return Value: A new #InfBuffer.
 **/
InfBuffer*
inf_buffer_new(void)
{
  GObject* object;

  object = g_object_new(INF_TYPE_BUFFER, NULL);

  return INF_BUFFER(object);
}

/** inf_buffer_add_stroke:
 *
 * @buffer: A #InfBuffer.
 * @stroke: A #InfStroke to add to @buffer.
 *
 * Adds @stroke to @buffer. If storke->id is 0, then the buffer chooses one
 * automatically. If it is set, make sure that the ID is not in use
 * already by another stroke in the buffe before calling this function. The
 * set ID is taken in this case.
 *
 * This function takes ownership of @stroke.
 **/
void
inf_buffer_add_stroke(InfBuffer* buffer,
                      InfStroke* stroke)
{
  InfBufferPrivate* priv;

  g_return_if_fail(INF_IS_BUFFER(buffer));
  g_return_if_fail(stroke != NULL);

  priv = INF_BUFFER_PRIVATE(buffer);

  g_return_if_fail(
    g_hash_table_lookup(priv->strokes, GUINT_TO_POINTER(stroke->id)) == NULL
  );

  g_signal_emit(G_OBJECT(buffer), buffer_signals[STROKE_ADD], 0, stroke);
}

/** inf_buffer_remove_stroke:
 *
 * @buffer: A #InfBuffer.
 * @stroke: A #InfStroke contained in @buffer.
 *
 * Removes @stroke from @buffer.
 **/
void
inf_buffer_remove_stroke(InfBuffer* buffer,
                         InfStroke* stroke)
{
  InfBufferPrivate* priv;

  g_return_if_fail(INF_IS_BUFFER(buffer));
  g_return_if_fail(stroke != NULL);

  priv = INF_BUFFER_PRIVATE(buffer);

  g_return_if_fail(
    g_hash_table_lookup(priv->strokes, GUINT_TO_POINTER(stroke->id)) == stroke
  );

  g_signal_emit(G_OBJECT(buffer), buffer_signals[STROKE_REMOVE], 0, stroke);
}

/** inf_buffer_move_stroke:
 *
 * @buffer: A #InfBuffer.
 * @stroke: A #InfStroke contained in @buffer.
 * @by_x: Relative movement on the X axis.
 * @by_y: Relative movement on the Y axis.
 *
 * Moves @stroke to another position within @buffer. Movement is performed
 * relatively to the current position of @stroke.
 **/
void inf_buffer_move_stroke(InfBuffer* buffer,
                            InfStroke* stroke,
                            gdouble by_x,
                            gdouble by_y)
{
  InfBufferPrivate* priv;

  g_return_if_fail(INF_IS_BUFFER(buffer));
  g_return_if_fail(stroke != NULL);

  priv = INF_BUFFER_PRIVATE(buffer);

  g_return_if_fail(
    g_hash_table_lookup(priv->strokes, GUINT_TO_POINTER(stroke->id)) == stroke
  );

  g_signal_emit(
    G_OBJECT(buffer),
    buffer_signals[STROKE_MOVE],
    0,
    stroke,
    by_x,
    by_y
  );
}

/** inf_buffer_get_stroke_by_id:
 *
 * @buffer: A #InfBuffer.
 * @id: ID to lookup.
 *
 * Looks up an #InfStroke contained in @buffer with the given ID.
 *
 * Return Value: An #InfStroke with matching ID, or %NULL.
 **/
InfStroke*
inf_buffer_get_stroke_by_id(InfBuffer* buffer,
                            guint id)
{
  InfBufferPrivate* priv;

  g_return_val_if_fail(INF_IS_BUFFER(buffer), NULL);

  priv = INF_BUFFER_PRIVATE(buffer);

  return g_hash_table_lookup(priv->strokes, GUINT_TO_POINTER(id));
}
