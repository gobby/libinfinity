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

#include <libinftext/inf-text-chunk.h>
#include <libinfinity/common/inf-xml-util.h>

#include <string.h>

/* Don't check integrity in stable releases */
/*#define CHUNK_CHECK_INTEGRITY*/

struct _InfTextChunk {
  GSequence* segments;
  guint length; /* in characters */
  GQuark encoding;
};

typedef struct _InfTextChunkSegment InfTextChunkSegment;
struct _InfTextChunkSegment {
  guint author;
  /* This is gchar so that we can do pointer arithmetic. It does not
   * necessarily store a full character in each byte. This depends on the
   * encoding specified in the InfTextChunk. */
  gchar* text;
  gsize length; /* in bytes */
  guint offset; /* absolute to chunk begin in characters, sort criteria */
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
  const InfTextChunkSegment* first_segment = (InfTextChunkSegment*)first;
  const InfTextChunkSegment* second_segment = (InfTextChunkSegment*)second;

  g_return_val_if_fail(second != NULL && first != NULL, 0);

  if (first_segment->offset < second_segment->offset)
    return -1;
  else if (first_segment->offset == second_segment->offset)
    return 0;
  else
    return 1;
}

static guint
inf_text_chunk_next_offset(InfTextChunk* self,
                           GSequenceIter* iter)
{
  GSequenceIter* next_iter;

  g_assert(iter != g_sequence_get_end_iter(self->segments));
  
  next_iter = g_sequence_iter_next(iter);
  if(next_iter == g_sequence_get_end_iter(self->segments))
    return self->length;
  else
    return ((InfTextChunkSegment*)g_sequence_get(next_iter))->offset;
}

#ifdef CHUNK_CHECK_INTEGRITY
static gboolean
inf_text_chunk_check_integrity(InfTextChunk* self)
{
  GSequenceIter* iter;
  InfTextChunkSegment* segment;
  guint offset;
  guint new_offset;

  offset = 0;

  for(iter = g_sequence_get_begin_iter(self->segments);
      iter != g_sequence_get_end_iter(self->segments);
      iter = g_sequence_iter_next(iter))
  {
    segment = (InfTextChunkSegment*)g_sequence_get(iter);
    if(offset != segment->offset)
      return FALSE;

    new_offset = inf_text_chunk_next_offset(self, iter);
    if(new_offset <= offset)
      return FALSE;

    if(new_offset - offset > segment->length)
      return FALSE;

    offset = new_offset;
  }

  return TRUE;
}
#endif

