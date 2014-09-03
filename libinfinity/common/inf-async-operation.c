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

/**
 * SECTION:inf-async-operation
 * @title: InfAsyncOperation
 * @short_description: Perform long-running operations in a separate thread
 * @include: libinfinity/inf-async-operation.h
 * @stability: Unstable
 *
 * #InfAsyncOperation is a simple mechanism to run some code in a separate
 * worker thread and then, once the result is computed, notify the main thread
 * about the result.
 **/

#include <libinfinity/common/inf-async-operation.h>
#include <libinfinity/inf-i18n.h>

struct _InfAsyncOperation {
  InfIo* io;
  InfIoDispatch* dispatch;
  GThread* thread;
  GMutex mutex;

  InfAsyncOperationRunFunc run_func;
  InfAsyncOperationDoneFunc done_func;
  gpointer user_data;

  gpointer run_data;
  GDestroyNotify run_notify;
};

static void
inf_async_operation_dispatch(gpointer data)
{
  InfAsyncOperation* op;
  op = (InfAsyncOperation*)data;

  g_mutex_lock(&op->mutex);
  op->dispatch = NULL;
  g_mutex_unlock(&op->mutex);

  if(op->done_func != NULL)
    op->done_func(op->run_data, op->user_data);

  if(op->run_notify != NULL)
    op->run_notify(op->run_data);

  op->run_data = NULL;
  op->run_notify = NULL;
  op->thread = NULL;
  g_mutex_clear(&op->mutex);

  inf_async_operation_free(op);
}

static gpointer
inf_async_operation_thread_start(gpointer data)
{
  InfAsyncOperation* op;
  op = (InfAsyncOperation*)data;

  op->run_func(&op->run_data, &op->run_notify, op->user_data);

  g_mutex_lock(&op->mutex);
  g_assert(op->dispatch == NULL);

  if(op->io != NULL)
  {
    op->dispatch = inf_io_add_dispatch(
      op->io,
      inf_async_operation_dispatch,
      op,
      NULL
    );

    g_mutex_unlock(&op->mutex);
  }
  else
  {
    if(op->run_notify != NULL)
      op->run_notify(op->run_data);

    g_mutex_unlock(&op->mutex);
    g_mutex_clear(&op->mutex);
    g_thread_unref(op->thread);
    g_slice_free(InfAsyncOperation, op);
  }

  return NULL;
}

static void
inf_async_operation_io_unref_func(gpointer user_data,
                                  GObject* where_the_object_was)
{
  g_error(
    "%s",
    _("InfIo object was deleted without InfAsyncOperation being freed! "
      "This is a programming error that leaves the program in an "
      "inconsistent state. Therefore, the program is aborted. Please "
      "fix your code.")
  );

  g_assert_not_reached();
}

/**
 * inf_async_operation_new:
 * @io: The #InfIo object used to pass back the result of the operation.
 * @run_func: A function to run asynchronously in a worker thread, computing
 * the result of the operation.
 * @done_func: A function to be called in the thread of @io once the result
 * is available.
 * @user_data: Additional user data to pass to both functions.
 *
 * This function creates a new #InfAsyncOperation. The function given by
 * @run_func will be run asynchronously in a worker thread. Once the function
 * finishes, its result is passed back to the main thread defined by @io, and
 * @done_func is called with the computed result in the main thread.
 *
 * To actually start the asynchronous operation, call
 * inf_async_operation_start(). This allows to save the returned value into
 * a structure before starting the operation, avoiding a potential race
 * condition if the asynchronous function finishes quickly.
 *
 * The asynchronous operation can be canceled by calling
 * inf_async_operation_free() on the returned #InfAsyncOperation object.
 * If the operation is not cancelled and after @done_func has been called,
 * the operation is freed automatically and must not be freed by the caller.
 * The caller must also keep a reference to @io while the operation is
 * running. Before dropping your reference to @io, make sure to free the
 * asynchronous operation. When the last reference to @io is dropped, the
 * operation is freed automatically, since it cannot pass back its result to
 * the main thread anymore.
 *
 * Returns: A new #InfAsyncOperation. Free with inf_async_operation_free() to
 * cancel the operation.
 */
