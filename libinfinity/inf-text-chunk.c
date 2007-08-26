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

#include <libinfinity/text/inf-text-chunk.h>

struct _InfTextChunk {
  GSequence* segments;
  guint length; /* in characters */
  const gchar* encoding;
};

struct _InfTextChunkSegment {
  guint author;
  gpointer text;
  gsize length; /* in bytes */
  guint offset; /* absolute to chunk begin in characters */
};

static void
inf_text_chunk_segment_free(InfTextChunkSegment* segment)
{
  g_free(segment->text);
  g_slice_free(InfTextChunkSegment, segment);
}

static int
inf_text_chunk_segment_cmp(gconstpointer first,
                           gconstpointer second,
                           gpointer userdata)
{
  const InfTextChunkSegment* first_segment = first;
  const InfTextChunkSegment* second_segment = second;

  g_return_val_if_fail(second != NULL && first != NULL, 0);

  if (first->offset < second->offset)
    return -1;
  else if (first->offset == second->offset)
    return 0;
  else
    return 1;
}

static guint
inf_text_chunk_next_offset(InfTextChunk* self,
                           GSequenceIter* iter)
{
  GSequenceIter* next_iter;

  g_assert(iter != g_sequnce_get_end_iter(self->segments));
  
  next_iter = g_sequence_iter_next(iter);
  if(next_iter == g_sequence_get_end_iter())
    return self->length;
  else
    return ((InfTextChunkSegment*)g_sequence_get(next_iter))->offset;
}

static GSequenceIter*
inf_text_chunk_get_segment_at_pos(InfTextChunk* self,
                                  guint pos,
                                  guint* index)
{
  InfTextChunkSegment* found;
  InfTextChunkSegment key;
  GSequenceIter* iter;

  GIConv cd;
  gchar buffer[4];

  gchar* inbuf;
  gchar* outbuf;
  gsize inlen;
  gsize outlen;
  guint count;
  gsize result;

  g_assert(pos <= self->length);

  /* only offset is relevant for lookup */
  key.offset = pos;

  /* TODO: Verify this does binary search */
  iter = g_sequence_search(
    self->segments,
    NULL,
    inf_text_chunk_segment_cmp,
    &key
  );

  if(self->length > 0)
  {
    g_assert(iter != g_sequence_get_begin_iter(self->segments));

    iter = g_sequence_iter_prev(iter);
    found = g_sequence_get(iter);

    g_assert(pos >= found->offset);

    /* This is not "<=" because it should rather find position 0 on the
     * next segment in that case. */
    /* TODO: I think this has to be <= for the last segment */
    g_assert(pos < inf_text_chunk_next_offset(self, iter));

    /* Find byte index in the segment where the specified character starts.
     * This is rather ugly, I wish iconv or glib or someone had some nice(r)
     * API for this. */
    if(index != NULL)
    {
      /* We convert the segment's text into UCS-4, character by character.
       * This assumes every UCS-4 character is 4 bytes in length */

      /* TODO: Can we use some faster glib UTF-8 functions in case
       * self->encoding is UTF-8? */
      cd = g_iconv_open("UCS-4", self->encoding);
      g_assert(cd != (GIConv)-1);
      
      inbuf = found->data;
      inlen = found->length;

      for(count = 0; count < pos - found->offset; ++ count)
      {
        g_assert(inlen > 0);

        outbuf = buffer;
        outlen = 4;

        result = g_iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
        g_assert(result == 0);
      }

      g_iconv_close(cd);
      *index = found->length - inlen;
    }
  }
  else
  {
    if(*index)
      *index = 0;
  }
  
  return iter;
}

GType
inf_text_chunk_get_type(void)
{
  static GType chunk_type = 0;

  if(!chunk_type)
  {
    chunk_type = g_boxed_type_register_static(
      "InfTextChunk",
      (GBoxedCopyFunc)inf_text_chunk_copy,
      (GBoxedFreeFunc)inf_text_chunk_free
    );
  }

  return chunk_type;
}

/** inf_text_chunk_new:
 *
 * @encoding: A content encoding, such as "UTF-8" or "LATIN1".
 *
 * Creates e new #InfTextChunk with no initial content that holds text
 * in the given encoding. TODO: Allow binary data with %NULL encoding.
 *
 * Return Value: A new #InfTextChunk.
 **/
InfTextChunk*
inf_text_chunk_new(const gchar* encoding)
{
  InfTextChunk* chunk = g_slice_new(InfTextChunk);
  
  chunk->segments = g_sequence_new(
    (GDestroyNotify)inf_text_chunk_segment_free
  );

  chunk->length = 0;
  chunk->encoding = encoding;
}

/** inf_text_chunk_copy:
 *
 * @self: A #InfTextChunk.
 *
 * Returns a copy of @self.
 *
 * Return Value: A new #InfTextChunk.
 **/
