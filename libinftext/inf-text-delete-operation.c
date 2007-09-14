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

#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/adopted/inf-adopted-split-operation.h>
#include <libinfinity/adopted/inf-adopted-no-operation.h>
#include <libinfinity/adopted/inf-adopted-operation.h>

#include <string.h>

typedef struct _InfTextDeleteOperationPrivate InfTextDeleteOperationPrivate;
struct _InfTextDeleteOperationPrivate {
  guint position;
  InfTextChunk* chunk;
};

enum {
  PROP_0,

  PROP_POSITION,
  PROP_CHUNK
};

#define INF_TEXT_DELETE_OPERATION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_DELETE_OPERATION, InfTextDeleteOperationPrivate))

static GObjectClass* parent_class;

static gboolean
inf_text_delete_operation_text_match(InfTextDeleteOperation* operation,
                                     InfTextBuffer* buffer)
{
  InfTextDeleteOperationPrivate* priv;
  InfTextChunk* chunk;
  gchar* first;
  gchar* second;
  gsize first_len;
  gsize second_len;
  int result;

  priv = INF_TEXT_DELETE_OPERATION_PRIVATE(operation);

  chunk = inf_text_buffer_get_slice(
    buffer,
    priv->position,
    inf_text_chunk_get_length(priv->chunk)
  );

  first = inf_text_chunk_get_text(priv->chunk, &first_len);
  second = inf_text_chunk_get_text(chunk, &second_len);
  inf_text_chunk_free(chunk);

  /* TODO: Owners must match, too */
  if(first_len == second_len)
    result = memcmp(first, second, first_len);
  else
    result = 1; /* != 0 */

  g_free(second);
  g_free(first);

  return result == 0;
}

static void
inf_text_delete_operation_init(GTypeInstance* instance,
                               gpointer g_class)
{
  InfTextDeleteOperation* operation;
  InfTextDeleteOperationPrivate* priv;

  operation = INF_TEXT_DELETE_OPERATION(instance);
  priv = INF_TEXT_DELETE_OPERATION_PRIVATE(operation);

  priv->position = 0;
  priv->chunk = NULL;
}

