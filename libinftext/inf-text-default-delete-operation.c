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

#include <libinftext/inf-text-default-delete-operation.h>
#include <libinftext/inf-text-default-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/adopted/inf-adopted-split-operation.h>
#include <libinfinity/adopted/inf-adopted-no-operation.h>
#include <libinfinity/adopted/inf-adopted-operation.h>

#include <string.h>

/* Don't check text match in stable releases */
/* #define DELETE_OPERATION_CHECK_TEXT_MATCH */

typedef struct _InfTextDefaultDeleteOperationPrivate InfTextDefaultDeleteOperationPrivate;
struct _InfTextDefaultDeleteOperationPrivate {
  guint position;
  InfTextChunk* chunk;
};

enum {
  PROP_0,

  PROP_POSITION,
  PROP_CHUNK
};

#define INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION, InfTextDefaultDeleteOperationPrivate))

static GObjectClass* parent_class;

#ifdef DELETE_OPERATION_CHECK_TEXT_MATCH
static gboolean
inf_text_default_delete_operation_text_match(
  InfTextDefaultDeleteOperation* operation,
  InfTextBuffer* buffer)
{
  InfTextDefaultDeleteOperationPrivate* priv;
  InfTextChunk* chunk;
  gchar* first;
  gchar* second;
  gsize first_len;
  gsize second_len;
  int result;

  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

  /* TODO: inf_text_chunk_cmp_substring */
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
#endif /* DELETE_OPERATION_CHECK_TEXT_MATCH */

static void
inf_text_default_delete_operation_init(GTypeInstance* instance,
                                       gpointer g_class)
{
  InfTextDefaultDeleteOperation* operation;
  InfTextDefaultDeleteOperationPrivate* priv;

  operation = INF_TEXT_DEFAULT_DELETE_OPERATION(instance);
  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

  priv->position = 0;
  priv->chunk = NULL;
}

static void
inf_text_default_delete_operation_finalize(GObject* object)
{
  InfTextDefaultDeleteOperation* operation;
  InfTextDefaultDeleteOperationPrivate* priv;

  operation = INF_TEXT_DEFAULT_DELETE_OPERATION(object);
  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

  inf_text_chunk_free(priv->chunk);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_text_default_delete_operation_set_property(GObject* object,
                                               guint prop_id,
                                               const GValue* value,
                                               GParamSpec* pspec)
{
  InfTextDefaultDeleteOperation* operation;
  InfTextDefaultDeleteOperationPrivate* priv;

  operation = INF_TEXT_DEFAULT_DELETE_OPERATION(object);
  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

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
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_default_delete_operation_get_property(GObject* object,
                                               guint prop_id,
                                               GValue* value,
                                               GParamSpec* pspec)
{
  InfTextDefaultDeleteOperation* operation;
  InfTextDefaultDeleteOperationPrivate* priv;

  operation = INF_TEXT_DEFAULT_DELETE_OPERATION(object);
  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

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

static gboolean
inf_text_default_delete_operation_need_concurrency_id(
  InfAdoptedOperation* operation,
  InfAdoptedOperation* against)
{
  g_assert(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(operation));

  return inf_text_delete_operation_need_concurrency_id(
    INF_TEXT_DELETE_OPERATION(operation),
    against
  );
}

static InfAdoptedConcurrencyId
inf_text_default_delete_operation_get_concurrency_id(
  InfAdoptedOperation* operation,
  InfAdoptedOperation* against)
{
  g_assert(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(operation));

  return inf_text_delete_operation_get_concurrency_id(
    INF_TEXT_DELETE_OPERATION(operation),
    against
  );
}

static InfAdoptedOperation*
inf_text_default_delete_operation_transform(InfAdoptedOperation* operation,
                                            InfAdoptedOperation* against,
                                            InfAdoptedConcurrencyId cid)
{
  g_assert(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(operation));

  if(INF_TEXT_IS_INSERT_OPERATION(against))
  {
    return inf_text_delete_operation_transform_insert(
      INF_TEXT_DELETE_OPERATION(operation),
      INF_TEXT_INSERT_OPERATION(against),
      cid
    );
  }
  else if(INF_TEXT_IS_DELETE_OPERATION(against))
  {
    return inf_text_delete_operation_transform_delete(
      INF_TEXT_DELETE_OPERATION(operation),
      INF_TEXT_DELETE_OPERATION(against),
      cid
    );
  }
  else
  {
    g_assert_not_reached();
    return NULL;
  }
}

static InfAdoptedOperation*
inf_text_default_delete_operation_copy(InfAdoptedOperation* operation)
{
  InfTextDefaultDeleteOperationPrivate* priv;
  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

  return INF_ADOPTED_OPERATION(
    g_object_new(
      INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION,
      "position", priv->position,
      "chunk", priv->chunk,
      NULL
    )
  );
}

static InfAdoptedOperationFlags
inf_text_default_delete_operation_get_flags(InfAdoptedOperation* operation)
{
  return INF_ADOPTED_OPERATION_AFFECTS_BUFFER |
         INF_ADOPTED_OPERATION_REVERSIBLE;
}

static void
inf_text_default_delete_operation_apply(InfAdoptedOperation* operation,
                                        InfAdoptedUser* by,
                                        InfBuffer* buffer)
{
  InfTextDefaultDeleteOperationPrivate* priv;

  g_assert(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(operation));
  g_assert(INF_TEXT_IS_BUFFER(buffer));

  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

#ifdef DELETE_OPERATION_CHECK_TEXT_MATCH
  g_assert(
    inf_text_default_delete_operation_text_match(
      INF_TEXT_DEFAULT_DELETE_OPERATION(operation),
      INF_TEXT_BUFFER(buffer)
    )
  );
#endif /* DELETE_OPERATION_CHECK_TEXT_MATCH */

  inf_text_buffer_erase_text(
    INF_TEXT_BUFFER(buffer),
    priv->position,
    inf_text_chunk_get_length(priv->chunk),
    INF_USER(by)
  );
}

static InfAdoptedOperation*
inf_text_default_delete_operation_revert(InfAdoptedOperation* operation)
{
  InfTextDefaultDeleteOperationPrivate* priv;
  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

  return INF_ADOPTED_OPERATION(
    inf_text_default_insert_operation_new(
      priv->position,
      priv->chunk
    )
  );
}

static guint
inf_text_default_delete_operation_get_position(
  InfTextDeleteOperation* operation)
{
  return INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation)->position;
}

static guint
inf_text_default_delete_operation_get_length(
  InfTextDeleteOperation* operation)
{
  return inf_text_chunk_get_length(
    INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation)->chunk
  );
}

static InfTextDeleteOperation*
inf_text_default_delete_operation_transform_position(
  InfTextDeleteOperation* operation,
  guint position)
{
  InfTextDefaultDeleteOperationPrivate* priv;
  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

  return INF_TEXT_DELETE_OPERATION(
    g_object_new(
      INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION,
      "position", position,
      "chunk", priv->chunk,
      NULL
    )
  );
}

static InfTextDeleteOperation*
inf_text_default_delete_operation_transform_overlap(
  InfTextDeleteOperation* operation,
  InfTextDeleteOperation* other,
  guint position,
  guint begin,
  guint other_begin,
  guint length)
{
  InfTextDefaultDeleteOperationPrivate* priv;
  InfTextChunk* chunk;
  GObject* result;

  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);
  chunk = inf_text_chunk_copy(priv->chunk);
  inf_text_chunk_erase(chunk, begin, length);

  result = g_object_new(
    INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION,
    "position", position,
    "chunk", chunk,
    NULL
  );

  inf_text_chunk_free(chunk);
  return INF_TEXT_DELETE_OPERATION(result);
}

static InfAdoptedSplitOperation*
inf_text_default_delete_operation_transform_split(
  InfTextDeleteOperation* operation,
  guint split_pos,
  guint split_len)
{
  InfTextDefaultDeleteOperationPrivate* priv;
  InfTextChunk* first_chunk;
  InfTextChunk* second_chunk;
  GObject* first;
  GObject* second;
  InfAdoptedSplitOperation* result;

  priv = INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation);

  first_chunk = inf_text_chunk_substring(priv->chunk, 0, split_pos);
  second_chunk = inf_text_chunk_substring(
    priv->chunk,
    split_pos,
    inf_text_chunk_get_length(priv->chunk) - split_pos
  );

  first = g_object_new(
    INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION,
    "position", priv->position,
    "chunk", first_chunk,
    NULL
  );

  second = g_object_new(
    INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION,
    "position", priv->position + split_pos + split_len,
    "chunk", second_chunk,
    NULL
  );

  inf_text_chunk_free(first_chunk);
  inf_text_chunk_free(second_chunk);

  result = inf_adopted_split_operation_new(
    INF_ADOPTED_OPERATION(first),
    INF_ADOPTED_OPERATION(second)
  );

  g_object_unref(first);
  g_object_unref(second);
  return result;
}