static GSequenceIter*
inf_text_chunk_get_segment(InfTextChunk* self,
                           guint pos,
                           gsize* index)
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
    &key,
    inf_text_chunk_segment_cmp,
    NULL
  );

  if(self->length > 0)
  {
    g_assert(iter != g_sequence_get_begin_iter(self->segments));

    iter = g_sequence_iter_prev(iter);
    found = g_sequence_get(iter);

    g_assert(pos >= found->offset);

    /* This is not "<=" because it should rather find position 0 on the
     * next segment in that case. */
    g_assert(pos <= inf_text_chunk_next_offset(self, iter));

    /* Find byte index in the segment where the specified character starts.
     * This is rather ugly, I wish iconv or glib or someone had some nice(r)
     * API for this. */
    if(index != NULL)
    {
      if(pos == inf_text_chunk_next_offset(self, iter))
      {
        *index = found->length;
      }
      else
      {
        /* We convert the segment's text into UCS-4, character by character.
         * This assumes every UCS-4 character is 4 bytes in length */

        /* TODO: Can we use some faster glib UTF-8 functions in case
         * self->encoding is UTF-8? */
        cd = g_iconv_open("UCS-4", g_quark_to_string(self->encoding));
        g_assert(cd != (GIConv)-1);
        
        inbuf = found->text;
        inlen = found->length;

        for(count = 0; count < pos - found->offset; ++ count)
        {
          g_assert(inlen > 0);

          outbuf = buffer;
          outlen = 4;

          result = g_iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
          g_assert(result == (size_t)(-1)); /* errno == E2BIG */
        }

        g_iconv_close(cd);
        *index = found->length - inlen;
      }
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

/**
 * inf_text_chunk_new:
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
  chunk->encoding = g_quark_from_string(encoding);

  return chunk;
}

/**
 * inf_text_chunk_copy:
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

  new_chunk = g_slice_new(InfTextChunk);
  new_chunk->segments = g_sequence_new(
    (GDestroyNotify)inf_text_chunk_segment_free
  );
  
  for(iter = g_sequence_get_begin_iter(self->segments);
      iter != g_sequence_get_end_iter(self->segments);
      iter = g_sequence_iter_next(iter))
  {
    InfTextChunkSegment* segment = g_sequence_get(iter);
    InfTextChunkSegment* new_segment = g_slice_new(InfTextChunkSegment);
    new_segment->author = segment->author;
    new_segment->text = g_memdup(segment->text, segment->length);
    new_segment->length = segment->length;
    new_segment->offset = segment->offset;
    g_sequence_append(new_chunk->segments, new_segment);
  }

  new_chunk->length = self->length;
  new_chunk->encoding = self->encoding;

  return new_chunk;
}

/**
 * inf_text_chunk_free:
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

/**
 * inf_text_chunk_get_encoding:
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
  return g_quark_to_string(self->encoding);
}

/**
 * inf_text_chunk_get_length:
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

/**
 * inf_text_chunk_substring:
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
  GSequenceIter* begin_iter;
  GSequenceIter* end_iter;
  gsize begin_index;
  gsize end_index;

  InfTextChunk* result;
  InfTextChunkSegment* segment;
  InfTextChunkSegment* new_segment;
  guint current_length;

  g_return_val_if_fail(self != NULL, NULL);
  g_return_val_if_fail(begin + length <= self->length, NULL);

  if(self->length > 0 && length > 0)
  {
    begin_iter = inf_text_chunk_get_segment(self, begin, &begin_index);
    end_iter = inf_text_chunk_get_segment(self, begin + length, &end_index);

    if(end_index == 0)
    {
      g_assert(end_iter != g_sequence_get_end_iter(self->segments));
      end_iter = g_sequence_iter_prev(end_iter);
      end_index = ((InfTextChunkSegment*)g_sequence_get(end_iter))->length;
    }

    result = inf_text_chunk_new(g_quark_to_string(self->encoding));

    current_length = 0;
    segment = g_sequence_get(begin_iter);

    while(begin_iter != end_iter)
    {
      new_segment = g_slice_new(InfTextChunkSegment);
      new_segment->author = segment->author;

      new_segment->text = g_memdup(
        segment->text + begin_index,
        segment->length - begin_index
      );
      
      new_segment->length = segment->length - begin_index;
      new_segment->offset = current_length;

      begin_iter = g_sequence_iter_next(begin_iter);
      segment = g_sequence_get(begin_iter);

      /* Add (remaining) length of this segment to current length */
      current_length = segment->offset - begin;

      /* So we get the next segment from the beginning. This may only be
       * non-zero during the first iteration. */
      begin_index = 0;
    /*  begin = new_segment->offset;*/
      
      g_sequence_append(result->segments, new_segment);
    }

    /* Don't forget last segment */
    new_segment = g_slice_new(InfTextChunkSegment);
    new_segment->author = segment->author;
    new_segment->text = g_memdup(
      segment->text + begin_index,
      end_index - begin_index
    );
    
    new_segment->length = end_index - begin_index;
    new_segment->offset = current_length;
    
    g_sequence_append(result->segments, new_segment);

    result->length = length;
    result->encoding = self->encoding;
  }
  else
  {
    g_assert(length == 0 || begin == 0);

    /* New, empty chunk */
    result = inf_text_chunk_new(g_quark_to_string(self->encoding));
  }

#ifdef CHUNK_CHECK_INTEGRITY
  g_assert(inf_text_chunk_check_integrity(result) == TRUE);
#endif

  return result;
}

