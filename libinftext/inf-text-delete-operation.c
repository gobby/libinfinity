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
 * inf_text_delete_operation_transform_insert:
 * @operation: A #InfTextDeleteOperation.
 * @against: A #InfTextInsertOperation.
 * @concurrency_id: The concurrency ID for the transformation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Return Value: A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_delete_operation_transform_insert(InfTextDeleteOperation* operation,
                                           InfTextInsertOperation* against,
                                           gint concurrency_id)
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
 * @concurrency_id: The concurrency ID for the transformation.
 *
 * Returns a new operation that includes the effect of @against into
 * @operation.
 *
 * Return Value: A new #InfAdoptedOperation.
 **/
InfAdoptedOperation*
inf_text_delete_operation_transform_delete(InfTextDeleteOperation* operation,
                                           InfTextDeleteOperation* against,
                                           gint concurrency_id)
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
