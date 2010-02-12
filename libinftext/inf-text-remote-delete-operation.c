/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#include <libinftext/inf-text-remote-delete-operation.h>
#include <libinftext/inf-text-default-delete-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/adopted/inf-adopted-split-operation.h>
#include <libinfinity/adopted/inf-adopted-operation.h>

/* recon is helper information to reconstruct the original delete operation.
 * It stores the parts that have been erased from the remote delete operation
 * by transforming against other delete operations, including the deleted
 * text that is obtained from the operations transformed against. This
 * is necessary because that information is no longer available in the buffer
 * in the state the remote delete operation is made reversible. */
typedef struct _InfTextRemoteDeleteOperationRecon
  InfTextRemoteDeleteOperationRecon;
struct _InfTextRemoteDeleteOperationRecon {
  guint position;
  InfTextChunk* chunk;
};

typedef struct _InfTextRemoteDeleteOperationPrivate
  InfTextRemoteDeleteOperationPrivate;
struct _InfTextRemoteDeleteOperationPrivate {
  guint position;
  guint length;

  /* TODO: Don't use GSList but link InfTextRemoteDeleteOperationRecon
   * directly? */
  GSList* recon;
  /* TODO: We don't actually need recon_offset, it is just used in an
   * assertion. Perhaps keep in debug code. */
  guint recon_offset;
};

enum {
  PROP_0,

  PROP_POSITION,
  PROP_LENGTH
};

#define INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_REMOTE_DELETE_OPERATION, InfTextRemoteDeleteOperationPrivate))

static GObjectClass* parent_class;

/* This appends an element to a GSList more efficiently than
 * g_slist_append() when the last item of the list in known. This updates
 * *last to point to the new last item. */
static GSList*
g_slist_append_fast(GSList* list,
                    GSList** last,
                    gpointer item)
{
  GSList* temp;

  g_assert(last != NULL);
  if(list == NULL)
  {
    list = g_slist_prepend(list, item);
    *last = list;
  }
  else
  {
    g_assert(*last != NULL);

    /* We don't actually need the return value, but glib warns if it
     * remains unused. We know that g_slist_append does not change the list
     * because the list has at least one element (otherwise it would be
     * NULL). */
    temp = g_slist_append(*last, item);
    *last = (*last)->next;
  }

  return list;
}

static GSList*
inf_text_remote_delete_operation_recon_copy(GSList* recon_list)
{
  GSList* item;
  InfTextRemoteDeleteOperationRecon* recon;

  GSList* new_list;
  GSList* last;
  InfTextRemoteDeleteOperationRecon* new_recon;

  new_list = NULL;
  for(item = recon_list; item != NULL; item = g_slist_next(item))
  {
    recon = (InfTextRemoteDeleteOperationRecon*)item->data;
    new_recon = g_slice_new(InfTextRemoteDeleteOperationRecon);
    new_recon->position = recon->position;
    new_recon->chunk = inf_text_chunk_copy(recon->chunk);
    new_list = g_slist_append_fast(new_list, &last, new_recon);
  }

  return new_list;
}

static void
inf_text_remote_delete_operation_recon_free(GSList* recon_list)
{
  GSList* item;
  InfTextRemoteDeleteOperationRecon* recon;

  for(item = recon_list; item != NULL; item = g_slist_next(item))
  {
    recon = (InfTextRemoteDeleteOperationRecon*)item->data;
    inf_text_chunk_free(recon->chunk);
    g_slice_free(InfTextRemoteDeleteOperationRecon, recon);
  }

  g_slist_free(recon_list);
}