static void
inf_text_default_delete_operation_class_init(gpointer g_class,
                                             gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextDefaultDeleteOperationPrivate));

  object_class->finalize = inf_text_default_delete_operation_finalize;
  object_class->set_property = inf_text_default_delete_operation_set_property;
  object_class->get_property = inf_text_default_delete_operation_get_property;

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
inf_text_default_delete_operation_operation_init(gpointer g_iface,
                                                 gpointer iface_data)
{
  InfAdoptedOperationIface* iface;
  iface = (InfAdoptedOperationIface*)g_iface;

  iface->need_concurrency_id =
    inf_text_default_delete_operation_need_concurrency_id;
  iface->get_concurrency_id =
    inf_text_default_delete_operation_get_concurrency_id;
  iface->transform = inf_text_default_delete_operation_transform;
  iface->copy = inf_text_default_delete_operation_copy;
  iface->get_flags = inf_text_default_delete_operation_get_flags;
  iface->apply = inf_text_default_delete_operation_apply;
  iface->apply_transformed = NULL;
  iface->revert = inf_text_default_delete_operation_revert;
}

static void
inf_text_default_delete_operation_delete_operation_init(gpointer g_iface,
                                                        gpointer iface_data)
{
  InfTextDeleteOperationIface* iface;
  iface = (InfTextDeleteOperationIface*)g_iface;

  iface->get_position = inf_text_default_delete_operation_get_position;
  iface->get_length = inf_text_default_delete_operation_get_length;
  iface->transform_position =
    inf_text_default_delete_operation_transform_position;
  iface->transform_overlap =
    inf_text_default_delete_operation_transform_overlap;
  iface->transform_split = inf_text_default_delete_operation_transform_split;
}