/**
 * inf_text_chunk_insert_text:
 * @self: A #InfTextChunk.
 * @offset: Character offset at which to insert text
 * @text: Text to insert.
 * @length: Number of characters contained in @text.
 * @bytes: Number of bytes of @text.
 * @author: User that wrote @text.
 *
 * Inserts text written by @author into @self. @text is expected to be in
 * the chunk's encoding.
 **/
void
inf_text_chunk_insert_text(InfTextChunk* self,
                           guint offset,
                           gconstpointer text,
                           gsize bytes,
                           guint length,
                           guint author)
{
  GSequenceIter* iter;
  gsize offset_index;
  InfTextChunkSegment* segment;
  InfTextChunkSegment* new_segment;

  g_return_if_fail(self != NULL);
  g_return_if_fail(offset <= self->length);

  if(self->length > 0)
  {
    iter = inf_text_chunk_get_segment(self, offset, &offset_index);
    segment = (InfTextChunkSegment*)g_sequence_get(iter);

    /* Have to split segment, unless it is between two segments in which
     * case we can perhaps append to the previous. */
    if(segment->author != author && offset > 0 && offset_index == 0)
    {
      g_assert(iter != g_sequence_get_begin_iter(self->segments));

      iter = g_sequence_iter_prev(iter);
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

        iter = g_sequence_iter_next(iter);
        iter = g_sequence_insert_before(iter, new_segment);

        /* Don't realloc to make smaller */
        segment->length = offset_index;
        /* Note that we did not invalidate offsets so far */
      }
      else if(offset_index == segment->length)
      {
        /* Insert behind segment */
        iter = g_sequence_iter_next(iter);
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
      iter = g_sequence_iter_next(iter);
    }

    /* Adjust offsets */
    while(iter != g_sequence_get_end_iter(self->segments))
    {
      segment = (InfTextChunkSegment*)g_sequence_get(iter);
      segment->offset += length;
      iter = g_sequence_iter_next(iter);
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

#ifdef CHUNK_CHECK_INTEGRITY
  g_assert(inf_text_chunk_check_integrity(self) == TRUE);
#endif
}

/**
 * inf_text_chunk_insert_chunk:
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

  g_return_if_fail(self != NULL);
  g_return_if_fail(offset <= self->length);
  g_return_if_fail(text != NULL);
  g_return_if_fail(self->encoding == text->encoding);

  if(self->length > 0 && text->length > 0)
  {
    if(g_sequence_get_length(text->segments) == 1)
    {
      segment = g_sequence_get(g_sequence_get_begin_iter(text->segments));

      inf_text_chunk_insert_text(
        self,
        offset,
        segment->text,
        segment->length,
        text->length,
        segment->author
      );
    }
    else
    {
      iter = inf_text_chunk_get_segment(self, offset, &offset_index);
      segment = (InfTextChunkSegment*)g_sequence_get(iter);

      /* First, we insert the first and last segment of text into self,
       * possibly merging with adjacent segments. Then, the rest is
       * copied. */
      first_iter = g_sequence_get_begin_iter(text->segments);
      last_iter = g_sequence_iter_prev(
        g_sequence_get_end_iter(text->segments)
      );

      first = (InfTextChunkSegment*)g_sequence_get(first_iter);
      last = (InfTextChunkSegment*)g_sequence_get(last_iter);

      /* 0 and 1 segment special cases have been handled above */
      g_assert(first != last);

      last_merge = segment;
      first_merge = segment;

      /* beyond points to the first segment that needs offset adjustment
       * after insertion */
      beyond = iter;

      /* Try merge with end of previous segment if inserting inbetween two
       * segments. */
      if(offset_index == 0 && offset > 0)
      {
        g_assert(iter != g_sequence_get_begin_iter(self->segments));

        iter = g_sequence_iter_prev(iter);
        first_merge = (InfTextChunkSegment*)g_sequence_get(iter);
        offset_index = first_merge->length;
      }

      if(offset == 0 || offset == self->length || first_merge != last_merge)
      {
        /* Insert between two segments, or at beginning/end */
        if(first_merge->author == first->author && offset > 0)
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
          first_iter = g_sequence_iter_next(first_iter);
        }

        if(last_merge->author == last->author && offset < self->length)
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

          /* Merged with last, so don't need to adjust last_merge->offset
           * anymore, continue behind with offset adjustment. */
          beyond = g_sequence_iter_next(beyond);
        }
        else
        {
          /* Could not merge last segment, so we have to insert it. Therefore,
           * make last_iter point to the end of the sequence. */
          last_iter = g_sequence_iter_next(last_iter);

          if(offset_index > 0) /* Note: This is only false for offset == 0 */
            beyond = g_sequence_iter_next(iter);
        }

        if(offset_index > 0) /* Note: This is only false for offset == 0 */
        {
          /* Pointing to the position before which to insert
           * the rest of text. */
          iter = g_sequence_iter_next(iter);
        }
      }
      else
      {
        /* Insert within a segment, split segment */

        new_segment = g_slice_new(InfTextChunkSegment);
        new_segment->author = last_merge->author;

        if(last_merge->author == last->author)
        {
          /* Merge last part into new segment */
          new_segment->length = last_merge->length - offset_index +
            last->length;

          new_segment->text = g_malloc(new_segment->length);
          memcpy(new_segment->text, last->text, last->length);

          memcpy(
            new_segment->text + last->length,
            last_merge->text + offset_index,
            last_merge->length - offset_index
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
            last_merge->text + offset_index,
            new_segment->length
          );

          new_segment->offset = offset + text->length;

          /* We still have to insert last segment since we could not merge */
          last_iter = g_sequence_iter_next(last_iter);
        }

        iter = g_sequence_iter_next(iter);
        iter = g_sequence_insert_before(iter, new_segment);

        /* The segment just inserted is the last one being inserted, so
         * we need to adjust beyond that */
        beyond = g_sequence_iter_next(iter);

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
          first_iter = g_sequence_iter_next(first_iter);
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
          text_iter = g_sequence_iter_next(text_iter))
      {
        segment = g_sequence_get(text_iter);
        new_segment = g_slice_new(InfTextChunkSegment);

        new_segment->author = segment->author;
        new_segment->text = g_memdup(segment->text, segment->length);
        new_segment->length = segment->length;
        new_segment->offset = offset + segment->offset;
        g_sequence_insert_before(iter, new_segment);
      }

      for(iter = beyond;
          iter != g_sequence_get_end_iter(self->segments);
          iter = g_sequence_iter_next(iter))
      {
        segment = g_sequence_get(iter);
        segment->offset += inf_text_chunk_get_length(text);
      }

      self->length += text->length;
    }
  }
  else
  {
    for(text_iter = g_sequence_get_begin_iter(text->segments);
        text_iter != g_sequence_get_end_iter(text->segments);
        text_iter = g_sequence_iter_next(text_iter))
    {
      segment = (InfTextChunkSegment*)g_sequence_get(text_iter);
      new_segment = g_slice_new(InfTextChunkSegment);

      new_segment->author = segment->author;
      new_segment->text = g_memdup(segment->text, segment->length);
      new_segment->length = segment->length;
      new_segment->offset = segment->offset;

      g_sequence_append(self->segments, new_segment);
    }

    self->length += text->length;
  }

