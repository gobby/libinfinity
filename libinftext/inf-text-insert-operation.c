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
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/adopted/inf-adopted-operation.h>

typedef struct _InfTextInsertOperationPrivate InfTextInsertOperationPrivate;
struct _InfTextInsertOperationPrivate {
  InfTextPword* pword;
  InfTextChunk* chunk;
  guint concurrency_id;
};

enum {
  PROP_0,

  PROP_PWORD,
  PROP_CHUNK,
  PROP_CONCURRENCY_ID
};

#define INF_TEXT_INSERT_OPERATION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_INSERT_OPERATION, InfTextInsertOperationPrivate))

static GObjectClass* parent_class;

static void
inf_text_insert_operation_init(GTypeInstance* instance,
                               gpointer g_class)
{
  InfTextInsertOperation* operation;
  InfTextInsertOperationPrivate* priv;

  operation = INF_TEXT_INSERT_OPERATION(instance);
  priv = INF_TEXT_INSERT_OPERATION_PRIVATE(operation);

  priv->pword = NULL;
  priv->chunk = NULL;
  priv->concurrency_id = 0;
}

static void
inf_text_insert_operation_finalize(GObject* object)
{
  InfTextInsertOperation* operation;
  InfTextInsertOperationPrivate* priv;

  operation = INF_TEXT_INSERT_OPERATION(object);
  priv = INF_TEXT_INSERT_OPERATION_PRIVATE(operation);

  inf_text_chunk_free(priv->chunk);
  inf_text_pword_free(priv->pword);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_text_insert_operation_set_property(GObject* object,
                                       guint prop_id,
                                       const GValue* value,
                                       GParamSpec* pspec)
{
  InfTextInsertOperation* operation;
  InfTextInsertOperationPrivate* priv;

  operation = INF_TEXT_INSERT_OPERATION(object);
  priv = INF_TEXT_INSERT_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_PWORD:
    g_assert(priv->pword == NULL); /* construct only */
    priv->pword = (InfTextPword*)g_value_dup_boxed(value);
    break;
  case PROP_CHUNK:
    g_assert(priv->chunk == NULL); /* construct only */
    priv->chunk = (InfTextChunk*)g_value_dup_boxed(value);
    break;
  case PROP_CONCURRENCY_ID:
    priv->concurrency_id = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_text_insert_operation_get_property(GObject* object,
                                       guint prop_id,
                                       GValue* value,
                                       GParamSpec* pspec)
{
  InfTextInsertOperation* operation;
  InfTextInsertOperationPrivate* priv;

  operation = INF_TEXT_INSERT_OPERATION(object);
  priv = INF_TEXT_INSERT_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_PWORD:
    g_value_set_boxed(value, priv->pword);
    break;
  case PROP_CHUNK:
    g_value_set_boxed(value, priv->chunk);
    break;
  case PROP_CONCURRENCY_ID:
    g_value_set_uint(value, priv->concurrency_id);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static InfAdoptedOperation*
inf_text_insert_operation_transform_insert(InfTextInsertOperation* operation,
                                           InfTextInsertOperation* against)
{
  InfTextInsertOperationPrivate* op_priv;
  InfTextInsertOperationPrivate* against_priv;
  InfTextPword* new_pword;
  GObject* result;

  guint op_concurrency_id;
  guint against_concurrency_id;
  int cmp_result;

  op_priv = INF_TEXT_INSERT_OPERATION_PRIVATE(operation);
  against_priv = INF_TEXT_INSERT_OPERATION_PRIVATE(against);
  
  op_concurrency_id = op_priv->concurrency_id;
  against_concurrency_id = against_priv->concurrency_id;

  g_assert(op_concurrency_id != against_concurrency_id);

  cmp_result = inf_text_pword_compare(op_priv->pword, against_priv->pword);

  if(cmp_result < 0 ||
     (cmp_result == 0 && op_concurrency_id < against_concurrency_id)) 
  {
    result = g_object_new(
      INF_TEXT_TYPE_INSERT_OPERATION,
      "pword", op_priv->pword,
      "chunk", op_priv->chunk,
      "concurrency-id", op_priv->concurrency_id,
      NULL
    );
  }
  else if(cmp_result > 0 ||
          (cmp_result == 0 && op_concurrency_id > against_concurrency_id))
  {
    new_pword = inf_text_pword_new_proceed(
      op_priv->pword, 
      inf_text_pword_get_current(op_priv->pword) +
      inf_text_chunk_get_length(against_priv->chunk)
    );

    result = g_object_new(
      INF_TEXT_TYPE_INSERT_OPERATION,
      "pword", new_pword,
      "chunk", op_priv->chunk,
      "concurrency-id", op_priv->concurrency_id,
      NULL
    );

    inf_text_pword_free(new_pword);
  }
  else
  {
    result = g_object_new(
      INF_TEXT_TYPE_INSERT_OPERATION,
      "pword", op_priv->pword,
      "chunk", op_priv->chunk,
      "concurrency-id", op_priv->concurrency_id,
      NULL
    );
  }

  return INF_ADOPTED_OPERATION(result);
}

static InfAdoptedOperation*
inf_text_insert_operation_transform_delete(InfTextInsertOperation* operation,
                                           InfTextDeleteOperation* against)
{
  InfTextInsertOperationPrivate* priv;
  InfTextPword* new_pword;
  GObject* result;

  guint own_pos;
  guint pos;
  guint len;

  priv = INF_TEXT_INSERT_OPERATION_PRIVATE(operation);
  own_pos = inf_text_pword_get_current(priv->pword);

  pos = inf_text_delete_operation_get_position(against);

  len = inf_text_chunk_get_length(
    inf_text_delete_operation_get_chunk(against)
  );

  if(own_pos >= pos + len)
  {
    new_pword = inf_text_pword_new_proceed(priv->pword, own_pos - len);

    result = g_object_new(
      INF_TEXT_TYPE_INSERT_OPERATION,
      "pword", new_pword,
      "chunk", priv->chunk,
      "concurrency-id", priv->concurrency_id,
      NULL
    );

    inf_text_pword_free(new_pword);
  }
  else if(own_pos < pos)
  {
    result = g_object_new(
      INF_TEXT_TYPE_INSERT_OPERATION,
      "pword", priv->pword,
      "chunk", priv->chunk,
      "concurrency-id", priv->concurrency_id,
      NULL
    );
  }
  else
  {
    new_pword = inf_text_pword_new_proceed(priv->pword, pos);

    result = g_object_new(
      INF_TEXT_TYPE_INSERT_OPERATION,
      "pword", new_pword,
      "chunk", priv->chunk,
      "concurrency-id", priv->concurrency_id,
      NULL
    );

    inf_text_pword_free(new_pword);
  }

  return INF_ADOPTED_OPERATION(result);
}

static InfAdoptedOperation*
inf_text_insert_operation_transform(InfAdoptedOperation* operation,
                                    InfAdoptedOperation* against)
{
  g_assert(INF_TEXT_IS_INSERT_OPERATION(operation));

  if(INF_TEXT_IS_INSERT_OPERATION(against))
  {
    return inf_text_insert_operation_transform_insert(
      INF_TEXT_INSERT_OPERATION(operation),
      INF_TEXT_INSERT_OPERATION(against)
    );
  }
  else if(INF_TEXT_IS_DELETE_OPERATION(against))
  {
    return inf_text_insert_operation_transform_delete(
      INF_TEXT_INSERT_OPERATION(operation),
      INF_TEXT_DELETE_OPERATION(against)
    );
  }
  else
  {
    g_assert_not_reached();
    return NULL;
  }
}

static InfAdoptedOperationFlags
inf_text_insert_operation_get_flags(InfAdoptedOperation* operation)
{
  return INF_ADOPTED_OPERATION_AFFECTS_BUFFER |
         INF_ADOPTED_OPERATION_REVERSIBLE;
}

static void
inf_text_insert_operation_apply(InfAdoptedOperation* operation,
                                InfAdoptedUser* by,
                                InfBuffer* buffer)
{
  InfTextInsertOperationPrivate* priv;

  g_assert(INF_TEXT_IS_INSERT_OPERATION(operation));
  g_assert(INF_TEXT_IS_BUFFER(buffer));

  priv = INF_TEXT_INSERT_OPERATION_PRIVATE(operation);

  inf_text_buffer_insert_chunk(
    INF_TEXT_BUFFER(buffer),
    inf_text_pword_get_current(priv->pword),
    priv->chunk,
    INF_USER(by)
  );
}

static InfAdoptedOperation*
inf_text_insert_operation_revert(InfAdoptedOperation* operation)
{
  InfTextInsertOperationPrivate* priv;
  priv = INF_TEXT_INSERT_OPERATION_PRIVATE(operation);

  return INF_ADOPTED_OPERATION(
    inf_text_delete_operation_new(
      inf_text_pword_get_current(priv->pword),
      priv->chunk
    )
  );
}

static InfAdoptedOperation*
inf_text_insert_operation_make_reversible(InfAdoptedOperation* operation,
                                          InfAdoptedOperation* with,
                                          InfBuffer* buffer)
{
  /* InsertOperation is always reversible */
  g_assert_not_reached();
  return NULL;
}

static void
inf_text_insert_operation_class_init(gpointer g_class,
                                   gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextInsertOperationPrivate));

  object_class->finalize = inf_text_insert_operation_finalize;
  object_class->set_property = inf_text_insert_operation_set_property;
  object_class->get_property = inf_text_insert_operation_get_property;

  g_object_class_install_property(
    object_class,
    PROP_PWORD,
    g_param_spec_boxed(
      "pword",
      "Pword",
      "Insert position and transformation history",
      INF_TEXT_TYPE_PWORD,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CHUNK,
    g_param_spec_boxed(
      "chunk",
      "Chunk",
      "The text to insert",
      INF_TEXT_TYPE_CHUNK,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CONCURRENCY_ID,
    g_param_spec_uint(
      "concurrency-id",
      "Concurreny ID",
      "Specifies which insert operation to move if two insert operations "
      "with the same pword are transformed",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
inf_text_insert_operation_operation_init(gpointer g_iface,
                                         gpointer iface_data)
{
  InfAdoptedOperationIface* iface;
  iface = (InfAdoptedOperationIface*)g_iface;

  iface->transform = inf_text_insert_operation_transform;
  iface->get_flags = inf_text_insert_operation_get_flags;
  iface->apply = inf_text_insert_operation_apply;
  iface->revert = inf_text_insert_operation_revert;
  iface->make_reversible = inf_text_insert_operation_make_reversible;
}

GType
inf_text_insert_operation_get_type(void)
{
  static GType insert_operation_type = 0;

  if(!insert_operation_type)
  {
    static const GTypeInfo insert_operation_type_info = {
      sizeof(InfTextInsertOperationClass),  /* class_size */
      NULL,                                 /* base_init */
      NULL,                                 /* base_finalize */
      inf_text_insert_operation_class_init, /* class_init */
      NULL,                                 /* class_finalize */
      NULL,                                 /* class_data */
      sizeof(InfTextInsertOperation),       /* instance_size */
      0,                                    /* n_preallocs */
      inf_text_insert_operation_init,       /* instance_init */
      NULL                                  /* value_table */
    };

    static const GInterfaceInfo operation_info = {
      inf_text_insert_operation_operation_init,
      NULL,
      NULL
    };

    insert_operation_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfTextInsertOperation",
      &insert_operation_type_info,
      0
    );

    g_type_add_interface_static(
      insert_operation_type,
      INF_ADOPTED_TYPE_OPERATION,
      &operation_info
    );
  }

  return insert_operation_type;
}

/** inf_text_insert_operation_new:
 *
 * @pos: The position at which to insert text.
 * @chunk: The text to insert.
 * @concurrency_id: A concurrency ID.
 *
 * Creates a new insert operation that, when applied, inserts @chunk at
 * @pos. @concurrency_id is used to determine which insert operation is
 * shifted when two insert operations at the same position are transformed.
 *
 * Return Value: A new #InfTextInsertOperation.
 **/
InfTextInsertOperation*
inf_text_insert_operation_new(guint pos,
                              InfTextChunk* chunk,
                              guint concurrency_id)
{
  GObject* object;
  InfTextPword* pword;

  g_return_val_if_fail(chunk != NULL, NULL);

  pword = inf_text_pword_new(pos);

  object = g_object_new(
    INF_TEXT_TYPE_INSERT_OPERATION,
    "pword", pword,
    "chunk", chunk,
    "concurrency-id", concurrency_id,
    NULL
  );

  inf_text_pword_free(pword);
  return INF_TEXT_INSERT_OPERATION(object);
}

/** inf_text_insert_operation_get_pword:
 *
 * @operation: A #InfTextInsertOperation.
 *
 * Returns the pword of @operation. The pword defines the position at which
 * to insert the operation's text and keeps a history of previous positions
 * in case the operation was transformed.
 *
 * Return Value: A #InfTextPword, owned by the operation.
 **/
InfTextPword*
inf_text_insert_operation_get_pword(InfTextInsertOperation* operation)
{
  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(operation), NULL);
  return INF_TEXT_INSERT_OPERATION_PRIVATE(operation)->pword;
}

/** inf_text_insert_operation_get_chunk:
 *
 * @operation: A #InfTextInsertOperation.
 *
 * Returns the text inserted by @operation.
 *
 * Return Value: A #InfTextChunk, owned by the operation.
 **/
InfTextChunk*
inf_text_insert_operation_get_chunk(InfTextInsertOperation* operation)
{
  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(operation), NULL);
  return INF_TEXT_INSERT_OPERATION_PRIVATE(operation)->chunk;
}

/** inf_text_insert_operation_get_concurrency_id:
 *
 * @operation: A #InfTextInsertOperation.
 *
 * Returns the concurrency ID of @operation. The concurrency ID is used to
 * determine which insert operation is shifted when two insert operations at
 * the same position are transformed.
 *
 * Return Value: @operation's concurrency ID.
 **/
guint
inf_text_insert_operation_get_concurrency_id(InfTextInsertOperation* op)
{
  g_return_val_if_fail(INF_TEXT_IS_INSERT_OPERATION(op), 0);
  return INF_TEXT_INSERT_OPERATION_PRIVATE(op)->concurrency_id;
}

/* vim:set et sw=2 ts=2: */