/* TODO: Make this work inline, adjust usages */
/* TODO: Merge adjacent text chunks */
static GSList*
inf_text_remote_delete_operation_recon_feed(GSList* recon_list,
                                            guint position,
                                            InfTextChunk* chunk)
{
  GSList* item;
  InfTextRemoteDeleteOperationRecon* recon;
  GSList* new_list;
  GSList* last;
  InfTextRemoteDeleteOperationRecon* new_recon;

  guint text_pos;
  guint cur_len;
  guint text_len;

  new_list = NULL;
  text_pos = 0;
  cur_len = 0;

  for(item = recon_list; item != NULL; item = g_slist_next(item))
  {
    recon = (InfTextRemoteDeleteOperationRecon*)item->data;
    if(position + text_pos + cur_len < recon->position &&
       text_pos < inf_text_chunk_get_length(chunk))
    {
      text_len = recon->position - position - text_pos - cur_len;
      if(text_len > inf_text_chunk_get_length(chunk) - text_pos)
        text_len = inf_text_chunk_get_length(chunk) - text_pos;

      new_recon = g_slice_new(InfTextRemoteDeleteOperationRecon);
      new_recon->position = position + text_pos + cur_len;
      new_recon->chunk = inf_text_chunk_substring(chunk, text_pos, text_len);
      new_list = g_slist_append_fast(new_list, &last, new_recon);
      text_pos += text_len;
    }

    cur_len += inf_text_chunk_get_length(recon->chunk);
    new_recon = g_slice_new(InfTextRemoteDeleteOperationRecon);
    new_recon->position = recon->position;
    new_recon->chunk = inf_text_chunk_copy(recon->chunk);
    new_list = g_slist_append_fast(new_list, &last, new_recon);
  }

  if(text_pos < inf_text_chunk_get_length(chunk))
  {
    new_recon = g_slice_new(InfTextRemoteDeleteOperationRecon);
    new_recon->position = position + text_pos + cur_len;
    new_recon->chunk = inf_text_chunk_substring(
      chunk,
      text_pos,
      inf_text_chunk_get_length(chunk) - text_pos
    );

    new_list = g_slist_append_fast(new_list, &last, new_recon);
  }

  return new_list;
}

static void
inf_text_remote_delete_operation_init(GTypeInstance* instance,
                                      gpointer g_class)
{
  InfTextRemoteDeleteOperation* operation;
  InfTextRemoteDeleteOperationPrivate* priv;

  operation = INF_TEXT_REMOTE_DELETE_OPERATION(instance);
  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation);

  priv->position = 0;
  priv->length = 0;

  priv->recon = NULL;
  priv->recon_offset = 0;
}

