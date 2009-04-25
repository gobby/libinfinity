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

#include "inf-test-util.h"

#include <libinftext/inf-text-default-insert-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>

#include <libinfinity/common/inf-xml-util.h>

#include <string.h>

static int
inf_test_util_dir_foreach_sort_func(gconstpointer first,
                                    gconstpointer second)
{
  return strcmp(first, second);
}

GQuark
inf_test_util_parse_error_quark(void)
{
  return g_quark_from_static_string("INF_TEST_UTIL_PARSE_ERROR");
}

void
inf_test_util_print_operation(InfAdoptedOperation* op)
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
    inf_test_util_print_operation(first);
    printf("    ");
    inf_test_util_print_operation(second);
    printf(")\n");
  }
  else
  {
    g_assert_not_reached();
  }
}

void
inf_test_util_print_request(InfAdoptedRequest* request)
{
  gchar* time;
  time = inf_adopted_state_vector_to_string(
    inf_adopted_request_get_vector(request)
  );

  printf("%u [%s] ", inf_adopted_request_get_user_id(request), time);
  g_free(time);

  switch(inf_adopted_request_get_request_type(request))
  {
  case INF_ADOPTED_REQUEST_DO:
    printf("DO {\n  ");
    inf_test_util_print_operation(inf_adopted_request_get_operation(request));
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

void
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

gboolean
inf_test_util_dir_foreach(const char* dirname,
                          void(*callback)(const char*, gpointer),
                          gpointer user_data,
                          GError** error)
{
  GDir* dir;
  const gchar* entry;
  gchar* path;
  GSList* items;
  GSList* item;
  gboolean result;

  dir = g_dir_open(dirname, 0, error);
  if(dir == NULL) return FALSE;

  items = NULL;
  for(entry = g_dir_read_name(dir); entry != NULL; entry = g_dir_read_name(dir))
  {
    /* Ignore hidden files */
    if(entry[0] == '.') continue;

    path = g_build_filename(dirname, entry, NULL);
    items = g_slist_prepend(items, path);
  }

  g_dir_close(dir);

  items = g_slist_sort(items, inf_test_util_dir_foreach_sort_func);

  result = TRUE;
  for(item = items; item != NULL; item = g_slist_next(item))
  {
    path = (gchar*)item->data;
    if(g_file_test(path, G_FILE_TEST_IS_DIR))
    {
      if(inf_test_util_dir_foreach(path, callback, user_data, error) == FALSE)
      {
        result = FALSE;
      }
    }
    else if(g_file_test(path, G_FILE_TEST_IS_REGULAR))
    {
      callback(path, user_data);
    }

    g_free(path);
  }

  g_slist_free(items);
  return result;
}

InfTextChunk*
inf_test_util_parse_buffer(xmlNodePtr xml,
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

      xmlFree(content);
    }
    else
    {
      g_set_error(
        error,
        inf_test_util_parse_error_quark(),
        INF_TEST_UTIL_PARSE_ERROR_UNEXPECTED_NODE,
        "Node '%s' unexpected",
        (const gchar*)child->name
      );

      inf_text_chunk_free(chunk);
      return NULL;
    }
  }

  return chunk;
}

gboolean
inf_test_util_parse_user(xmlNodePtr xml,
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
      inf_test_util_parse_error_quark(),
      INF_TEST_UTIL_PARSE_ERROR_USER_ALREADY_EXISTS,
      "User with ID %u exists already",
      id
    );

    return FALSE;
  }

  *users = g_slist_prepend(*users, GUINT_TO_POINTER(id));
  return TRUE;
}

/* vim:set et sw=2 ts=2: */
