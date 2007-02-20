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

#include <infinity/inf-stroke.h>

#include <string.h>

GType
inf_stroke_get_type(void)
{
  static GType stroke_type = 0;

  if(!stroke_type)
  {
    stroke_type = g_boxed_type_register_static(
      "InfStroke",
      (GBoxedCopyFunc)inf_stroke_copy,
      (GBoxedFreeFunc)inf_stroke_free
    );
  }

  return stroke_type;
}

/** inf_stroke_new:
 *
 * Creates a new #InfStroke object with unassigned ID (0) and no points.
 *
 * Return Value: A #InfStroke object.
 **/
InfStroke*
inf_stroke_new(void)
{
  InfStroke* stroke;
  stroke = g_new(InfStroke, 1);

  stroke->id = 0;
  stroke->points = NULL;
  stroke->n_points = 0;

  stroke->x = 0.0;
  stroke->y = 0.0;

  return stroke;
}

/** inf_stroke_new_at:
 *
 * @x: X position of the new stroke.
 * @y: Y position of the new stroke.
 *
 * Creates a new #InfStroke at the specified position. The coordinates of
 * the points within the stroke are relative to the stroke's position.
 *
 * Return Value: A new #InfStroke.
 **/
InfStroke*
inf_stroke_new_at(gdouble x,
                  gdouble y)
{
}

/** inf_stroke_copy:
 *
 * @stroke: Source stroke.
 *
 * Creates a deep copy of a #InfStroke. The function fails if stroke->id is
 * nonzero because if @stroke has an ID assigned, the result of this
 * function would be a stroke with the same ID, but IDs are supposed to
 * be unique.
 *
 * Return Value: A copy of @stroke.
 **/
InfStroke*
inf_stroke_copy(const InfStroke* stroke)
{
  InfStroke* new_stroke;

  /* Only allow to copy strokes with unassigned IDs. Otherwise, we would
   * result in two strokes with the same ID, but IDs are supposed to be
   * unique. */
  g_return_val_if_fail(stroke != NULL, NULL);
  g_return_val_if_fail(stroke->id != 0, NULL);

  new_stroke = g_new(InfStroke, 1);

  new_stroke->id = 0;
  new_stroke->points = g_new(InfStrokePoint, stroke->n_points);
  new_stroke->n_points = stroke->n_points;

  new_stroke->x = stroke->x;
  new_stroke->y = stroke->y;

  if(stroke->n_points > 0)
  {
    memcpy(
      new_stroke->points,
      stroke->points,
      stroke->n_points * sizeof(InfStrokePoint)
    );
  }

  return new_stroke;
}

/** inf_stroke_free:
 *
 * @stroke: The #InfStroke object to free.
 *
 * Frees an #InfStroke object allocated with inf_stroke_new() or 
 * inf_stroke_copy().
 **/
void
inf_stroke_free(InfStroke* stroke)
{
  g_return_if_fail(stroke != NULL);

  g_free(stroke->points);
  g_free(stroke);
}
