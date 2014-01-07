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

#include "util/inf-test-util.h"

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-undo-grouping.h>
#include <libinftext/inf-text-default-insert-operation.h>
#include <libinftext/inf-text-default-delete-operation.h>
#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-move-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinfinity/adopted/inf-adopted-session-replay.h>
#include <libinfinity/adopted/inf-adopted-no-operation.h>
#include <libinfinity/common/inf-init.h>

#include <string.h>

typedef struct _InfTestTextReplayUndoGroupingInfo
  InfTestTextReplayUndoGroupingInfo;
struct _InfTestTextReplayUndoGroupingInfo {
  InfAdoptedAlgorithm* algorithm;
  GSList* undo_groupings;
};

static InfSession*
inf_test_text_replay_session_new(InfIo* io,
                                 InfCommunicationManager* manager,
                                 InfSessionStatus status,
                                 InfCommunicationJoinedGroup* sync_group,
                                 InfXmlConnection* sync_connection,
                                 gpointer user_data)
{
  InfTextDefaultBuffer* buffer;
  InfTextSession* session;

  buffer = inf_text_default_buffer_new("UTF-8");
  session = inf_text_session_new(
    manager,
    INF_TEXT_BUFFER(buffer),
    io,
    status,
    INF_COMMUNICATION_GROUP(sync_group),
    sync_connection
  );
  g_object_unref(buffer);

  return INF_SESSION(session);
}

static const InfcNotePlugin INF_TEST_TEXT_REPLAY_TEXT_PLUGIN = {
  NULL, "InfText", inf_test_text_replay_session_new
};

/*
 * String check
 */

/* These functions assume that buffer and chunks contain UTF-8 */

static GString*
inf_test_text_replay_load_buffer(InfTextBuffer* buffer)
{
  InfTextBufferIter* iter;
  GString* result;
  gchar* text;
  gsize bytes;

  result = g_string_sized_new(inf_text_buffer_get_length(buffer));

  g_assert(strcmp(inf_text_buffer_get_encoding(buffer), "UTF-8") == 0);

  iter = inf_text_buffer_create_begin_iter(buffer);
  if(iter != NULL)
  {
    do
    {
      text = inf_text_buffer_iter_get_text(buffer, iter);
      bytes = inf_text_buffer_iter_get_bytes(buffer, iter);
      g_string_append_len(result, text, bytes);
      g_free(text);
    } while(inf_text_buffer_iter_next(buffer, iter));

    inf_text_buffer_destroy_iter(buffer, iter);
  }

  return result;
}

static void
inf_test_text_replay_text_inserted_cb(InfTextBuffer* buffer,
                                      guint pos,
                                      InfTextChunk* chunk,
                                      InfUser* user,
                                      gpointer user_data)
{
  GString* own_content;
  InfTextChunkIter iter;
  gsize bpos;
  GString* buffer_content;

  own_content = (GString*)user_data;

  /* apply operation to string */
  if(inf_text_chunk_iter_init_begin(chunk, &iter))
  {
    /* Convert from pos to byte */
    bpos = g_utf8_offset_to_pointer(own_content->str, pos) - own_content->str;

    do
    {
      g_string_insert_len(
        own_content,
        bpos,
        inf_text_chunk_iter_get_text(&iter),
        inf_text_chunk_iter_get_bytes(&iter)
      );

      bpos += inf_text_chunk_iter_get_bytes(&iter);
    } while(inf_text_chunk_iter_next(&iter));
  }

  /* Compare with buffer content */
  buffer_content = inf_test_text_replay_load_buffer(buffer);

  g_assert(strcmp(buffer_content->str, own_content->str) == 0);
  g_string_free(buffer_content, TRUE);
}

