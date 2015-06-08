/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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
 * SECTION:inf-text-undo-grouping
 * @title:InfTextUndoGrouping
 * @short_description: Undo grouping for text operations
 * @include: libinftext/inf-text-undo-grouping.h
 * @see_also: #InfAdoptedUndoGrouping
 * @stabilitiy: Unstable
 *
 * #InfTextUndoGrouping handles undo grouping for text operations. It makes
 * sure many insert or delete operations occuring in a row can be undone
 * simultaneousely, taking into account that other users might have issued
 * requests inbetween.
 *
 * Using this class you don't need to connect to
 * #InfAdoptedUndoGrouping::group-requests to perform the grouping.
 */

#include <libinftext/inf-text-undo-grouping.h>
#include <libinftext/inf-text-default-insert-operation.h>
#include <libinftext/inf-text-default-delete-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-move-operation.h>
#include <libinftext/inf-text-chunk.h>

#define INF_TEXT_UNDO_GROUPING_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_UNDO_GROUPING, InfTextUndoGroupingPrivate))

G_DEFINE_TYPE(InfTextUndoGrouping, inf_text_undo_grouping, INF_ADOPTED_TYPE_UNDO_GROUPING)

/* Returns the gunichar of the first character of a InfTextChunk */
static gunichar
inf_text_undo_grouping_get_char_from_chunk(InfTextChunk* chunk)
{
  GIConv cd;
  InfTextChunkIter iter;
  gchar* inbuf;
  size_t inlen;
  gchar* outbuf;
  size_t outlen;
  size_t result;
  gchar buffer[6];

  cd = g_iconv_open("UTF-8", inf_text_chunk_get_encoding(chunk));
  g_assert(cd != (GIConv)-1);

  inf_text_chunk_iter_init_begin(chunk, &iter);
  /* cast const away without warning */ /* more or less */
  *(gconstpointer*) &inbuf = inf_text_chunk_iter_get_text(&iter);
  inlen = inf_text_chunk_iter_get_bytes(&iter);
  outbuf = buffer;
  outlen = 6; /* max length of a UTF-8 character */

  result = g_iconv(cd, &inbuf, &inlen, &outbuf, &outlen);
  /* we expect exactly one char in chunk, so there should be enough space */
  g_assert(result == 0);/* || (result == (size_t)(-1) && errno == E2BIG));*/

  g_iconv_close(cd);
  return g_utf8_get_char(buffer);
}

static guint
inf_text_undo_grouping_get_translated_position(InfAdoptedAlgorithm* algorithm,
                                               InfAdoptedRequest* from,
                                               InfAdoptedRequest* to,
                                               guint pos)
{
  guint move_id;
  InfAdoptedOperation* move_op;
  InfAdoptedStateVector* move_vec;
  InfAdoptedRequest* move_req;

  InfAdoptedRequest* moved_req;
  InfAdoptedOperation* moved_op;
  guint move_pos;

  move_id = inf_adopted_request_get_user_id(from);

  move_op = INF_ADOPTED_OPERATION(inf_text_move_operation_new(pos, 0));
  move_vec =
    inf_adopted_state_vector_copy(inf_adopted_request_get_vector(from));
  inf_adopted_state_vector_set(
    move_vec,
    move_id,
    inf_adopted_request_get_index(to)
  );
  move_req = inf_adopted_request_new_do(
    move_vec,
    move_id,
    move_op,
    inf_adopted_request_get_receive_time(from)
  );

  inf_adopted_state_vector_free(move_vec);
  g_object_unref(move_op);

  /* This should always succeed because of the vdiff check in
   * inf_text_undo_grouping_group_requests(). */
  moved_req = inf_adopted_algorithm_translate_request(
    algorithm,
    move_req,
    inf_adopted_request_get_vector(to)
  );

  g_object_unref(move_req);
  moved_op = inf_adopted_request_get_operation(moved_req);
  move_pos =
    inf_text_move_operation_get_position(INF_TEXT_MOVE_OPERATION(moved_op));

  g_object_unref(moved_req);
  return move_pos;
}

