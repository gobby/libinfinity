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
  guint total;
  guint passed;
} test_result;

typedef enum {
  PARSE_ERROR_UNEXPECTED_NODE,
  PARSE_ERROR_USER_ALREADY_EXISTS
} ParseError;

static GQuark
parse_error_quark(void)
{
  return g_quark_from_static_string("PARSE_ERROR");
}

/* Useful for debugging in gdb */
static void
print_op(InfAdoptedOperation* op)
{
  InfAdoptedOperation* first;
  InfAdoptedOperation* second;
  gchar* chunk_text;
  gsize chunk_bytes;

  if(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(op))
  {
    chunk_text = inf_text_chunk_get_text(
      inf_text_default_insert_operation_get_chunk(
        INF_TEXT_DEFAULT_INSERT_OPERATION(op)
      ),
      &chunk_bytes
    );

    printf(
      "insert(%u, %.*s)\n",
      inf_text_insert_operation_get_position(INF_TEXT_INSERT_OPERATION(op)),
      (int)chunk_bytes, chunk_text
    );

    g_free(chunk_text);
  }
  else if(INF_TEXT_IS_DELETE_OPERATION(op))
  {
    printf(
      "delete(%u/%u)\n",
      inf_text_delete_operation_get_position(INF_TEXT_DELETE_OPERATION(op)),
      inf_text_delete_operation_get_length(INF_TEXT_DELETE_OPERATION(op))
    );
  }
  else if(INF_ADOPTED_IS_SPLIT_OPERATION(op))
  {
    g_object_get(G_OBJECT(op), "first", &first, "second", &second, NULL);

    printf("split(\n    ");
    print_op(first);
    printf("    ");
    print_op(second);
    printf(")\n");
  }
  else
  {
    g_assert_not_reached();
  }
}

/* Useful for debugging in gdb */
G_GNUC_UNUSED static void
print_req(InfAdoptedRequest* request)
{
  gchar* time;
  time = inf_adopted_state_vector_to_string(inf_adopted_request_get_vector(request));
  printf("%u [%s] ", inf_adopted_request_get_user_id(request), time);
  g_free(time);

  switch(inf_adopted_request_get_request_type(request))
  {
  case INF_ADOPTED_REQUEST_DO:
    printf("DO {\n  ");
    print_op(inf_adopted_request_get_operation(request));
    printf("}\n");
    break;
  case INF_ADOPTED_REQUEST_UNDO:
    printf("UNDO\n");
    break;
  case INF_ADOPTED_REQUEST_REDO:
    printf("REDO\n");
    break;
  }
}

/* Useful for debugging in gdb */
G_GNUC_UNUSED static void
print_buffer(InfTextBuffer* buffer)
{
  InfTextChunk* chunk;
  gchar* text;
  gsize bytes;

  chunk = inf_text_buffer_get_slice(buffer, 0, inf_text_buffer_get_length(buffer));
  text = inf_text_chunk_get_text(chunk, &bytes);
  inf_text_chunk_free(chunk);

  printf("%.*s\n", (int)bytes, text);
  g_free(text);
}

static gboolean
dir_foreach(const char* dirname,
            void(*callback)(const char*, gpointer),
            gpointer user_data,
            GError** error)
{
  GDir* dir;
  const gchar* entry;
  gchar* path;

  dir = g_dir_open(dirname, 0, error);
  if(dir == NULL) return FALSE;

  for(entry = g_dir_read_name(dir); entry != NULL; entry = g_dir_read_name(dir))
  {
    /* Ignore hidden files */
    if(entry[0] == '.') continue;

    path = g_build_filename(dirname, entry, NULL);
    if(g_file_test(path, G_FILE_TEST_IS_DIR))
    {
      if(dir_foreach(path, callback, user_data, error) == FALSE)
      {
        g_free(path);
        g_dir_close(dir);
        return FALSE;
      }
    }
    else if(g_file_test(path, G_FILE_TEST_IS_REGULAR))
    {
      callback(path, user_data);
    }
    g_free(path);
  }

  g_dir_close(dir);
  return TRUE;
}