static void
inf_test_text_replay_text_erased_cb(InfTextBuffer* buffer,
                                    guint pos,
                                    InfTextChunk* chunk,
                                    InfUser* user,
                                    gpointer user_data)
{
  GString* own_content;
  guint beg;
  guint end;
  gsize bbeg;
  gsize bend;
  GString* buffer_content;

  own_content = (GString*)user_data;

  /* apply operation to string */
  beg = pos;
  end = beg + inf_text_chunk_get_length(chunk);
  bbeg = g_utf8_offset_to_pointer(own_content->str, pos) - own_content->str;
  bend = g_utf8_offset_to_pointer(own_content->str, end) - own_content->str;

  g_string_erase(own_content, bbeg, bend - bbeg);

  /* Compare with buffer content */
  buffer_content = inf_test_text_replay_load_buffer(buffer);

  g_assert(strcmp(buffer_content->str, own_content->str) == 0);
  g_string_free(buffer_content, TRUE);
}

static const gchar*
inf_test_text_replay_request_typestring(InfAdoptedRequest* request)
{
  switch(inf_adopted_request_get_request_type(request))
  {
  case INF_ADOPTED_REQUEST_DO: return "DO";
  case INF_ADOPTED_REQUEST_UNDO: return "UNDO";
  case INF_ADOPTED_REQUEST_REDO: return "REDO";
  default: g_assert_not_reached(); return NULL;
  }
}

static gint64 test;

static void
inf_test_text_replay_begin_execute_request_cb(InfAdoptedAlgorithm* algorithm,
                                              InfAdoptedUser* user,
                                              InfAdoptedRequest* request,
                                              gpointer user_data)
{
  gchar* current_str;
  gchar* request_str;
  guint vdiff;

  vdiff = inf_adopted_state_vector_vdiff(
    inf_adopted_request_get_vector(request),
    inf_adopted_algorithm_get_current(algorithm)
  );

  if(vdiff > 10)
  {
    current_str = inf_adopted_state_vector_to_string(
      inf_adopted_algorithm_get_current(algorithm)
    );

    request_str = inf_adopted_state_vector_to_string(
      inf_adopted_request_get_vector(request)
    );

    fprintf(
      stderr,
      "WARNING: Transforming %s request \"%s\" of user \"%s\" to state \"%s\""
      ", vdiff=%u\n",
      inf_test_text_replay_request_typestring(request),
      request_str,
      inf_user_get_name(INF_USER(user)),
      current_str,
      vdiff
    );

    g_free(current_str);
    g_free(request_str);
  }

  test = g_get_monotonic_time();
}

static void
inf_test_text_replay_end_execute_request_cb(InfAdoptedAlgorithm* algorithm,
                                            InfAdoptedUser* user,
                                            InfAdoptedRequest* request,
                                            InfAdoptedRequest* translated,
                                            const GError* error,
                                            gpointer user_data)
{
  InfAdoptedStateVector* current;
  gchar* current_str;
  gchar* request_str;
  gint64 time;

  time = g_get_monotonic_time();

  if(error == NULL)
  {
    if(time - test > 10000.)
    {
      current_str = inf_adopted_state_vector_to_string(
        inf_adopted_request_get_vector(translated)
      );

      request_str = inf_adopted_state_vector_to_string(
        inf_adopted_request_get_vector(request)
      );

      fprintf(
        stderr,
        "WARNING: Transforming %s request \"%s\" of user \"%s\" to state "
        "\"%s\" took %.3g ms\n",
        inf_test_text_replay_request_typestring(request),
        request_str,
        inf_user_get_name(INF_USER(user)),
        current_str,
        (time - test)/1000.
      );

      g_free(current_str);
      g_free(request_str);
    }
  }
  else
  {
    g_assert(error != NULL);

    current_str = inf_adopted_state_vector_to_string(
      inf_adopted_algorithm_get_current(algorithm)
    );

    request_str = inf_adopted_state_vector_to_string(
      inf_adopted_request_get_vector(request)
    );

    fprintf(
      stderr,
      "WARNING: Transforming %s request \"%s\" of user \"%s\" to "
      "state \"%s\" failed: %s\n",
      inf_test_text_replay_request_typestring(request),
      request_str,
      inf_user_get_name(INF_USER(user)),
      current_str,
      error->message
    );

    g_free(current_str);
    g_free(request_str);
  }
}

/*
 * Undo grouping
 */