GType
inf_text_default_delete_operation_get_type(void)
{
  static GType default_delete_operation_type = 0;

  if(!default_delete_operation_type)
  {
    static const GTypeInfo default_delete_operation_type_info = {
      sizeof(InfTextDefaultDeleteOperationClass),   /* class_size */
      NULL,                                         /* base_init */
      NULL,                                         /* base_finalize */
      inf_text_default_delete_operation_class_init, /* class_init */
      NULL,                                         /* class_finalize */
      NULL,                                         /* class_data */
      sizeof(InfTextDefaultDeleteOperation),        /* instance_size */
      0,                                            /* n_preallocs */
      inf_text_default_delete_operation_init,       /* instance_init */
      NULL                                          /* value_table */
    };

    static const GInterfaceInfo operation_info = {
      inf_text_default_delete_operation_operation_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo delete_operation_info = {
      inf_text_default_delete_operation_delete_operation_init,
      NULL,
      NULL
    };

    default_delete_operation_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfTextDefaultDeleteOperation",
      &default_delete_operation_type_info,
      0
    );

    g_type_add_interface_static(
      default_delete_operation_type,
      INF_ADOPTED_TYPE_OPERATION,
      &operation_info
    );

    g_type_add_interface_static(
      default_delete_operation_type,
      INF_TEXT_TYPE_DELETE_OPERATION,
      &delete_operation_info
    );
  }

  return default_delete_operation_type;
}

/**
 * inf_text_default_delete_operation_new:
 * @position: The position at which to delete text.
 * @chunk: The text to delete.
 *
 * Creates a new delete operation that, when applied, deletes the text @chunk 
 * that starts at character offset @position in the buffer. The operation
 * cannot be applied, if there is some other text at that position in the
 * buffer.
 *
 * Return Value: A new #InfTextDefaultDeleteOperation.
 **/
InfTextDefaultDeleteOperation*
inf_text_default_delete_operation_new(guint position,
                                      InfTextChunk* chunk)
{
  GObject* object;

  g_return_val_if_fail(chunk != NULL, NULL);

  object = g_object_new(
    INF_TEXT_TYPE_DEFAULT_DELETE_OPERATION,
    "position", position,
    "chunk", chunk,
    NULL
  );

  return INF_TEXT_DEFAULT_DELETE_OPERATION(object);
}

/**
 * inf_text_default_delete_operation_get_chunk:
 * @operation: A #InfTextDefaultDeleteOperation.
 *
 * Returns the text deleted by @operation.
 *
 * Return Value: A #InfTextChunk, owned by the operation.
 **/
InfTextChunk*
inf_text_default_delete_operation_get_chunk(
  InfTextDefaultDeleteOperation* operation)
{
  g_return_val_if_fail(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(operation), NULL);
  return INF_TEXT_DEFAULT_DELETE_OPERATION_PRIVATE(operation)->chunk;
}

/* vim:set et sw=2 ts=2: */
