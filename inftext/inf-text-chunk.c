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

#include <libinftext/inf-text-chunk.h>

struct _InfTextChunk {
  GSequence* segments;
  guint length; /* in characters */
  const gchar* encoding;
};

struct _InfTextChunkSegment {
  guint author;
  /* This is gchar so that we can do pointer arithmetic. It does not
   * necessarily store a full character in each byte. This depends on the
   * encoding specified in the InfTextChunk. */
  gchar* text;
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
 * Creates a new #InfTextChunk with no initial content that holds text
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
  g_return_val_if_fail(begin + length <= self->length, NULL);

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

/** inf_text_chunk_insert:
 *
 * @self: A #InfTextChunk.
 * @offset: Character offset at which to insert text
 * @text: Text to insert.
 * @length: Number of characters contained in @text.
 * @bytes: Number of bytes of @text.
 * @author: User that wrote @text.
 *
 * Inserts text written by @author into @self. @text is exepected to be in
 * the chunk's encoding.
 **/
void
inf_text_chunk_insert(InfTextChunk* self,
                      guint offset,
                      const gchar* text,
                      guint length,
                      guint bytes,
                      guint author)
{
  GSequenceIter* iter;
  gsize offset_index;
  InfTextChunkSegment* segment;
  InfTextChunkSegment* new_segment;

  g_return_val_if_fail(self != NULL, NULL);
  g_return_val_if_fail(offset <= self->length, NULL);

  if(self->length > 0)
  {
    iter = get_segment_at_pos(self, offset, &offset_index);
    segment = (InfTextChunkSegment*)g_sequence_get(iter);

    /* Have to split segment, unless it is between two segments in which
     * case we can perhaps append to the previous. */
    if(segment->author != author && offset > 0 && offset_index == 0)
    {
      g_assert(iter != g_sequence_get_begin_iter(iter));

      iter = g_sequence_prev_iter(iter);
      segment = (InfTextChunkSegment*)g_sequence_get(iter);
      offset_index = segment->length;
    }

    if(segment->author != author)
    {
      /* No luck, split if necessary */
      if(offset_index > 0 && offset_index < segment->length)
      {
        new_segment = g_slice_new(InfTextChunkSegment);
        new_segment->author = segment->author;
        new_segment->text = g_memdup(
          segment->text + offset_index,
          segment->length - offset_index
        );

        new_segment->length = segment->length - offset_index;
        new_segment->offset = offset;

        iter = g_sequence_next_iter(iter);
        g_sequence_insert_before(iter, new_segment);

        /* Don't realloc to make smaller */
        segment->length = offset_index;
        /* Note that we did not invalidate offsets so far */
      }
      else if(offset_index == segment->length)
      {
        /* Insert behind segment */
        iter = g_sequence_next_iter(iter);
      }

      new_segment = g_slice_new(InfTextChunkSegment);
      new_segment->author = author;
      new_segment->text = g_memdup(text, bytes);
      new_segment->length = bytes;
      new_segment->offset = offset;
      g_sequence_insert_before(iter, new_segment);
    }
    else
    {
      /* TODO: g_malloc + g_free + 2*memcpy? */
      segment->text = g_realloc(segment->text, segment->length + bytes);
      if(offset_index < segment->length)
      {
        g_memmove(
          segment->text + offset_index + bytes,
          segment->text + offset_index,
          segment->length - offset_index
        );
      }
      
      memcpy(segment->text + offset_index, text, bytes);
      segment->length += bytes;
      iter = g_sequence_next_iter(iter);
    }

    /* Adjust offsets */
    while(iter != g_sequence_get_end_iter(self->segments))
    {
      segment = (InfTextChunkSegment*)g_sequence_get(iter);
      iter->offset += length;
      iter = g_sequence_next_iter(iter);
    }

    self->length += length;
  }
  else
  {
    new_segment = g_slice_new(InfTextChunkSegment);
    new_segment->author = author;
    new_segment->text = g_memdup(text, bytes);
    new_segment->length = bytes;
    new_segment->offset = 0;

    g_sequence_append(self->segments, new_segment);
    self->length = length;
  }
}

/** inf_text_chunk_insert_chunk:
 *
 * @self: A #InfTextChunk.
 * @offset: Character offset at which to insert text.
 * @text: Chunk to insert into @self.
 *
 * Inserts @text into @self at position @offset. @text and @self must
 * have the same encoding.
 **/
void
inf_text_chunk_insert_chunk(InfTextChunk* self,
                            guint offset,
                            InfTextChunk* text)
{
  GSequenceIter* iter;
  GSequenceIter* text_iter;
  gsize offset_index;
  InfTextChunkSegment* segment;
  InfTextChunkSegment* new_segment;

  GSequenceIter* first_iter;
  GSequenceIter* last_iter;
  InfTextChunkSegment* first;
  InfTextChunkSegment* last;

  InfTextChunkSegment* first_merge;
  InfTextChunkSegment* last_merge;
  GSequenceIter* beyond;

  g_return_val_if_fail(self != NULL, NULL);
  g_return_val_if_fail(offset <= self->length, NULL);
  g_return_val_if_fail(text != NULL, NULL);
  g_return_val_if_fail(strcmp(self->encoding, text->encoding) == 0, NULL);

  if(self->length > 0 && text->length > 0)
  {
    if(g_sequence_get_length(text->segments) == 1)
    {
      segment = g_sequence_get(g_sequence_get_begin_iter(text->segments));

      inf_text_chunk_insert(
        self,
        offset,
        segment->text,
        text->length,
        segment->length,
        segment->author
      );
    }
    else
    {
      iter = get_segment_at_pos(self, offset, &offset_index);
      segment = (InfTextChunkSegment*)g_sequence_get(iter);

      /* First, we insert the first and last segment of text into self,
       * possibly merging with adjacent segments. Then, the rest is
       * copied. */
      first_iter = g_sequence_get_begin_iter(text);
      last_iter = g_sequence_prev_iter(g_sequence_get_end_iter(text));

      first = (InfTextChunkSegment*)g_sequence_get(first_iter);
      last = (InfTextChunkSegment*)g_sequence_get(last_iter);

      /* 0 and 1 segment special cases have been handled above */
      g_assert(first != last);

      last_merge = segment;
      first_merge = segment;

      /* beyond points to the first segment that needs offset adjustments
       * after insertion */
      beyond = iter;

      /* Try merge with end of previous segment if inserting inbetween two
       * segments. */
      if(offset_index == 0 && offset > 0)
      {
        g_assert(iter != g_sequence_get_begin_iter(self));

        iter = g_sequence_prev_iter(iter);
        first_merge = (InfTextChunkSegment*)g_sequence_get(iter);
        offset_index = first_merge->length;
      }

      if(offset == 0 || offset == self->length || first_merge != last_merge)
      {
        /* Insert between two segments, or at beginning/end */
        if(first_merge == first->author && offset > 0)
        {
          /* Can merge first segment */
          first_merge->length += first->length;

          first_merge->text = g_realloc(
            first_merge->text,
            first_merge->length
          );

          memcpy(
            first_merge->text + first_merge->length - first->length,
            first->text,
            first->length
          );

          /* Already inserted */
          first_iter = g_sequence_next_iter(first_iter);
        }

        if(last_merge == last->author && offset < self->length)
        {
          /* Can merge last segment */
          last_merge->length += last->length;
          last_merge->text = g_realloc(last_merge->text, last_merge->length);

          g_memmove(
            last_merge->text + last->length,
            last_merge->text,
            last_merge->length - last->length
          );

          memcpy(last_merge->text, last->text, last->length);
          last_merge->offset = offset + last->offset;

          /* Already adjusted offset here */
          beyond = g_sequence_next_iter(beyond);
        }
        else
        {
          /* We still have to insert last segment */
          last_iter = g_sequence_next_iter(last_iter);
        }

        if(offset > 0)
        {
          /* Pointing to the position before which to insert
           * the rest of text. */
          iter = g_sequence_next_iter(iter);
        }
      }
      else
      {
        /* Insert within a segment, split segment*/

        new_segment = g_slice_new(InfTextChunkSegment);
        new_segment->author = last_merge->author;

        if(last_merge->author == last->author)
        {
          /* Merge last part into new segment */
          new_segment->length = last_merge->length - offset_index + last->length;
          new_segment->text = g_malloc(new_segment->length);
          memcpy(new_segment->text, last->text, last->length);

          memcpy(
            new_segment->text + last->length,
            last_merge->text + index_offset,
            last_merge->length - index_offset
          );

          new_segment->offset = offset + last->offset;
        }
        else
        {
          /* Split up last part */
          new_segment->length = last_merge->length - offset_index;
          new_segment->text = g_malloc(new_segment->length);

          memcpy(
            new_segment->text,
            last_merge->text + index_offset,
            new_segment->length
          );

          new_segment->offset = offset + text->length;

          /* We still have to insert last segment since we could not merge */
          last_iter = g_sequence_next_iter(last_iter);
        }

        iter = g_sequence_iter_next(iter);
        iter = g_sequence_insert_before(iter, new_segment);

        /* The segment just inserted is the last one being inserted, so
         * we need to adjust beyond that */
        beyond = g_sequence_next_iter(iter);

        /* Note first_merge == last_merge */
        if(first_merge->author == first->author)
        {
          /* Merge into first */
          if(first_merge->length < offset_index + first->length)
          {
            first_merge->text = g_realloc(
              first_merge->text,
              offset_index + first->length
            );
          }

          first_merge->length = offset_index + first->length;

          memcpy(
            first_merge->text + offset_index,
            first->text,
            first->length
          );

          /* Already inserted */
          first_iter = g_sequence_next_iter(first_iter);
        }
        else
        {
          /* Cannot merge, just cut */
          first_merge->length = offset_index;
        }
      }

      /* iter now points to the segment before which the rest of text has to
       * be inserted */

      for(text_iter = first_iter;
          text_iter != last_iter;
          text_iter = g_sequence_next_iter(text_iter))
      {
        segment = g_sequence_get(text_iter);
        new_segment = g_slice_new(InfTextChunkSegment);

        new_segment->author = segment->author;
        new_segment->text = g_malloc(segment->length);
        new_segment->length = segment->length;
        new_segment->offset = offset + segment->offset;
        g_sequence_insert_before(iter, new_segment);
      }

      for(iter = beyond;
          iter != g_sequence_get_end_iter(self);
          iter = g_sequence_next_iter(iter))
      {
        segment = g_sequence_get(iter);
        segment->offset += offset;
      }
    }
  }
  else
  {
    for(text_iter = g_sequence_get_begin_iter(text);
        text_iter != g_sequence_get_end_iter(text);
        text_iter = g_sequence_next_iter(text_iter))
    {
      segment = (InfTextChunkSegment*)g_sequence_get(text_iter);
      new_segment = g_slice_new(InfTextChunkSegment);

      new_segment->author = segment->author;
      new_segment->text = g_memdup(segment->text, segment->length);
      new_segment->length = segment->length;
      new_segment->offset = segment->offset;

      g_sequence_append(self->segments, new_segment);
    }
  }

  self->length += text->length;
}

/** inf_text_chunk_erase:
 *
 * @self: A #InfTextChunk.
 * @begin: A character offset into @self.
 * @length: Number of characters to erase.
 *
 * Removes @length characters of @self, starting from character offset @begin.
 **/
void
inf_text_chunk_erase(InfTextChunk* self,
                     guint begin,
                     guint length)
{
  GSequenceIter* first_iter;
  GSequenceIter* last_iter;
  GSequenceIter* beyond;
  gsize first_index;
  gsize last_index;
  InfTextChunkSegment* first;
  InfTextChunkSegment* last;

  g_return_if_fail(self != NULL);
  g_return_if_fail(begin + length <= self->length);

  if(self->length > 0 && length > 0)
  {
    first_iter = get_segment_at_pos(self, begin, &first_iter);
    last_iter = get_segment_at_pos(self, begin + length, &last_iter);

    first = (InfTextChunkSegment*)g_sequence_get(first_iter);
    last = (InfTextChunkSegment*)g_sequence_get(last_iter);

    if(begin > 0 && begin + length < self->length)
    {
      if(first_index == 0)
      {
        g_assert(first_iter != g_sequence_get_end_iter(self->segments));
        first_iter = g_sequence_prev_iter(first_iter);
        first = (InfTextChunkSegment*)g_sequence_get(first_iter);
        first_index = first->length;
      }

      if(first->author == last->author)
      {
        /* Can merge */
        if(first == last)
        {
          /* Remove within a segment */
          g_memmove(
            first->text + first_index,
            first->text + last_index,
            first->length - last_index
          );

          first->length -= (last_index - first_index);
          beyond = last_iter;
        }
        else
        {
          merged_len = first_index + last->length - last_index;
          if(first->length < first_index + last->length - last_index)
          {
            first->text = g_realloc(
              first->text,
              first_index + last->length - last_index
            );
          }

          first->length = first_index + last->length - last_index;

          memcpy(
            first->text + first_index,
            last->text,
            last->length - last_index
          );

          last_iter = g_sequence_next_iter(last_iter);
          beyond = last_iter;
        }
      }
      else
      {
        /* This could only happen if these are the first or last segment,
         * of the whole chunk, respectively, which they are not
         * (as checked above). */
        g_assert(first_index > 0);
        g_assert(last_index < last->length);
        
        /* Erase from border segments */
        first->length = first_index;

        if(last_index > 0)
        {
          g_memmove(
            last->text,
            last->text + last_index,
            last->length - last_index
          );
        }

        last->length -= last_index;
        last->offset = begin;

        /* We already adjusted offset of last, so only adjust behind */
        beyond = g_sequence_next_iter(last_iter);
      }

      /* Keep first alive, we merged stuff into it */
      first_iter = g_sequence_next_iter(first_iter);
    }
    else
    {
      if(begin == 0 && length == self->length)
      {
        /* Erase everything */
        last_iter = g_sequence_next_iter(last_iter);
        g_assert(last_iter == g_sequence_get_end_iter(self->segments));

        beyond = last_iter;
      }
      else if(begin == 0)
      {
        g_assert(last_index < last->length);

        /* Erase from beginning */
        if(last_index > 0)
        {
          g_memmove(
            last->text,
            last->text + last->index,
            last_length - last_index
          );

          last->length -= last_index;
          last->offset = 0;
        }

        beyond = g_sequence_next_iter(last_iter);
      }
      else
      {
        /* Erase until end */

        /* This could only be possible in the last segment, but then we
         * would erase 0-length, which is a special case we would have
         * catched elsewhere. */
        g_assert(first_index < first->length);


        if(first_index > 0)
        {
          /* Cannot erase whole first chunk */
          first->length = first_index;
          first_iter = g_sequence_next_iter(first_iter);
        }
        
        last_iter = g_sequence_next_iter(last_iter);
        g_assert(last_iter == g_sequence_get_end_iter(self->segments));

        beyond = last_iter;
      }
    }

    /* first_iter and last_iter have been adjusted so that we have to
     * remove [first_iter, last_iter). */

    /* segments are freed through the sequence's destroy function */
    g_sequence_remove_range(first_iter, last_iter);

    /* adjust offsets */
    for(first_iter = beyond;
        first_iter != g_sequence_get_end_iter(self->segments);
        first_iter = g_sequence_next_iter(self->segments))
    {
      first = (InfTextChunkSegment*)g_sequence_get(first_iter);
      first->offset -= length;
    }
  }

  self->length -= length;
}

/** inf_text_chunk_get_text:
 *
 * @self: A #InfTextChunk.
 * @length: Location to write the number of bytes to, or %NULL.
 *
 * Returns the content of @self as an array. The text is encoded in
 * @self's encoding. @length is set to the number of bytes in the returned
 * buffer, if non-%NULL. The result is _not_ zero-terminated.
 *
 * Return Value: Content of @self. Free with g_free() if no longer in use.
 **/
gchar*
inf_text_chunk_get_text(InfTextChunk* self,
                        gsize* length)
{
  GSequnceIter* iter;
  InfTextChunkSegment* segment;
  gsize bytes;
  gsize cur;
  gchar* result;
  
  g_return_val_if_fail(self != NULL, NULL);
  bytes = 0;

  /* First pass, determine size */
  for(iter = g_sequence_get_begin_iter(self->segments);
      iter != g_sequence_get_end_iter(self->segments);
      iter = g_sequence_iter_next(iter))
  {
    segment = (InfTextChunkSegment*)g_sequence_get(iter);
    bytes += segment->length;
  }

  /* Second pass, copy */
  result = g_malloc(bytes);
  cur = 0;

  for(iter = g_sequence_get_begin_iter(self->segments);
      iter != g_sequence_get_end_iter(self->segments);
      iter = g_sequence_iter_next(iter))
  {
    segment = (InfTextChunkSegment*)g_sequence_get(iter);
    memcpy(result + cur, segment->text, segment->length);
    cur += segment->length;
  }

  if(length != NULL) *length = bytes;
  return result;
}

/** inf_text_chunk_to_xml:
 *
 * @self: A #InfTextChunk.
 * @xml: XML node to write into.
 *
 * Serializes @self into an XML node. This adds <author> child nodes to @xml,
 * with text written by the respective author as content.
 **/
void
inf_text_chunk_to_xml(InfTextChunk* self,
                      xmlNodePtr xml)
{
  GSequenceIter* iter;
  InfTextChunkSegment* segment;
  gchar* content;
  gsize utf_bytes;
  xmlNodePtr child;
  GIConv cd;

  g_return_if_fail(self != NULL);
  g_return_if_fail(xml != NULL);

  cd = g_iconv_open("UTF-8", self->encoding);
  g_assert(cd != (GIConv)-1);

  for(iter = g_sequence_get_begin_iter(self->segments);
      iter != g_sequence_get_end_iter(self->segments);
      iter = g_sequence_iter_next(iter))
  {
    segment = (InfTextChunkSegment*)g_sequence_get(iter);

    content = g_convert_with_iconv(
      segment->text,
      segment->length,
      cd,
      NULL,
      &utf_bytes,
      NULL
    );

    /* Conversion into UTF-8 should always succeed */
    g_assert(content != NULL);

    child = xmlNewTextChild(
      xml,
      NULL,
      (const xmlChar*)"segment",
      (const xmlChar*)content
    );

    g_free(content);
    xml_util_set_attribute_uint(child, "author", segment->author);
  }
  
  g_iconv_close(cd);
}

/** inf_text_chunk_from_xml:
 *
 * @xml: A XML node.
 * @encoding: Character encoding for the new text chunk.
 * @error: Location to store error information, or %NULL.
 *
 * Converts a XML node created by inf_text_chunk_to_xml() back to a 
 * #InfTextChunk. The created #InfTextChunk will have the given encoding.
 *
 * Return Value: A #InfTextChunk, or %NULL if an error occured.
 **/
InfTextChunk*
inf_text_chunk_from_xml(xmlNodePtr xml,
                        const gchar* encoding,
                        GError** error)
{
  InfTextChunk* chunk;
  InfTextChunkSegment* segment;
  guint author;
  gboolean result;

  xmlNodePtr child;
  GIConv cd;
  gsize bytes_written;
  gsize bytes_read;
  glong len;
  guint offset;
  xmlChar* content;
  gchar* text;

  g_return_val_if_fail(xml != NULL, NULL);
  g_return_val_if_fail(encoding != NULL, NULL);

  chunk = inf_text_chunk_new(encoding);

  cd = g_iconv_open(encoding, "UTF-8");
  g_assert(cd != (GIConv)-1);

  offset = 0;

  for(child = xml->child; child != NULL; child = child->next)
  {
    if(strcmp((const char*)child->name, "segment") == 0)
    {
      content = xmlNodeGetContent(child);
      if(content != NULL)
      {
        result = inf_xml_util_get_attribute_uint_required(
          child,
          "author",
          &author,
          error
        );

        if(result == FALSE)
        {
          g_iconv_close(cd);
          inf_text_chunk_free(chunk);
          return NULL;
        }

        text = g_convert_with_iconv(
          (const gchar*)content,
          -1,
          cd,
          &bytes_read,
          &bytes_written,
          error
        );

        len = g_utf8_strlen(text, bytes_read);
        xmlFree(content);

        if(text == NULL)
        {
          g_iconv_close(cd);
          inf_text_chunk_free(chunk);
          return NULL;
        }

        segment = g_slice_new(InfTextChunkSegment);
        segment->author = author;
        segment->text = text;
        segment->length = bytes_written;
        segment->offset = offset;

        offset += len;
        g_sequence_append(chunk->segments, segment);
      }
    }
  }
  
  g_iconv_close(cd);
  return chunk;
}

/* vim:set et sw=2 ts=2: */
