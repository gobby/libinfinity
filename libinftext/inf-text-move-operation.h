/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_MOVE_OPERATION_H__
#define __INF_TEXT_MOVE_OPERATION_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_MOVE_OPERATION                 (inf_text_move_operation_get_type())
#define INF_TEXT_MOVE_OPERATION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_MOVE_OPERATION, InfTextMoveOperation))
#define INF_TEXT_MOVE_OPERATION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_TYPE_MOVE_OPERATION, InfTextMoveOperationClass))
#define INF_TEXT_IS_MOVE_OPERATION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_MOVE_OPERATION))
#define INF_TEXT_IS_MOVE_OPERATION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_TYPE_MOVE_OPERATION))
#define INF_TEXT_MOVE_OPERATION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_TYPE_MOVE_OPERATION, InfTextMoveOperationClass))

typedef struct _InfTextMoveOperation InfTextMoveOperation;
typedef struct _InfTextMoveOperationClass
  InfTextMoveOperationClass;

struct _InfTextMoveOperationClass {
  GObjectClass parent_class;
};

struct _InfTextMoveOperation {
  GObject parent;
};

GType
inf_text_move_operation_get_type(void) G_GNUC_CONST;

InfTextMoveOperation*
inf_text_move_operation_new(guint position,
                            gint length);

guint
inf_text_move_operation_get_position(InfTextMoveOperation* operation);

gint
inf_text_move_operation_get_length(InfTextMoveOperation* operation);

void
inf_text_move_operation_transform_insert(guint insert_position,
                                         guint insert_length,
                                         guint* move_position,
                                         gint* move_length,
                                         gboolean left_gravity);

void
inf_text_move_operation_transform_delete(guint delete_position,
                                         guint delete_length,
                                         guint* move_position,
                                         gint* move_length);

G_END_DECLS

#endif /* __INF_TEXT_MOVE_OPERATION_H__ */

/* vim:set et sw=2 ts=2: */
