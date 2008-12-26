/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008 Armin Burgmeier <armin@arbur.net>
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

typedef struct {
  guint total;
  guint passed;
} test_result;

typedef enum {
  INF_TEST_TEXT_CLEANUP_USER_UNAVAILABLE,
  INF_TEST_TEXT_CLEANUP_UNSUPPORTED,
  INF_TEST_TEXT_CLEANUP_VERIFY_FAILED
} InfTestTextCleanupError;

static GQuark
inf_test_text_cleanup_error_quark()
{
  return g_quark_from_static_string("INF_TEST_TEXT_CLEANUP_ERROR");
}

static gboolean
perform_test(guint max_total_log_size,
             InfTextChunk* initial,
             GSList* users,
             GSList* requests,
             GError** error)
{
  InfTextBuffer* buffer;
  InfCommunicationManager* manager;
  InfIo* io;
  InfTextSession* session;
  InfAdoptedAlgorithm* algorithm;

  InfUserTable* user_table;
  InfTextUser* user;
  gchar* user_name;

  GSList* item;
  xmlNodePtr request;
  gboolean result;
  GError* local_error;
  
  guint verify_user_id;
  InfAdoptedUser* verify_user;
  guint verify_log_size;
  gint verify_can_undo;
  gint verify_can_redo;

  InfAdoptedRequestLog* log;
  guint log_size;

  buffer = INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8"));
  inf_text_buffer_insert_chunk(buffer, 0, initial, NULL);

  manager = inf_communication_manager_new();
  io = INF_IO(inf_standalone_io_new());
  user_table = inf_user_table_new();
  local_error = NULL;

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

  session = INF_TEXT_SESSION(
    g_object_new(
      INF_TEXT_TYPE_SESSION,
      "connection-manager", manager,
      "buffer", buffer,
      "io", io,
      "user_table", user_table,
      "max-total-log-size", max_total_log_size,
      NULL
    )
  );
  
  algorithm = inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));

  g_object_unref(io);
  g_object_unref(manager);
  g_object_unref(user_table);
  g_object_unref(buffer);

  for(item = requests; item != NULL; item = item->next)
  {
    request = (xmlNodePtr)item->data;
    
    if(strcmp((const char*)request->name, "request") == 0)
    {
      /* Request */
      result = inf_communication_object_received(
        INF_COMMUNICATION_OBJECT(session),
        NULL,
        request,
        &local_error
      );
      
      if(local_error != NULL)
        goto fail;
    }
    else
    {
      /* TODO: Make an extra function out of this: */
      /* Verify */
      result = inf_xml_util_get_attribute_uint_required(
        request,
        "user",
        &verify_user_id,
        &local_error
      );
      
      if(result == FALSE)
        goto fail;

      verify_user = INF_ADOPTED_USER(
        inf_user_table_lookup_user_by_id(user_table, verify_user_id)
      );

      if(verify_user == NULL)
      {
        g_set_error(
          error,
          inf_test_text_cleanup_error_quark(),
          INF_TEST_TEXT_CLEANUP_USER_UNAVAILABLE,
          "User ID '%u' not available",
          verify_user_id
        );
        
        goto fail;
      }

      result = inf_xml_util_get_attribute_uint(
        request,
        "log-size",
        &verify_log_size,
        &local_error
      );

      if(local_error) goto fail;

      if(result)
      {
        log = inf_adopted_user_get_request_log(INF_ADOPTED_USER(verify_user));
        log_size = inf_adopted_request_log_get_end(log) -
          inf_adopted_request_log_get_begin(log);
        if(verify_log_size != log_size)
        {
          g_set_error(
            error,
            inf_test_text_cleanup_error_quark(),
            INF_TEST_TEXT_CLEANUP_VERIFY_FAILED,
            "Log size does not match; got %u, but expected %u",
            log_size,
            verify_log_size
          );

          goto fail;
        }
      }
      
      result = inf_xml_util_get_attribute_int(
        request,
        "can-undo",
        &verify_can_undo,
        &local_error
      );

      if(local_error) goto fail;

      if(result)
      {
        result = inf_adopted_algorithm_can_undo(algorithm, verify_user);
        if(result != verify_can_undo)
        {
          g_set_error(
            error,
            inf_test_text_cleanup_error_quark(),
            INF_TEST_TEXT_CLEANUP_VERIFY_FAILED,
            "can-undo does not match; got %d, but expected %d",
            (guint)result,
            verify_can_undo
          );

          goto fail;
        }
      }

      result = inf_xml_util_get_attribute_int(
        request,
        "can-redo",
        &verify_can_redo,
        &local_error
      );

      if(local_error) goto fail;

      if(result)
      {
        result = inf_adopted_algorithm_can_redo(algorithm, verify_user);
        if(result != verify_can_redo)
        {
          g_set_error(
            error,
            inf_test_text_cleanup_error_quark(),
            INF_TEST_TEXT_CLEANUP_VERIFY_FAILED,
            "can-redo does not match; got %d, but expected %d",
            (guint)result,
            verify_can_redo
          );

          goto fail;
        }
      }
    }
  }

  g_object_unref(session);
  return TRUE;

fail:
  g_object_unref(session);
  if(local_error) g_propagate_error(error, local_error);
  return FALSE;
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
  GSList* users;
  guint max_total_log_size;
  GError* error;
  gboolean res;

  /* Only process XML files, not the Makefiles or other stuff */
  if(!g_str_has_suffix(testfile, ".xml"))
    return;

  result = (test_result*)user_data;
  doc = xmlParseFile(testfile);

  requests = NULL;
  initial = NULL;
  users = NULL;
  max_total_log_size = 0;
  error = NULL;

  printf("%s... ", testfile);
  fflush(stdout);

  ++ result->total;

  if(doc != NULL)
  {
    root = xmlDocGetRootElement(doc);
    for(child = root->children; child != NULL; child = child->next)
    {
      if(child->type != XML_ELEMENT_NODE) continue;

      if(strcmp((const char*)child->name, "log") == 0)
      {
        res = inf_xml_util_get_attribute_uint_required(
          child,
          "size",
          &max_total_log_size,
          &error
        );

        if(!res)
          break;
      }
      else if(strcmp((const char*)child->name, "initial-buffer") == 0)
      {
        if(initial != NULL) inf_text_chunk_free(initial);
        initial = inf_test_util_parse_buffer(child, &error);
        if(initial == NULL) break;
      }
      else if(strcmp((const char*)child->name, "user") == 0)
      {
        if(inf_test_util_parse_user(child, &users, &error) == FALSE)
          break;
      }
      else if(strcmp((const char*)child->name, "request") == 0 ||
              strcmp((const char*)child->name, "verify") == 0)
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
      g_slist_free(users);
    }
    else
    {
      g_assert(initial != NULL);

      requests = g_slist_reverse(requests);
      if(perform_test(max_total_log_size, initial, users, requests, &error) ==
         TRUE)
      {
        ++ result->passed;
        printf("OK\n");
      }
      else
      {
        printf("FAILED (%s)\n", error->message);
        g_error_free(error);
      }

      xmlFreeDoc(doc);
      g_slist_free(requests);
      inf_text_chunk_free(initial);
      g_slist_free(users);
    }
  }
}

int main(int argc, char* argv[])
{
  const char* dir;
  GError* error;
  test_result result;

  g_type_init();

  if(argc > 1)
    dir = argv[1];
  else
    dir = "cleanup";

  result.total = 0;
  result.passed = 0;

  error = NULL;
  if(inf_test_util_dir_foreach(dir, foreach_test_func, &result, &error) ==
     FALSE)
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
