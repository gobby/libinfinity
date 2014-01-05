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
inf_test_text_recover_text_erased_cb(InfTextBuffer* buffer,
                                     guint pos,
                                     InfTextChunk* chunk,
                                     InfUser* user,
                                     gpointer user_data)
{
  InfAdoptedOperation* operation;
  guint len;

  InfTextChunk* print_chunk;
  gsize print_bytes;
  gpointer print_text;

  /* If the document has substantial content and this deletes most of it,
   * then print out the document here. */
  len = inf_text_chunk_get_length(chunk);
  if(inf_text_buffer_get_length(buffer) + len >= 50)
  {
    if(len >= (inf_text_buffer_get_length(buffer) + len)*75/100)
    {
      if(*(int*)user_data == 0)
      {
        print_chunk = inf_text_buffer_get_slice(
          buffer,
          0,
          inf_text_buffer_get_length(buffer)
        );

        inf_text_chunk_insert_chunk(print_chunk, pos, chunk);
        print_text = inf_text_chunk_get_text(print_chunk, &print_bytes);
        inf_text_chunk_free(print_chunk);

        printf("%.*s\n", (int)print_bytes, (gchar*)print_text);
        g_free(print_text);
      }

      --*(int*)user_data;
    }
  }

  g_object_unref(buffer);
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

      g_signal_connect(
        buffer,
        "text-erased",
        G_CALLBACK(inf_test_text_recover_text_erased_cb),
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