#ifdef CHUNK_CHECK_INTEGRITY
  g_assert(inf_text_chunk_check_integrity(self) == TRUE);
#endif
}

/**
 * inf_text_chunk_erase:
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
    first_iter = inf_text_chunk_get_segment(self, begin, &first_index);
    last_iter = inf_text_chunk_get_segment(self, begin + length, &last_index);

    first = (InfTextChunkSegment*)g_sequence_get(first_iter);
    last = (InfTextChunkSegment*)g_sequence_get(last_iter);

    if(begin > 0 && begin + length < self->length)
    {
      if(first_index == 0)
      {
        g_assert(first_iter != g_sequence_get_end_iter(self->segments));
        first_iter = g_sequence_iter_prev(first_iter);
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
          beyond = g_sequence_iter_next(last_iter);
        }
        else
        {
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
            last->text + last_index,
            last->length - last_index
          );

          last_iter = g_sequence_iter_next(last_iter);
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
        beyond = g_sequence_iter_next(last_iter);
      }

      /* Keep first alive, we merged stuff into it */
      first_iter = g_sequence_iter_next(first_iter);
    }
    else
    {
      if(begin == 0 && length == self->length)
      {
        /* Erase everything */
        last_iter = g_sequence_iter_next(last_iter);
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
            last->text + last_index,
            last->length - last_index
          );

          last->length -= last_index;
          last->offset = 0;

          beyond = g_sequence_iter_next(last_iter);
        }
        else
        {
          /* First segments are completely removed */
          beyond = last_iter;
        }
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
          first_iter = g_sequence_iter_next(first_iter);
        }
        
        last_iter = g_sequence_iter_next(last_iter);
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
        first_iter = g_sequence_iter_next(first_iter))
    {
      first = (InfTextChunkSegment*)g_sequence_get(first_iter);
      first->offset -= length;
    }
  }

  self->length -= length;

