/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-text-delete-operation
 * @title: InfTextDeleteOperation
 * @short_description: Interface for an operation erasing text
 * @include: libinftext/inf-text-delete-operation.h
 * @see_also: #InfTextInsertOperation
 * @stability: Unstable
 *
 * #InfTextDeleteOperation is an interface for an operation removing text
 * from the document. It implements the transformation logic for
 * transformation against other delete operations or insert operations.
 *
 * This interface does not make any assumption on what kind of text is
 * removed, it works only with character offsets and lengths. This information
 * is enough to perform transformation of this operation or other operations
 * against this operation. Whether the actual operation only knows about the
 * offsets, too, or actually knows the text that is being erased (and if so,
 * in what representation), is up to the implementation.
 */

#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>

G_DEFINE_INTERFACE(InfTextDeleteOperation, inf_text_delete_operation, INF_ADOPTED_TYPE_OPERATION)

static void
inf_text_delete_operation_default_init(InfTextDeleteOperationInterface* iface)
{
}

/**
 * inf_text_delete_operation_get_position:
 * @operation: A #InfTextDeleteOperation.
 *
 * Returns the position at which @operation starts to delete dext.
 *
 * Returns: The position of @operation.
 **/
guint
inf_text_delete_operation_get_position(InfTextDeleteOperation* operation)
{
  InfTextDeleteOperationInterface* iface;

  g_return_val_if_fail(INF_TEXT_IS_DELETE_OPERATION(operation), 0);

  iface = INF_TEXT_DELETE_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->get_position != NULL, 0);

  return iface->get_position(operation);
}

/**
 * inf_text_delete_operation_get_length:
 * @operation: A #InfTextDeleteOperation.
 *
 * Returns the number of characters deleted by @operation.
 *
 * Returns: The length of @operation.
 **/
guint
inf_text_delete_operation_get_length(InfTextDeleteOperation* operation)
{
  InfTextDeleteOperationInterface* iface;

  g_return_val_if_fail(INF_TEXT_IS_DELETE_OPERATION(operation), 0);

  iface = INF_TEXT_DELETE_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->get_length != NULL, 0);

  return iface->get_length(operation);
}

/**
 * inf_text_delete_operation_need_concurrency_id:
 * @op: A #InfTextDeleteOperation.
 * @against: Another #InfAdoptedOperation.
 *
 * Returns whether transforming @op against @against requires a concurrency ID
 * (see inf_adopted_operation_need_concurrency_id() for further information).
 *
 * Returns: Whether transforming @op against @against requires a concurrency
 * ID.
 */
gboolean
inf_text_delete_operation_need_concurrency_id(InfTextDeleteOperation* op,
                                              InfAdoptedOperation* against)
{
  g_return_val_if_fail(INF_TEXT_IS_DELETE_OPERATION(op), FALSE);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(against), FALSE);

  return FALSE;
}

/**
 * inf_text_delete_operation_transform_insert:
 * @operation: A #InfTextDeleteOperation.
 * @against: A #InfTextInsertOperation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Returns: (transfer full): A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_delete_operation_transform_insert(InfTextDeleteOperation* operation,
                                           InfTextInsertOperation* against)
{
  InfTextDeleteOperationInterface* iface;
  guint own_pos;
  guint own_len;
  guint other_pos;
  guint other_len;

  g_return_val_if_fail(INF_TEXT_IS_DELETE_OPERATION(operation), NULL);
  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(against), NULL);

  iface = INF_TEXT_DELETE_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->transform_position != NULL, NULL);
  g_return_val_if_fail(iface->transform_split != NULL, NULL);

  own_pos = inf_text_delete_operation_get_position(operation);
  own_len = inf_text_delete_operation_get_length(operation);
  other_pos = inf_text_insert_operation_get_position(against);
  other_len = inf_text_insert_operation_get_length(against);

  if(other_pos >= own_pos + own_len)
  {
    return inf_adopted_operation_copy(INF_ADOPTED_OPERATION(operation));
  }
  else if(other_pos <= own_pos)
  {
    return INF_ADOPTED_OPERATION(
      iface->transform_position(operation, own_pos + other_len)
    );
  }
  else
  {
    return INF_ADOPTED_OPERATION(
      iface->transform_split(operation, other_pos - own_pos, other_len)
    );
  }
}

/**
 * inf_text_delete_operation_transform_delete:
 * @operation: A #InfTextDeleteOperation.
 * @against: Another #InfTextDeleteOperation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Returns: (transfer full): A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_delete_operation_transform_delete(InfTextDeleteOperation* operation,
                                           InfTextDeleteOperation* against)
{
  InfTextDeleteOperationInterface* iface;
  guint own_pos;
  guint own_len;
  guint other_pos;
  guint other_len;

  g_return_val_if_fail(INF_TEXT_IS_DELETE_OPERATION(operation), NULL);
  g_return_val_if_fail(INF_TEXT_IS_DELETE_OPERATION(against), NULL);

  iface = INF_TEXT_DELETE_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->transform_position != NULL, NULL);
  g_return_val_if_fail(iface->transform_overlap != NULL, NULL);

  own_pos = inf_text_delete_operation_get_position(operation);
  own_len = inf_text_delete_operation_get_length(operation);
  other_pos = inf_text_delete_operation_get_position(against);
  other_len = inf_text_delete_operation_get_length(against);

  if(own_pos + own_len <= other_pos)
  {
    return inf_adopted_operation_copy(INF_ADOPTED_OPERATION(operation));
  }
  else if(own_pos >= other_pos + other_len)
  {
    return INF_ADOPTED_OPERATION(
      iface->transform_position(operation, own_pos - other_len)
    );
  }
  /* Somehow overlapping now */
  else if(other_pos <= own_pos && other_pos + other_len >= own_pos + own_len)
  {
    return INF_ADOPTED_OPERATION(
      iface->transform_overlap(
        operation,
        against,
        other_pos,
        0,
        own_pos - other_pos,
        own_len
      )
    );
  }
  else if(other_pos <= own_pos && other_pos + other_len < own_pos + own_len)
  {
    return INF_ADOPTED_OPERATION(
      iface->transform_overlap(
        operation,
        against,
        other_pos,
        0,
        own_pos - other_pos,
        other_pos + other_len - own_pos
      )
    );
  }
  else if(other_pos > own_pos && other_pos + other_len >= own_pos + own_len)
  {
    return INF_ADOPTED_OPERATION(
      iface->transform_overlap(
        operation,
        against,
        own_pos,
        other_pos - own_pos,
        0,
        own_pos + own_len - other_pos
      )
    );
  }
  else
  {
    return INF_ADOPTED_OPERATION(
      iface->transform_overlap(
        operation,
        against,
        own_pos,
        other_pos - own_pos,
        0,
        other_len
      )
    );
  }
}

/* vim:set et sw=2 ts=2: */
