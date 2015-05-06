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

/**
 * SECTION:inf-text-fixline-buffer
 * @title: InfTextFixlineBuffer
 * @short_description: Keep a fixed number of trailing lines
 * @include: libinftext/inf-text-fixline-buffer.h
 * @see_also: #InfTextBuffer, #InfTextDefaultBuffer
 * @stability: Unstable
 *
 * This class is a wrapper around another #InfTextBuffer. It makes sure that
 * the number of trailing empty lines in the underlying buffer is always fixed
 * to a given number while it itself keeps track of the missing or additional
 * lines. This can be used for example to keep the number of empty lines
 * fixed on the client side even if lines are added and removed within a
 * text session.
 */

#include <libinftext/inf-text-fixline-buffer.h>
#include <libinftext/inf-text-user.h>
#include <libinftext/inf-text-move-operation.h>
#include <libinfinity/common/inf-buffer.h>
#include <libinfinity/inf-signals.h>

#include <string.h>

struct _InfTextBufferIter {
  InfTextBufferIter* base_iter;
  /* NULL base_iter means that keep_begin and keep_end are used */
  guint keep_begin;
  guint keep_end;
};

typedef struct _InfTextFixlineBufferPrivate InfTextFixlineBufferPrivate;
struct _InfTextFixlineBufferPrivate {
  InfIo* io;
  InfTextBuffer* buffer;
  guint lines;

  /* base + n_keep == buffer */
  guint* keep;
  gint n_keep;

  InfIoDispatch* dispatch;
};

enum {
  PROP_0,

  /* overwritten */
  PROP_MODIFIED,

  PROP_IO,
  PROP_BUFFER,
  PROP_LINES
};

#define INF_TEXT_FIXLINE_BUFFER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_FIXLINE_BUFFER, InfTextFixlineBufferPrivate))

static GObjectClass* parent_class;

/* Checks whether chunk consists only of newline characters */
static gboolean
inf_text_fixline_buffer_chunk_only_newlines(InfTextChunk* chunk)
{
  const gchar* text;
  gsize bytes;
  const gchar* end;
  gunichar c;

  /* TODO: Implement this properly with iconv */
  g_assert(strcmp(inf_text_chunk_get_encoding(chunk), "UTF-8") == 0);

  text = inf_text_chunk_get_text(chunk, &bytes);
  end = text + bytes;

  while(text != end)
  {
    c = g_utf8_get_char(text);
    if(c != '\n') return FALSE;

    text = g_utf8_next_char(text);
  }

  return TRUE;
}

/* Adds len newlines into chunk at the given position */
static void
inf_text_fixline_buffer_newlines_to_chunk(InfTextChunk* chunk,
                                          guint chunk_pos,
                                          guint len,
                                          guint user_id)
{
  gchar stext[16];
  gchar* text;
  guint i;

  if(len > sizeof(stext))
    text = g_malloc(len * sizeof(gchar));
  else
    text = stext;

  for(i = 0; i < len; ++i)
    text[i] = '\n';

  /* TODO: Implement this properly with iconv */
  g_assert(strcmp(inf_text_chunk_get_encoding(chunk), "UTF-8") == 0);

  inf_text_chunk_insert_text(chunk, chunk_pos, text, len, len, user_id);

  if(text != stext)
    g_free(text);
}

/* Count the number of trailing newlines in the buffer, but only check
 * up to the given position. Set min_check to 0 to check the whole buffer. */
static guint
inf_text_fixline_buffer_buffer_count_trailing_newlines(InfTextBuffer* buffer,
                                                       guint min_check)
{
  InfTextBufferIter* iter;
  guint cur_pos;

  gchar* text;
  gchar* text_pos;
  gunichar c;

  /* TODO: Implement this properly with iconv */
  g_assert(strcmp(inf_text_buffer_get_encoding(buffer), "UTF-8") == 0);

  iter = inf_text_buffer_create_end_iter(buffer);
  if(iter == NULL) return 0;

  cur_pos = inf_text_buffer_get_length(buffer);
  text = inf_text_buffer_iter_get_text(buffer, iter);
  text_pos = text + inf_text_buffer_iter_get_bytes(buffer, iter);

  for(cur_pos = inf_text_buffer_get_length(buffer);
      cur_pos > min_check;
      --cur_pos)
  {
    if(text_pos == text)
    {
      g_free(text);
      inf_text_buffer_iter_prev(buffer, iter);
      text = inf_text_buffer_iter_get_text(buffer, iter);
      text_pos = text + inf_text_buffer_iter_get_bytes(buffer, iter);
    }

    g_assert(text_pos > text);

    text_pos = g_utf8_prev_char(text_pos);
    c = g_utf8_get_char(text_pos);
    if(c != '\n') break;
  }

  g_free(text);
  inf_text_buffer_destroy_iter(buffer, iter);

  return inf_text_buffer_get_length(buffer) - cur_pos;
}

/* Checks whether the given buffer contains only newline characters
 * after the given position */
static gboolean
inf_text_fixline_buffer_buffer_only_newlines_after(InfTextBuffer* buffer,
                                                   guint pos)
{
  guint new_lines;

  new_lines = inf_text_fixline_buffer_buffer_count_trailing_newlines(
    buffer,
    pos
  );

  if(inf_text_buffer_get_length(buffer) - new_lines <= pos) return TRUE;
  return FALSE;
}

/* advance to next author in keep */
static guint
inf_text_fixline_buffer_keep_next(const guint* keep,
                                  guint n_keep,
                                  guint pos)
{
  guint new_author;

  g_assert(pos < n_keep);

  new_author = keep[pos++];
  while(pos < n_keep && keep[pos] == new_author)
    ++pos;

  return pos;
}

/* advance to previous author in keep */
static guint
inf_text_fixline_buffer_keep_prev(const guint* keep,
                                  guint n_keep,
                                  guint pos)
{
  guint new_author;

  g_assert(pos > 0);

  new_author = keep[--pos];
  while(pos > 0 && keep[pos-1] == new_author)
    --pos;

  return pos;
}

