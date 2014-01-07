/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INF_TEXT_OPERATIONS_H__
#define __INF_TEXT_OPERATIONS_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* Forward declaration for inf-text-insert-operation.h and
 * inf-text-delete-operation.h each of which needs both. */

/**
 * InfTextOperationError:
 * @INF_TEXT_OPERATION_ERROR_INVALID_INSERT: A #InfTextInsertOperation
 * attempted to insert text after the end of the buffer.
 * @INF_TEXT_OPERATION_ERROR_INVALID_DELETE: A #InfTextDeleteOperation
 * attempted to delete text from after the end of the buffer.
 * @INF_TEXT_OPERATION_ERROR_INVALID_MOVE: A #InfTextMoveOperation attempted
 * to move the cursor of a user behind the end of the buffer.
 * @INF_TEXT_OPERATION_ERROR_FAILED: No further specified error code.
 *
 * Error codes that can occur when applying a #InfTextInsertOperation,
 * #InfTextDeleteOperation or #InfTextMoveOperation to the buffer.
 */
typedef enum _InfTextOperationError {
  INF_TEXT_OPERATION_ERROR_INVALID_INSERT,
  INF_TEXT_OPERATION_ERROR_INVALID_DELETE,
  INF_TEXT_OPERATION_ERROR_INVALID_MOVE,

  INF_TEXT_OPERATION_ERROR_FAILED
} InfTextOperationError;

/**
 * InfTextDeleteOperation:
 *
 * #InfTextDeleteOperation is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfTextDeleteOperation InfTextDeleteOperation;

/**
 * InfTextInsertOperation:
 *
 * #InfTextInsertOperation is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfTextInsertOperation InfTextInsertOperation;

G_END_DECLS

#endif /* __INF_TEXT_OPERATIONS_H__ */

/* vim:set et sw=2 ts=2: */