static void
inf_text_delete_operation_finalize(GObject* object)
{
  InfTextDeleteOperation* operation;
  InfTextDeleteOperationPrivate* priv;

  operation = INF_TEXT_DELETE_OPERATION(object);
  priv = INF_TEXT_DELETE_OPERATION_PRIVATE(operation);

  inf_text_chunk_free(priv->chunk);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_text_delete_operation_set_property(GObject* object,
                                       guint prop_id,
                                       const GValue* value,
                                       GParamSpec* pspec)
{
  InfTextDeleteOperation* operation;
  InfTextDeleteOperationPrivate* priv;

  operation = INF_TEXT_DELETE_OPERATION(object);
  priv = INF_TEXT_DELETE_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_POSITION:
    priv->position = g_value_get_uint(value);
    break;
  case PROP_CHUNK:
    g_assert(priv->chunk == NULL); /* construct only */
    priv->chunk = (InfTextChunk*)g_value_dup_boxed(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_text_delete_operation_get_property(GObject* object,
                                       guint prop_id,
                                       GValue* value,
                                       GParamSpec* pspec)
{
  InfTextDeleteOperation* operation;
  InfTextDeleteOperationPrivate* priv;

  operation = INF_TEXT_DELETE_OPERATION(object);
  priv = INF_TEXT_DELETE_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_POSITION:
    g_value_set_uint(value, priv->position);
    break;
  case PROP_CHUNK:
    g_value_set_boxed(value, priv->chunk);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static InfAdoptedOperation*
inf_text_delete_operation_transform_insert(InfTextDeleteOperation* operation,
                                           InfTextInsertOperation* against)
{
  InfTextDeleteOperationPrivate* priv;
  GObject* result;

  InfTextChunk* first_chunk;
  InfTextChunk* second_chunk;
  GObject* first_operation;
  GObject* second_operation;

  guint pos;
  guint len;
  guint other_pos;
  guint other_len;

  priv = INF_TEXT_DELETE_OPERATION_PRIVATE(operation);
  pos = priv->position;
  len = inf_text_chunk_get_length(priv->chunk);

  other_pos = inf_text_pword_get_current(
    inf_text_insert_operation_get_pword(against)
  );

  other_len = inf_text_chunk_get_length(
    inf_text_insert_operation_get_chunk(against)
  );

  if(other_pos >= pos + len)
  {
    result = g_object_new(
      INF_TEXT_TYPE_DELETE_OPERATION,
      "position", priv->position,
      "chunk", priv->chunk,
      NULL
    );
  }
  else if(other_pos <= pos)
  {
    result = g_object_new(
      INF_TEXT_TYPE_DELETE_OPERATION,
      "position", pos + other_len,
      "chunk", priv->chunk,
      NULL
    );
  }
  else
  {
    first_chunk = inf_text_chunk_substring(
      priv->chunk,
      0,
      other_pos - pos
    );

    second_chunk = inf_text_chunk_substring(
      priv->chunk,
      other_pos - pos,
      len - (other_pos - pos)
    );

    first_operation = g_object_new(
      INF_TEXT_TYPE_DELETE_OPERATION,
      "position", priv->position,
      "chunk", first_chunk,
      NULL
    );
    
    second_operation = g_object_new(
      INF_TEXT_TYPE_DELETE_OPERATION,
      "position", other_pos + other_len,
      "chunk", second_chunk,
      NULL
    );

    inf_text_chunk_free(second_chunk);
    inf_text_chunk_free(first_chunk);

    result = G_OBJECT(
      inf_adopted_split_operation_new(
        INF_ADOPTED_OPERATION(first_operation),
        INF_ADOPTED_OPERATION(second_operation)
      )
    );

    g_object_unref(G_OBJECT(second_operation));
    g_object_unref(G_OBJECT(first_operation));
  }

  return INF_ADOPTED_OPERATION(result);
}

static InfAdoptedOperation*
inf_text_delete_operation_transform_delete(InfTextDeleteOperation* operation,
                                           InfTextDeleteOperation* against)
{
  InfTextDeleteOperationPrivate* priv;
  InfTextChunk* new_chunk;
  InfTextChunk* other_chunk;
  GObject* result;

  guint pos;
  guint len;
  guint other_pos;
  guint other_len;

  priv = INF_TEXT_DELETE_OPERATION_PRIVATE(operation);
  pos = priv->position;
  len = inf_text_chunk_get_length(priv->chunk);
  other_pos = inf_text_delete_operation_get_position(against);
  other_len = inf_text_chunk_get_length(
    inf_text_delete_operation_get_chunk(against)
  );

  if(pos + len <= other_pos)
  {
    result = g_object_new(
      INF_TEXT_TYPE_DELETE_OPERATION,
      "position", priv->position,
      "chunk", priv->chunk,
      NULL
    );
  }
  else if(pos >= other_pos + other_len)
  {
    result = g_object_new(
      INF_TEXT_TYPE_DELETE_OPERATION,
      "position", pos - other_len,
      "chunk", priv->chunk,
      NULL
    );
  }
  /* Somehow overlapping now */
  else if(other_pos <= pos && other_pos + other_len >= pos + len)
  {
    result = G_OBJECT(inf_adopted_no_operation_new());
  }
  else if(other_pos <= pos && other_pos + other_len < pos + len)
  {
    new_chunk = inf_text_chunk_substring(
      priv->chunk,
      other_pos + other_len - pos,
      len - (other_pos + other_len - pos)
    );

    result = g_object_new(
      INF_TEXT_TYPE_DELETE_OPERATION,
      "position", other_pos,
      "chunk", new_chunk,
      NULL
    );

    inf_text_chunk_free(new_chunk);
  }
  else if(other_pos > pos && other_pos + other_len >= pos + len)
  {
    new_chunk = inf_text_chunk_substring(
      priv->chunk,
      0,
      other_pos - pos
    );

    result = g_object_new(
      INF_TEXT_TYPE_DELETE_OPERATION,
      "position", pos,
      "chunk", new_chunk,
      NULL
    );

    inf_text_chunk_free(new_chunk);
  }
  else
  {
    new_chunk = inf_text_chunk_substring(
      priv->chunk,
      0,
      other_pos - pos
    );

    other_chunk = inf_text_chunk_substring(
      priv->chunk,
      other_pos + other_len - pos,
      len - (other_pos + other_len - pos)
    );

    inf_text_chunk_insert_chunk(new_chunk, other_pos - pos, other_chunk);
    inf_text_chunk_free(other_chunk);

    result = g_object_new(
      INF_TEXT_TYPE_DELETE_OPERATION,
      "position", pos,
      "chunk", new_chunk,
      NULL
    );

    inf_text_chunk_free(new_chunk);
  }
  
  return INF_ADOPTED_OPERATION(result);
}

static InfAdoptedOperation*
inf_text_delete_operation_transform(InfAdoptedOperation* operation,
                                    InfAdoptedOperation* against)
{
  g_assert(INF_TEXT_IS_DELETE_OPERATION(operation));

  if(INF_TEXT_IS_INSERT_OPERATION(against))
  {
    return inf_text_delete_operation_transform_insert(
      INF_TEXT_DELETE_OPERATION(operation),
      INF_TEXT_INSERT_OPERATION(against)
    );
  }
  else if(INF_TEXT_IS_DELETE_OPERATION(against))
  {
    return inf_text_delete_operation_transform_delete(
      INF_TEXT_DELETE_OPERATION(operation),
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
inf_text_delete_operation_get_flags(InfAdoptedOperation* operation)
{
  return INF_ADOPTED_OPERATION_AFFECTS_BUFFER |
         INF_ADOPTED_OPERATION_REVERSIBLE;
}

static void
inf_text_delete_operation_apply(InfAdoptedOperation* operation,
                                InfAdoptedUser* by,
                                InfBuffer* buffer)
{
  InfTextDeleteOperationPrivate* priv;

  g_assert(INF_TEXT_IS_DELETE_OPERATION(operation));
  g_assert(INF_TEXT_IS_BUFFER(buffer));

  priv = INF_TEXT_DELETE_OPERATION_PRIVATE(operation);
  g_assert(
    inf_text_delete_operation_text_match(
      INF_TEXT_DELETE_OPERATION(operation),
      INF_TEXT_BUFFER(buffer)
    )
  );

  inf_text_buffer_erase_text(
    INF_TEXT_BUFFER(buffer),
    priv->position,
    inf_text_chunk_get_length(priv->chunk),
    INF_USER(by)
  );
}

static InfAdoptedOperation*
inf_text_delete_operation_revert(InfAdoptedOperation* operation)
{
  InfTextDeleteOperationPrivate* priv;
  priv = INF_TEXT_DELETE_OPERATION_PRIVATE(operation);

  /* TODO: Find correct concurrency ID here. 0 works for now because the
   * reversed operation is never recorded into the request log. */
  /* This might not even be true. Two reversed delete operations may have
   * to be transformed against each other. */
  return INF_ADOPTED_OPERATION(
    inf_text_insert_operation_new(
      priv->position,
      priv->chunk,
      0
    )
  );
}

static InfAdoptedOperation*
inf_text_delete_operation_make_reversible(InfAdoptedOperation* operation,
                                          InfAdoptedOperation* with,
                                          InfBuffer* buffer)
{
  /* DeleteOperation is always reversible */
  g_assert_not_reached();
  return NULL;
}

static void
inf_text_delete_operation_class_init(gpointer g_class,
                                     gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextDeleteOperationPrivate));

  object_class->finalize = inf_text_delete_operation_finalize;
  object_class->set_property = inf_text_delete_operation_set_property;
  object_class->get_property = inf_text_delete_operation_get_property;

  g_object_class_install_property(
    object_class,
    PROP_POSITION,
    g_param_spec_uint(
      "position",
      "Position",
      "Position where to start deleting characters",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_CHUNK,
    g_param_spec_boxed(
      "chunk",
      "Chunk",
      "The deleted text",
      INF_TEXT_TYPE_CHUNK,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
inf_text_delete_operation_operation_init(gpointer g_iface,
                                         gpointer iface_data)
{
  InfAdoptedOperationIface* iface;
  iface = (InfAdoptedOperationIface*)g_iface;

  iface->transform = inf_text_delete_operation_transform;
  iface->get_flags = inf_text_delete_operation_get_flags;
  iface->apply = inf_text_delete_operation_apply;
  iface->revert = inf_text_delete_operation_revert;
  iface->make_reversible = inf_text_delete_operation_make_reversible;
}

GType
inf_text_delete_operation_get_type(void)
{
  static GType delete_operation_type = 0;

  if(!delete_operation_type)
  {
    static const GTypeInfo delete_operation_type_info = {
      sizeof(InfTextDeleteOperationClass),  /* class_size */
      NULL,                                 /* base_init */
      NULL,                                 /* base_finalize */
      inf_text_delete_operation_class_init, /* class_init */
      NULL,                                 /* class_finalize */
      NULL,                                 /* class_data */
      sizeof(InfTextDeleteOperation),       /* instance_size */
      0,                                    /* n_preallocs */
      inf_text_delete_operation_init,       /* instance_init */
      NULL                                  /* value_table */
    };

    static const GInterfaceInfo operation_info = {
      inf_text_delete_operation_operation_init,
      NULL,
      NULL
    };

    delete_operation_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfTextDeleteOperation",
      &delete_operation_type_info,
      0
    );

    g_type_add_interface_static(
      delete_operation_type,
      INF_ADOPTED_TYPE_OPERATION,
      &operation_info
    );
  }

  return delete_operation_type;
}

/** inf_text_delete_operation_new:
 *
 * @position: The position at which to delete text.
 * @chunk: The text to delete.
 *
 * Creates a new delete operation that, when applied, deletes the text @chunk 
 * that starts at character offset @position in the buffer. The operation
 * cannot be applied, if there is some other text at that position in the
 * buffer.
 *
 * Return Value: A new #InfTextDeleteOperation.
 **/
InfTextDeleteOperation*
inf_text_delete_operation_new(guint position,
                              InfTextChunk* chunk)
{
  GObject* object;

  g_return_val_if_fail(chunk != NULL, NULL);

  object = g_object_new(
    INF_TEXT_TYPE_DELETE_OPERATION,
    "position", position,
    "chunk", chunk,
    NULL
  );

  return INF_TEXT_DELETE_OPERATION(object);
}

/** inf_text_delete_operation_get_position:
 *
 * @operation: A #InfTextDeleteOperation.
 *
 * Returns the position at which @operation starts to delete text.
 *
 * Return Value: The position of @operation.
 **/
guint
inf_text_delete_operation_get_pword(InfTextDeleteOperation* operation)
{
  g_return_val_if_fail(INF_TEXT_IS_DELETE_OPERATION(operation), 0);
  return INF_TEXT_DELETE_OPERATION_PRIVATE(operation)->position;
}

/** inf_text_delete_operation_get_chunk:
 *
 * @operation: A #InfTextDeleteOperation.
 *
 * Returns the text deleted by @operation.
 *
 * Return Value: A #InfTextChunk, owned by the operation.
 **/
InfTextChunk*
inf_text_delete_operation_get_chunk(InfTextDeleteOperation* operation)
{
  g_return_val_if_fail(INF_TEXT_IS_DELETE_OPERATION(operation), NULL);
  return INF_TEXT_DELETE_OPERATION_PRIVATE(operation)->chunk;
}

/* vim:set et sw=2 ts=2: */
