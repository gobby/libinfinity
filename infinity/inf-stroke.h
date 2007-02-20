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

#ifndef __INF_STROKE_H__
#define __INF_STROKE_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TYPE_STROKE                 (inf_stroke_get_type())

typedef struct _InfStroke InfStroke;

typedef struct _InfStrokePoint InfStrokePoint;
struct _InfStrokePoint {
  gdouble x;
  gdouble y;

  /* TODO: Add curve information, pressure, tilt */
};

struct _InfStroke {
  /* all fields are read-only */
  guint id; /* used by InfBuffer */

  InfStrokePoint* points;
  guint n_points;

  gdouble x;
  gdouble y;
};

GType
inf_stroke_get_type(void) G_GNUC_CONST;

InfStroke*
inf_stroke_new(void);

InfStroke*
inf_stroke_new_at(gdouble x,
                  gdouble y);

InfStroke*
inf_stroke_copy(const InfStroke* stroke);

void
inf_stroke_free(InfStroke* stroke);

/* TODO: Add methods to add points */

G_END_DECLS

#endif /* __INF_STROKE_H__ */