InfAsyncOperation*
inf_async_operation_new(InfIo* io,
                        InfAsyncOperationRunFunc run_func,
                        InfAsyncOperationDoneFunc done_func,
                        gpointer user_data)
{
  InfAsyncOperation* op;

  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(run_func != NULL, NULL);
  g_return_val_if_fail(done_func != NULL, NULL);

  op = g_slice_new(InfAsyncOperation);

  op->io = io;
  op->dispatch = NULL;
  op->thread = NULL;

  op->run_func = run_func;
  op->done_func = done_func;
  op->user_data = user_data;

  op->run_data = NULL;
  op->run_notify = NULL;

  g_object_weak_ref(
    G_OBJECT(io),
    inf_async_operation_io_unref_func,
    op
  );

  return op;
}

/**
 * inf_async_operation_start:
 * @op: A #InfAsyncOperation.
 * @error: Location to store error information, if any.
 *
 * Starts the operation given in @op. The operation must have been created
 * before with inf_async_operation_new(). If the operation cannot be started,
 * @error is set and %FALSE is returned. In that case, the operation must not
 * be used anymore since it will be automatically freed.
 *
 * Returns: %TRUE on success or %FALSE if the operation could not be started.
 */
gboolean
inf_async_operation_start(InfAsyncOperation* op,
                          GError** error)
{
  g_return_val_if_fail(op != NULL, FALSE);
  g_return_val_if_fail(op->thread == NULL, FALSE);

  g_mutex_init(&op->mutex);
  g_mutex_lock(&op->mutex);

  op->thread = g_thread_try_new(
    "InfAsyncOperation",
    inf_async_operation_thread_start,
    op,
    error
  );

  if(op->thread == NULL)
  {
    g_mutex_unlock(&op->mutex);
    g_mutex_clear(&op->mutex);
    inf_async_operation_free(op);
    return FALSE;
  }

  g_mutex_unlock(&op->mutex);
  return TRUE;
}

/**
 * inf_async_operation_free:
 * @op: A #InfAsyncOperation.
 *
 * Frees the given asynchronous operation and cancels it if it is currently
 * running. This should only be called to cancel a running operation, or to
 * free an operation that has not been started. In all other cases, the
 * operation is freed automatically.
 */
void
inf_async_operation_free(InfAsyncOperation* op)
{
  g_return_if_fail(op != NULL);

  if(op->thread == NULL)
  {
    /* The async operation has not started yet,
     * or it has finished (dispatched) already. */
    g_assert(op->io != NULL);

    g_object_weak_unref(
      G_OBJECT(op->io),
      inf_async_operation_io_unref_func,
      op
    );

    g_slice_free(InfAsyncOperation, op);
  }
  else
  {
    g_mutex_lock(&op->mutex);
    g_assert(op->io != NULL);

    if(op->dispatch == NULL)
    {
      /* We have not dispatched yet, i.e. the worker thread is still running.
       * We keep the object alive, but remove the IO object, so that the
       * worker thread does not attempt to dispatch. This also allows to
       * unreference the IO object from this point onwards. The operation
       * object is deleted when the thread finishes. */
      g_object_weak_unref(
        G_OBJECT(op->io),
        inf_async_operation_io_unref_func,
        op
      );

      op->io = NULL;
      g_mutex_unlock(&op->mutex);
    }
    else
    {
      /* The dispatch has been set, i.e. the worker thread has finished, but
       * the main thread has not yet executed the dispatch function. We
       * cancel the dispatch and delete the operation object. */
      inf_io_remove_dispatch(op->io, op->dispatch);
      if(op->run_notify != NULL) op->run_notify(op->run_data);

      g_mutex_unlock(&op->mutex);
      g_mutex_clear(&op->mutex);
      g_thread_unref(op->thread);
      g_slice_free(InfAsyncOperation, op);
    }
  }
}

/* vim:set et sw=2 ts=2: */
