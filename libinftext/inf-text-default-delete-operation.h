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

#ifndef __INF_TEXT_DEFAULT_DELETE_OPERATION_H__
#define __INF_TEXT_DEFAULT_DELETE_OPERATION_H__

#include <libinftext/inf-text-chunk.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION                 (inf_text_default_delete_operation_get_type())
#define INF_TEXT_DEFAULT_DELETE_OPERATION(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION, InfTextDefaultDeleteOperation))
#define INF_TEXT_DEFAULT_DELETE_OPERATION_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION, InfTextDefaultDeleteOperationClass))
#define INF_TEXT_IS_DEFAULT_DELETE_OPERATION(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION))
#define INF_TEXT_IS_DEFAULT_DELETE_OPERATION_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION))
#define INF_TEXT_DEFAULT_DELETE_OPERATION_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION, InfTextDefaultDeleteOperationClass))

typedef struct _InfTextDefaultDeleteOperation InfTextDefaultDeleteOperation;
typedef struct _InfTextDefaultDeleteOperationClass
  InfTextDefaultDeleteOperationClass;

struct _InfTextDefaultDeleteOperationClass {
  GObjectClass parent_class;
};

struct _InfTextDefaultDeleteOperation {
  GObject parent;
};

GType
inf_text_default_delete_operation_get_type(void) G_GNUC_CONST;

InfTextDefaultDeleteOperation*
inf_text_default_delete_operation_new(guint position,
                                      InfTextChunk* chunk);

InfTextChunk*
inf_text_default_delete_operation_get_chunk(
  InfTextDefaultDeleteOperation* operation);

G_END_DECLS

#endif /* __INF_TEXT_DEFAULT_DELETE_OPERATION_H__ */

/* vim:set et sw=2 ts=2: */