static void
inf_test_text_replay_add_undo_grouping(InfTestTextReplayUndoGroupingInfo* fo,
                                       InfAdoptedUser* user)
{
  InfTextUndoGrouping* grouping;
  grouping = inf_text_undo_grouping_new();

  inf_adopted_undo_grouping_set_algorithm(INF_ADOPTED_UNDO_GROUPING(grouping),
                                          fo->algorithm,
                                          user);
  fo->undo_groupings = g_slist_prepend(fo->undo_groupings, grouping);
}

static void
inf_test_text_replay_add_user_cb(InfUserTable* user_table,
                                 InfUser* user,
                                 gpointer user_data)
{
  inf_test_text_replay_add_undo_grouping(user_data, INF_ADOPTED_USER(user));
}

static void
inf_test_text_replay_play_user_table_foreach_func(InfUser* user,
                                                  gpointer user_data)
{
  inf_test_text_replay_add_undo_grouping(user_data, INF_ADOPTED_USER(user));
}

/*
 * Entry point
 */

int main(int argc, char* argv[])
{
  InfAdoptedSessionReplay* replay;
  InfAdoptedSession* session;
  GError* error;
  int i;
  int ret;

  GString* content;
  InfBuffer* buffer;
  InfUserTable* user_table;
  InfTestTextReplayUndoGroupingInfo data;
  GSList* item;

  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <record-file1> <record-file2> ...\n", argv[0]);
    return -1;
  }

  error = NULL;
  if(!inf_init(&error))
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  ret = 0;
  for(i = 1; i < argc; ++ i)
  {
    fprintf(stderr, "%s... ", argv[i]);
    fflush(stderr);

    replay = inf_adopted_session_replay_new();
    inf_adopted_session_replay_set_record(
      replay,
      argv[i],
      &INF_TEST_TEXT_REPLAY_TEXT_PLUGIN,
      &error
    );

    if(error != NULL)
    {
      fprintf(stderr, "%s\n", error->message);
      g_error_free(error);
      error = NULL;

      ret = -1;
    }
    else
    {
      session = inf_adopted_session_replay_get_session(replay);
      buffer = inf_session_get_buffer(INF_SESSION(session));
      content = inf_test_text_replay_load_buffer(INF_TEXT_BUFFER(buffer));
      user_table = inf_session_get_user_table(INF_SESSION(session));
      data.algorithm = inf_adopted_session_get_algorithm(session);
      data.undo_groupings = NULL;

      g_signal_connect(
        inf_session_get_buffer(INF_SESSION(session)),
        "text-inserted",
        G_CALLBACK(inf_test_text_replay_text_inserted_cb),
        content
      );

      g_signal_connect(
        inf_session_get_buffer(INF_SESSION(session)),
        "text-erased",
        G_CALLBACK(inf_test_text_replay_text_erased_cb),
        content
      );

      g_signal_connect(
        data.algorithm,
        "begin-execute-request",
        G_CALLBACK(inf_test_text_replay_begin_execute_request_cb),
        content
      );

      g_signal_connect(
        data.algorithm,
        "end-execute-request",
        G_CALLBACK(inf_test_text_replay_end_execute_request_cb),
        content
      );

      /* Let an undo grouper group stuff, just as a consistency check
       * that it does not crash or behave otherwise badly. */
      inf_user_table_foreach_user(
        user_table,
        inf_test_text_replay_play_user_table_foreach_func,
        &data
      );

      g_signal_connect_after(
        user_table,
        "add-user",
        G_CALLBACK(inf_test_text_replay_add_user_cb),
        &data
      );

      if(!inf_adopted_session_replay_play_to_end(replay, &error))
      {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        error = NULL;

        ret = -1;
      }
      else
      {
        fprintf(stderr, "\n");
        /*inf_test_util_print_buffer(INF_TEXT_BUFFER(buffer));*/
      }

      g_string_free(content, TRUE);
      for(item = data.undo_groupings; item != NULL; item = item->next)
        g_object_unref(item->data);
      g_slist_free(data.undo_groupings);
    }

    g_object_unref(replay);
  }

  return ret;
}

/* vim:set et sw=2 ts=2: */
