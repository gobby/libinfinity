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

#include "util/inf-test-util.h"

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-default-insert-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-user.h>
#include <libinfinity/common/inf-user-table.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-xml-util.h>

#include <string.h>

#define NUM_PERMUTATIONS 100

typedef struct {
  GRand* rand;
  guint total;
  guint passed;
  gdouble time;
} test_result;

static gboolean
perform_single_test(InfTextChunk* initial,
                    InfTextChunk* final,
                    GSList* users,
                    GSList* requests,
                    gdouble* time)
{
  InfTextBuffer* buffer;
  InfCommunicationManager* manager;
  InfIo* io;
  InfTextSession* session;

  InfUserTable* user_table;
  InfTextUser* user;
  gchar* user_name;

  GSList* item;
  xmlNodePtr request;

  gboolean result;
  InfTextChunk* test_chunk;
  gchar* first;
  gchar* second;
  gsize first_bytes;
  gsize second_bytes;

  GTimer* timer;

  buffer = INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8"));
  inf_text_buffer_insert_chunk(buffer, 0, initial, NULL);

  manager = inf_communication_manager_new();
  io = INF_IO(inf_standalone_io_new());
  user_table = inf_user_table_new();

  for(item = users; item != NULL; item = g_slist_next(item))
  {
    user_name = g_strdup_printf("User_%u", GPOINTER_TO_UINT(item->data));

    user = INF_TEXT_USER(
      g_object_new(
        INF_TEXT_TYPE_USER,
        "id", GPOINTER_TO_UINT(item->data),
        "name", user_name,
        "status", INF_USER_ACTIVE,
        "flags", 0,
        NULL
      )
    );

    g_free(user_name);
    inf_user_table_add_user(user_table, INF_USER(user));
    g_object_unref(user);
  }

  session = inf_text_session_new_with_user_table(
    manager,
    buffer,
    io,
    user_table,
    NULL,
    NULL
  );

  g_object_unref(G_OBJECT(io));
  g_object_unref(G_OBJECT(manager));
  g_object_unref(G_OBJECT(user_table));

  timer = g_timer_new();
  for(item = requests; item != NULL; item = item->next)
  {
    request = (xmlNodePtr)item->data;

    /* TODO: Check error? */
    inf_communication_object_received(
      INF_COMMUNICATION_OBJECT(session),
      NULL,
      request,
      NULL
    );
  }

  *time = g_timer_elapsed(timer, NULL);
  g_timer_destroy(timer);

  test_chunk = inf_text_buffer_get_slice(
    buffer,
    0,
    inf_text_buffer_get_length(buffer)
  );

  g_object_unref(G_OBJECT(session));

  result = inf_text_chunk_equal(test_chunk, final);

  if(result == FALSE)
  {
    first = inf_text_chunk_get_text(final, &first_bytes);
    second = inf_text_chunk_get_text(test_chunk, &second_bytes);
    printf("(%.*s vs. %.*s) ", (int)second_bytes, second, (int)first_bytes, first);
    g_free(second);
    g_free(first);
  }

  inf_text_chunk_free(test_chunk);
  g_object_unref(G_OBJECT(buffer));
  return result;
}

static gboolean
perform_test(InfTextChunk* initial,
             InfTextChunk* final,
             GSList* users,
             GSList* requests,
             GRand* rand,
             gdouble* time)
{
  GSList* permutation;
  GSList* item;
  GSList* item2;
  GSList* dist_item;
  unsigned int i;
  unsigned int dist;
  unsigned int rval;
  gpointer temp;
  gboolean retval;
  gdouble local_time;

  guint user;
  guint user2;
  InfAdoptedStateVector* v;
  GError* error;

  g_assert(requests != NULL);
  permutation = g_slist_copy(requests);

  /* Mark per-user request order which must be kept when applying for the 
   * state vector diffs to work. */
  v = inf_adopted_state_vector_new();
  error = NULL;

  for(item = permutation; item != NULL; item = g_slist_next(item))
  {
    if(!inf_xml_util_get_attribute_uint(item->data, "user", &user, &error))
    {
      printf("%s\n", error->message);
      inf_adopted_state_vector_free(v);
      g_slist_free(permutation);
      return FALSE;
    }

    ((xmlNodePtr)item->data)->_private = GUINT_TO_POINTER(
      inf_adopted_state_vector_get(v, user)
    );

    inf_adopted_state_vector_add(v, user, 1);
  }

  inf_adopted_state_vector_free(v);

  *time = 0.0;
  for(i = 0; i < NUM_PERMUTATIONS; ++ i)
  {
    dist = 0;

    /* TODO: This can be optimized */
    /* shuffle random */
    for(item = permutation->next; item != NULL; item = g_slist_next(item))
    {
      ++ dist;
      dist_item = g_slist_nth(permutation, g_rand_int(rand) % (dist + 1));
      rval = g_rand_int(rand) % (dist + 1);

      temp = item->data;
      item->data = dist_item->data;
      dist_item->data = temp;
    }

    /* Resort according to per-user request order */
    /* We could probably use g_slist_sort if it was stable, but the
     * documentation says nothing about it. So just do a primitive bubblesort
     * for now. */
    for(item = permutation; item != NULL; item = g_slist_next(item))
    {
      for(item2 = item->next; item2 != NULL; item2 = g_slist_next(item2))
      {
        /* This can't fail anymore, otherwise it
         * would already have failed above. */
        inf_xml_util_get_attribute_uint(item->data, "user", &user, NULL);
        inf_xml_util_get_attribute_uint(item2->data, "user", &user2, NULL);
        if(user == user2)
        {
          if(GPOINTER_TO_UINT( ((xmlNodePtr)item->data)->_private) >
             GPOINTER_TO_UINT( ((xmlNodePtr)item2->data)->_private) )
          {
            temp = item->data;
            item->data = item2->data;
            item2->data = temp;
          }
        }
      }
    }

    if(i % (MAX(NUM_PERMUTATIONS/30, 1)) == 0)
    {
      printf(".");
      fflush(stdout);
    }

    retval = perform_single_test(
      initial,
      final,
      users,
      permutation,
      &local_time
    );

    if(!retval) break;

    *time += local_time;
  }

  g_slist_free(permutation);
  return retval;
}

