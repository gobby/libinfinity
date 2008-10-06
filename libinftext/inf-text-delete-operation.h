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

struct _InfTextDeleteOperationIface {
  GTypeInterface parent;

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

InfAdoptedConcurrencyId
inf_text_delete_operation_get_concurrency_id(InfTextDeleteOperation* op,
                                             InfAdoptedOperation* against);

InfAdoptedOperation*
inf_text_delete_operation_transform_insert(InfTextDeleteOperation* operation,
                                           InfTextInsertOperation* against,
                                           InfAdoptedConcurrencyId cid);

InfAdoptedOperation*
inf_text_delete_operation_transform_delete(InfTextDeleteOperation* operation,
                                           InfTextDeleteOperation* against,
                                           InfAdoptedConcurrencyId cid);

G_END_DECLS

#endif /* __INF_TEXT_DELETE_OPERATION_H__ */

/* vim:set et sw=2 ts=2: */
