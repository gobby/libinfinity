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

#ifndef __INF_INK_STROKE_H__
#define __INF_INK_STROKE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_INK_STROKE                 (inf_ink_stroke_get_type())

typedef struct _InfInkStroke InfInkStroke;

typedef struct _InfInkStrokePoint InfInkStrokePoint;
struct _InfInkStrokePoint {
  gdouble x;
  gdouble y;

  /* TODO: Add curve information, pressure, tilt */
};

struct _InfInkStroke {
  /* all fields are read-only */
  guint id; /* used by InfInkBuffer implementations */

  InfInkStrokePoint* points;
  guint n_points;

  gdouble x;
  gdouble y;
};

GType
inf_ink_stroke_get_type(void) G_GNUC_CONST;

InfInkStroke*
inf_ink_stroke_new(void);

InfInkStroke*
inf_ink_stroke_new_at(gdouble x,
                      gdouble y);

InfInkStroke*
inf_ink_stroke_copy(const InfInkStroke* ink_stroke);

void
inf_ink_stroke_free(InfInkStroke* ink_stroke);

/* TODO: Add methods to add points */

G_END_DECLS

#endif /* __INF_INK_STROKE_H__ */