/* Fills a part of the keep from a chunk */
static void
inf_text_fixline_buffer_chunk_to_keep(InfTextFixlineBuffer* fixline_buffer,
                                      InfTextChunk* chunk,
                                      guint chunk_pos,
                                      guint keep_pos,
                                      guint len)
{
  InfTextFixlineBufferPrivate* priv;
  InfTextChunkIter iter;
  gboolean result;
  guint offset;
  guint i;

  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  g_assert(priv->n_keep > 0);
  g_assert(keep_pos + len <= (guint)priv->n_keep);
  g_assert(chunk_pos + len <= inf_text_chunk_get_length(chunk));
  g_assert(chunk_pos == 0); /* TODO: Lift this restriction */

  offset = 0;
  result = inf_text_chunk_iter_init_begin(chunk, &iter);
  g_assert(result == TRUE || len == 0);

  for(i = 0; i < len; ++i)
  {
    g_assert(i - offset <= inf_text_chunk_iter_get_length(&iter));
    if(i - offset == inf_text_chunk_iter_get_length(&iter))
    {
      offset = i;
      result = inf_text_chunk_iter_next(&iter);
      g_assert(result == TRUE);
    }

    g_assert(inf_text_chunk_iter_get_length(&iter) > 0);
    priv->keep[keep_pos + i] = inf_text_chunk_iter_get_author(&iter);
  }
}

/* Insert into a chunk from the keep */
static void
inf_text_fixline_buffer_keep_to_chunk(InfTextFixlineBuffer* fixline_buffer,
                                      InfTextChunk* chunk,
                                      guint chunk_pos,
                                      guint keep_pos,
                                      guint len)
{
  InfTextFixlineBufferPrivate* priv;
  guint i;

  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  g_assert(priv->n_keep > 0);
  g_assert(keep_pos + len <= (guint)priv->n_keep);
  g_assert(chunk_pos <= inf_text_chunk_get_length(chunk));

  /* TODO: Implement this properly with iconv */
  g_assert(strcmp(inf_text_chunk_get_encoding(chunk), "UTF-8") == 0);

  /* TODO: merge for adjacent users, so we
   * have fewer calls to inf_text_chunk_insert_text */
  for(i = 0; i < len; ++i)
  {
    inf_text_chunk_insert_text(
      chunk,
      chunk_pos + i,
      "\n",
      1,
      1,
      priv->keep[keep_pos + i]
    );
  }
}

static void
inf_text_fixline_buffer_text_inserted_cb(InfTextBuffer* buffer,
                                         guint pos,
                                         InfTextChunk* chunk,
                                         InfUser* user,
                                         gpointer user_data);

static void
inf_text_fixline_buffer_text_erased_cb(InfTextBuffer* buffer,
                                       guint pos,
                                       InfTextChunk* chunk,
                                       InfUser* user,
                                       gpointer user_data);

/* Move some newlines from the keep to the base buffer */
static void
inf_text_fixline_buffer_keep_to_base(InfTextFixlineBuffer* fixline_buffer,
                                     guint len)
{
  InfTextFixlineBufferPrivate* priv;
  InfTextChunk* chunk;
  guint keep_len;

  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  chunk = inf_text_chunk_new(inf_text_buffer_get_encoding(priv->buffer));

  if(priv->n_keep > 0)
  {
    keep_len = MIN( (guint)priv->n_keep, len);

    inf_text_fixline_buffer_keep_to_chunk(
      fixline_buffer,
      chunk,
      0,
      0,
      keep_len
    );

    if(keep_len < priv->n_keep)
    {
      g_memmove(
        priv->keep,
        priv->keep + keep_len,
        (priv->n_keep - keep_len) * sizeof(guint)
      );

      priv->keep = g_realloc(
        priv->keep,
        (priv->n_keep - keep_len) * sizeof(guint)
      );
    }
    else
    {
      g_free(priv->keep);
      priv->keep = NULL;
    }

    priv->n_keep -= keep_len;
    len -= keep_len;
  }

  if(len > 0)
  {
    g_assert(priv->n_keep <= 0);

    inf_text_fixline_buffer_newlines_to_chunk(
      chunk,
      inf_text_chunk_get_length(chunk),
      len,
      0
    );

    priv->n_keep -= (gint)len;
  }

  inf_signal_handlers_block_by_func(
    priv->buffer,
    G_CALLBACK(inf_text_fixline_buffer_text_inserted_cb),
    fixline_buffer
  );

  inf_text_buffer_insert_chunk(
    priv->buffer,
    inf_text_buffer_get_length(priv->buffer),
    chunk,
    NULL
  );

  inf_signal_handlers_unblock_by_func(
    priv->buffer,
    G_CALLBACK(inf_text_fixline_buffer_text_inserted_cb),
    fixline_buffer
  );

  inf_text_chunk_free(chunk);
}

