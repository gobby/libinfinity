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
#include <libinfinity/adopted/inf-adopted-concurrency-warning.h>

static void
inf_text_delete_operation_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    initialized = TRUE;
  }
}

GType
inf_text_delete_operation_get_type(void)
{
  static GType delete_operation_type = 0;

  if(!delete_operation_type)
  {
    static const GTypeInfo delete_operation_info = {
      sizeof(InfTextDeleteOperationIface),    /* class_size */
      inf_text_delete_operation_base_init,    /* base_init */
      NULL,                                   /* base_finalize */
      NULL,                                   /* class_init */
      NULL,                                   /* class_finalize */
      NULL,                                   /* class_data */
      0,                                      /* instance_size */
      0,                                      /* n_preallocs */
      NULL,                                   /* instance_init */
      NULL                                    /* value_table */
    };

    delete_operation_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfTextDeleteOperation",
      &delete_operation_info,
      0
    );

    g_type_interface_add_prerequisite(
      delete_operation_type,
      INF_ADOPTED_TYPE_OPERATION
    );
  }

  return delete_operation_type;
}

/**
 * inf_text_delete_operation_get_position:
 * @operation: A #InfTextDeleteOperation.
 *
 * Returns the position at which @operation starts to delete dext.
 *
 * Return Value: The position of @operation.
 **/
guint
inf_text_delete_operation_get_position(InfTextDeleteOperation* operation)
{
  InfTextDeleteOperationIface* iface;

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
 * Return Value: The length of @operation.
 **/
guint
inf_text_delete_operation_get_length(InfTextDeleteOperation* operation)
{
  InfTextDeleteOperationIface* iface;

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
 * inf_text_delete_operation_get_concurrency_id:
 * @op: A #InfTextDeleteOperation.
 * @against: Another #InfAdoptedOperation.
 *
 * Returns a concurrency ID for transformation of @op against @against
 * (see inf_adopted_operation_get_concurrency_id() for further information).
 *
 * Returns: A concurrency ID between @op and @against.
 */
InfAdoptedConcurrencyId
inf_text_delete_operation_get_concurrency_id(InfTextDeleteOperation* op,
                                             InfAdoptedOperation* against)
{
  g_return_val_if_fail(
    INF_TEXT_IS_DELETE_OPERATION(op),
    INF_ADOPTED_CONCURRENCY_NONE
  );

  g_return_val_if_fail(
    INF_ADOPTED_IS_OPERATION(against),
    INF_ADOPTED_CONCURRENCY_NONE
  );

  _inf_adopted_concurrency_warning(INF_TEXT_TYPE_DELETE_OPERATION);
  return INF_ADOPTED_CONCURRENCY_NONE;
}

/**
 * inf_text_delete_operation_transform_insert:
 * @operation: A #InfTextDeleteOperation.
 * @against: A #InfTextInsertOperation.
 * @cid: The concurrency ID for the transformation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Return Value: A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_delete_operation_transform_insert(InfTextDeleteOperation* operation,
                                           InfTextInsertOperation* against,
                                           InfAdoptedConcurrencyId cid)
{
  InfTextDeleteOperationIface* iface;
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
 * @cid: The concurrency ID for the transformation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Return Value: A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_delete_operation_transform_delete(InfTextDeleteOperation* operation,
                                           InfTextDeleteOperation* against,
                                           InfAdoptedConcurrencyId cid)
{
  InfTextDeleteOperationIface* iface;
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