#ifdef CHUNK_CHECK_INTEGRITY
  g_assert(inf_text_chunk_check_integrity(self) == TRUE);
#endif
}

/**
 * inf_text_chunk_get_text:
 * @self: A #InfTextChunk.
 * @length: Location to write the number of bytes to, or %NULL.
 *
 * Returns the content of @self as an array. The text is encoded in
 * @self's encoding. @length is set to the number of bytes in the returned
 * buffer, if non-%NULL. The result is _not_ zero-terminated.
 *
 * Return Value: Content of @self. Free with g_free() if no longer in use.
 **/
gpointer
inf_text_chunk_get_text(InfTextChunk* self,
                        gsize* length)
{
  GSequenceIter* iter;
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

/**
 * inf_text_chunk_equal:
 * @self: A #InfTextChunk.
 * @other: Another #InfTextChunk.
 *
 * Returns whether the two text chunks contain the same text and the same
 * segments were written by the same authors.
 *
 * Return Value: Whether the two chunks are equal.
 **/
gboolean
inf_text_chunk_equal(InfTextChunk* self,
                     InfTextChunk* other)
{
  GSequenceIter* iter1;
  GSequenceIter* iter2;
  InfTextChunkSegment* segment1;
  InfTextChunkSegment* segment2;

  g_return_val_if_fail(self != NULL, FALSE);
  g_return_val_if_fail(other != NULL, FALSE);
  g_return_val_if_fail(self->encoding == other->encoding, FALSE);

  iter1 = g_sequence_get_begin_iter(self->segments);
  iter2 = g_sequence_get_begin_iter(other->segments);

  while(iter1 != g_sequence_get_end_iter(self->segments) &&
        iter2 != g_sequence_get_end_iter(other->segments))
  {
    segment1 = (InfTextChunkSegment*)g_sequence_get(iter1);
    segment2 = (InfTextChunkSegment*)g_sequence_get(iter2);

    if(segment1->length != segment2->length)
      return FALSE;

    if(memcmp(segment1->text, segment2->text, segment1->length) != 0)
      return FALSE;

    iter1 = g_sequence_iter_next(iter1);
    iter2 = g_sequence_iter_next(iter2);
  }

  if(iter1 != g_sequence_get_end_iter(self->segments) ||
     iter2 != g_sequence_get_end_iter(other->segments))
  {
    return FALSE;
  }

  return TRUE;
}

/**
 * inf_text_chunk_iter_init:
 * @self: A #InfTextChunk.
 * @iter: A #InfTextChunkIter.
 *
 * Sets @iter to point to the first segment of @self. If there are no
 * segments (i.e. @self is empty), @iter is left untouched and the function
 * returns %FALSE.
 *
 * Return Value: Whether @iter was set.
 **/
gboolean
inf_text_chunk_iter_init(InfTextChunk* self,
                         InfTextChunkIter* iter)
{
  g_return_val_if_fail(self != NULL, FALSE);
  g_return_val_if_fail(iter != NULL, FALSE);

  if(self->length > 0)
  {
    iter->chunk = self;
    iter->first = g_sequence_get_begin_iter(self->segments);
    iter->second = g_sequence_iter_next(iter->first);
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/**
 * inf_text_chunk_iter_next:
 * @iter: An initialized #InfTextChunkIter.
 *
 * Sets @iter to point to the next segment. If @iter already points to the
 * last segment, the function returns %FALSE.
 *
 * Return Value: Whether @iter was set.
 **/
gboolean
inf_text_chunk_iter_next(InfTextChunkIter* iter)
{
  g_return_val_if_fail(iter != NULL, FALSE);

  if(g_sequence_iter_is_end(iter->second) == FALSE)
  {
    iter->first = iter->second;
    iter->second = g_sequence_iter_next(iter->first);
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/**
 * inf_text_chunk_iter_prev:
 * @iter: An initialized #InfTextChunkIter.
 *
 * Sets @iter to point to the previous segment. If @iter already points to
 * the first segment, the function returns %FALSE.
 *
 * Returns: Whether @iter has changed.
 **/
gboolean
inf_text_chunk_iter_prev(InfTextChunkIter* iter)
{
  g_return_val_if_fail(iter != NULL, FALSE);

  if(g_sequence_iter_is_begin(iter->first) == FALSE)
  {
    iter->second = iter->first;
    iter->first = g_sequence_iter_prev(iter->first);
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/**
 * inf_text_chunk_iter_get_text:
 * @iter: An initialized #InfTextChunkIter.
 *
 * Returns the text of the segment @iter points to. The text is in the
 * underlaying #InfTextChunk's encoding.
 *
 * Return Value: The text of the segment @iter points to.
 **/
gconstpointer
inf_text_chunk_iter_get_text(InfTextChunkIter* iter)
{
  g_return_val_if_fail(iter != NULL, NULL);
  return ((InfTextChunkSegment*)g_sequence_get(iter->first))->text;
}

/**
 * inf_text_chunk_iter_get_length:
 * @iter: An initialized #InfTextChunkIter.
 *
 * Returns the number of characters in the segment @iter points to.
 *
 * Return Value: The number of characters in the segment @iter points to.
 **/
guint
inf_text_chunk_iter_get_length(InfTextChunkIter* iter)
{
  InfTextChunkSegment* first;
  InfTextChunkSegment* second;

  g_return_val_if_fail(iter != NULL, 0);

  if(g_sequence_iter_is_end(iter->second) == TRUE)
  {
    first = (InfTextChunkSegment*)g_sequence_get(iter->first);
    return iter->chunk->length - first->offset;
  }
  else
  {
    first = (InfTextChunkSegment*)g_sequence_get(iter->first);
    second = (InfTextChunkSegment*)g_sequence_get(iter->second);
    return second->offset - first->offset;
  }
}

/**
 * inf_text_chunk_iter_get_bytes:
 * @iter: An initialized #InfTextChunkIter.
 *
 * Returns the number of bytes in the segment @iter points to.
 *
 * Return Value: The number of bytes in the segment @iter points to.
 **/
gsize
inf_text_chunk_iter_get_bytes(InfTextChunkIter* iter)
{
  g_return_val_if_fail(iter != NULL, 0);
  return ((InfTextChunkSegment*)g_sequence_get(iter->first))->length;
}

/**
 * inf_text_chunk_iter_get_author:
 * @iter: An initialized #InfTextChunkIter.
 *
 * Returns the user ID of the author of the segment @iter points to.
 *
 * Return Value: The user ID of the author of the segment @iter points to.
 **/
guint
inf_text_chunk_iter_get_author(InfTextChunkIter* iter)
{
  g_return_val_if_fail(iter != NULL, 0);
  return ((InfTextChunkSegment*)g_sequence_get(iter->first))->author;
}

/* vim:set et sw=2 ts=2: */