/* Move some newlines from the base buffer to the keep */
static void
inf_text_fixline_buffer_base_to_keep(InfTextFixlineBuffer* fixline_buffer,
                                     guint len)
{
  InfTextFixlineBufferPrivate* priv;
  guint use_keep;
  guint remaining_len;

  InfTextBufferIter* iter;
  guint iter_offset;
  guint iter_pos;
  gboolean iter_result;

  guint keep_len;
  guint keep_index;
  guint keep_author;
  guint i;

  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  g_assert(inf_text_buffer_get_length(priv->buffer) >= len);

  if(priv->n_keep < 0)
  {
    use_keep = MIN( (guint)(-priv->n_keep), len);
    priv->n_keep = -(gint)((guint)(-priv->n_keep) - use_keep);
    remaining_len = len - use_keep;
  }
  else
  {
    remaining_len = len;
  }

  if(remaining_len > 0)
  {
    g_assert(priv->n_keep >= 0);

    iter = inf_text_buffer_create_end_iter(priv->buffer);
    g_assert(iter != NULL);

    iter_pos = 0;
    iter_offset = inf_text_buffer_iter_get_length(priv->buffer, iter);

    /* Go back len - remaining_len characters from the end of the buffer.
     * These characters are in the represented buffer already since we had
     * negative keeps for them */
    while(iter_pos < len - remaining_len)
    {
      if(iter_offset <= len - remaining_len)
      {
        iter_pos = iter_offset;
        iter_result = inf_text_buffer_iter_prev(priv->buffer, iter);
        g_assert(iter_result == TRUE);

        iter_offset += inf_text_buffer_iter_get_length(priv->buffer, iter);
      }
      else
      {
        iter_pos = (len - remaining_len);
      }
    }

    /* Now, from here, move remaining_len characters into the keep */
    priv->keep = g_realloc(
      priv->keep,
      (priv->n_keep + remaining_len) * sizeof(guint)
    );

    g_memmove(
      priv->keep + remaining_len,
      priv->keep,
      priv->n_keep * sizeof(guint)
    );

    while(iter_pos < len)
    {
      keep_author = inf_text_buffer_iter_get_author(priv->buffer, iter);
      keep_index = len - iter_pos;

      if(iter_offset <= len)
      {
        keep_len = iter_offset - iter_pos;
        iter_pos = iter_offset;
        iter_result = inf_text_buffer_iter_prev(priv->buffer, iter);
        g_assert(iter_result == TRUE);

        iter_offset += inf_text_buffer_iter_get_length(priv->buffer, iter);
      }
      else
      {
        keep_len = len - iter_pos;
        iter_pos = len;
      }

      for(i = 0; i < keep_len; ++i)
        priv->keep[keep_index - 1 - i] = keep_author;
    }

    inf_text_buffer_destroy_iter(priv->buffer, iter);
    priv->n_keep += remaining_len;
  }

  /* The keep is now updated, we can go ahead and make the change to the
   * base buffer now */

  inf_signal_handlers_block_by_func(
    priv->buffer,
    G_CALLBACK(inf_text_fixline_buffer_text_erased_cb),
    fixline_buffer
  );

  inf_text_buffer_erase_text(
    priv->buffer,
    inf_text_buffer_get_length(priv->buffer) - len,
    len,
    NULL
  );

  inf_signal_handlers_unblock_by_func(
    priv->buffer,
    G_CALLBACK(inf_text_fixline_buffer_text_erased_cb),
    fixline_buffer
  );
}

static void
inf_text_fixline_buffer_fix_lines(InfTextFixlineBuffer* fixline_buffer)
{
  InfTextFixlineBufferPrivate* priv;
  guint count;

  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  count = inf_text_fixline_buffer_buffer_count_trailing_newlines(
    priv->buffer,
    0
  );

  if(count < priv->lines)
  {
    inf_text_fixline_buffer_keep_to_base(fixline_buffer, priv->lines - count);
  }
  else if(count > priv->lines)
  {
    inf_text_fixline_buffer_base_to_keep(fixline_buffer, count - priv->lines);
  }
}

static void
inf_text_fixline_buffer_dispatch_func(gpointer user_data)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(user_data);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  g_assert(priv->dispatch != NULL);
  priv->dispatch = NULL;

  inf_text_fixline_buffer_fix_lines(fixline_buffer);
}

static void
inf_text_fixline_buffer_text_inserted_cb(InfTextBuffer* buffer,
                                         guint pos,
                                         InfTextChunk* chunk,
                                         InfUser* user,
                                         gpointer user_data)
{
  InfTextFixlineBufferPrivate* priv;
  guint chunk_length;
  guint buffer_length;
  guint end;

  guint user_pos;
  gint user_len;

  InfTextChunk* new_chunk;
  gchar stext[16];
  gchar* text;
  guint i;

  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(user_data);

  chunk_length = inf_text_chunk_get_length(chunk);
  g_assert(inf_text_buffer_get_length(buffer) >= chunk_length);

  /* Note that this is the buffer length before the operation */
  buffer_length = inf_text_buffer_get_length(buffer) - chunk_length;
  g_assert(priv->n_keep > 0 || buffer_length >= (guint)(-priv->n_keep));

  /* This is now the end of the infinote buffer */
  end = buffer_length + priv->n_keep;

  if(inf_text_fixline_buffer_chunk_only_newlines(chunk) &&
     inf_text_fixline_buffer_buffer_only_newlines_after(buffer, pos + chunk_length))
  {
    /* Newlines were inserted at the end of the buffer. Don't propagate.
     * Note that this step is optional, we could also propagate it to the
     * infinote buffer, but it might lead to a strange user experience */

    /* TODO: In principle we have to re-brand some
     * of the newlines inside the buffer here: */
#if 0
    for(i = 0; i < inf_text_buffer_get_length(buffer) - inf_text_chunk_get_length(chunk) - pos; ++i)
      author(i + pos) = author(i + pos + inf_text_chunk_get_length(chunk))

    if(priv->n_keep > 0)
      for(i = 0; i < priv->n_keep && i < inf_text_chunk_get_length(chunk); ++i)
        author(i + pos + inf_text_chunk_get_length(chunk)) = priv->keep[i];
#endif

    /* remove the used keeps */
    if(priv->n_keep > 0 && inf_text_chunk_get_length(chunk) > (guint)priv->n_keep)
      priv->n_keep = -(int)(inf_text_chunk_get_length(chunk) - priv->n_keep);
    else
      priv->n_keep = priv->n_keep - inf_text_chunk_get_length(chunk);

    if(priv->n_keep > 0)
    {
      g_memmove(
        priv->keep,
        priv->keep + inf_text_chunk_get_length(chunk),
        priv->n_keep * sizeof(guint)
      );

      priv->keep = g_realloc(priv->keep, priv->n_keep * sizeof(guint));
    }
    else
    {
      g_free(priv->keep);
      priv->keep = NULL;
    }

    /* TODO: We don't know whether this was an insert-caret or not, but
     * assume for now that it was. Advance the user's caret, which would
     * account for the added newlines that we swallowed above. */
    /* TODO: A better way might be to just skip this special handling
     * altogether and just actually propagate this change. */
    user_pos = inf_text_user_get_caret_position(INF_TEXT_USER(user));
    user_len = inf_text_user_get_selection_length(INF_TEXT_USER(user));

    inf_text_move_operation_transform_insert(
      pos,
      chunk_length,
      &user_pos,
      &user_len,
      FALSE
    );

    inf_text_user_set_selection(
      INF_TEXT_USER(user),
      user_pos,
      user_len,
      TRUE
    );
  }
  else
  {
    if(pos > end)
    {
      /* TODO: Should we handle caret updates here as well? */
      g_assert(priv->n_keep < 0);
      g_assert((guint)(-priv->n_keep) >= pos - end);

      /* We remove the first pos - end keeps and prepend them to the text */
      priv->n_keep += (pos - end);

      new_chunk = inf_text_chunk_copy(chunk);

      inf_text_fixline_buffer_newlines_to_chunk(
        new_chunk,
        0,
        pos - end,
        user == NULL ? 0 : inf_user_get_id(user)
      );

      inf_text_buffer_text_inserted(
        INF_TEXT_BUFFER(user_data),
        end,
        new_chunk,
        user
      );

      inf_text_chunk_free(new_chunk);
    }
    else
    {
      /* Just propagate */
      inf_text_buffer_text_inserted(
        INF_TEXT_BUFFER(user_data),
        pos,
        chunk,
        user
      );
    }
  }

  /* Schedule a dispatch to correct the number of newlines in the underlying
   * buffer. */
  if(priv->dispatch == NULL)
  {
    priv->dispatch = inf_io_add_dispatch(
      priv->io,
      inf_text_fixline_buffer_dispatch_func,
      user_data,
      NULL
    );
  }
}

