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

#ifndef __INF_TEXT_DELETE_OPERATION_H__
#define __INF_TEXT_DELETE_OPERATION_H__

#include <libinftext/inf-text-operations.h>
#include <libinfinity/adopted/inf-adopted-split-operation.h>
#include <libinfinity/adopted/inf-adopted-operation.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_DELETE_OPERATION                 (inf_text_delete_operation_get_type())
#define INF_TEXT_DELETE_OPERATION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_DELETE_OPERATION, InfTextDeleteOperation))
#define INF_TEXT_IS_DELETE_OPERATION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_DELETE_OPERATION))
#define INF_TEXT_DELETE_OPERATION_GET_IFACE(inst)      (G_TYPE_INSTANCE_GET_INTERFACE((inst), INF_TEXT_TYPE_DELETE_OPERATION, InfTextDeleteOperationIface))

typedef struct _InfTextDeleteOperationIface InfTextDeleteOperationIface;

/**
 * InfTextDeleteOperationIface:
 * @get_position: Virtual function to retrieve the start position of the
 * delete operation.
 * @get_length: Virtual function to retrieve the end position of the number
 * of characters removed by the delete operation.
 * @transform_position: Virtual function to transform the operation such that
 * the start position of the operation changes.
 * @transform_overlap: Virtual function to transform the operation against
 * another delete operation with overlapping regions.
 * @transform_split: Virtual function to transform the operation against an
 * insert operation such that this operation needs to be split in two.
 *
 * This structure contains virtual methods of the #InfTextDeleteOperation
 * interface.
 */
struct _InfTextDeleteOperationIface {
  /*< private >*/
  GTypeInterface parent;

  /*< public >*/

  /* Virtual table */
  guint(*get_position)(InfTextDeleteOperation* operation);

  guint(*get_length)(InfTextDeleteOperation* operation);

  InfTextDeleteOperation*(*transform_position)(InfTextDeleteOperation* op,
                                               guint position);

  InfTextDeleteOperation*(*transform_overlap)(InfTextDeleteOperation* op,
                                              InfTextDeleteOperation* other,
                                              guint position,
                                              guint begin,
                                              guint other_begin,
                                              guint length);

  InfAdoptedSplitOperation*(*transform_split)(InfTextDeleteOperation* op,
                                              guint split_pos,
                                              guint split_length);
};

GType
inf_text_delete_operation_get_type(void) G_GNUC_CONST;

guint
inf_text_delete_operation_get_position(InfTextDeleteOperation* operation);

guint
inf_text_delete_operation_get_length(InfTextDeleteOperation* operation);

gboolean
inf_text_delete_operation_need_concurrency_id(InfTextDeleteOperation* op,
                                              InfAdoptedOperation* against);

InfAdoptedOperation*
inf_text_delete_operation_transform_insert(InfTextDeleteOperation* operation,
                                           InfTextInsertOperation* against);

InfAdoptedOperation*
inf_text_delete_operation_transform_delete(InfTextDeleteOperation* operation,
                                           InfTextDeleteOperation* against);

G_END_DECLS

#endif /* __INF_TEXT_DELETE_OPERATION_H__ */

/* vim:set et sw=2 ts=2: */
