/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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

#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-buffer.h>
#include <libinftext/inf-text-chunk.h>
#include <libinfinity/common/inf-buffer.h>

struct _InfTextBufferIter {
  InfTextChunkIter chunk_iter;
};

typedef struct _InfTextDefaultBufferPrivate InfTextDefaultBufferPrivate;
struct _InfTextDefaultBufferPrivate {
  gchar* encoding;
  InfTextChunk* chunk;
  gboolean modified;
};

enum {
  PROP_0,

  /* overwritten */
  PROP_MODIFIED,

  PROP_ENCODING
};

#define INF_TEXT_DEFAULT_BUFFER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_DEFAULT_BUFFER, InfTextDefaultBufferPrivate))

static void inf_text_default_buffer_buffer_iface_init(InfBufferInterface* iface);
static void inf_text_default_buffer_text_buffer_iface_init(InfTextBufferInterface* iface);
G_DEFINE_TYPE_WITH_CODE(InfTextDefaultBuffer, inf_text_default_buffer, G_TYPE_OBJECT,
  G_ADD_PRIVATE(InfTextDefaultBuffer)
  G_IMPLEMENT_INTERFACE(INF_TYPE_BUFFER, inf_text_default_buffer_buffer_iface_init)
  G_IMPLEMENT_INTERFACE(INF_TEXT_TYPE_BUFFER, inf_text_default_buffer_text_buffer_iface_init))

static void
inf_text_default_buffer_init(InfTextDefaultBuffer* buffer)
{
  InfTextDefaultBufferPrivate* priv;
  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(buffer);

  priv->encoding = NULL;
  priv->chunk = NULL;
  priv->modified = FALSE;
}

static void
inf_text_default_buffer_finalize(GObject* object)
{
  InfTextDefaultBuffer* default_buffer;
  InfTextDefaultBufferPrivate* priv;

  default_buffer = INF_TEXT_DEFAULT_BUFFER(object);
  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(default_buffer);

  inf_text_chunk_free(priv->chunk);
  g_free(priv->encoding);

  G_OBJECT_CLASS(inf_text_default_buffer_parent_class)->finalize(object);
}

