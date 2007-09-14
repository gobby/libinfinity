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

#ifndef __INF_TEXT_INSERT_OPERATION_H__
#define __INF_TEXT_INSERT_OPERATION_H__

#include <libinftext/inf-text-chunk.h>
#include <libinftext/inf-text-pword.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_INSERT_OPERATION                 (inf_text_insert_operation_get_type())
#define INF_TEXT_INSERT_OPERATION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_INSERT_OPERATION, InfTextInsertOperation))
#define INF_TEXT_INSERT_OPERATION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_TYPE_INSERT_OPERATION, InfTextInsertOperationClass))
#define INF_TEXT_IS_INSERT_OPERATION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_INSERT_OPERATION))
#define INF_TEXT_IS_INSERT_OPERATION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_TYPE_INSERT_OPERATION))
#define INF_TEXT_INSERT_OPERATION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_TYPE_INSERT_OPERATION, InfTextInsertOperationClass))

typedef struct _InfTextInsertOperation InfTextInsertOperation;
typedef struct _InfTextInsertOperationClass InfTextInsertOperationClass;

struct _InfTextInsertOperationClass {
  GObjectClass parent_class;
};

struct _InfTextInsertOperation {
  GObject parent;
};

GType
inf_text_insert_operation_get_type(void) G_GNUC_CONST;

InfTextInsertOperation*
inf_text_insert_operation_new(guint pos,
                              InfTextChunk* chunk,
                              guint concurrency_id);

InfTextPword*
inf_text_insert_operation_get_pword(InfTextInsertOperation* operation);

InfTextChunk*
inf_text_insert_operation_get_chunk(InfTextInsertOperation* operation);

guint
inf_text_insert_operation_get_concurrency_id(InfTextInsertOperation* op);

G_END_DECLS

#endif /* __INF_TEXT_INSERT_OPERATION_H__ */

/* vim:set et sw=2 ts=2: */
