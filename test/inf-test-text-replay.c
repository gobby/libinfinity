/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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

  iter = inf_text_buffer_create_iter(buffer);
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
inf_test_text_replay_apply_operation_to_string(GString* string,
                                               InfAdoptedOperation* operation)
{
  InfTextChunk* chunk;
  InfTextChunkIter iter;
  guint position;
  guint length;
  InfAdoptedOperation* first;
  InfAdoptedOperation* second;
  InfAdoptedOperation* new_second;

  if(INF_TEXT_IS_INSERT_OPERATION(operation))
  {
    g_assert(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(operation));

    chunk = inf_text_default_insert_operation_get_chunk(
      INF_TEXT_DEFAULT_INSERT_OPERATION(operation)
    );

    position = inf_text_insert_operation_get_position(
      INF_TEXT_INSERT_OPERATION(operation)
    );

    g_assert(strcmp(inf_text_chunk_get_encoding(chunk), "UTF-8") == 0);

    if(inf_text_chunk_iter_init(chunk, &iter))
    {
      /* Convert from pos to byte */
      position = g_utf8_offset_to_pointer(string->str, position) - string->str;

      do
      {
        g_string_insert_len(
          string,
          position,
          inf_text_chunk_iter_get_text(&iter),
          inf_text_chunk_iter_get_bytes(&iter)
        );

        position += inf_text_chunk_iter_get_bytes(&iter);
      } while(inf_text_chunk_iter_next(&iter));
    }
  }
  else if(INF_TEXT_IS_DELETE_OPERATION(operation))
  {
    position = inf_text_delete_operation_get_position(
      INF_TEXT_DELETE_OPERATION(operation)
    );
    length = inf_text_delete_operation_get_length(
      INF_TEXT_DELETE_OPERATION(operation)
    );

    length = g_utf8_offset_to_pointer(string->str, position+length) -
      string->str;
    position = g_utf8_offset_to_pointer(string->str, position) - string->str;
    length -= position;

    g_string_erase(string, position, length);
  }
  else if(INF_ADOPTED_IS_SPLIT_OPERATION(operation))
  {
    g_object_get(
      G_OBJECT(operation),
      "first", &first,
      "second", &second,
      NULL
    );

    new_second = inf_adopted_operation_transform(
      second,
      first,
      INF_ADOPTED_CONCURRENCY_NONE
    );

    inf_test_text_replay_apply_operation_to_string(string, first);
    inf_test_text_replay_apply_operation_to_string(string, new_second);

    g_object_unref(first);
    g_object_unref(second);
    g_object_unref(new_second);
  }
  else if(INF_TEXT_IS_MOVE_OPERATION(operation) ||
          INF_ADOPTED_IS_NO_OPERATION(operation))
  {
    /* Nothing to do */
  }
  else
  {
    g_error(
      "Operation type \"%s\" not supported",
      g_type_name(G_TYPE_FROM_INSTANCE(operation))
    );

    g_assert_not_reached();
  }
}

static void
inf_test_text_replay_apply_request_cb_before(InfAdoptedAlgorithm* algorithm,
                                             InfAdoptedUser* user,
                                             InfAdoptedRequest* request,
                                             gpointer user_data)
{
  InfAdoptedOperation* operation;

  g_assert(
    inf_adopted_request_get_request_type(request) == INF_ADOPTED_REQUEST_DO
  );

  operation = inf_adopted_request_get_operation(request);
#if 0
  /* This can be used to set a breakpoint if the operation meats special
   * conditions when debugging a specific problem. */
  if(INF_TEXT_IS_INSERT_OPERATION(operation))
    if(inf_text_insert_operation_get_position(INF_TEXT_INSERT_OPERATION(operation)) == 1730)
      printf("tada\n");
#endif
}

static void
inf_test_text_replay_apply_request_cb_after(InfAdoptedAlgorithm* algorithm,
                                            InfAdoptedUser* user,
                                            InfAdoptedRequest* request,
                                            gpointer user_data)
{
  InfTextBuffer* buffer;
  InfAdoptedOperation* operation;
  GString* own_content;
  GString* buffer_content;

  g_object_get(G_OBJECT(algorithm), "buffer", &buffer, NULL);
  own_content = (GString*)user_data;

  g_assert(
    inf_adopted_request_get_request_type(request) == INF_ADOPTED_REQUEST_DO
  );

  operation = inf_adopted_request_get_operation(request);

  /* Apply operation to own string */
  inf_test_text_replay_apply_operation_to_string(own_content, operation);

  /* Compare with buffer content */
  buffer_content = inf_test_text_replay_load_buffer(buffer);
  g_object_unref(buffer);

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
inf_test_text_replay_execute_request_cb_before(InfAdoptedAlgorithm* algorithm,
                                               InfAdoptedUser* user,
                                               InfAdoptedRequest* request,
                                               gboolean apply,
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

    /* TODO: Write what type of request it is */
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
inf_test_text_replay_execute_request_cb_after(InfAdoptedAlgorithm* algorithm,
                                              InfAdoptedUser* user,
                                              InfAdoptedRequest* request,
                                              gboolean apply,
                                              gpointer user_data)
{
  InfAdoptedStateVector* current;
  gchar* current_str;
  gchar* request_str;
  gint64 time;

  time = g_get_monotonic_time();

  if(time - test > 10000)
  {
    current = inf_adopted_state_vector_copy(
      inf_adopted_algorithm_get_current(algorithm)
    );
    if(inf_adopted_request_affects_buffer(request))
    {
      inf_adopted_state_vector_add(
        current,
        inf_user_get_id(INF_USER(user)),
        -1
      );
    }
    current_str = inf_adopted_state_vector_to_string(current);
    inf_adopted_state_vector_free(current);

    request_str = inf_adopted_state_vector_to_string(
      inf_adopted_request_get_vector(request)
    );

    fprintf(
      stderr,
      "WARNING: Transforming %s request \"%s\" of user \"%s\" to state \"%s\" "
      "took %.3g ms\n",
      inf_test_text_replay_request_typestring(request),
      request_str,
      inf_user_get_name(INF_USER(user)),
      current_str,
      (time - test)/1000.0
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
        data.algorithm,
        "apply-request",
        G_CALLBACK(inf_test_text_replay_apply_request_cb_before),
        content
      );

      g_signal_connect_after(
        data.algorithm,
        "apply-request",
        G_CALLBACK(inf_test_text_replay_apply_request_cb_after),
        content
      );

      g_signal_connect(
        data.algorithm,
        "execute-request",
        G_CALLBACK(inf_test_text_replay_execute_request_cb_before),
        content
      );

      g_signal_connect_after(
        data.algorithm,
        "execute-request",
        G_CALLBACK(inf_test_text_replay_execute_request_cb_after),
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
