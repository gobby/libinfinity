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

#include <libinfinity/inf-ink-stroke.h>

#include <string.h>

GType
inf_ink_stroke_get_type(void)
{
  static GType ink_stroke_type = 0;

  if(!ink_stroke_type)
  {
    ink_stroke_type = g_boxed_type_register_static(
      "InfInkStroke",
      (GBoxedCopyFunc)inf_ink_stroke_copy,
      (GBoxedFreeFunc)inf_ink_stroke_free
    );
  }

  return ink_stroke_type;
}

/** inf_ink_stroke_new:
 *
 * Creates a new #InfInkStroke object with unassigned ID (0) and no points.
 *
 * Return Value: A #InfInkStroke object.
 **/
InfInkStroke*
inf_ink_stroke_new(void)
{
  InfInkStroke* ink_stroke;
  ink_stroke = g_new(InfInkStroke, 1);

  ink_stroke->id = 0;
  ink_stroke->points = NULL;
  ink_stroke->n_points = 0;

  ink_stroke->x = 0.0;
  ink_stroke->y = 0.0;

  return ink_stroke;
}

/** inf_ink_stroke_new_at:
 *
 * @x: X position of the new ink_stroke.
 * @y: Y position of the new ink_stroke.
 *
 * Creates a new #InfInkStroke at the specified position. The coordinates of
 * the points within the ink_stroke are relative to the ink_stroke's position.
 *
 * Return Value: A new #InfInkStroke.
 **/
InfInkStroke*
inf_ink_stroke_new_at(gdouble x,
                  gdouble y)
{
  InfInkStroke* ink_stroke;
  ink_stroke = g_new(InfInkStroke, 1);

  ink_stroke->id = 0;
  ink_stroke->points = NULL;
  ink_stroke->n_points = 0;

  ink_stroke->x = x;
  ink_stroke->y = y;

  return ink_stroke;
}

/** inf_ink_stroke_copy:
 *
 * @ink_stroke: Source ink_stroke.
 *
 * Creates a deep copy of a #InfInkStroke. The function fails if
 * ink_stroke->id is nonzero because if @ink_stroke has an ID assigned, the
 * result of this function would be a ink_stroke with the same ID, but IDs
 * are supposed to be unique.
 *
 * Return Value: A copy of @ink_stroke.
 **/
InfInkStroke*
inf_ink_stroke_copy(const InfInkStroke* ink_stroke)
{
  InfInkStroke* new_ink_stroke;

  /* Only allow to copy ink_strokes with unassigned IDs. Otherwise, we would
   * result in two ink_strokes with the same ID, but IDs are supposed to be
   * unique. */
  g_return_val_if_fail(ink_stroke != NULL, NULL);
  g_return_val_if_fail(ink_stroke->id != 0, NULL);

  new_ink_stroke = g_new(InfInkStroke, 1);

  new_ink_stroke->id = 0;
  new_ink_stroke->points = g_new(InfInkStrokePoint, ink_stroke->n_points);
  new_ink_stroke->n_points = ink_stroke->n_points;

  new_ink_stroke->x = ink_stroke->x;
  new_ink_stroke->y = ink_stroke->y;

  if(ink_stroke->n_points > 0)
  {
    memcpy(
      new_ink_stroke->points,
      ink_stroke->points,
      ink_stroke->n_points * sizeof(InfInkStrokePoint)
    );
  }

  return new_ink_stroke;
}

/** inf_ink_stroke_free:
 *
 * @ink_stroke: The #InfInkStroke object to free.
 *
 * Frees an #InfInkStroke object allocated with inf_ink_stroke_new() or 
 * inf_ink_stroke_copy().
 **/
void
inf_ink_stroke_free(InfInkStroke* ink_stroke)
{
  g_return_if_fail(ink_stroke != NULL);

  g_free(ink_stroke->points);
  g_free(ink_stroke);
}
