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

#include <libinfinity/adopted/inf-adopted-operation.h>
#include <libinfinity/adopted/inf-adopted-user.h>

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
      sizeof(InfBufferIface),                   /* class_size */
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

/** inf_adopted_operation_transform:
 *
 * @operation: The #InfAdoptedOperation to transform.
 * @against: The operation to transform against.
 *
 * Performs an inclusion transformation of @operation against @against,
 * meaning that the effect of @against is included in @operation.
 *
 * Return Value: The transformed #InfAdoptedOperation, or %NULL if the
 * transformation failed.
 **/
InfAdoptedOperation*
inf_adopted_operation_transform(InfAdoptedOperation* operation,
                                InfAdoptedOperation* against)
{
  InfAdoptedOperationIface* iface;

  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(operation), NULL);
  g_return_val_if_fail(INF_ADOPTED_IS_OPERATION(against), NULL);

  iface = INF_ADOPTED_OPERATION_GET_IFACE(operation);

  if(iface->transform != NULL)
    return (*iface->transform)(operation, against);
  else
    return NULL;
}

#if 0
/** inf_adopted_operation_copy:
 *
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
#endif

/** inf_adopted_operation_get_flags:
 *
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

/** inf_adopted_operation_apply:
 *
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

/** inf_adopted_operation_is_reversible:
 *
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

/** inf_adopted_operation_revert:
 *
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

/** inf_adopted_operation_make_reversible:
 *
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
