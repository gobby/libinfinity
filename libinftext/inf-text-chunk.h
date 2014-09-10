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

#ifndef __INF_TEXT_CHUNK_H__
#define __INF_TEXT_CHUNK_H__

#include <libxml/tree.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INF_TEXT_TYPE_CHUNK_ITER       (inf_text_chunk_iter_get_type())
#define INF_TEXT_TYPE_CHUNK            (inf_text_chunk_get_type())

/**
 * InfTextChunk:
 *
 * #InfTextChunk is an opaque data type. You should only access it
 * via the public API functions.
 */
typedef struct _InfTextChunk InfTextChunk;

/**
 * InfTextChunkIter:
 *
 * #InfTextChunkIter is an opaque data type. You should only access it
 * via the public API functions.
 *
 * #InfTextChunkIter can be safely allocated on the stack and copied by value.
 * Use inf_text_chunk_iter_init_begin() or inf_text_chunk_iter_init_end() to
 * initialize a #InfTextChunkIter. There is no deinitialization required. A
 * #InfTextChunkIter is valid as long as the chunk is not modified.
 */
typedef struct _InfTextChunkIter InfTextChunkIter;
struct _InfTextChunkIter {
  /*< private >*/
  InfTextChunk* chunk;
  GSequenceIter* first;
  GSequenceIter* second;
};

GType
inf_text_chunk_iter_get_type(void) G_GNUC_CONST;

GType
inf_text_chunk_get_type(void) G_GNUC_CONST;

InfTextChunkIter*
inf_text_chunk_iter_copy(const InfTextChunkIter* iter);

void
inf_text_chunk_iter_free(InfTextChunkIter* iter);

InfTextChunk*
inf_text_chunk_new(const gchar* encoding);

InfTextChunk*
inf_text_chunk_copy(InfTextChunk* self);

void
inf_text_chunk_free(InfTextChunk* self);

const gchar*
inf_text_chunk_get_encoding(InfTextChunk* self);

guint
inf_text_chunk_get_length(InfTextChunk* self);

InfTextChunk*
inf_text_chunk_substring(InfTextChunk* self,
                         guint begin,
                         guint length);

void
inf_text_chunk_insert_text(InfTextChunk* self,
                           guint offset,
                           gconstpointer text,
                           gsize bytes,
                           guint length,
                           guint author);

void
inf_text_chunk_insert_chunk(InfTextChunk* self,
                            guint offset,
                            InfTextChunk* text);

void
inf_text_chunk_erase(InfTextChunk* self,
                     guint begin,
                     guint length);

gpointer
inf_text_chunk_get_text(InfTextChunk* self,
                        gsize* length);

gboolean
inf_text_chunk_equal(InfTextChunk* self,
                     InfTextChunk* other);

gboolean
inf_text_chunk_iter_init_begin(InfTextChunk* self,
                               InfTextChunkIter* iter);

gboolean
inf_text_chunk_iter_init_end(InfTextChunk* self,
                             InfTextChunkIter* iter);

gboolean
inf_text_chunk_iter_next(InfTextChunkIter* iter);

gboolean
inf_text_chunk_iter_prev(InfTextChunkIter* iter);

gconstpointer
inf_text_chunk_iter_get_text(InfTextChunkIter* iter);

guint
inf_text_chunk_iter_get_offset(InfTextChunkIter* iter);

guint
inf_text_chunk_iter_get_length(InfTextChunkIter* iter);

gsize
inf_text_chunk_iter_get_bytes(InfTextChunkIter* iter);

guint
inf_text_chunk_iter_get_author(InfTextChunkIter* iter);

G_END_DECLS

#endif /* __INF_TEXT_CHUNK_H__ */

/* vim:set et sw=2 ts=2: */
