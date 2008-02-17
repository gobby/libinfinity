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

#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>

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
  g_return_val_if_fail(iface->get_pword != NULL, 0);

  return inf_text_pword_get_current(iface->get_pword(operation));
}

/**
 * inf_text_insert_operation_get_pword:
 * @operation: A #InfTextInsertOperation.
 *
 * Returns the #InfTextPword of @operation which contains the current
 * position and the transformation history of the positions @operation
 * inserted text before being transformed.
 *
 * Return Value: A #InfTextPword, owned by @operation.
 **/
InfTextPword*
inf_text_insert_operation_get_pword(InfTextInsertOperation* operation)
{
  InfTextInsertOperationIface* iface;

  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(operation), NULL);

  iface = INF_TEXT_INSERT_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->get_pword != NULL, NULL);

  return iface->get_pword(operation);
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
 * inf_text_insert_operation_transform_insert:
 * @operation: A #InfTextInsertOperation.
 * @against: Another #InfTextInsertOperation.
 * @concurrency_id: The concurrency ID for the transformation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Return Value: A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_insert_operation_transform_insert(InfTextInsertOperation* operation,
                                           InfTextInsertOperation* against,
                                           gint concurrency_id)
{
  InfTextInsertOperationIface* iface;
  InfTextPword* op_pword;
  InfTextPword* against_pword;
  guint against_length;
  int cmp_result;

  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(operation), NULL);
  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(against), NULL);

  iface = INF_TEXT_INSERT_OPERATION_GET_IFACE(operation);
  g_return_val_if_fail(iface->transform_position != NULL, NULL);

  op_pword = inf_text_insert_operation_get_pword(operation);
  against_pword = inf_text_insert_operation_get_pword(against);

  cmp_result = inf_text_pword_compare(op_pword, against_pword);

  if(cmp_result < 0 || (cmp_result == 0 && concurrency_id < 0)) 
  {
    return inf_adopted_operation_copy(INF_ADOPTED_OPERATION(operation));
  }
  else if(cmp_result > 0 || (cmp_result == 0 && concurrency_id > 0))
  {
    against_length = inf_text_insert_operation_get_length(against);

    return INF_ADOPTED_OPERATION(
      iface->transform_position(
        operation,
        inf_text_pword_get_current(op_pword) + against_length
      )
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
 * @concurrency_id: The concurrency ID for the transformation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Return Value: A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_insert_operation_transform_delete(InfTextInsertOperation* operation,
                                           InfTextDeleteOperation* against,
                                           gint concurrency_id)
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