InfTextChunk*
inf_text_chunk_copy(InfTextChunk* self)
{
  InfTextChunk* new_chunk;
  GSequenceIter* iter;

  g_return_val_if_fail(self != NULL, NULL);

  new_chunk->segments = g_sequence_new(
    (GDestroyNotify)inf_text_chunk_segment_free
  );
  
  for(iter = g_sequence_get_begin_iter(self->segments);
      iter != g_sequnce_get_end_iter(self->segment);
      iter = g_sequence_iter_next(self->segment))
  {
    InfTextChunkSegment* segment = g_sequence_get(iter);
    InfTextChunkSegment* new_segment = g_slice_new(InfTextChunkSegment);
    new_segment->author = segment->author;
    new_segment->text = g_memdup(segment->text, segment->length);
    new_segment->length = segment->length;
    new_segment->offset = segment->offset;
    g_sequnce_append(new_chunk->segments, new_segment);
  }

  new_chunk->length = self->length;
  new_chunk->encoding = self->encoding;
  return new_chunk;
}

/** inf_text_chunk_free:
 *
 * @self: A #InfTextChunk.
 *
 * Frees a #InfTextChunk allocated with inf_text_chunk_new(),
 * inf_text_chunk_copy() or inf_text_chunk_substring().
 **/
void
inf_text_chunk_free(InfTextChunk* self)
{
  g_return_if_fail(self != NULL);
  g_sequence_free(self->segments);
  g_slice_free(InfTextChunk, self);
}

/** inf_text_chunk_get_encoding:
 *
 * @self: A #InfTextChunk.
 *
 * Returns the character encoding in which the content of @self is encoded.
 *
 * Return Value: The encoding of @self.
 **/
const gchar*
inf_text_chunk_get_encoding(InfTextChunk* self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return self->encoding;
}

/** inf_text_chunk_get_length:
 *
 * @self: A #InfTextChunk.
 *
 * Returns the number of characters contained in @self.
 *
 * Return Value: The number of characters of @self.
 **/
guint
inf_text_chunk_get_length(InfTextChunk* self)
{
  g_return_val_if_fail(self != NULL, 0);
  return self->length;
}

/** inf_text_chunk_substring:
 *
 * @self: A #InfTextChunk.
 * @begin: A character offset into @self.
 * @length: The length of the text to extract.
 *
 * Returns a new #InfTextChunk containing a substring of @self, beginning
 * at character offset @begin and @length characters long.
 *
 * Return Value: A new #InfTextChunk.
 **/
InfTextChunk*
inf_text_chunk_substring(InfTextChunk* self,
                         guint begin,
                         guint length)
{
  GSequenceIter* next_iter;
  GSequenceIter* end_iter;
  gsize begin_index;
  gsize end_index;

  InfTextChunk* result;
  InfTextChunkSegment* segment;
  InfTextChunkSegment* new_segment;
  guint current_length;

  g_return_val_if_fail(self != NULL, NULL);
  g_return_val_if_fail(begin + length < self->length, NULL);
  
  if(length > 0)
  {
    begin_iter = get_segment_at_pos(self, begin, &begin_index);
    end_iter = get_segment_at_pos(self, begin + length, &end_index);

    if(end_index == 0)
    {
      g_assert(end_iter != g_sequence_get_end_iter(self->segments));
      end_iter = g_sequence_iter_prev(end_iter);
      end_index = ((InfTextChunkSegment*)g_sequence_get(end_iter))->length;
    }

    result = inf_text_chunk_new();

    current_length = 0;
    segment = g_sequence_get(begin_iter);

    while(begin_iter != end_iter)
    {
      new_segment = g_slice_new(InfTextChunkSegment);
      new_segment->author = segment->author;

      new_segment->data = g_memdup(
        (gchar*)segment->data + begin_index,
        segment->length - begin_index
      );
      
      new_segment->length = segment->length - begin_index;
      new_segment->offset = current_length;

      begin_iter = g_sequnce_iter_next(begin_iter);
      segment = g_sequence_get(begin_iter);

      /* Add (remaining) length of this segment to current length */
      current_length += segment->offset - begin;

      /* So we get the next segment from the beginning. This may only be
       * non-zero during the first iteration. */
      begin_index = 0;
      begin = new_segment->offset;
      
      g_sequence_append(result->segments, new_segment);
    }

    /* Don't forget last segment */
    new_segment = g_slice_new(InfTextChunkSegment);
    new_segment->author = segment->author;
    new_segment->data = g_memdup(
      (gchar*)segment->data + begin_index,
      end_index - begin_index
    );
    
    new_segment->length = end_index - begin_index;
    new_segment->offset = current_length;
    
    g_sequence_append(result->segments, new_segment);

    result->length = length;
    result->encoding = self->encoding;
    return result;
  }
  else
  {
    g_return_val_if_fail(begin == 0, NULL);

    /* New, empty chunk */
    return inf_text_chunk_new();
  }
}

void
inf_text_chunk_insert(InfTextChunk* self,
                      guint offset,
                      const gchar* text,
                      guint author);

void
inf_text_chunk_insert(InfTextChunk* self,
                      guint offset,
                      InfTextChunk* text);

void
inf_text_chunk_erase(InfTextChunk* self,
                     guint begin,
                     guint length);

/* vim:set et sw=2 ts=2: */