static void
foreach_test_func(const gchar* testfile,
                  gpointer user_data)
{
  test_result* result;
  xmlDocPtr doc;
  xmlNodePtr root;
  xmlNodePtr child;

  GSList* requests;
  InfTextChunk* initial;
  InfTextChunk* final;
  GSList* users;
  GError* error;
  gboolean retval;

  gdouble local_time;

  /* Only process XML files, not the Makefiles or other stuff */
  if(!g_str_has_suffix(testfile, ".xml"))
    return;

  result = (test_result*)user_data;
  doc = xmlParseFile(testfile);

  requests = NULL;
  initial = NULL;
  final = NULL;
  error = NULL;
  users = NULL;

  printf("%s... ", testfile);
  fflush(stdout);

  ++ result->total;

  if(doc != NULL)
  {
    root = xmlDocGetRootElement(doc);
    for(child = root->children; child != NULL; child = child->next)
    {
      if(child->type != XML_ELEMENT_NODE) continue;

      if(strcmp((const char*)child->name, "initial-buffer") == 0)
      {
        if(initial != NULL) inf_text_chunk_free(initial);
        initial = inf_test_util_parse_buffer(child, &error);
        if(initial == NULL) break;
      }
      else if(strcmp((const char*)child->name, "final-buffer") == 0)
      {
        if(final != NULL) inf_text_chunk_free(final);
        final = inf_test_util_parse_buffer(child, &error);
        if(final == NULL) break;
      }
      else if(strcmp((const char*)child->name, "user") == 0)
      {
        if(inf_test_util_parse_user(child, &users, &error) == FALSE)
          break;
      }
      else if(strcmp((const char*)child->name, "request") == 0)
      {
        requests = g_slist_prepend(requests, child);
      }
      else
      {
        g_set_error(
          &error,
          inf_test_util_parse_error_quark(),
          INF_TEST_UTIL_PARSE_ERROR_UNEXPECTED_NODE,
          "Node '%s' unexpected",
          (const gchar*)child->name
        );

        break;
      }
    }

    if(error != NULL)
    {
      printf("Failed to parse: %s\n", error->message);
      g_error_free(error);
      xmlFreeDoc(doc);

      g_slist_free(requests);
      if(initial != NULL) inf_text_chunk_free(initial);
      if(final != NULL) inf_text_chunk_free(final);
      g_slist_free(users);
    }
    else
    {
      g_assert(initial != NULL);
      g_assert(final != NULL);

      requests = g_slist_reverse(requests);

      retval = perform_test(
        initial,
        final,
        users,
        requests,
        result->rand,
        &local_time
      );
      
      if(retval == TRUE)
      {
        ++ result->passed;
        printf("OK (%g secs)\n", local_time);
        result->time += local_time;
      }
      else
      {
        printf("FAILED\n");
      }

      xmlFreeDoc(doc);
      g_slist_free(requests);
      inf_text_chunk_free(initial);
      inf_text_chunk_free(final);

      g_slist_free(users);
    }
  }
}

int main(int argc, char* argv[])
{
  const char* dir;
  GError* error;
  test_result result;
  unsigned int rseed;
  int dirarg;
  gboolean retval;
  GTimer* timer;
  gdouble elapsed;

  dirarg = 1;
  if(argc > 1)
  {
    rseed = atoi(argv[1]);
    if(rseed > 0) dirarg = 2;
    else rseed = time(NULL);
  }
  else
  {
    rseed = time(NULL);
  }

  printf("Using random seed %u\n", rseed);

  g_type_init();

  if(argc > dirarg)
    dir = argv[dirarg];
  else
    dir = "session";

  result.rand = g_rand_new_with_seed(rseed);
  result.total = 0;
  result.passed = 0;
  result.time = 0.0;

  error = NULL;
  timer = g_timer_new();
  retval = inf_test_util_dir_foreach(
    dir,
    foreach_test_func,
    &result,
    &error
  );

  g_timer_stop(timer);
  elapsed = g_timer_elapsed(timer, NULL);
  g_rand_free(result.rand);
  g_timer_destroy(timer);

  if(retval == FALSE)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  printf(
    "%u out of %u tests passed (real %g secs, algo %g secs)\n",
    result.passed, result.total, elapsed, result.time
  );

  if(result.passed < result.total)
    return -1;

  return 0;
}

/* vim:set et sw=2 ts=2: */