static void
inf_text_fixline_buffer_text_erased_cb(InfTextBuffer* buffer,
                                       guint pos,
                                       InfTextChunk* chunk,
                                       InfUser* user,
                                       gpointer user_data)
{
  InfTextFixlineBufferPrivate* priv;
  guint chunk_length;
  guint buffer_length;
  guint end;
  guint use_keep;
  InfTextChunk* new_chunk;

  guint user_pos;
  gint user_len;

  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(user_data);

  chunk_length = inf_text_chunk_get_length(chunk);
  buffer_length = inf_text_buffer_get_length(buffer) + chunk_length;
  g_assert(priv->n_keep > 0 || buffer_length >= (guint)(-priv->n_keep));

  end = buffer_length + priv->n_keep;

  /* buffer_length: length of base buffer before the operation
   * end: length of buffer before the operation */

  if(inf_text_fixline_buffer_chunk_only_newlines(chunk) &&
     inf_text_fixline_buffer_buffer_only_newlines_after(buffer, pos))
  {
    /* Newlines were removed from the end of the buffer. Don't propagate.
     * Note that this step is optional, we could also propagate it to the
     * infinote buffer, but it might lead to a strange user experience */

    /* TODO: In principle we have to re-brand some
     * of the newlines inside the buffer here: */
#if 0
    for(i = pos; i < inf_text_buffer_get_length(buffer); ++i)
      rebrand(i);
#endif

    if(priv->n_keep > 0)
      use_keep = inf_text_chunk_get_length(chunk);
    else if((guint)(-priv->n_keep) < inf_text_chunk_get_length(chunk))
      use_keep = inf_text_chunk_get_length(chunk) - (guint)(-priv->n_keep);
    else
      use_keep = 0;

    priv->n_keep += inf_text_chunk_get_length(chunk);
    if(priv->n_keep > 0)
    {
      g_assert(
        priv->keep == NULL ||
        (guint)priv->n_keep > inf_text_chunk_get_length(chunk)
      );

      priv->keep = g_realloc(priv->keep, priv->n_keep * sizeof(guint));
      if((guint)priv->n_keep > inf_text_chunk_get_length(chunk))
      {
        g_memmove(
          priv->keep + inf_text_chunk_get_length(chunk),
          priv->keep,
          (priv->n_keep - inf_text_chunk_get_length(chunk)) * sizeof(guint)
        );
      }

      inf_text_fixline_buffer_chunk_to_keep(
        INF_TEXT_FIXLINE_BUFFER(user_data),
        chunk,
        0,
        0,
        use_keep
      );
    }

    /* TODO: We don't know whether this was an erase-caret or not, but
     * assume for now that it was. Advance the user's caret, which would
     * account for the removed newlines that we swallowed above. */
    /* TODO: A better way might be to just skip this special handling
     * altogether and just actually propagate this change. */
    user_pos = inf_text_user_get_caret_position(INF_TEXT_USER(user));
    user_len = inf_text_user_get_selection_length(INF_TEXT_USER(user));

    inf_text_move_operation_transform_delete(
      pos,
      chunk_length,
      &user_pos,
      &user_len
    );

    inf_text_user_set_selection(
      INF_TEXT_USER(user),
      user_pos,
      user_len,
      TRUE
    );
  }
  else
  {
    if(pos > end)
    {
      g_assert(priv->n_keep < 0);
      /* only keep modifications. We could handle it in principle, but it
       * should not happen, since the keep stores only newline characters,
       * in which case we would have ended up in the case above.  */
      g_assert_not_reached();
    }
    else if(pos + chunk_length > end)
    {
      /* Propagate partly */
      /* TODO: Should we handle caret updates here as well? */
      g_assert(priv->n_keep < 0);
      g_assert(chunk_length - (end - pos) <= (guint)(-priv->n_keep));

      priv->n_keep += (chunk_length - (end - pos));

      new_chunk = inf_text_chunk_copy(chunk);
      inf_text_chunk_erase(new_chunk, end - pos, chunk_length - (end - pos));

      inf_text_buffer_text_erased(
        INF_TEXT_BUFFER(user_data),
        pos,
        new_chunk,
        user
      );

      inf_text_chunk_free(new_chunk);

    }
    else
    {
      /* Propagate fully */
      inf_text_buffer_text_erased(
        INF_TEXT_BUFFER(user_data),
        pos,
        chunk,
        user
      );
    }
  }

  /* sanity check */
  g_assert(
    priv->n_keep > 0 ||
    inf_text_buffer_get_length(buffer) >= (guint)(-priv->n_keep)
  );

  /* Schedule a dispatch to correct the number of newlines in the underlying
   * buffer. */
  if(priv->dispatch == NULL)
  {
    priv->dispatch = inf_io_add_dispatch(
      priv->io,
      inf_text_fixline_buffer_dispatch_func,
      user_data,
      NULL
    );
  }
}

static void
inf_text_fixline_buffer_init(GTypeInstance* instance,
                             gpointer g_class)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(instance);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  priv->io = NULL;
  priv->buffer = NULL;
  priv->lines = 0;
  priv->keep = NULL;
  priv->n_keep = 0;
  priv->dispatch = NULL;
}

