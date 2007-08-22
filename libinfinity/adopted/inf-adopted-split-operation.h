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

#ifndef __INF_ADOPTED_SPLIT_OPERATION_H__
#define __INF_ADOPTED_SPLIT_OPERATION_H__

#include <libinfinity/adopted/inf-adopted-operation.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_ADOPTED_TYPE_SPLIT_OPERATION                 (inf_adopted_split_operation_get_type())
#define INF_ADOPTED_SPLIT_OPERATION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_ADOPTED_TYPE_SPLIT_OPERATION, InfAdoptedSplitOperation))
#define INF_ADOPTED_SPLIT_OPERATION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_ADOPTED_TYPE_SPLIT_OPERATION, InfAdoptedSplitOperationClass))
#define INF_ADOPTED_IS_SPLIT_OPERATION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_ADOPTED_TYPE_SPLIT_OPERATION))
#define INF_ADOPTED_IS_SPLIT_OPERATION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_ADOPTED_TYPE_SPLIT_OPERATION))
#define INF_ADOPTED_SPLIT_OPERATION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_ADOPTED_TYPE_SPLIT_OPERATION, InfAdoptedSplitOperationClass))

typedef struct _InfAdoptedSplitOperation InfAdoptedSplitOperation;
typedef struct _InfAdoptedSplitOperationClass InfAdoptedSplitOperationClass;

struct _InfAdoptedSplitOperationClass {
  GObjectClass parent_class;
};

struct _InfAdoptedSplitOperation {
  GObject parent;
};

GType
inf_adopted_split_operation_get_type(void) G_GNUC_CONST;

InfAdoptedSplitOperation*
inf_adopted_split_operation_new(InfAdoptedOperation* first,
                                InfAdoptedOperation* second);

GSList*
inf_adopted_split_operation_unsplit(InfAdoptedSplitOperation* operation);

G_END_DECLS

#endif /* __INF_ADOPTED_SPLIT_OPERATION_H__ */

/* vim:set et sw=2 ts=2: */
