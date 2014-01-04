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

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinfinity/adopted/inf-adopted-session-replay.h>
#include <libinfinity/common/inf-init.h>

#include <string.h>

static void
inf_test_util_print_buffer(InfTextBuffer* buffer)
{
  InfTextChunk* chunk;
  gchar* text;
  gsize bytes;

  chunk = inf_text_buffer_get_slice(
    buffer,
    0,
    inf_text_buffer_get_length(buffer)
  );

  text = inf_text_chunk_get_text(chunk, &bytes);
  inf_text_chunk_free(chunk);

  printf("%.*s\n", (int)bytes, text);
  g_free(text);
}

static InfSession*
inf_test_text_recover_session_new(InfIo* io,
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

static const InfcNotePlugin INF_TEST_TEXT_RECOVER_TEXT_PLUGIN = {
  NULL, "InfText", inf_test_text_recover_session_new
};

static void
inf_test_text_recover_apply_request_cb_before(InfAdoptedAlgorithm* algorithm,
                                              InfAdoptedUser* user,
                                              InfAdoptedRequest* request,
                                              InfAdoptedRequest* orig_request,
                                              gpointer user_data)
{
  InfAdoptedOperation* operation;
  InfTextBuffer* buffer;
  guint len;

  g_assert(
    inf_adopted_request_get_request_type(request) == INF_ADOPTED_REQUEST_DO
  );

  operation = inf_adopted_request_get_operation(request);

  if(INF_TEXT_IS_DELETE_OPERATION(operation))
  {
    /* If the document has substantial content and this deletes most of it,
     * then print out the document here. */
    g_object_get(G_OBJECT(algorithm), "buffer", &buffer, NULL);
    if(inf_text_buffer_get_length(buffer) >= 50)
    {
      len = inf_text_delete_operation_get_length(
        INF_TEXT_DELETE_OPERATION(operation)
      );

      if(len >= inf_text_buffer_get_length(INF_TEXT_BUFFER(buffer))*75/100)
      {
        if(*(int*)user_data == 0)
        {
          inf_test_util_print_buffer(INF_TEXT_BUFFER(buffer));
        }

        --*(int*)user_data;
      }
    }

    g_object_unref(buffer);
  }
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

  InfBuffer* buffer;
  InfAdoptedAlgorithm* algo;
  GSList* item;
  gint counter;

  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <record-file> [index]\n", argv[0]);
    return -1;
  }

  counter = 0;
  if(argc > 2) counter = atoi(argv[2]);

  error = NULL;
  if(!inf_init(&error))
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  ret = 0;
  for(i = 1; i < 2; ++ i)
  {
    replay = inf_adopted_session_replay_new();
    inf_adopted_session_replay_set_record(
      replay,
      argv[i],
      &INF_TEST_TEXT_RECOVER_TEXT_PLUGIN,
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
      algo = inf_adopted_session_get_algorithm(session);

      g_signal_connect(
        algo,
        "apply-request",
        G_CALLBACK(inf_test_text_recover_apply_request_cb_before),
        &counter
      );

      if(!inf_adopted_session_replay_play_to_end(replay, &error))
      {
        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
        error = NULL;

        ret = -1;
      }
      else if(counter == 0)
      {
        inf_test_util_print_buffer(INF_TEXT_BUFFER(buffer));
      }
    }

    g_object_unref(replay);
  }

  return ret;
}

/* vim:set et sw=2 ts=2: */