static gboolean
inf_text_undo_grouping_group_requests(InfAdoptedUndoGrouping* grouping,
                                      InfAdoptedRequest* first,
                                      InfAdoptedRequest* second)
{
  InfAdoptedOperation* first_op;
  InfAdoptedOperation* second_op;
  InfAdoptedStateVector* first_vector;
  InfAdoptedStateVector* second_vector;
  InfAdoptedAlgorithm* algorithm;
  guint max_total_log_size;
  guint vdiff;
  guint first_length;
  guint second_length;
  guint first_pos;
  guint second_pos;
  gunichar first_char;
  gunichar second_char;

  g_assert(inf_adopted_request_get_request_type(first) ==
           INF_ADOPTED_REQUEST_DO);
  g_assert(inf_adopted_request_get_request_type(second) ==
           INF_ADOPTED_REQUEST_DO);

  /* We want to avoid doing excessive transformations so that if someone
   * types a character, waits a long time with others writing a lot in the
   * meantime, and then writes another character. This also avoids the
   * problem  that we might not have all the requests from other users
   * around anymore to do the transformation. */
  first_vector = inf_adopted_request_get_vector(first);
  second_vector = inf_adopted_request_get_vector(second);
  vdiff = inf_adopted_state_vector_vdiff(first_vector, second_vector);
  algorithm = inf_adopted_undo_grouping_get_algorithm(grouping);
  g_object_get(
    G_OBJECT(algorithm),
    "max-total-log-size", &max_total_log_size,
    NULL
  );

  if(vdiff > max_total_log_size)
    return FALSE;

  first_op = inf_adopted_request_get_operation(first);
  second_op = inf_adopted_request_get_operation(second);

  g_assert(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(first_op) ||
           INF_TEXT_IS_DEFAULT_DELETE_OPERATION(first_op));
  g_assert(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(second_op) ||
           INF_TEXT_IS_DEFAULT_DELETE_OPERATION(second_op));

  /* Never group insert and delete operations */
  if(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(first_op) &&
     INF_TEXT_IS_DEFAULT_DELETE_OPERATION(second_op))
  {
    return FALSE;
  }
  else if(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(first_op) &&
          INF_TEXT_IS_DEFAULT_INSERT_OPERATION(second_op))
  {
    return FALSE;
  }
  else if(INF_TEXT_IS_INSERT_OPERATION(first_op))
  {
    first_length = inf_text_insert_operation_get_length(
      INF_TEXT_INSERT_OPERATION(first_op)
    );
    second_length = inf_text_insert_operation_get_length(
      INF_TEXT_INSERT_OPERATION(second_op)
    );

    if(first_length > 1 || second_length > 1)
    {
      return FALSE;
    }
    else
    {
      first_pos = inf_text_insert_operation_get_position(
        INF_TEXT_INSERT_OPERATION(first_op)
      );

      first_pos = inf_text_undo_grouping_get_translated_position(
        inf_adopted_undo_grouping_get_algorithm(grouping),
        first,
        second,
        first_pos + 1
      );

      second_pos = inf_text_insert_operation_get_position(
        INF_TEXT_INSERT_OPERATION(second_op)
      );

      if(first_pos != second_pos)
        return FALSE;

      /* start new group when going from whitespace to non-whitespace */
      first_char = inf_text_undo_grouping_get_char_from_chunk(
        inf_text_default_insert_operation_get_chunk(
          INF_TEXT_DEFAULT_INSERT_OPERATION(first_op)
        )
      );
      second_char = inf_text_undo_grouping_get_char_from_chunk(
        inf_text_default_insert_operation_get_chunk(
          INF_TEXT_DEFAULT_INSERT_OPERATION(second_op)
        )
      );

      if(g_unichar_isspace(first_char) && !g_unichar_isspace(second_char))
        return FALSE;

      return TRUE;
    }
  }
  else if(INF_TEXT_IS_DELETE_OPERATION(first_op))
  {
    first_length = inf_text_delete_operation_get_length(
      INF_TEXT_DELETE_OPERATION(first_op)
    );
    second_length = inf_text_delete_operation_get_length(
      INF_TEXT_DELETE_OPERATION(second_op)
    );

    if(first_length > 1 || second_length > 1)
    {
      return FALSE;
    }
    else
    {
      first_pos = inf_text_delete_operation_get_position(
        INF_TEXT_DELETE_OPERATION(first_op)
      );

      first_pos = inf_text_undo_grouping_get_translated_position(
        inf_adopted_undo_grouping_get_algorithm(grouping),
        first,
        second,
        first_pos
      );

      second_pos = inf_text_delete_operation_get_position(
        INF_TEXT_DELETE_OPERATION(second_op)
      );

      if(first_pos != second_pos && first_pos != second_pos + 1)
        return FALSE;

      /* start new group when going from whitespace to non-whitespace */
      first_char = inf_text_undo_grouping_get_char_from_chunk(
        inf_text_default_delete_operation_get_chunk(
          INF_TEXT_DEFAULT_DELETE_OPERATION(first_op)
        )
      );
      second_char = inf_text_undo_grouping_get_char_from_chunk(
        inf_text_default_delete_operation_get_chunk(
          INF_TEXT_DEFAULT_DELETE_OPERATION(second_op)
        )
      );

      if(g_unichar_isspace(first_char) && !g_unichar_isspace(second_char))
        return FALSE;

      return TRUE;
    }
  }
  else
  {
    g_assert_not_reached();
    return FALSE;
  }
}

static void
inf_text_undo_grouping_init(InfTextUndoGrouping* grouping)
{
}

static void
inf_text_undo_grouping_class_init(
  InfTextUndoGroupingClass* text_undo_grouping_class)
{
  InfAdoptedUndoGroupingClass* undo_grouping_class;

  undo_grouping_class =
    INF_ADOPTED_UNDO_GROUPING_CLASS(text_undo_grouping_class);

  undo_grouping_class->group_requests = inf_text_undo_grouping_group_requests;
}

/*
 * Public API.
 */

/**
 * inf_text_undo_grouping_new: (constructor)
 *
 * Creates a new #InfTextUndoGrouping.
 *
 * Returns: (transfer full): A new #InfTextUndoGrouping. To be freed with
 * g_object_unref().
 **/
InfTextUndoGrouping*
inf_text_undo_grouping_new(void)
{
  GObject* object;
  object = g_object_new(INF_TEXT_TYPE_UNDO_GROUPING, NULL);
  return INF_TEXT_UNDO_GROUPING(object);
}

/* vim:set et sw=2 ts=2: */