static void
inf_text_remote_delete_operation_finalize(GObject* object)
{
  InfTextRemoteDeleteOperation* operation;
  InfTextRemoteDeleteOperationPrivate* priv;

  operation = INF_TEXT_REMOTE_DELETE_OPERATION(object);
  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation);

  inf_text_remote_delete_operation_recon_free(priv->recon);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_text_remote_delete_operation_set_property(GObject* object,
                                              guint prop_id,
                                              const GValue* value,
                                              GParamSpec* pspec)
{
  InfTextRemoteDeleteOperation* operation;
  InfTextRemoteDeleteOperationPrivate* priv;

  operation = INF_TEXT_REMOTE_DELETE_OPERATION(object);
  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_POSITION:
    priv->position = g_value_get_uint(value);
    break;
  case PROP_LENGTH:
    priv->length = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_remote_delete_operation_get_property(GObject* object,
                                              guint prop_id,
                                              GValue* value,
                                              GParamSpec* pspec)
{
  InfTextRemoteDeleteOperation* operation;
  InfTextRemoteDeleteOperationPrivate* priv;

  operation = INF_TEXT_REMOTE_DELETE_OPERATION(object);
  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation);

  switch(prop_id)
  {
  case PROP_POSITION:
    g_value_set_uint(value, priv->position);
    break;
  case PROP_LENGTH:
    g_value_set_uint(value, priv->length);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static gboolean
inf_text_remote_delete_operation_need_concurrency_id(
  InfAdoptedOperation* operation,
  InfAdoptedOperation* against)
{
  g_assert(INF_TEXT_IS_REMOTE_DELETE_OPERATION(operation));

  return inf_text_delete_operation_need_concurrency_id(
    INF_TEXT_DELETE_OPERATION(operation),
    against
  );
}

static InfAdoptedConcurrencyId
inf_text_remote_delete_operation_get_concurrency_id(
  InfAdoptedOperation* operation,
  InfAdoptedOperation* against)
{
  g_assert(INF_TEXT_IS_REMOTE_DELETE_OPERATION(operation));

  return inf_text_delete_operation_get_concurrency_id(
    INF_TEXT_DELETE_OPERATION(operation),
    against
  );
}

static InfAdoptedOperation*
inf_text_remote_delete_operation_transform(InfAdoptedOperation* operation,
                                           InfAdoptedOperation* against,
                                           InfAdoptedConcurrencyId cid)
{
  g_assert(INF_TEXT_IS_REMOTE_DELETE_OPERATION(operation));

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
inf_text_remote_delete_operation_copy(InfAdoptedOperation* operation)
{
  InfTextRemoteDeleteOperationPrivate* priv;
  GObject* result;
  InfTextRemoteDeleteOperationPrivate* result_priv;

  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation);

  result = g_object_new(
    INF_TEXT_TYPE_REMOTE_DELETE_OPERATION,
    "position", priv->position,
    "length", priv->length,
    NULL
  );

  result_priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(result);

  result_priv->recon = inf_text_remote_delete_operation_recon_copy(
    priv->recon
  );

  result_priv->recon_offset = priv->recon_offset;
  return INF_ADOPTED_OPERATION(result);
}

static InfAdoptedOperationFlags
inf_text_remote_delete_operation_get_flags(InfAdoptedOperation* operation)
{
  /* RemoteDeleteOperation is not reversible because it does not know
   * what text has been deleted */
  return INF_ADOPTED_OPERATION_AFFECTS_BUFFER;
}

static void
inf_text_remote_delete_operation_apply(InfAdoptedOperation* operation,
                                       InfAdoptedUser* by,
                                       InfBuffer* buffer)
{
  InfTextRemoteDeleteOperationPrivate* priv;

  g_assert(INF_TEXT_IS_REMOTE_DELETE_OPERATION(operation));
  g_assert(INF_TEXT_IS_BUFFER(buffer));

  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation);

  inf_text_buffer_erase_text(
    INF_TEXT_BUFFER(buffer),
    priv->position,
    priv->length,
    INF_USER(by)
  );
}

static InfAdoptedOperation*
inf_text_remote_delete_operation_make_reversible(InfAdoptedOperation* op,
                                                 InfAdoptedOperation* with,
                                                 InfBuffer* buffer)
{
  InfTextRemoteDeleteOperationPrivate* priv;
  InfTextChunk* chunk;
  InfTextChunk* temp_slice;
  GSList* list;
  GSList* item;
  GSList* recon_list;
  GSList* recon_item;
  InfTextRemoteDeleteOperationRecon* recon;
  InfTextDefaultDeleteOperation* result;

  g_assert(INF_TEXT_IS_REMOTE_DELETE_OPERATION(op));
  g_assert(INF_TEXT_IS_BUFFER(buffer));

  /* TODO: We can probably optimize this function, but then we should
   * a) profile it and b) in many cases input parameters to this function
   * are trivial anyway. */
  if(INF_ADOPTED_IS_SPLIT_OPERATION(with))
  {
    list = inf_adopted_split_operation_unsplit(
      INF_ADOPTED_SPLIT_OPERATION(with)
    );
  }
  else
  {
    list = g_slist_prepend(NULL, with);
  }

  chunk = inf_text_chunk_new(
    inf_text_buffer_get_encoding(INF_TEXT_BUFFER(buffer))
  );

  /* We assume the list of remote delete operations to be in order */
  for(item = list; item != NULL; item = g_slist_next(item))
  {
    g_assert(INF_TEXT_IS_REMOTE_DELETE_OPERATION(item->data));
    priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(item->data);

    if(priv->length > 0)
    {
      temp_slice = inf_text_buffer_get_slice(
        INF_TEXT_BUFFER(buffer),
        priv->position,
        priv->length
      );

      recon_list = inf_text_remote_delete_operation_recon_feed(
        priv->recon,
        0,
        temp_slice
      );

      inf_text_chunk_free(temp_slice);
    }
    else
    {
      recon_list = priv->recon;
    }

    for(recon_item = recon_list;
        recon_item != NULL;
        recon_item = g_slist_next(recon_item))
    {
      recon = (InfTextRemoteDeleteOperationRecon*)recon_item->data;
      g_assert(priv->recon_offset + recon->position ==
               inf_text_chunk_get_length(chunk));

      inf_text_chunk_insert_chunk(
        chunk,
        inf_text_chunk_get_length(chunk),
        recon->chunk
      );
    }

    /* Free recon list if newly allocated */
    if(priv->length > 0)
      inf_text_remote_delete_operation_recon_free(recon_list);
  }

  g_slist_free(list);

  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(op);
  result = inf_text_default_delete_operation_new(priv->position, chunk);

  inf_text_chunk_free(chunk);

  return INF_ADOPTED_OPERATION(result);
}

static guint
inf_text_remote_delete_operation_get_position(
  InfTextDeleteOperation* operation)
{
  return INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation)->position;
}

static guint
inf_text_remote_delete_operation_get_length(InfTextDeleteOperation* operation)
{
  return INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation)->length;
}