static void
inf_text_fixline_buffer_constructed(GObject* object)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(object);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  /* Keep the number of lines at the end fixed */
  inf_text_fixline_buffer_fix_lines(fixline_buffer);
}

static void
inf_text_fixline_buffer_dispose(GObject* object)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(object);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(priv->buffer != NULL)
  {
    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_fixline_buffer_text_inserted_cb),
      fixline_buffer
    );

    inf_signal_handlers_disconnect_by_func(
      G_OBJECT(priv->buffer),
      G_CALLBACK(inf_text_fixline_buffer_text_erased_cb),
      fixline_buffer
    );

    g_object_unref(priv->buffer);
    priv->buffer = NULL;
  }

  if(priv->io != NULL)
  {
    if(priv->dispatch != NULL)
    {
      inf_io_remove_dispatch(priv->io, priv->dispatch);
      priv->dispatch = NULL;
    }

    g_object_unref(priv->io);
    priv->io = NULL;
  }

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_text_fixline_buffer_finalize(GObject* object)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(object);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  g_free(priv->keep);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_text_fixline_buffer_set_property(GObject* object,
                                     guint prop_id,
                                     const GValue* value,
                                     GParamSpec* pspec)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  gboolean modified;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(object);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  switch(prop_id)
  {
  case PROP_IO:
    /* construct only */
    g_assert(priv->io == NULL);
    priv->io = INF_IO(g_value_dup_object(value));
    break;
  case PROP_BUFFER:
    /* construct only */
    g_assert(priv->buffer == NULL);
    priv->buffer = INF_TEXT_BUFFER(g_value_dup_object(value));

    g_signal_connect(
      G_OBJECT(priv->buffer),
      "text-inserted",
      G_CALLBACK(inf_text_fixline_buffer_text_inserted_cb),
      fixline_buffer
    );

    g_signal_connect(
      G_OBJECT(priv->buffer),
      "text-erased",
      G_CALLBACK(inf_text_fixline_buffer_text_erased_cb),
      fixline_buffer
    );

    break;
  case PROP_LINES:
    /* construct only */
    g_assert(priv->lines == 0);
    priv->lines = g_value_get_uint(value);
    break;
  case PROP_MODIFIED:
    inf_buffer_set_modified(
      INF_BUFFER(priv->buffer),
      g_value_get_boolean(value)
    );

    break;
  fixline:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_text_fixline_buffer_get_property(GObject* object,
                                     guint prop_id,
                                     GValue* value,
                                     GParamSpec* pspec)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  gboolean modified;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(object);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  switch(prop_id)
  {
  case PROP_IO:
    g_value_set_object(value, priv->io);
    break;
  case PROP_BUFFER:
    g_value_set_object(value, priv->buffer);
    break;
  case PROP_LINES:
    g_value_set_uint(value, priv->lines);
    break;
  case PROP_MODIFIED:
    g_value_set_boolean(
      value,
      inf_buffer_get_modified(INF_BUFFER(priv->buffer))
    );

    break;
  fixline:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static gboolean
inf_text_fixline_buffer_buffer_get_modified(InfBuffer* buffer)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  return inf_buffer_get_modified(INF_BUFFER(priv->buffer));
}

static void
inf_text_fixline_buffer_buffer_set_modified(InfBuffer* buffer,
                                            gboolean modified)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  inf_buffer_set_modified(INF_BUFFER(priv->buffer), modified);
}

static const gchar*
inf_text_fixline_buffer_buffer_get_encoding(InfTextBuffer* buffer)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  return inf_text_buffer_get_encoding(priv->buffer);
}

static guint
inf_text_fixline_buffer_get_length(InfTextBuffer* buffer)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  guint buf_len;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  buf_len = inf_text_buffer_get_length(priv->buffer);
  g_assert(priv->n_keep > 0 || buf_len >= (guint)(-priv->n_keep));

  return buf_len + priv->n_keep;
}

static InfTextChunk*
inf_text_fixline_buffer_buffer_get_slice(InfTextBuffer* buffer,
                                         guint pos,
                                         guint len)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  guint buf_len;
  guint keep_begin;
  InfTextChunk* chunk;
  guint i;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);
  buf_len = inf_text_buffer_get_length(priv->buffer);

  if(pos + len > buf_len)
  {
    g_assert(priv->n_keep > 0);
    if(pos < buf_len)
    {
      chunk = inf_text_buffer_get_slice(priv->buffer, pos, buf_len - pos);

      inf_text_fixline_buffer_keep_to_chunk(
        fixline_buffer,
        chunk,
        inf_text_chunk_get_length(chunk),
        0,
        pos + len - buf_len
      );
    }
    else
    {
      chunk = inf_text_chunk_new(inf_text_buffer_get_encoding(priv->buffer));

      inf_text_fixline_buffer_keep_to_chunk(
        fixline_buffer,
        chunk,
        0,
        pos - buf_len,
        len
      );
    }
  }
  else
  {
    chunk = inf_text_buffer_get_slice(priv->buffer, pos, len);
  }

  return chunk;
}