static void
inf_text_default_buffer_set_property(GObject* object,
                                     guint prop_id,
                                     const GValue* value,
                                     GParamSpec* pspec)
{
  InfTextDefaultBuffer* default_buffer;
  InfTextDefaultBufferPrivate* priv;

  default_buffer = INF_TEXT_DEFAULT_BUFFER(object);
  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(default_buffer);

  switch(prop_id)
  {
  case PROP_ENCODING:
    /* construct only */
    g_assert(priv->encoding == NULL);
    g_assert(priv->chunk == NULL);

    priv->encoding = g_value_dup_string(value);
    priv->chunk = inf_text_chunk_new(priv->encoding);
    break;
  case PROP_MODIFIED:
    priv->modified = g_value_get_boolean(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_default_buffer_get_property(GObject* object,
                                     guint prop_id,
                                     GValue* value,
                                     GParamSpec* pspec)
{
  InfTextDefaultBuffer* default_buffer;
  InfTextDefaultBufferPrivate* priv;

  default_buffer = INF_TEXT_DEFAULT_BUFFER(object);
  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(default_buffer);

  switch(prop_id)
  {
  case PROP_ENCODING:
    g_value_set_string(value, priv->encoding);
    break;
  case PROP_MODIFIED:
    g_value_set_boolean(value, priv->modified);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static gboolean
inf_text_default_buffer_buffer_get_modified(InfBuffer* buffer)
{
  return INF_TEXT_DEFAULT_BUFFER_PRIVATE(buffer)->modified;
}

static void
inf_text_default_buffer_buffer_set_modified(InfBuffer* buffer,
                                            gboolean modified)
{
  InfTextDefaultBuffer* default_buffer;
  InfTextDefaultBufferPrivate* priv;

  default_buffer = INF_TEXT_DEFAULT_BUFFER(buffer);
  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(default_buffer);

  if(priv->modified != modified)
  {
    priv->modified = modified;
    g_object_notify(G_OBJECT(buffer), "modified");
  }
}

static const gchar*
inf_text_default_buffer_buffer_get_encoding(InfTextBuffer* buffer)
{
  return INF_TEXT_DEFAULT_BUFFER_PRIVATE(buffer)->encoding;
}

static guint
inf_text_default_buffer_get_length(InfTextBuffer* buffer)
{
  InfTextDefaultBufferPrivate* priv;
  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(buffer);
  return inf_text_chunk_get_length(priv->chunk);
}

static InfTextChunk*
inf_text_default_buffer_buffer_get_slice(InfTextBuffer* buffer,
                                         guint pos,
                                         guint len)
{
  InfTextDefaultBufferPrivate* priv;
  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(buffer);
  return inf_text_chunk_substring(priv->chunk, pos, len);
}

static void
inf_text_default_buffer_buffer_insert_text(InfTextBuffer* buffer,
                                           guint pos,
                                           InfTextChunk* chunk,
                                           InfUser* user)
{
  InfTextDefaultBufferPrivate* priv;
  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(buffer);

  inf_text_chunk_insert_chunk(priv->chunk, pos, chunk);

  inf_text_buffer_text_inserted(buffer, pos, chunk, user);

  if(priv->modified == FALSE)
  {
    priv->modified = TRUE;
    g_object_notify(G_OBJECT(buffer), "modified");
  }
}

static void
inf_text_default_buffer_buffer_erase_text(InfTextBuffer* buffer,
                                          guint pos,
                                          guint len,
                                          InfUser* user)
{
  InfTextDefaultBufferPrivate* priv;
  InfTextChunk* chunk;

  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(buffer);

  chunk = inf_text_chunk_substring(priv->chunk, pos, len);
  inf_text_chunk_erase(priv->chunk, pos, len);

  inf_text_buffer_text_erased(buffer, pos, chunk, user);
  inf_text_chunk_free(chunk);

  if(priv->modified == FALSE)
  {
    priv->modified = TRUE;
    g_object_notify(G_OBJECT(buffer), "modified");
  }
}

static InfTextBufferIter*
inf_text_default_buffer_buffer_create_begin_iter(InfTextBuffer* buffer)
{
  InfTextDefaultBufferPrivate* priv;
  InfTextChunkIter chunk_iter;
  InfTextBufferIter* iter;

  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(buffer);
  if(inf_text_chunk_iter_init_begin(priv->chunk, &chunk_iter) == TRUE)
  {
    iter = g_slice_new(InfTextBufferIter);
    iter->chunk_iter = chunk_iter;
    return iter;
  }
  else
  {
    return NULL;
  }
}

static InfTextBufferIter*
inf_text_default_buffer_buffer_create_end_iter(InfTextBuffer* buffer)
{
  InfTextDefaultBufferPrivate* priv;
  InfTextChunkIter chunk_iter;
  InfTextBufferIter* iter;

  priv = INF_TEXT_DEFAULT_BUFFER_PRIVATE(buffer);
  if(inf_text_chunk_iter_init_end(priv->chunk, &chunk_iter) == TRUE)
  {
    iter = g_slice_new(InfTextBufferIter);
    iter->chunk_iter = chunk_iter;
    return iter;
  }
  else
  {
    return NULL;
  }
}

static void
inf_text_default_buffer_buffer_destroy_iter(InfTextBuffer* buffer,
                                            InfTextBufferIter* iter)
{
  g_slice_free(InfTextBufferIter, iter);
}

static gboolean
inf_text_default_buffer_buffer_iter_next(InfTextBuffer* buffer,
                                         InfTextBufferIter* iter)
{
  return inf_text_chunk_iter_next(&iter->chunk_iter);
}

static gboolean
inf_text_default_buffer_buffer_iter_prev(InfTextBuffer* buffer,
                                         InfTextBufferIter* iter)
{
  return inf_text_chunk_iter_prev(&iter->chunk_iter);
}

static gpointer
inf_text_default_buffer_buffer_iter_get_text(InfTextBuffer* buffer,
                                             InfTextBufferIter* iter)
{
  return g_memdup(
    inf_text_chunk_iter_get_text(&iter->chunk_iter),
    inf_text_chunk_iter_get_bytes(&iter->chunk_iter)
  );
}

static guint
inf_text_default_buffer_buffer_iter_get_offset(InfTextBuffer* buffer,
                                               InfTextBufferIter* iter)
{
  return inf_text_chunk_iter_get_offset(&iter->chunk_iter);
}

static guint
inf_text_default_buffer_buffer_iter_get_length(InfTextBuffer* buffer,
                                               InfTextBufferIter* iter)
{
  return inf_text_chunk_iter_get_length(&iter->chunk_iter);
}

static gsize
inf_text_default_buffer_buffer_iter_get_bytes(InfTextBuffer* buffer,
                                              InfTextBufferIter* iter)
{
  return inf_text_chunk_iter_get_bytes(&iter->chunk_iter);
}

static guint
inf_text_default_buffer_buffer_iter_get_author(InfTextBuffer* buffer,
                                               InfTextBufferIter* iter)
{
  return inf_text_chunk_iter_get_author(&iter->chunk_iter);
}

static void
inf_text_default_buffer_class_init(
  InfTextDefaultBufferClass* default_buffer_class)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(default_buffer_class);

  object_class->finalize = inf_text_default_buffer_finalize;
  object_class->set_property = inf_text_default_buffer_set_property;
  object_class->get_property = inf_text_default_buffer_get_property;

  g_object_class_override_property(object_class, PROP_MODIFIED, "modified");

  g_object_class_install_property(
    object_class,
    PROP_ENCODING,
    g_param_spec_string(
      "encoding",
      "Encoding",
      "The character encoding of the text buffer",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
inf_text_default_buffer_buffer_iface_init(InfBufferInterface* iface)
{
  iface->get_modified = inf_text_default_buffer_buffer_get_modified;
  iface->set_modified = inf_text_default_buffer_buffer_set_modified;
}

static void
inf_text_default_buffer_text_buffer_iface_init(InfTextBufferInterface* iface)
{
  iface->get_encoding = inf_text_default_buffer_buffer_get_encoding;
  iface->get_length = inf_text_default_buffer_get_length;
  iface->get_slice = inf_text_default_buffer_buffer_get_slice;
  iface->insert_text = inf_text_default_buffer_buffer_insert_text;
  iface->erase_text = inf_text_default_buffer_buffer_erase_text;
  iface->create_begin_iter = inf_text_default_buffer_buffer_create_begin_iter;
  iface->create_end_iter = inf_text_default_buffer_buffer_create_end_iter;
  iface->destroy_iter = inf_text_default_buffer_buffer_destroy_iter;
  iface->iter_next = inf_text_default_buffer_buffer_iter_next;
  iface->iter_prev = inf_text_default_buffer_buffer_iter_prev;
  iface->iter_get_text = inf_text_default_buffer_buffer_iter_get_text;
  iface->iter_get_offset = inf_text_default_buffer_buffer_iter_get_offset;
  iface->iter_get_length = inf_text_default_buffer_buffer_iter_get_length;
  iface->iter_get_bytes = inf_text_default_buffer_buffer_iter_get_bytes;
  iface->iter_get_author = inf_text_default_buffer_buffer_iter_get_author;
  iface->text_inserted = NULL;
  iface->text_erased = NULL;
}

/**
 * inf_text_default_buffer_new: (constructor)
 * @encoding: The character encoding to use for the buffer.
 *
 * Creates a new, empty #InfTextDefaultBuffer.
 *
 * Returns: (transfer full): A #InfTextDefaultBuffer.
 **/
InfTextDefaultBuffer*
inf_text_default_buffer_new(const gchar* encoding)
{
  GObject* object;

  object = g_object_new(
    INF_TEXT_TYPE_DEFAULT_BUFFER,
    "encoding", encoding,
    NULL
  );

  return INF_TEXT_DEFAULT_BUFFER(object);
}

/* vim:set et sw=2 ts=2: */
