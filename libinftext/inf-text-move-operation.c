/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <libinftext/inf-text-move-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-user.h>

#include <libinfinity/adopted/inf-adopted-operation.h>
#include <libinfinity/adopted/inf-adopted-concurrency-warning.h>

typedef struct _InfTextMoveOperationPrivate InfTextMoveOperationPrivate;
struct _InfTextMoveOperationPrivate {
  guint position;
  gint length;
};

enum {
  PROP_0,

  PROP_POSITION,
  PROP_LENGTH
};

#define INF_TEXT_MOVE_OPERATION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_MOVE_OPERATION, InfTextMoveOperationPrivate))

static GObjectClass* parent_class;

static void
inf_text_move_operation_init(GTypeInstance* instance,
                             gpointer g_class)
{
  InfTextMoveOperation* operation;
  InfTextMoveOperationPrivate* priv;

  operation = INF_TEXT_MOVE_OPERATION(instance);
  priv = INF_TEXT_MOVE_OPERATION_PRIVATE(operation);

  priv->position = 0;
  priv->length = 0;
}

static void
inf_text_move_operation_set_property(GObject* object,
                                     guint prop_id,
                                     const GValue* value,
                                     GParamSpec* pspec)
{
  InfTextMoveOperation* operation;
  InfTextMoveOperationPrivate* priv;

  operation = INF_TEXT_MOVE_OPERATION(object);
  priv = INF_TEXT_MOVE_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_POSITION:
    priv->position = g_value_get_uint(value);
    break;
  case PROP_LENGTH:
    priv->length = g_value_get_int(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_move_operation_get_property(GObject* object,
                                     guint prop_id,
                                     GValue* value,
                                     GParamSpec* pspec)
{
  InfTextMoveOperation* operation;
  InfTextMoveOperationPrivate* priv;

  operation = INF_TEXT_MOVE_OPERATION(object);
  priv = INF_TEXT_MOVE_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_POSITION:
    g_value_set_uint(value, priv->position);
    break;
  case PROP_LENGTH:
    g_value_set_int(value, priv->length);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static gboolean
inf_text_move_operation_need_concurrency_id(InfAdoptedOperation* operation,
                                            InfAdoptedOperation* against)
{
  g_assert(INF_TEXT_IS_MOVE_OPERATION(operation));
  return FALSE;
}

static InfAdoptedConcurrencyId
inf_text_move_operation_get_concurrency_id(InfAdoptedOperation* operation,
                                           InfAdoptedOperation* against)
{
  g_assert(INF_TEXT_IS_MOVE_OPERATION(operation));
  _inf_adopted_concurrency_warning(INF_TEXT_TYPE_MOVE_OPERATION);
  return INF_ADOPTED_CONCURRENCY_NONE;
}

static InfAdoptedOperation*
inf_text_move_operation_transform(InfAdoptedOperation* operation,
                                  InfAdoptedOperation* against,
                                  gint concurrency_id)
{
  InfTextMoveOperationPrivate* priv;
  guint new_pos;
  gint new_len;

  g_assert(INF_TEXT_IS_MOVE_OPERATION(operation));

  priv = INF_TEXT_MOVE_OPERATION_PRIVATE(operation);
  new_pos = priv->position;
  new_len = priv->length;

  if(INF_TEXT_IS_INSERT_OPERATION(against))
  {
    inf_text_move_operation_transform_insert(
      inf_text_insert_operation_get_position(
        INF_TEXT_INSERT_OPERATION(against)
      ),
      inf_text_insert_operation_get_length(
        INF_TEXT_INSERT_OPERATION(against)
      ),
      &new_pos,
      &new_len,
      TRUE /* left gravity */
    );
  }
  else if(INF_TEXT_IS_DELETE_OPERATION(against))
  {
    inf_text_move_operation_transform_delete(
      inf_text_delete_operation_get_position(
        INF_TEXT_DELETE_OPERATION(against)
      ),
      inf_text_delete_operation_get_length(
        INF_TEXT_DELETE_OPERATION(against)
      ),
      &new_pos,
      &new_len
    );
  }
  else
  {
    g_assert_not_reached();
    return NULL;
  }

  return INF_ADOPTED_OPERATION(
    g_object_new(
      INF_TEXT_TYPE_MOVE_OPERATION,
      "position", new_pos,
      "length", new_len,
      NULL
    )
  );
}

static InfAdoptedOperation*
inf_text_move_operation_copy(InfAdoptedOperation* operation)
{
  InfTextMoveOperationPrivate* priv;
  GObject* object;

  g_assert(INF_TEXT_IS_MOVE_OPERATION(operation));

  priv = INF_TEXT_MOVE_OPERATION_PRIVATE(operation);

  object = g_object_new(
    INF_TEXT_TYPE_MOVE_OPERATION,
    "position", priv->position,
    "length", priv->length,
    NULL
  );

  return INF_ADOPTED_OPERATION(object);
}

static InfAdoptedOperationFlags
inf_text_move_operation_get_flags(InfAdoptedOperation* operation)
{
  /* Does not affect the buffer */
  return 0;
}

static void
inf_text_move_operation_apply(InfAdoptedOperation* operation,
                              InfAdoptedUser* by,
                              InfBuffer* buffer)
{
  InfTextMoveOperationPrivate* priv;

  g_assert(INF_TEXT_IS_MOVE_OPERATION(operation));
  g_assert(INF_TEXT_IS_USER(by));

  priv = INF_TEXT_MOVE_OPERATION_PRIVATE(operation);  

  inf_text_user_set_selection(
    INF_TEXT_USER(by),
    priv->position,
    priv->length
  );
}

static InfAdoptedOperation*
inf_text_move_operation_revert(InfAdoptedOperation* operation)
{
  /* Not reversible */
  g_assert_not_reached();
  return NULL;
}

static InfAdoptedOperation*
inf_text_move_operation_make_reversible(InfAdoptedOperation* operation,
                                        InfAdoptedOperation* with,
                                        InfBuffer* buffer)
{
  /* MoveOperation cannot be made reversible */
  g_assert_not_reached();
  return NULL;
}

static void
inf_text_move_operation_class_init(gpointer g_class,
                                   gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextMoveOperationPrivate));

  object_class->set_property = inf_text_move_operation_set_property;
  object_class->get_property = inf_text_move_operation_get_property;

  g_object_class_install_property(
    object_class,
    PROP_POSITION,
    g_param_spec_uint(
      "position",
      "Position",
      "Position where to place the user's caret at",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_LENGTH,
    g_param_spec_int(
      "length",
      "Length",
      "The number of characters of the selected text",
      G_MININT,
      G_MAXINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
inf_text_move_operation_operation_init(gpointer g_iface,
                                       gpointer iface_data)
{
  InfAdoptedOperationIface* iface;
  iface = (InfAdoptedOperationIface*)g_iface;

  iface->need_concurrency_id = inf_text_move_operation_need_concurrency_id;
  iface->get_concurrency_id = inf_text_move_operation_get_concurrency_id;
  iface->transform = inf_text_move_operation_transform;
  iface->copy = inf_text_move_operation_copy;
  iface->get_flags = inf_text_move_operation_get_flags;
  iface->apply = inf_text_move_operation_apply;
  iface->revert = inf_text_move_operation_revert;
  iface->make_reversible = inf_text_move_operation_make_reversible;
}

GType
inf_text_move_operation_get_type(void)
{
  static GType move_operation_type = 0;

  if(!move_operation_type)
  {
    static const GTypeInfo move_operation_type_info = {
      sizeof(InfTextMoveOperationClass),  /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      inf_text_move_operation_class_init, /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof(InfTextMoveOperation),       /* instance_size */
      0,                                  /* n_preallocs */
      inf_text_move_operation_init,       /* instance_init */
      NULL                                /* value_table */
    };

    static const GInterfaceInfo operation_info = {
      inf_text_move_operation_operation_init,
      NULL,
      NULL
    };

    move_operation_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfTextMoveOperation",
      &move_operation_type_info,
      0
    );

    g_type_add_interface_static(
      move_operation_type,
      INF_ADOPTED_TYPE_OPERATION,
      &operation_info
    );
  }

  return move_operation_type;
}

/**
 * inf_text_move_operation_new:
 * @position: The position to place the user's caret at.
 * @length: The number of characters to select. Negative means selection
 * towards the beginning of the buffer.
 *
 * Creates a new move operation that, when applied, changes the caret and
 * selection of the applying user.
 *
 * Return Value: A new #InfTextMoveOperation.
 **/
InfTextMoveOperation*
inf_text_move_operation_new(guint position,
                            gint length)
{
  GObject* object;

  object = g_object_new(
    INF_TEXT_TYPE_MOVE_OPERATION,
    "position", position,
    "length", length,
    NULL
  );

  return INF_TEXT_MOVE_OPERATION(object);
}

/**
 * inf_text_move_operation_get_position:
 * @operation: A #InfTextMoveOperation.
 *
 * Returns the position at which @operation places the user's cursor.
 *
 * Return Value: The position of @operation.
 **/
guint
inf_text_move_operation_get_position(InfTextMoveOperation* operation)
{
  g_return_val_if_fail(INF_TEXT_IS_MOVE_OPERATION(operation), 0);
  return INF_TEXT_MOVE_OPERATION_PRIVATE(operation)->position;
}

/**
 * inf_text_move_operation_get_length:
 * @operation: A #InfTextMoveOperation.
 *
 * Returns the length to which @operation changes the user's selection.
 * Negative means selection towards the beginning of the buffer.
 *
 * Return Value: The length of @operation.
 **/
gint
inf_text_move_operation_get_length(InfTextMoveOperation* operation)
{
  g_return_val_if_fail(INF_TEXT_IS_MOVE_OPERATION(operation), 0);
  return INF_TEXT_MOVE_OPERATION_PRIVATE(operation)->length;
}

/**
 * inf_text_move_operation_transform_insert:
 * @insert_position: The position at which text is inserted.
 * @insert_length: The number of inserted characters.
 * @move_position: Points to the character offset to which the caret is moved.
 * @move_length: Points to the number of characters selected. Negative means
 * towards the beginning.
 * @left_gravity: Whether the move position and length have left gravity.
 *
 * Changes *@move_position and *@move_length so that they point to the same
 * region when @insert_length characters are inserted at @insert_position.
 *
 * If text is inserted at the same position as @move_position, then
 * @move_position is kept at the position it currently is, otherwise it is
 * shifted to the right.
 **/
void
inf_text_move_operation_transform_insert(guint insert_position,
                                         guint insert_length,
                                         guint* move_position,
                                         gint* move_length,
                                         gboolean left_gravity)
{
  guint cur_pos;
  guint cur_bound;
  
  g_return_if_fail(move_position != NULL);
  g_return_if_fail(move_length != NULL);

  cur_pos = *move_position;
  cur_bound = *move_position + *move_length;

  if( (insert_position < cur_pos) ||
      (insert_position == cur_pos && !left_gravity))
  {
    cur_pos += insert_length;
  }

  if( (insert_position < cur_bound) ||
      (insert_position == cur_bound && !left_gravity))
  {
    cur_bound += insert_length;
  }

  *move_position = cur_pos;
  *move_length = (gint)cur_bound - (gint)cur_pos;
}

/**
 * inf_text_move_operation_transform_delete:
 * @delete_position: The position at which text is deleted.
 * @delete_length: The number of deleted characters.
 * @move_position: Points to the character offset to which the caret is moved.
 * @move_length: Points to the number of characters selected. Negative means
 * towards the beginning.
 *
 * Changes *@move_position and *@move_length so that they point to the same
 * region when @delete_length characters are deleted starting from
 * @delete_position.
 **/
void
inf_text_move_operation_transform_delete(guint delete_position,
                                         guint delete_length,
                                         guint* move_position,
                                         gint* move_length)
{
  guint cur_pos;
  gint cur_len;

  g_return_if_fail(move_position != NULL);
  g_return_if_fail(move_length != NULL);

  cur_pos = *move_position;
  cur_len = *move_length;

  if(cur_pos >= delete_position + delete_length)
    *move_position = cur_pos - delete_length;
  else if(cur_pos > delete_position)
    *move_position = delete_position;
  else
    *move_position = cur_pos;

  if(cur_len < 0)
  {
    if(delete_position + delete_length <= cur_pos + cur_len)
    {
      *move_length = cur_len;
    }
    else if(delete_position >= cur_pos)
    {
      *move_length = cur_len;
    }
    else if(delete_position <= cur_pos + cur_len &&
            delete_position + delete_length >= cur_pos)
    {
      *move_length = 0;
    }
    else if(delete_position <= cur_pos + cur_len &&
            delete_position + delete_length > cur_pos + cur_len)
    {
      *move_length = cur_pos - delete_position;
    }
    else if(delete_position > cur_pos + cur_len &&
            delete_position + delete_length > cur_pos)
    {
      *move_length = delete_position - (cur_pos + cur_len);
    }
    else if(delete_position > cur_pos + cur_len &&
            delete_position + delete_length <= cur_pos)
    {
      *move_length = cur_len - delete_length;
    }
    else
    {
      g_assert_not_reached();
    }
  }
  else
  {
    if(delete_position + delete_length <= cur_pos)
    {
      *move_length = cur_len;
    }
    else if(delete_position >= cur_pos + cur_len)
    {
      *move_length = cur_len;
    }
    else if(delete_position <= cur_pos &&
            delete_position + delete_length >= cur_pos + cur_len)
    {
      *move_length = 0;
    }
    else if(delete_position <= cur_pos &&
            delete_position + delete_length > cur_pos)
    {
      *move_length = cur_pos + cur_len - delete_position;
    }
    else if(delete_position > cur_pos &&
             delete_position + delete_length > cur_pos + cur_len)
    {
      *move_length = cur_pos - delete_position;
    }
    else if(delete_position > cur_pos &&
            delete_position + delete_length <= cur_pos + cur_len)
    {
      *move_length = cur_len - delete_length;
    }
    else
    {
      g_assert_not_reached();
    }
  }
}

/* vim:set et sw=2 ts=2: */