static void
inf_text_fixline_buffer_buffer_insert_text(InfTextBuffer* buffer,
                                           guint pos,
                                           InfTextChunk* chunk,
                                           InfUser* user)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  guint buf_len;
  InfTextChunk* new_chunk;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);
  buf_len = inf_text_buffer_get_length(priv->buffer);

  inf_signal_handlers_block_by_func(
    priv->buffer,
    G_CALLBACK(inf_text_fixline_buffer_text_inserted_cb),
    fixline_buffer
  );

  if(pos >= buf_len && inf_text_fixline_buffer_chunk_only_newlines(chunk))
  {
    /* Add the added newlines only to the keep */
    g_assert(priv->n_keep >= 0);
    g_assert(pos - buf_len <= (guint)priv->n_keep);

    priv->keep = g_realloc(
      priv->keep,
      (priv->n_keep + inf_text_chunk_get_length(chunk)) * sizeof(guint)
    );

    if(pos - buf_len < priv->n_keep)
    {
      g_memmove(
        priv->keep + (pos - buf_len + inf_text_chunk_get_length(chunk)),
        priv->keep + (pos - buf_len),
        priv->n_keep - (pos - buf_len)
      );
    }

    priv->n_keep += inf_text_chunk_get_length(chunk);

    inf_text_fixline_buffer_chunk_to_keep(
      fixline_buffer,
      chunk,
      0,
      pos - buf_len,
      inf_text_chunk_get_length(chunk)
    );
  }
  else if(pos > buf_len)
  {
    g_assert(priv->n_keep > 0);
    g_assert((guint)priv->n_keep >= pos - buf_len);

    new_chunk = inf_text_chunk_copy(chunk);

    inf_text_fixline_buffer_keep_to_chunk(
      fixline_buffer,
      new_chunk,
      0,
      0,
      pos - buf_len
    );

    g_memmove(
      priv->keep,
      priv->keep + (pos - buf_len),
      ((guint)priv->n_keep - (pos - buf_len)) * sizeof(guint)
    );

    priv->keep = g_realloc(
      priv->keep,
      ((guint)priv->n_keep - (pos - buf_len)) * sizeof(guint)
    );

    priv->n_keep -= (pos - buf_len);

    inf_text_buffer_insert_chunk(priv->buffer, buf_len, new_chunk, user);

    inf_text_chunk_free(new_chunk);
  }
  else
  {
    inf_text_buffer_insert_chunk(priv->buffer, pos, chunk, user);
  }

  inf_signal_handlers_unblock_by_func(
    priv->buffer,
    G_CALLBACK(inf_text_fixline_buffer_text_inserted_cb),
    fixline_buffer
  );

  /* Notify */
  inf_text_buffer_text_inserted(buffer, pos, chunk, user);

  /* Keep the number of lines at the end fixed */
  inf_text_fixline_buffer_fix_lines(fixline_buffer);
}

static void
inf_text_fixline_buffer_buffer_erase_text(InfTextBuffer* buffer,
                                          guint pos,
                                          guint len,
                                          InfUser* user)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  InfTextChunk* erased_content;
  guint buf_len;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);
  buf_len = inf_text_buffer_get_length(priv->buffer);

  inf_signal_handlers_block_by_func(
    priv->buffer,
    G_CALLBACK(inf_text_fixline_buffer_text_erased_cb),
    fixline_buffer
  );

  erased_content = inf_text_buffer_get_slice(buffer, pos, len);

  if(pos + len > buf_len)
  {
    if(pos > buf_len)
    {
      /* Only erase in the keep */
      g_assert(priv->n_keep > 0);
      g_assert((guint)priv->n_keep >= len);
      g_assert(pos + len - buf_len <= (guint)priv->n_keep);

      if(pos + len - buf_len < (guint)priv->n_keep)
      {
        g_memmove(
          priv->keep + (pos - buf_len),
          priv->keep + (pos + len - buf_len),
          (priv->n_keep - (pos + len - buf_len)) * sizeof(guint)
        );
      }

      priv->keep = g_realloc(
        priv->keep,
        (priv->n_keep - len) * sizeof(guint)
      );

      priv->n_keep -= len;
    }
    else
    {
      g_assert(priv->n_keep > 0);
      g_assert(pos + len - buf_len <= (guint)priv->n_keep);

      if(pos + len - buf_len < (guint)priv->n_keep)
      {
        g_memmove(
          priv->keep,
          priv->keep + (pos + len - buf_len),
          (priv->n_keep - (pos + len - buf_len)) * sizeof(guint)
        );
      }

      priv->keep = g_realloc(
        priv->keep,
        (priv->n_keep - (pos + len - buf_len)) * sizeof(guint)
      );

      priv->n_keep -= (pos + len - buf_len);

      inf_text_buffer_erase_text(priv->buffer, pos, buf_len - pos, user);
    }
  }
  else
  {
    inf_text_buffer_erase_text(priv->buffer, pos, len, user);
  }

  inf_signal_handlers_unblock_by_func(
    priv->buffer,
    G_CALLBACK(inf_text_fixline_buffer_text_erased_cb),
    fixline_buffer
  );

  /* Notify */
  inf_text_buffer_text_erased(buffer, pos, erased_content, user);
  inf_text_chunk_free(erased_content);

  /* Keep the number of lines at the end fixed */
  inf_text_fixline_buffer_fix_lines(fixline_buffer);
}

static InfTextBufferIter*
inf_text_fixline_buffer_buffer_create_begin_iter(InfTextBuffer* buffer)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  InfTextBufferIter* base_iter;
  InfTextBufferIter* iter;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(priv->n_keep <= 0 &&
     inf_text_buffer_get_length(priv->buffer) == (guint)(-priv->n_keep))
  {
    return NULL;
  }

  base_iter = inf_text_buffer_create_begin_iter(priv->buffer);
  if(base_iter == NULL)
  {
    g_assert(priv->n_keep > 0);

    iter = g_slice_new(InfTextBufferIter);
    iter->base_iter = NULL;
    iter->keep_begin = 0;

    iter->keep_end = inf_text_fixline_buffer_keep_next(
      priv->keep,
      priv->n_keep,
      0
    );
  }
  else
  {
    iter = g_slice_new(InfTextBufferIter);
    iter->base_iter = base_iter;
  }

  return iter;
}

static InfTextBufferIter*
inf_text_fixline_buffer_buffer_create_end_iter(InfTextBuffer* buffer)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  InfTextBufferIter* iter;
  InfTextBufferIter* base_iter;
  guint buf_len;
  gboolean result;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);
  buf_len = inf_text_buffer_get_length(priv->buffer);

  if(priv->n_keep > 0)
  {
    iter = g_slice_new(InfTextBufferIter);
    iter->base_iter = NULL;
    iter->keep_end = priv->n_keep;

    iter->keep_begin = inf_text_fixline_buffer_keep_prev(
      priv->keep,
      priv->n_keep,
      priv->n_keep
    );
  }
  else
  {
    if(buf_len == (guint)(-priv->n_keep))
      return NULL;

    base_iter = inf_text_buffer_create_end_iter(priv->buffer);
    g_assert(base_iter != NULL);

    while(inf_text_buffer_iter_get_offset(priv->buffer, base_iter) >=
          buf_len - (guint)(-priv->n_keep))
    {
      result = inf_text_buffer_iter_prev(priv->buffer, base_iter);
      g_assert(result == TRUE);
    }

    iter = g_slice_new(InfTextBufferIter);
    iter->base_iter = base_iter;
  }

  return iter;
}

