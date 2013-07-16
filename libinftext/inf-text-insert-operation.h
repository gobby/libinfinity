/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_INSERT_OPERATION_H__
#define __INF_TEXT_INSERT_OPERATION_H__

#include <libinftext/inf-text-operations.h>
#include <libinfinity/adopted/inf-adopted-operation.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_INSERT_OPERATION                 (inf_text_insert_operation_get_type())
#define INF_TEXT_INSERT_OPERATION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_INSERT_OPERATION, InfTextInsertOperation))
#define INF_TEXT_IS_INSERT_OPERATION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_INSERT_OPERATION))
#define INF_TEXT_INSERT_OPERATION_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TEXT_TYPE_INSERT_OPERATION, InfTextInsertOperationIface))

typedef struct _InfTextInsertOperationIface InfTextInsertOperationIface;

struct _InfTextInsertOperationIface {
  GTypeInterface parent;

  /* Virtual table */
  guint(*get_position)(InfTextInsertOperation* operation);

  guint(*get_length)(InfTextInsertOperation* operation);

  InfTextInsertOperation*(*transform_position)(InfTextInsertOperation* op,
                                               guint position);
};

GType
inf_text_insert_operation_get_type(void) G_GNUC_CONST;

guint
inf_text_insert_operation_get_position(InfTextInsertOperation* operation);

guint
inf_text_insert_operation_get_length(InfTextInsertOperation* operation);

gboolean
inf_text_insert_operation_need_concurrency_id(InfTextInsertOperation* op,
                                              InfAdoptedOperation* against);

InfAdoptedConcurrencyId
inf_text_insert_operation_get_concurrency_id(InfTextInsertOperation* op,
                                             InfAdoptedOperation* against);

InfAdoptedOperation*
inf_text_insert_operation_transform_insert(InfTextInsertOperation* operation,
                                           InfTextInsertOperation* against,
                                           InfAdoptedConcurrencyId cid);

InfAdoptedOperation*
inf_text_insert_operation_transform_delete(InfTextInsertOperation* operation,
                                           InfTextDeleteOperation* against,
                                           InfAdoptedConcurrencyId cid);

G_END_DECLS

#endif /* __INF_TEXT_INSERT_OPERATION_H__ */

/* vim:set et sw=2 ts=2: */
