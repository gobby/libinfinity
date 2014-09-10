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

#ifndef __INF_ASYNC_OPERATION_H__
#define __INF_ASYNC_OPERATION_H__

#include <libinfinity/common/inf-io.h>

#include <glib.h>

G_BEGIN_DECLS

/**
 * InfAsyncOperationRunFunc:
 * @run_data: Location where to write the result of the asynchronous
 * operation.
 * @run_notify: Function to be used to free @run_data, or %NULL.
 * @user_data: Data passed in inf_async_operation_new().
 *
 * This function performs the asynchronous task and is executed in a separate
 * thread. The pointer written into @run_data is passed back to the main
 * thread after the function has finished executing.
 */
typedef void(*InfAsyncOperationRunFunc)(gpointer* run_data,
                                        GDestroyNotify* run_notify,
                                        gpointer user_data);

/**
 * InfAsyncOperationDoneFunc:
 * @run_data: The result of the asynchronous operation.
 * @user_data: Data passed in inf_async_operation_new().
 *
 * This function is called in the main thread once the asynchronous operation
 * has finished.
 */
typedef void(*InfAsyncOperationDoneFunc)(gpointer run_data,
                                         gpointer user_data);

/**
 * InfAsyncOperation: (foreign)
 *
 * #InfAsyncOperation is an opaque data type and should only be accessed via
 * the public API functions.
 */
typedef struct _InfAsyncOperation InfAsyncOperation;

InfAsyncOperation*
inf_async_operation_new(InfIo* io,
                        InfAsyncOperationRunFunc run_func,
                        InfAsyncOperationDoneFunc done_func,
                        gpointer user_data);

gboolean
inf_async_operation_start(InfAsyncOperation* op,
                          GError** error);

void
inf_async_operation_free(InfAsyncOperation* op);

G_END_DECLS

#endif /* __INF_ASYNC_OPERATION_H__ */

/* vim:set et sw=2 ts=2: */