static void
inf_text_fixline_buffer_buffer_destroy_iter(InfTextBuffer* buffer,
                                            InfTextBufferIter* iter)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(iter->base_iter != NULL)
    inf_text_buffer_destroy_iter(priv->buffer, iter->base_iter);

  g_slice_free(InfTextBufferIter, iter);
}

static gboolean
inf_text_fixline_buffer_buffer_iter_next(InfTextBuffer* buffer,
                                         InfTextBufferIter* iter)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  guint offset;
  guint length;
  guint buf_len;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(iter->base_iter == NULL)
  {
    g_assert(priv->n_keep > 0);

    /* Move inside keep */
    if(iter->keep_end == priv->n_keep)
      return FALSE;

    iter->keep_begin = iter->keep_end;
    iter->keep_end = inf_text_fixline_buffer_keep_next(
      priv->keep,
      priv->n_keep,
      iter->keep_end
    );

    return TRUE;
  }
  else if(priv->n_keep > 0)
  {
    /* Try to advance; if not possible: move to keep */
    if(inf_text_buffer_iter_next(priv->buffer, iter->base_iter) == TRUE)
      return TRUE;

    inf_text_buffer_destroy_iter(priv->buffer, iter->base_iter);
    iter->base_iter = NULL;

    iter->keep_begin = 0;
    iter->keep_end = inf_text_fixline_buffer_keep_next(
      priv->keep,
      priv->n_keep,
      0
    );

    return TRUE;
  }
  else
  {
    offset = inf_text_buffer_iter_get_offset(priv->buffer, iter->base_iter);
    length = inf_text_buffer_iter_get_length(priv->buffer, iter->base_iter);
    buf_len = inf_text_buffer_get_length(priv->buffer);

    if(offset + length >= buf_len - (guint)(-priv->n_keep))
      return FALSE;

    return inf_text_buffer_iter_next(priv->buffer, iter->base_iter);
  }
}

static gboolean
inf_text_fixline_buffer_buffer_iter_prev(InfTextBuffer* buffer,
                                         InfTextBufferIter* iter)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(iter->base_iter == NULL)
  {
    g_assert(priv->n_keep > 0);

    if(iter->keep_begin == 0)
    {
      iter->base_iter = inf_text_buffer_create_end_iter(priv->buffer);
      if(iter->base_iter == NULL)
        return FALSE;

      return TRUE;
    }
    else
    {
      iter->keep_end = iter->keep_begin;
      iter->keep_begin = inf_text_fixline_buffer_keep_next(
        priv->keep,
        priv->n_keep,
        iter->keep_begin
      );

      return TRUE;
    }
  }
  else
  {
    return inf_text_buffer_iter_prev(priv->buffer, iter->base_iter);
  }
}

static gpointer
inf_text_fixline_buffer_buffer_iter_get_text(InfTextBuffer* buffer,
                                             InfTextBufferIter* iter)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  gpointer text;
  guint i;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(iter->base_iter == NULL)
  {
    /* TODO: This only works for UTF-8 encoding: */
    g_assert(iter->keep_end > iter->keep_begin);
    text = g_malloc(iter->keep_end - iter->keep_begin);
    for(i = 0; i < iter->keep_end - iter->keep_begin; ++i)
      ((gchar*)text)[i] = '\n';
    return text;
  }
  else
  {
    text = inf_text_buffer_iter_get_text(priv->buffer, iter->base_iter);
    /* TODO: We could cut away the end here with a g_realloc, but it's not
     * really necessary. */
    return text;
  }
}

static guint
inf_text_fixline_buffer_buffer_iter_get_offset(InfTextBuffer* buffer,
                                               InfTextBufferIter* iter)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(iter->base_iter == NULL)
  {
    return inf_text_buffer_get_length(priv->buffer) + iter->keep_begin;
  }
  else
  {
    return inf_text_buffer_iter_get_offset(priv->buffer, iter->base_iter);
  }
}

static guint
inf_text_fixline_buffer_buffer_iter_get_length(InfTextBuffer* buffer,
                                               InfTextBufferIter* iter)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  guint length;
  guint offset;
  guint buf_len;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(iter->base_iter == NULL)
  {
    return iter->keep_end - iter->keep_begin;
  }
  else
  {
    length = inf_text_buffer_iter_get_length(priv->buffer, iter->base_iter);
    if(priv->n_keep >= 0)
      return length;

    buf_len = inf_text_buffer_get_length(priv->buffer);
    offset = inf_text_buffer_iter_get_offset(priv->buffer, iter->base_iter);
    g_assert(offset + length <= buf_len);

    if(offset + length > buf_len - (guint)(-priv->n_keep))
    {
      return length - (buf_len - (guint)(-priv->n_keep) - offset);
    }
    else
    {
      return length;
    }
  }
}

static gsize
inf_text_fixline_buffer_buffer_iter_get_bytes(InfTextBuffer* buffer,
                                              InfTextBufferIter* iter)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  gsize bytes;
  guint length;
  guint offset;
  guint buf_len;
  guint extra_chars;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(iter->base_iter == NULL)
  {
    /* TODO: This assumes encoding is UTF-8: */
    return iter->keep_end - iter->keep_begin;
  }
  else
  {
    bytes = inf_text_buffer_iter_get_bytes(priv->buffer, iter->base_iter);
    if(priv->n_keep >= 0)
      return bytes;

    length = inf_text_buffer_iter_get_length(priv->buffer, iter->base_iter);
    offset = inf_text_buffer_iter_get_offset(priv->buffer, iter->base_iter);
    buf_len = inf_text_buffer_get_length(priv->buffer);
    g_assert(offset + length <= buf_len);

    if(offset + length > buf_len - (guint)(-priv->n_keep))
    {
      extra_chars = (buf_len - (guint)(-priv->n_keep) - offset);
      /* TODO: This assumes encoding is UTF-8: */
      return bytes - extra_chars;
    }
    else
    {
      return bytes;
    }
  }
}

