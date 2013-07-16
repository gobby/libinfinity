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

#include <libinfinity/adopted/inf-adopted-operation.h>
#include <libinfinity/adopted/inf-adopted-split-operation.h>
#include <libinfinity/adopted/inf-adopted-user.h>

/**
 * SECTION:inf-adopted-operation
 * @title: InfAdoptedOperation
 * @short_description: Operation that can be processed by
 * #InfAdoptedAlgorithm.
 * @include: libinfinity/adopted/inf-adopted-operation.h
 * @see_also: #InfAdoptedRequest, #InfAdoptedAlgorithm
 * @stability: Unstable
 *
 * The #InfAdoptedOperation interface must be implemented by operations that
 * are supposed to be used by #InfAdoptedAlgorithm. They basically need to
 * define transformation rules for transformation against other operations.
 **/

static void
inf_adopted_operation_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    initialized = TRUE;
  }
}

GType
inf_adopted_concurrency_id_get_type(void)
{
  static GType concurrency_id_type = 0;

  if(!concurrency_id_type)
  {
    static const GEnumValue concurrency_id_type_values[] = {
      {
        INF_ADOPTED_CONCURRENCY_SELF,
        "INF_ADOPTED_CONCURRENCY_SELF",
        "self"
      }, {
        INF_ADOPTED_CONCURRENCY_NONE,
        "INF_ADOPTED_CONCURRENCY_NONE",
        "none"
      }, {
        INF_ADOPTED_CONCURRENCY_OTHER,
        "INF_ADOPTED_CONCURRENCY_OTHER",
        "other"
      }, {
        0,
        NULL,
        NULL
      }
    };

    concurrency_id_type = g_enum_register_static(
      "InfAdoptedConcurrencyId",
      concurrency_id_type_values
    );
  }

  return concurrency_id_type;
}

GType
inf_adopted_operation_flags_get_type(void)
{
  static GType operation_flags_type = 0;

  if(!operation_flags_type)
  {
    static const GFlagsValue operation_flags_type_values[] = {
      {
        INF_ADOPTED_OPERATION_AFFECTS_BUFFER,
        "INF_ADOPTED_OPERATION_AFFECTS_BUFFER",
        "affects_buffer",
      }, {
        INF_ADOPTED_OPERATION_REVERSIBLE,
        "INF_ADOPTED_OPERATION_REVERSIBLE",
        "reversible",
      }, {
        0,
        NULL,
        NULL
      }
    };

    operation_flags_type = g_flags_register_static(
      "InfAdoptedOperationFlags",
      operation_flags_type_values
    );
  }

  return operation_flags_type;
}

GType
inf_adopted_operation_get_type(void)
{
  static GType adopted_operation_type = 0;

  if(!adopted_operation_type)
  {
    static const GTypeInfo adopted_operation_info = {
      sizeof(InfAdoptedOperationIface),         /* class_size */
      inf_adopted_operation_base_init,          /* base_init */
      NULL,                                     /* base_finalize */
      NULL,                                     /* class_init */
      NULL,                                     /* class_finalize */
      NULL,                                     /* class_data */
      0,                                        /* instance_size */
      0,                                        /* n_preallocs */
      NULL,                                     /* instance_init */
      NULL                                      /* value_table */
    };

    adopted_operation_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfAdoptedOperation",
      &adopted_operation_info,
      0
    );

    g_type_interface_add_prerequisite(adopted_operation_type, G_TYPE_OBJECT);
  }

  return adopted_operation_type;
}

/**
 * inf_adopted_operation_need_concurrency_id:
 * @operation: The #InfAdoptedOperation to transform.
 * @against: The operation to transform against.
 *
 * This function returns whether transforming @operation against @against
 * is not defined unambiguously. In that case, transformation requires a
 * so-called concurrency ID which determines which of the two operations
 * is transformed.
 *
 * Returns: Whether transformation of @operation against @against requires a
 * concurrency ID to be defined.
 */
gboolean
inf_adopted_operation_need_concurrency_id(InfAdoptedOperation* operation,
                                          InfAdoptedOperation* against)
{
  InfAdoptedOperationIface* iface;

  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), FALSE);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(against), FALSE);

  iface = INF_ADOPTED_OPERATION_GET_IFACE(operation);
  g_assert(iface->need_concurrency_id != NULL);

  return iface->need_concurrency_id(operation, against);
}