static InfTextDeleteOperation*
inf_text_remote_delete_operation_transform_position(
  InfTextDeleteOperation* operation,
  guint position)
{
  InfTextRemoteDeleteOperationPrivate* priv;
  GObject* result;
  InfTextRemoteDeleteOperationPrivate* result_priv;

  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation);
  result = g_object_new(
    INF_TEXT_TYPE_REMOTE_DELETE_OPERATION,
    "position", position,
    "length", priv->length,
    NULL
  );
  result_priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(result);

  result_priv->recon = inf_text_remote_delete_operation_recon_copy(
    priv->recon
  );
  result_priv->recon_offset = priv->recon_offset;
  return INF_TEXT_DELETE_OPERATION(result);
}

static InfTextDeleteOperation*
inf_text_remote_delete_operation_transform_overlap(
  InfTextDeleteOperation* operation,
  InfTextDeleteOperation* other,
  guint position,
  guint begin,
  guint other_begin,
  guint length)
{
  InfTextRemoteDeleteOperationPrivate* priv;
  InfTextChunk* chunk;
  GObject* result;
  InfTextRemoteDeleteOperationPrivate* result_priv;


  /* It is actually possible that two remote delete operations are
   * transformed against each other (actually the parts of a splitted
   * remote delete operation). However, they must not overlap. */
  g_assert(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(other));

  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation);

  chunk = inf_text_chunk_substring(
    inf_text_default_delete_operation_get_chunk(
      INF_TEXT_DEFAULT_DELETE_OPERATION(other)
    ),
    other_begin,
    length
  );

  result = g_object_new(
    INF_TEXT_TYPE_REMOTE_DELETE_OPERATION,
    "position", position,
    "length", priv->length - length,
    NULL
  );

  result_priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(result);

  result_priv->recon = inf_text_remote_delete_operation_recon_feed(
    priv->recon,
    begin,
    chunk
  );

  inf_text_chunk_free(chunk);

  result_priv->recon_offset = priv->recon_offset;
  return INF_TEXT_DELETE_OPERATION(result);
}

static InfAdoptedSplitOperation*
inf_text_remote_delete_operation_transform_split(
  InfTextDeleteOperation* operation,
  guint split_pos,
  guint split_len)
{
  /* Need to split the delete operation and the recon list */
  InfTextRemoteDeleteOperationPrivate* priv;
  InfAdoptedSplitOperation* result;
  GObject* first_operation;
  GObject* second_operation;
  InfTextRemoteDeleteOperationPrivate* result_priv;
  InfTextRemoteDeleteOperationRecon* recon;
  InfTextRemoteDeleteOperationRecon* new_recon;
  GSList* first_recon;
  GSList* second_recon;
  guint recon_cur_len;
  GSList* item;

  priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(operation);

  first_recon = NULL;
  second_recon = NULL;
  recon_cur_len = 0;

  for(item = priv->recon; item != NULL; item = g_slist_next(item))
  {
    recon = (InfTextRemoteDeleteOperationRecon*)item->data;
    if(recon->position - recon_cur_len <= split_pos)
    {
      new_recon = g_slice_new(InfTextRemoteDeleteOperationRecon);
      new_recon->position = recon->position;
      new_recon->chunk = inf_text_chunk_copy(recon->chunk);
      first_recon = g_slist_prepend(first_recon, new_recon);
    }
    else
    {
      new_recon = g_slice_new(InfTextRemoteDeleteOperationRecon);
      new_recon->position = recon->position - (split_pos + recon_cur_len);
      new_recon->chunk = inf_text_chunk_copy(recon->chunk);
      second_recon = g_slist_prepend(second_recon, new_recon);
    }
  }

  first_operation = g_object_new(
    INF_TEXT_TYPE_REMOTE_DELETE_OPERATION,
    "position", priv->position,
    "length", split_pos,
    NULL
  );
  
  second_operation = g_object_new(
    INF_TEXT_TYPE_REMOTE_DELETE_OPERATION,
    "position", priv->position + split_pos + split_len,
    "length", priv->length - split_pos,
    NULL
  );

  result_priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(first_operation);
  result_priv->recon = first_recon;
  result_priv->recon_offset = priv->recon_offset;

  result_priv = INF_TEXT_REMOTE_DELETE_OPERATION_PRIVATE(second_operation);
  result_priv->recon = second_recon;
  result_priv->recon_offset = priv->recon_offset + split_pos + recon_cur_len;

  result = inf_adopted_split_operation_new(
    INF_ADOPTED_OPERATION(first_operation),
    INF_ADOPTED_OPERATION(second_operation)
  );

  g_object_unref(G_OBJECT(second_operation));
  g_object_unref(G_OBJECT(first_operation));
  return result;
}

