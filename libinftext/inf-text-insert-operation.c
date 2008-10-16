/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinfinity/adopted/inf-adopted-concurrency-warning.h>

static void
inf_text_insert_operation_base_init(gpointer g_class)
{
  static gboolean initialized = FALSE;

  if(!initialized)
  {
    initialized = TRUE;
  }
}

GType
inf_text_insert_operation_get_type(void)
{
  static GType insert_operation_type = 0;

  if(!insert_operation_type)
  {
    static const GTypeInfo insert_operation_info = {
      sizeof(InfTextInsertOperationIface),    /* class_size */
      inf_text_insert_operation_base_init,    /* base_init */
      NULL,                                   /* base_finalize */
      NULL,                                   /* class_init */
      NULL,                                   /* class_finalize */
      NULL,                                   /* class_data */
      0,                                      /* instance_size */
      0,                                      /* n_preallocs */
      NULL,                                   /* instance_init */
      NULL                                    /* value_table */
    };

    insert_operation_type = g_type_register_static(
      G_TYPE_INTERFACE,
      "InfTextInsertOperation",
      &insert_operation_info,
      0
    );

    g_type_interface_add_prerequisite(
      insert_operation_type,
      INF_ADOPTED_TYPE_OPERATION
    );
  }

  return insert_operation_type;
}

/**
 * inf_text_insert_operation_get_position:
 * @operation: A #InfTextInsertOperation.
 *
 * Returns the position at which @operation inserts text.
 *
 * Return Value: The position to insert text.
 **/
guint
inf_text_insert_operation_get_position(InfTextInsertOperation* operation)
{
  InfTextInsertOperationIface* iface;

  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(operation), 0);

  iface = INF_TEXT_INSERT_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->get_position != NULL, 0);

  return iface->get_position(operation);
}

/**
 * inf_text_insert_operation_get_length:
 * @operation: A #InfTextInsertOperation.
 *
 * Returns the length of the text inserted by @operation.
 *
 * Return Value: The length of @operation.
 **/
guint
inf_text_insert_operation_get_length(InfTextInsertOperation* operation)
{
  InfTextInsertOperationIface* iface;

  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(operation), 0);

  iface = INF_TEXT_INSERT_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->get_length != NULL, 0);

  return iface->get_length(operation);
}

/**
 * inf_text_insert_operation_need_concurrency_id:
 * @op: A #InfTextInsertOperation.
 * @against: Another #InfAdoptedOperation.
 *
 * Returns whether transforming @op against @against requires a concurrency ID
 * (see inf_adopted_operation_need_concurrency_id() for further information).
 *
 * Returns: Whether transforming @op against @against requires a concurrency
 * ID.
 */