/**
 * inf_adopted_operation_get_concurrency_id:
 * @operation: The #InfAdoptedOperation to transform.
 * @against: The operation to transform against.
 *
 * This function returns a concurrency ID for transformation of @operation
 * against @against. It always returns %INF_ADOPTED_CONCURRENCY_NONE when
 * inf_adopted_operation_need_concurrency_id() returns %TRUE for
 * @operation and @against (but that's not necessarily true the other way
 * around), since it is not possible to decide which operation to transform
 * without any additional information.
 *
 * However, the function can be called on the same operations in a previous
 * state. In some cases, a decision can be made based on those previous
 * operations. This can then be used as concurrency ID to call
 * inf_adopted_operation_transform().
 *
 * Note that the function is antisymmetric. If it returns
 * %INF_ADOPTED_CONCURRENCY_SELF, then it returns
 * %INF_ADOPTED_CONCURRENCY_OTHER for swapped arguments.
 *
 * Returns: A concurrency ID between @operation and @against. Can be
 * %INF_ADOPTED_CONCURRENCY_NONE in case no decision can be made.
 */
InfAdoptedConcurrencyId
inf_adopted_operation_get_concurrency_id(InfAdoptedOperation* operation,
                                         InfAdoptedOperation* against)
{
  InfAdoptedOperationIface* iface;
  InfAdoptedConcurrencyId id;

  g_return_val_if_fail(
    INF_ADOPTED_IS_OPERATION(operation),
    INF_ADOPTED_CONCURRENCY_NONE
  );
  g_return_val_if_fail(
    INF_ADOPTED_IS_OPERATION(against),
    INF_ADOPTED_CONCURRENCY_NONE
  );

  /* Use antisymmetricity if second argument is split operation, so that
   * subclasses don't need to handle that case explicitely. */
  if(!INF_ADOPTED_IS_SPLIT_OPERATION(operation) &&
     INF_ADOPTED_IS_SPLIT_OPERATION(against))
  {
    iface = INF_ADOPTED_OPERATION_GET_IFACE(against);
    g_assert(iface->get_concurrency_id != NULL);

    id = iface->get_concurrency_id(against, operation);
    switch(id)
    {
    case INF_ADOPTED_CONCURRENCY_SELF:
      return INF_ADOPTED_CONCURRENCY_OTHER;
    case INF_ADOPTED_CONCURRENCY_NONE:
      return INF_ADOPTED_CONCURRENCY_NONE;
    case INF_ADOPTED_CONCURRENCY_OTHER:
      return INF_ADOPTED_CONCURRENCY_SELF;
    default:
      g_assert_not_reached();
      return INF_ADOPTED_CONCURRENCY_NONE;
    }
  }
  else
  {
    iface = INF_ADOPTED_OPERATION_GET_IFACE(operation);
    g_assert(iface->get_concurrency_id != NULL);

    return iface->get_concurrency_id(operation, against);
  }
}


/**
 * inf_adopted_operation_transform:
 * @operation: The #InfAdoptedOperation to transform.
 * @against: The operation to transform against.
 * @concurrency_id: The concurrency ID for the transformation.
 *
 * Performs an inclusion transformation of @operation against @against,
 * meaning that the effect of @against is included in @operation.
 *
 * If inf_adopted_operation_need_concurrency_id() returns %TRUE for @operation
 * and @against, then @concurrency_id must not be
 * %INF_ADOPTED_CONCURRENCY_NONE. Otherwise, the parameter is ignored.
 *
 * Return Value: The transformed #InfAdoptedOperation, or %NULL if the
 * transformation failed.
 **/
InfAdoptedOperation*
inf_adopted_operation_transform(InfAdoptedOperation* operation,
                                InfAdoptedOperation* against,
                                InfAdoptedConcurrencyId concurrency_id)
{
  InfAdoptedOperationIface* iface;

  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(against), NULL);

  /* Transform against both parts of split operation if we are transforming
   * against split operation. */
  if(INF_ADOPTED_IS_SPLIT_OPERATION(against))
  {
    return inf_adopted_split_operation_transform_other(
      INF_ADOPTED_SPLIT_OPERATION(against),
      operation,
      concurrency_id
    );
  }
  else
  {
    iface = INF_ADOPTED_OPERATION_GET_IFACE(operation);
    g_assert(iface->transform != NULL);

    return (*iface->transform)(operation, against, concurrency_id);
  }
}

/**
 * inf_adopted_operation_copy:
 * @operation: The #InfAdoptedOperation to copy.
 *
 * Returns a copy of @operation.
 *
 * Return Value: A copy of @operation.
 **/
InfAdoptedOperation*
inf_adopted_operation_copy(InfAdoptedOperation* operation)
{
  InfAdoptedOperationIface* iface;

  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), NULL);

  iface = INF_ADOPTED_OPERATION_GET_IFACE(operation);

  g_assert(iface->copy != NULL);
  return (*iface->copy)(operation);
}

