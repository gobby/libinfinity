/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_UNDO_GROUPING_H__
#define __INF_TEXT_UNDO_GROUPING_H__

#include <libinfinity/adopted/inf-adopted-undo-grouping.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_UNDO_GROUPING                 (inf_text_undo_grouping_get_type())
#define INF_TEXT_UNDO_GROUPING(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_UNDO_GROUPING, InfTextUndoGrouping))
#define INF_TEXT_UNDO_GROUPING_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_TYPE_UNDO_GROUPING, InfTextUndoGroupingClass))
#define INF_TEXT_IS_UNDO_GROUPING(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_UNDO_GROUPING))
#define INF_TEXT_IS_UNDO_GROUPING_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_TYPE_UNDO_GROUPING))
#define INF_TEXT_UNDO_GROUPING_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_TYPE_UNDO_GROUPING, InfTextUndoGroupingClass))

typedef struct _InfTextUndoGrouping InfTextUndoGrouping;
typedef struct _InfTextUndoGroupingClass InfTextUndoGroupingClass;

/**
 * InfATextUndoGroupingClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfTextUndoGroupingClass {
  /*< private >*/
  InfAdoptedUndoGroupingClass parent_class;
};

/**
 * InfTextUndoGrouping:
 *
 * #InfTextUndoGrouping is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfTextUndoGrouping {
  /*< private >*/
  InfAdoptedUndoGrouping parent;
};

GType
inf_text_undo_grouping_get_type(void);

InfTextUndoGrouping*
inf_text_undo_grouping_new(void);

G_END_DECLS

#endif /* __INF_TEXT_UNDO_GROUPING_H__ */

/* vim:set et sw=2 ts=2: */