static InfTextChunk*
parse_buffer(xmlNodePtr xml,
             GError** error)
{
  InfTextChunk* chunk;
  xmlNodePtr child;
  guint author;
  xmlChar* content;

  chunk = inf_text_chunk_new("UTF-8");

  for(child = xml->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;

    if(strcmp((const char*)child->name, "segment") == 0)
    {
      if(!inf_xml_util_get_attribute_uint(child, "author", &author, error))
      {
        inf_text_chunk_free(chunk);
        return NULL;
      }

      content = xmlNodeGetContent(child);

      inf_text_chunk_insert_text(
        chunk,
        inf_text_chunk_get_length(chunk),
        (const gchar*)content,
        g_utf8_strlen((const gchar*)content, -1),
        strlen((const char*)content),
        author
      );
    }
    else
    {
      g_set_error(
        error,
        parse_error_quark(),
        PARSE_ERROR_UNEXPECTED_NODE,
        "Node '%s' unexpected",
        (const gchar*)child->name
      );

      inf_text_chunk_free(chunk);
      return NULL;
    }
  }

  return chunk;
}

static gboolean
parse_user(xmlNodePtr xml,
           GSList** users,
           GError** error)
{
  guint id;

  if(!inf_xml_util_get_attribute_uint_required(xml, "id", &id, error))
    return FALSE;

  if(g_slist_find(*users, GUINT_TO_POINTER(id)) != NULL)
  {
    g_set_error(
      error,
      parse_error_quark(),
      PARSE_ERROR_USER_ALREADY_EXISTS,
      "User with ID %u exists already",
      id
    );

    return FALSE;
  }

  *users = g_slist_prepend(*users, GUINT_TO_POINTER(id));
  return TRUE;
}

static gboolean
perform_single_test(InfTextChunk* initial,
                    InfTextChunk* final,
                    GSList* users,
                    GSList* requests)
{
  InfTextBuffer* buffer;
  InfConnectionManager* manager;
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

  buffer = INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8"));
  inf_text_buffer_insert_chunk(buffer, 0, initial, NULL);

  manager = inf_connection_manager_new();
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
        "status", INF_USER_AVAILABLE,
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

  for(item = requests; item != NULL; item = item->next)
  {
    request = (xmlNodePtr)item->data;

    /* TODO: Check error? */
    inf_net_object_received(
      INF_NET_OBJECT(session),
      NULL,
      request,
      NULL
    );
  }

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
             GSList* requests)
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

  for(i = 0; i < NUM_PERMUTATIONS; ++ i)
  {
    dist = 0;

    /* TODO: This can be optimized */
    /* shuffle random */
    for(item = permutation->next; item != NULL; item = g_slist_next(item))
    {
      ++ dist;
      dist_item = g_slist_nth(permutation, rand() % (dist + 1));
      rval = rand() % (dist + 1);

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

    if(i % (MAX(NUM_PERMUTATIONS/40, 1)) == 0)
    {
      printf(".");
      fflush(stdout);
    }

    retval = perform_single_test(initial, final, users, permutation);
    if(!retval) break;
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
        initial = parse_buffer(child, &error);
        if(initial == NULL) break;
      }
      else if(strcmp((const char*)child->name, "final-buffer") == 0)
      {
        if(final != NULL) inf_text_chunk_free(final);
        final = parse_buffer(child, &error);
        if(final == NULL) break;
      }
      else if(strcmp((const char*)child->name, "user") == 0)
      {
        if(parse_user(child, &users, &error) == FALSE)
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
          parse_error_quark(),
          PARSE_ERROR_UNEXPECTED_NODE,
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
      if(perform_test(initial, final, users, requests) == TRUE)
      {
        ++ result->passed;
        printf("OK\n");
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

  dirarg = 1;
  if(argc > 1)
  {
    rseed = atoi(argv[1]);
    if(rseed > 0) dirarg = 2;
  }
  else
  {
    rseed = time(NULL);
    printf("Using random seed %u\n", rseed);
  }

  srand(rseed);
  g_type_init();

  if(argc > dirarg)
    dir = argv[dirarg];
  else
    dir = "std";

  result.total = 0;
  result.passed = 0;

  error = NULL;
  if(dir_foreach(dir, foreach_test_func, &result, &error) == FALSE)
  {
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return -1;
  }

  printf("%u out of %u tests passed\n", result.passed, result.total);
  if(result.passed < result.total)
    return -1;

  return 0;
}