/**
 * inf_adopted_operation_get_flags:
 * @operation: A #InfAdoptedOperation.
 *
 * Returns the flags for @operation.
 *
 * Return Value: #InfAdoptedOperationFlags for @operation.
 **/
InfAdoptedOperationFlags
inf_adopted_operation_get_flags(InfAdoptedOperation* operation)
{
  InfAdoptedOperationIface* iface;

  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), 0);

  iface = INF_ADOPTED_OPERATION_GET_IFACE(operation);

  if(iface->get_flags != NULL)
    return (*iface->get_flags)(operation);
  else
    return 0;
}

/**
 * inf_adopted_operation_apply:
 * @operation: A #InfAdoptedOperation.
 * @by: A #InfAdoptedUser.
 * @buffer: The #InfBuffer to apply the operation to.
 *
 * Applies @operation to @buffer. The operation is considered to be applied by
 * user @by.
 **/
void
inf_adopted_operation_apply(InfAdoptedOperation* operation,
                            InfAdoptedUser* by,
                            InfBuffer* buffer)
{
  InfAdoptedOperationIface* iface;

  g_return_if_fail(INF_ADOPTED_IS_OPERATION(operation));
  g_return_if_fail(INF_ADOPTED_IS_USER(by));
  g_return_if_fail(INF_IS_BUFFER(buffer));

  iface = INF_ADOPTED_OPERATION_GET_IFACE(operation);

  /* apply must be implemented */
  g_assert(iface->apply != NULL);
  (*iface->apply)(operation, by, buffer);
}

/**
 * inf_adopted_operation_is_reversible:
 * @operation: A #InfAdoptedOperation.
 *
 * Returns whether @operation is reversible.
 *
 * Return Value: Whether @operation is reversible.
 **/
gboolean
inf_adopted_operation_is_reversible(InfAdoptedOperation* operation)
{
  InfAdoptedOperationFlags flags;

  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), FALSE);

  flags = inf_adopted_operation_get_flags(operation);
  if( (flags & INF_ADOPTED_OPERATION_REVERSIBLE) != 0)
    return TRUE;
  else
    return FALSE;
}

/**
 * inf_adopted_operation_revert:
 * @operation: A #InfAdoptedOperation.
 *
 * Returns a new #InfAdoptedOperation that undoes the effect of @operation. If
 * @operation and then its reverse operation are applied to a buffer (in that
 * order), the buffer remains unchanged.
 *
 * @operation must be reversible for this function to be called (i.e.
 * inf_adopted_operation_is_reversible() must return TRUE).
 *
 * Return Value: The reverse operation of @operation.
 **/
InfAdoptedOperation*
inf_adopted_operation_revert(InfAdoptedOperation* operation)
{
  InfAdoptedOperationIface* iface;

  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), NULL);
  g_assert(inf_adopted_operation_is_reversible(operation) == TRUE);

  iface = INF_ADOPTED_OPERATION_GET_IFACE(operation);

  /* When inf_adopted_operation_is_reversible() returns TRUE for an operation
   * it must implement revert. */
  g_assert(iface->revert != NULL);
  return (*iface->revert)(operation);
}

/**
 * inf_adopted_operation_make_reversible:
 * @operation: A #InfAdoptedOperation.
 * @with: Another #InfAdoptedOperation that emerged from @operation by
 * transforming it.
 * @buffer: A #InfBuffer.
 *
 * Some operations may not be reversible, but can be made reversible with
 * some extra information such as another operation that collected
 * enough information while being transformed, and the current buffer.
 *
 * This function can only be called when @operation is not yet reversible
 * and returns a new operation that has the same effect as @operation, but is
 * reversible.
 *
 * For example, an operation that deletes some range of text in a text editor
 * is not reversible if it only stores the position and length of the range,
 * but can be made reversible when it looks up what there is at that position
 * in the buffer.
 *
 * Return Value: A reversible #InfAdoptedOperation, or %NULL if @operation
 * cannot be made reversible with the given transformed operation @with and
 * @buffer.
 **/
InfAdoptedOperation*
inf_adopted_operation_make_reversible(InfAdoptedOperation* operation,
                                      InfAdoptedOperation* with,
                                      InfBuffer* buffer)
{
  InfAdoptedOperationIface* iface;

  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(with), NULL);
  g_return_val_if_fail(INF_IS_BUFFER(buffer), NULL);

  g_assert(inf_adopted_operation_is_reversible(operation) == FALSE);

  iface = INF_ADOPTED_OPERATION_GET_IFACE(operation);

  if(iface->make_reversible != NULL)
    return (*iface->make_reversible)(operation, with, buffer);
  else
    return NULL;
}

/* vim:set et sw=2 ts=2: */