static void
inf_text_remote_delete_operation_class_init(gpointer g_class,
                                            gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextRemoteDeleteOperationPrivate));

  object_class->finalize = inf_text_remote_delete_operation_finalize;
  object_class->set_property = inf_text_remote_delete_operation_set_property;
  object_class->get_property = inf_text_remote_delete_operation_get_property;

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
    PROP_LENGTH,
    g_param_spec_uint(
      "length",
      "Length",
      "The length of the deleted text",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
inf_text_remote_delete_operation_operation_init(gpointer g_iface,
                                                gpointer iface_data)
{
  InfAdoptedOperationIface* iface;
  iface = (InfAdoptedOperationIface*)g_iface;

  iface->need_concurrency_id =
    inf_text_remote_delete_operation_need_concurrency_id;
  iface->get_concurrency_id =
    inf_text_remote_delete_operation_get_concurrency_id;
  iface->transform = inf_text_remote_delete_operation_transform;
  iface->copy = inf_text_remote_delete_operation_copy;
  iface->get_flags = inf_text_remote_delete_operation_get_flags;
  iface->apply = inf_text_remote_delete_operation_apply;
  /* RemoteDeleteOperation is not reversible */
  iface->revert = NULL;
  iface->make_reversible = inf_text_remote_delete_operation_make_reversible;
}

static void
inf_text_remote_delete_operation_delete_operation_init(gpointer g_iface,
                                                       gpointer iface_data)
{
  InfTextDeleteOperationIface* iface;
  iface = (InfTextDeleteOperationIface*)g_iface;

  iface->get_position = inf_text_remote_delete_operation_get_position;
  iface->get_length = inf_text_remote_delete_operation_get_length;
  iface->transform_position =
    inf_text_remote_delete_operation_transform_position;
  iface->transform_overlap =
    inf_text_remote_delete_operation_transform_overlap;
  iface->transform_split = inf_text_remote_delete_operation_transform_split;
}

GType
inf_text_remote_delete_operation_get_type(void)
{
  static GType remote_delete_operation_type = 0;

  if(!remote_delete_operation_type)
  {
    static const GTypeInfo remote_delete_operation_type_info = {
      sizeof(InfTextRemoteDeleteOperationClass),   /* class_size */
      NULL,                                        /* base_init */
      NULL,                                        /* base_finalize */
      inf_text_remote_delete_operation_class_init, /* class_init */
      NULL,                                        /* class_finalize */
      NULL,                                        /* class_data */
      sizeof(InfTextRemoteDeleteOperation),        /* instance_size */
      0,                                           /* n_preallocs */
      inf_text_remote_delete_operation_init,       /* instance_init */
      NULL                                         /* value_table */
    };

    static const GInterfaceInfo operation_info = {
      inf_text_remote_delete_operation_operation_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo delete_operation_info = {
      inf_text_remote_delete_operation_delete_operation_init,
      NULL,
      NULL
    };

    remote_delete_operation_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfTextRemoteDeleteOperation",
      &remote_delete_operation_type_info,
      0
    );

    g_type_add_interface_static(
      remote_delete_operation_type,
      INF_ADOPTED_TYPE_OPERATION,
      &operation_info
    );

    g_type_add_interface_static(
      remote_delete_operation_type,
      INF_TEXT_TYPE_DELETE_OPERATION,
      &delete_operation_info
    );
  }

  return remote_delete_operation_type;
}

/**
 * inf_text_remote_delete_operation_new:
 * @position: The position at which to delete text.
 * @length: The number of characters to delete.
 *
 * Creates a new delete operation that, when applied, deletes @length
 * characters starting from position @position. Note that this operation is
 * not reversible because it does not know the text to delete and is therefore
 * only used to transmit a delete operation through the network to reduce
 * bandwidth usage. The other part can then reconstruct the deleted text
 * using the make_reversible vfunc.
 *
 * However, it is easier to just use #InfTextDefaultDeleteOperation if you
 * want the  operation to be reversible.
 *
 * Return Value: A new #InfTextRemoteDeleteOperation.
 **/
InfTextRemoteDeleteOperation*
inf_text_remote_delete_operation_new(guint position,
                                     guint length)
{
  GObject* object;

  object = g_object_new(
    INF_TEXT_TYPE_REMOTE_DELETE_OPERATION,
    "position", position,
    "length", length,
    NULL
  );

  return INF_TEXT_REMOTE_DELETE_OPERATION(object);
}

/* vim:set et sw=2 ts=2: */
