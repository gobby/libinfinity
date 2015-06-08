/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_ADOPTED_UNDO_GROUPING_H__
#define __INF_ADOPTED_UNDO_GROUPING_H__

#include <libinfinity/adopted/inf-adopted-algorithm.h>
#include <libinfinity/adopted/inf-adopted-request.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_UNDO_GROUPING                 (inf_adopted_undo_grouping_get_type())
#define INF_ADOPTED_UNDO_GROUPING(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_UNDO_GROUPING, InfAdoptedUndoGrouping))
#define INF_ADOPTED_UNDO_GROUPING_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_ADOPTED_TYPE_UNDO_GROUPING, InfAdoptedUndoGroupingClass))
#define INF_ADOPTED_IS_UNDO_GROUPING(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_UNDO_GROUPING))
#define INF_ADOPTED_IS_UNDO_GROUPING_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_ADOPTED_TYPE_UNDO_GROUPING))
#define INF_ADOPTED_UNDO_GROUPING_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_ADOPTED_TYPE_UNDO_GROUPING, InfAdoptedUndoGroupingClass))

typedef struct _InfAdoptedUndoGrouping InfAdoptedUndoGrouping;
typedef struct _InfAdoptedUndoGroupingClass InfAdoptedUndoGroupingClass;

/**
 * InfAdoptedUndoGroupingClass:
 * @group_requests: Default signal handler for the
 * #InfAdoptedUndoGrouping::group-requests signal.
 *
 * This structure contains default signal handlers for #InfAdoptedUndoGrouping.
 */
struct _InfAdoptedUndoGroupingClass {
  /*< private >*/
  GObjectClass parent_class;

  /*< public >*/
  gboolean (*group_requests)(InfAdoptedUndoGrouping* grouping,
                             InfAdoptedRequest* first,
                             InfAdoptedRequest* second);
};

/**
 * InfAdoptedUndoGrouping:
 *
 * #InfAdoptedUndoGrouping is an opaque data type. You should only access it via
 * the public API functions.
 */
struct _InfAdoptedUndoGrouping {
  /*< private >*/
  GObject parent;
};

GType
inf_adopted_undo_grouping_get_type(void) G_GNUC_CONST;

InfAdoptedUndoGrouping*
inf_adopted_undo_grouping_new(void);

InfAdoptedAlgorithm*
inf_adopted_undo_grouping_get_algorithm(InfAdoptedUndoGrouping* grouping);

void
inf_adopted_undo_grouping_set_algorithm(InfAdoptedUndoGrouping* grouping,
                                       InfAdoptedAlgorithm* algorithm,
                                       InfAdoptedUser* user);

void
inf_adopted_undo_grouping_start_group(InfAdoptedUndoGrouping* grouping,
                                      gboolean allow_group_with_prev);

void
inf_adopted_undo_grouping_end_group(InfAdoptedUndoGrouping* grouping,
                                    gboolean allow_group_with_next);

guint
inf_adopted_undo_grouping_get_undo_size(InfAdoptedUndoGrouping* grouping);

guint
inf_adopted_undo_grouping_get_redo_size(InfAdoptedUndoGrouping* grouping);

G_END_DECLS

#endif /* __INF_ADOPTED_UNDO_GROUPING_H__ */

/* vim:set et sw=2 ts=2: */