gboolean
inf_text_insert_operation_need_concurrency_id(InfTextInsertOperation* op,
                                              InfAdoptedOperation* against)
{
  InfTextInsertOperation* insert_against;

  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(op), FALSE);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(against), FALSE);

  if(INF_TEXT_IS_INSERT_OPERATION(against))
  {
    insert_against = INF_TEXT_INSERT_OPERATION(against);
    if(inf_text_insert_operation_get_position(op) ==
       inf_text_insert_operation_get_position(insert_against))
    {
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * inf_text_insert_operation_get_concurrency_id:
 * @op: A #InfTextInsertOperation.
 * @against: Another #InfAdoptedOperation.
 *
 * Returns a concurrency ID for transformation of @op against @against
 * (see inf_adopted_operation_get_concurrency_id() for further information).
 *
 * Returns: A concurrency ID between @op and @against.
 */
InfAdoptedConcurrencyId
inf_text_insert_operation_get_concurrency_id(InfTextInsertOperation* op,
                                             InfAdoptedOperation* against)
{
  InfTextInsertOperation* insert_against;
  guint own_pos;
  guint against_pos;

  g_return_val_if_fail(
    INF_TEXT_IS_INSERT_OPERATION(op),
    INF_ADOPTED_CONCURRENCY_NONE
  );

  g_return_val_if_fail(
    INF_ADOPTED_IS_OPERATION(against),
    INF_ADOPTED_CONCURRENCY_NONE
  );

  if(INF_TEXT_IS_INSERT_OPERATION(against))
  {
    insert_against = INF_TEXT_INSERT_OPERATION(against);
    own_pos = inf_text_insert_operation_get_position(op);
    against_pos = inf_text_insert_operation_get_position(insert_against);
    if(own_pos < against_pos)
      return INF_ADOPTED_CONCURRENCY_OTHER;
    else if(own_pos > against_pos)
      return INF_ADOPTED_CONCURRENCY_SELF;
    else
      return INF_ADOPTED_CONCURRENCY_NONE;
  }
  else
  {
    _inf_adopted_concurrency_warning(INF_TEXT_TYPE_INSERT_OPERATION);
    return INF_ADOPTED_CONCURRENCY_NONE;
  }
}

/**
 * inf_text_insert_operation_transform_insert:
 * @operation: A #InfTextInsertOperation.
 * @against: Another #InfTextInsertOperation.
 * @cid: The concurrency ID for the transformation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Return Value: A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_insert_operation_transform_insert(InfTextInsertOperation* operation,
                                           InfTextInsertOperation* against,
                                           InfAdoptedConcurrencyId cid)
{
  InfTextInsertOperationIface* iface;
  guint op_pos;
  guint against_pos;
  guint against_length;

  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(operation), NULL);
  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(against), NULL);

  iface = INF_TEXT_INSERT_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->transform_position != NULL, NULL);

  op_pos = inf_text_insert_operation_get_position(operation);
  against_pos = inf_text_insert_operation_get_position(against);

  if(op_pos < against_pos ||
     (op_pos == against_pos && cid == INF_ADOPTED_CONCURRENCY_OTHER))
  {
    return inf_adopted_operation_copy(INF_ADOPTED_OPERATION(operation));
  }
  else if(op_pos > against_pos ||
          (op_pos == against_pos && cid == INF_ADOPTED_CONCURRENCY_SELF))
  {
    against_length = inf_text_insert_operation_get_length(against);

    return INF_ADOPTED_OPERATION(
      iface->transform_position(operation, op_pos + against_length)
    );
  }
  else
  {
    /* Note this can actually occur when a split operation has to transform
     * one of its parts against the other. It is also possible for a split
     * operation to hold two insert operations, for example when reversing a
     * splitted delete operation. However, it is illegal that two such insert
     * operations insert text at the same position. */
    g_assert_not_reached();
    return NULL;
  }
}

/**
 * inf_text_insert_operation_transform_delete:
 * @operation: A #InfTextInsertOperation.
 * @against: A #InfTextDeleteOperation.
 * @cid: The concurrency ID for the transformation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Return Value: A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_insert_operation_transform_delete(InfTextInsertOperation* operation,
                                           InfTextDeleteOperation* against,
                                           InfAdoptedConcurrencyId cid)
{
  InfTextInsertOperationIface* iface;
  guint own_pos;
  guint other_pos;
  guint other_len;

  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(operation), NULL);
  g_return_val_if_fail(INF_TEXT_IS_DELETE_OPERATION(against), NULL);

  iface = INF_TEXT_INSERT_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->transform_position != NULL, NULL);

  own_pos = inf_text_insert_operation_get_position(operation);
  other_pos = inf_text_delete_operation_get_position(against);
  other_len = inf_text_delete_operation_get_length(against);

  if(own_pos >= other_pos + other_len)
  {
    return INF_ADOPTED_OPERATION(
      iface->transform_position(operation, own_pos - other_len)
    );
  }
  else if(own_pos < other_pos)
  {
    return inf_adopted_operation_copy(INF_ADOPTED_OPERATION(operation));
  }
  else
  {
    return INF_ADOPTED_OPERATION(
      iface->transform_position(operation, other_pos)
    );
  }
}

/* vim:set et sw=2 ts=2: */