static guint
inf_text_fixline_buffer_buffer_iter_get_author(InfTextBuffer* buffer,
                                               InfTextBufferIter* iter)
{
  InfTextFixlineBuffer* fixline_buffer;
  InfTextFixlineBufferPrivate* priv;
  gsize bytes;
  guint length;
  guint offset;
  guint buf_len;
  guint extra_chars;

  fixline_buffer = INF_TEXT_FIXLINE_BUFFER(buffer);
  priv = INF_TEXT_FIXLINE_BUFFER_PRIVATE(fixline_buffer);

  if(iter->base_iter == NULL)
  {
    g_assert(priv->n_keep > 0);
    g_assert(iter->keep_begin < (guint)priv->n_keep);
    return priv->keep[iter->keep_begin];
  }
  else
  {
    return inf_text_buffer_iter_get_author(priv->buffer, iter->base_iter);
  }
}

static void
inf_text_fixline_buffer_class_init(gpointer g_class,
                                   gpointer class_data)
{
  GObjectClass* object_class;
  object_class = G_OBJECT_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextFixlineBufferPrivate));

  object_class->constructed = inf_text_fixline_buffer_constructed;
  object_class->dispose = inf_text_fixline_buffer_dispose;
  object_class->finalize = inf_text_fixline_buffer_finalize;
  object_class->set_property = inf_text_fixline_buffer_set_property;
  object_class->get_property = inf_text_fixline_buffer_get_property;

  g_object_class_override_property(object_class, PROP_MODIFIED, "modified");

  g_object_class_install_property(
    object_class,
    PROP_IO,
    g_param_spec_object(
      "io",
      "IO",
      "The I/O object used to schedule line keeping",
      INF_TYPE_IO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_BUFFER,
    g_param_spec_object(
      "buffer",
      "Buffer",
      "The buffer for which to keep a fixed line count",
      INF_TEXT_TYPE_BUFFER,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_LINES,
    g_param_spec_uint(
      "lines",
      "lines",
      "The number of lines to keep in the underlying buffer",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
inf_text_fixline_buffer_buffer_init(gpointer g_iface,
                                    gpointer iface_data)
{
  InfBufferIface* iface;
  iface = (InfBufferIface*)g_iface;

  iface->get_modified = inf_text_fixline_buffer_buffer_get_modified;
  iface->set_modified = inf_text_fixline_buffer_buffer_set_modified;
}

static void
inf_text_fixline_buffer_text_buffer_init(gpointer g_iface,
                                         gpointer iface_data)
{
  InfTextBufferIface* iface;
  iface = (InfTextBufferIface*)g_iface;

  iface->get_encoding = inf_text_fixline_buffer_buffer_get_encoding;
  iface->get_length = inf_text_fixline_buffer_get_length;
  iface->get_slice = inf_text_fixline_buffer_buffer_get_slice;
  iface->insert_text = inf_text_fixline_buffer_buffer_insert_text;
  iface->erase_text = inf_text_fixline_buffer_buffer_erase_text;
  iface->create_begin_iter = inf_text_fixline_buffer_buffer_create_begin_iter;
  iface->create_end_iter = inf_text_fixline_buffer_buffer_create_end_iter;
  iface->destroy_iter = inf_text_fixline_buffer_buffer_destroy_iter;
  iface->iter_next = inf_text_fixline_buffer_buffer_iter_next;
  iface->iter_prev = inf_text_fixline_buffer_buffer_iter_prev;
  iface->iter_get_text = inf_text_fixline_buffer_buffer_iter_get_text;
  iface->iter_get_offset = inf_text_fixline_buffer_buffer_iter_get_offset;
  iface->iter_get_length = inf_text_fixline_buffer_buffer_iter_get_length;
  iface->iter_get_bytes = inf_text_fixline_buffer_buffer_iter_get_bytes;
  iface->iter_get_author = inf_text_fixline_buffer_buffer_iter_get_author;
  iface->text_inserted = NULL;
  iface->text_erased = NULL;
}

GType
inf_text_fixline_buffer_get_type(void)
{
  static GType fixline_buffer_type = 0;

  if(!fixline_buffer_type)
  {
    static const GTypeInfo fixline_buffer_type_info = {
      sizeof(InfTextFixlineBufferClass),  /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      inf_text_fixline_buffer_class_init, /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof(InfTextFixlineBuffer),       /* instance_size */
      0,                                  /* n_preallocs */
      inf_text_fixline_buffer_init,       /* instance_init */
      NULL                                /* value_table */
    };

    static const GInterfaceInfo buffer_info = {
      inf_text_fixline_buffer_buffer_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo text_buffer_info = {
      inf_text_fixline_buffer_text_buffer_init,
      NULL,
      NULL
    };

    fixline_buffer_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfTextFixlineBuffer",
      &fixline_buffer_type_info,
      0
    );

    g_type_add_interface_static(
      fixline_buffer_type,
      INF_TYPE_BUFFER,
      &buffer_info
    );

    g_type_add_interface_static(
      fixline_buffer_type,
      INF_TEXT_TYPE_BUFFER,
      &text_buffer_info
    );
  }

  return fixline_buffer_type;
}

/**
 * inf_text_fixline_buffer_new:
 * @io: A #InfIo object to schedule timeouts.
 * @buffer: The underlying buffer for which to keep a fixed line count.
 * @n_lines: The number of lines to be kept fixed.
 *
 * Creates a new #InfTextFixlineBuffer which keeps the number of trailing
 * lines of the given underlying buffer fixed to @n_lines.
 *
 * Return Value: A #InfTextFixlineBuffer.
 **/
InfTextFixlineBuffer*
inf_text_fixline_buffer_new(InfIo* io,
                            InfTextBuffer* buffer,
                            guint n_lines)
{
  GObject* object;

  object = g_object_new(
    INF_TEXT_TYPE_FIXLINE_BUFFER,
    "io", io,
    "buffer", buffer,
    "lines", n_lines,
    NULL
  );

  return INF_TEXT_FIXLINE_BUFFER(object);
}

/* vim:set et sw=2 ts=2: */
