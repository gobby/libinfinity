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

/* Cuts away front and back of a replay, so that it still fails. It's very
 * primitive, and more sophisticated methods can still be implemented. */

/* TODO: Break as soon as either (stderr) output or exit status changes */

#include "util/inf-test-util.h"

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-default-buffer.h>
#include <libinfinity/client/infc-note-plugin.h>
#include <libinfinity/adopted/inf-adopted-session-replay.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-init.h>

#include <glib/gstdio.h>

#ifndef G_OS_WIN32
# include <sys/wait.h>
#endif
#include <string.h>

static const gchar REPLAY[] = ".libs/inf-test-text-replay";

typedef struct _InfTestReduceReplayValidateUserData
  InfTestReduceReplayValidateUserData;
struct _InfTestReduceReplayValidateUserData {
  guint undo_cur;
  guint undo_max;
  InfAdoptedStateVector* time;
};

static InfTestReduceReplayValidateUserData*
inf_test_reduce_replay_validate_user_data_new(const gchar* time_string,
                                              GError** error)
{
  InfTestReduceReplayValidateUserData* data;
  InfAdoptedStateVector* time;

  time = inf_adopted_state_vector_from_string(time_string, error);
  if(time == NULL) return NULL;

  data = g_slice_new(InfTestReduceReplayValidateUserData);

  data->undo_cur = 0;
  data->undo_max = 0;
  data->time = time;

  return data;
}

static void
inf_test_reduce_replay_valiadate_user_data_free(
  InfTestReduceReplayValidateUserData* data)
{
  inf_adopted_state_vector_free(data->time);
  g_slice_free(InfTestReduceReplayValidateUserData, data);
}

static InfTestReduceReplayValidateUserData*
inf_test_reduce_replay_add_validate_user_from_xml(GHashTable* table,
                                                  InfAdoptedStateVector* time,
                                                  xmlNodePtr xml,
                                                  GError** error)
{
  InfTestReduceReplayValidateUserData* data;
  xmlChar* time_str;
  guint user_id;

  /* The XML node can either be a <user> or a <sync-user> element */
  if(!inf_xml_util_get_attribute_uint_required(xml, "id", &user_id, error))
    return NULL;

  time_str = inf_xml_util_get_attribute_required(xml, "time", error);
  if(time_str == NULL) return NULL;

  data = inf_test_reduce_replay_validate_user_data_new(
    (const char*)time_str,
    error
  );

  xmlFree(time_str);

  if(data == NULL)
    return NULL;

  if(time != NULL)
  {
    inf_adopted_state_vector_set(
      time,
      user_id,
      inf_adopted_state_vector_get(data->time, user_id)
    );
  }

  g_hash_table_insert(table, GUINT_TO_POINTER(user_id), data);
  return data;
}

static InfSession*
inf_test_reduce_replay_session_new(InfIo* io,
                                   InfCommunicationManager* manager,
                                   InfSessionStatus status,
                                   InfCommunicationGroup* sync_group,
                                   InfXmlConnection* sync_connection,
                                   const gchar* path,
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
    sync_group,
    sync_connection
  );
  g_object_unref(buffer);

  return INF_SESSION(session);
}

/* TODO: This should maybe go to inf-test-util */
static const InfcNotePlugin INF_TEST_REDUCE_REPLAY_TEXT_PLUGIN = {
  NULL, "InfText", inf_test_reduce_replay_session_new
};

static xmlNodePtr
inf_test_reduce_replay_find_node(xmlNodePtr xml,
                                 const gchar* name)
{
  xmlNodePtr child;
  for(child = xml->children; child != NULL; child = child->next)
    if(child->type == XML_ELEMENT_NODE)
      if(strcmp((const char*)child->name, name) == 0)
        return child;
  return NULL;
}

static xmlNodePtr
inf_test_reduce_replay_next_node(xmlNodePtr xml)
{
  do
  {
    xml = xml->next;
  } while(xml && xml->type != XML_ELEMENT_NODE);

  return xml;
}

static xmlNodePtr
inf_test_reduce_replay_first_node(xmlNodePtr xml)
{
  while(xml && xml->type != XML_ELEMENT_NODE)
    xml = xml->next;
  return xml;
}

static gboolean
inf_test_reduce_replay_validate_test(xmlDocPtr doc,
                                     GError** error)
{
  GHashTable* table;
  xmlNodePtr root;
  xmlNodePtr cur;
  xmlNodePtr child;
  InfTestReduceReplayValidateUserData* data;
  InfAdoptedStateVector* initial_time;
  InfAdoptedStateVector* vector;
  xmlChar* time_str;
  guint user_id;
  gboolean result;

  root = xmlDocGetRootElement(doc);
  cur = inf_test_reduce_replay_find_node(root, "initial");
  g_assert(cur != NULL);

  table = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)inf_test_reduce_replay_valiadate_user_data_free
  );

  /* Insert initial users into table */
  initial_time = inf_adopted_state_vector_new();

  for(child = cur->children;
      child != NULL;
      child = inf_test_reduce_replay_next_node(child))
  {
    if(strcmp((const char*)child->name, "sync-user") == 0)
    {
      data = inf_test_reduce_replay_add_validate_user_from_xml(
        table,
        initial_time,
        child,
        error
      );

      if(data == NULL)
      {
        inf_adopted_state_vector_free(initial_time);
        g_hash_table_unref(table);
        return FALSE;
      }
    }
  }

  /* Check all requests */
  while( (cur = inf_test_reduce_replay_next_node(cur)) != NULL)
  {
    if(strcmp((const char*)cur->name, "user") == 0)
    {
      data = inf_test_reduce_replay_add_validate_user_from_xml(
        table,
        NULL,
        cur,
        error
      );

      if(data == NULL)
      {
        inf_adopted_state_vector_free(initial_time);
        g_hash_table_unref(table);
        return FALSE;
      }
    }
    else if(strcmp((const char*)cur->name, "request") == 0)
    {
      child = cur->children;
      g_assert(child);
      if(child->type != XML_ELEMENT_NODE)
        child = inf_test_reduce_replay_next_node(child);

      result = inf_xml_util_get_attribute_uint_required(
        cur,
        "user",
        &user_id,
        error
      );

      if(result == FALSE)
      {
        inf_adopted_state_vector_free(initial_time);
        g_hash_table_unref(table);
        return FALSE;
      }

      data = g_hash_table_lookup(table, GUINT_TO_POINTER(user_id));
      g_assert(data != NULL);

      /* Check the vector time */
      time_str = inf_xml_util_get_attribute_required(cur, "time", error);
      if(time_str == NULL)
      {
        inf_adopted_state_vector_free(initial_time);
        g_hash_table_unref(table);
        return FALSE;
      }

      vector = inf_adopted_state_vector_from_string_diff(
        (const char*)time_str,
        data->time,
        error
      );

      xmlFree(time_str);

      if(vector == NULL)
      {
        inf_adopted_state_vector_free(initial_time);
        g_hash_table_unref(table);
        return FALSE;
      }

      if(!inf_adopted_state_vector_causally_before(initial_time, vector))
      {
        g_set_error(
          error,
          g_quark_from_static_string("INF_TEST_REDUCE_REPLAY_ERROR"),
          0,
          "Concurrent request at line %d",
          cur->line
        );

        inf_adopted_state_vector_free(vector);
        inf_adopted_state_vector_free(initial_time);
        g_hash_table_unref(table);
        return FALSE;
      }

      inf_adopted_state_vector_add(vector, user_id, 1);
      inf_adopted_state_vector_free(data->time);
      data->time = vector;

      /* Check undo/redo counts */
      if(strcmp((const char*)child->name, "move") != 0 &&
         strcmp((const char*)child->name, "no-op") != 0)
      {
        if(strcmp((const char*)child->name, "undo") == 0 ||
           strcmp((const char*)child->name, "undo-caret") == 0)
        {
          if(data->undo_cur == 0)
          {
            g_set_error(
              error,
              g_quark_from_static_string("INF_TEST_REDUCE_REPLAY_ERROR"),
              1,
              "Dangling undo request at line %d",
              child->line
            );

            inf_adopted_state_vector_free(initial_time);
            g_hash_table_unref(table);
            return FALSE;
          }

          --data->undo_cur;
        }
        else if(strcmp((const char*)child->name, "redo") == 0 ||
                strcmp((const char*)child->name, "redo-caret") == 0)
        {
          if(data->undo_cur == data->undo_max)
          {
            g_set_error(
              error,
              g_quark_from_static_string("INF_TEST_REDUCE_REPLAY_ERROR"),
              2,
              "Dangling redo request at line %d",
              child->line
            );

            inf_adopted_state_vector_free(initial_time);
            g_hash_table_unref(table);
            return FALSE;
          }

          ++data->undo_cur;
        }
        else
        {
          data->undo_max = data->undo_cur + 1;
          data->undo_cur = data->undo_max;
        }
      }
    }
  }

  inf_adopted_state_vector_free(initial_time);
  g_hash_table_unref(table);
  return TRUE;
}

static gboolean
inf_test_reduce_replay_run_test(xmlDocPtr doc)
{
  GError* error;
  gchar* cmd;
  gchar* stdout_buf;
  gchar* stderr_buf;
  int ret;

  xmlSaveFile("test.xml", doc);
  /*cmd = g_strdup_printf("(%s %s 2>&1) > /dev/null", REPLAY, "test.xml");*/
  cmd = g_strdup_printf("%s %s", REPLAY, "test.xml");

  /* make it die on algorithm errors */
  g_setenv("G_DEBUG", "fatal-warnings", TRUE);

  error = NULL;
  if(!g_spawn_command_line_sync(cmd, &stdout_buf, &stderr_buf, &ret, &error))
  {
    g_unlink("test.xml");
    g_free(cmd);
    fprintf(stderr, "Failed to run test: %s\n", error->message);
    g_error_free(error);
    return FALSE;
  }

  /* Reset, so that we don't die ourselves */
  g_setenv("G_DEBUG", "", TRUE);

  /* These are just dummy variables to suppress the console output */
  g_free(stdout_buf);
  g_free(stderr_buf);

  /*g_unlink("test.xml");*/

#ifndef G_OS_WIN32
  if(WIFSIGNALED(ret) &&
     (WTERMSIG(ret) == SIGABRT ||
      WTERMSIG(ret) == SIGSEGV ||
      WTERMSIG(ret) == SIGTRAP))
  {
    return FALSE;
  }
  else if(WIFEXITED(ret))
  {
    if(WEXITSTATUS(ret))
      return FALSE;
    else
      return TRUE;
  }
  else
#endif
  {
    /* what happen? */
    g_assert_not_reached();
  }
}

static void
inf_test_reduce_replay_remove_sync_requests(xmlNodePtr initial)
{
  xmlNodePtr child;
  xmlNodePtr next;
  xmlNodePtr prev;
  xmlNodePtr sync_begin;
  guint count;

  count = 0;
  for(child = inf_test_reduce_replay_first_node(initial->children);
      child != NULL; child = next)
  {
    next = inf_test_reduce_replay_next_node(child);
    if(strcmp((const char*)child->name, "sync-request") == 0)
    {
      while(child != next)
      {
        prev = child;
        child = child->next;
        xmlUnlinkNode(prev);
        xmlFreeNode(prev);
      }
    }
    else
    {
      if(strcmp((const char*)child->name, "sync-begin") == 0)
        sync_begin = child;
      else if(strcmp((const char*)child->name, "sync-end") != 0)
        ++count;
    }
  }

  g_assert(sync_begin != NULL);
  inf_xml_util_set_attribute_uint(sync_begin, "num-messages", count);
}

static gboolean
inf_test_reduce_replay_reduce(xmlDocPtr doc,
                              const char* filename,
                              guint skip)
{
  InfAdoptedSessionReplay* local_replay;
  InfAdoptedSession* session;
  InfSessionClass* session_class;
  xmlDocPtr last_fail;
  xmlDocPtr back_doc;
  gboolean result;

  xmlNodePtr root;
  xmlNodePtr initial;
  xmlNodePtr next;
  xmlNodePtr request;
  xmlNodePtr sync_begin;
  GError* error;
  guint i;

  error = NULL;
  root = xmlDocGetRootElement(doc);
  if(inf_test_reduce_replay_run_test(doc) == TRUE)
  {
    fprintf(stderr, "Test does not initially fail\n");
    return FALSE;
  }

  if(!inf_test_reduce_replay_validate_test(doc, &error))
  {
    fprintf(stderr, "Test does not initially validate: %s\n", error->message);
    g_error_free(error);
    return FALSE;
  }

  initial = inf_test_reduce_replay_find_node(root, "initial");
  if(!initial)
  {
    fprintf(stderr, "Test has no initial\n");
    return FALSE;
  }

  /* Remove all sync-requests. We require test to work without for now. */
  inf_test_reduce_replay_remove_sync_requests(initial);

  root = xmlDocGetRootElement(doc);
  if(inf_test_reduce_replay_run_test(doc) == TRUE)
  {
    fprintf(stderr, "Test does not fail without sync-requests anymore\n");
    return FALSE;
  }

  /* Initialize local replay */
  error = NULL;
  local_replay = inf_adopted_session_replay_new();
  inf_adopted_session_replay_set_record(
    local_replay,
    filename,
    &INF_TEST_REDUCE_REPLAY_TEXT_PLUGIN,
    &error
  );

  session = inf_adopted_session_replay_get_session(local_replay);
  session_class = INF_SESSION_GET_CLASS(session);

  if(error)
  {
    fprintf(stderr, "Creating local replay failed: %s\n", error->message);
    g_error_free(error);
    return FALSE;
  }

  last_fail = xmlCopyDoc(doc, 1);
  request = inf_test_reduce_replay_next_node(initial);

  i = 0;
  for(;;)
  {
    /* Play next request */
    if(inf_adopted_session_replay_play_next(local_replay, &error))
    {
      /* The InfAdoptedSessionReply is synchronized with our request
       * variable, so if we could play another step, request cannot be
       * NULL at this point. */
      g_assert(request != NULL);

      ++i;
      fprintf(stderr, "%.6u... ", i);
      fflush(stderr);

      if(strcmp((const char*)request->name, "request") != 0 &&
         strcmp((const char*)request->name, "user") != 0)
      {
        fprintf(stderr, "NOREQ <%s>\n", request->name);
        request = inf_test_reduce_replay_next_node(request);
      }
      else
      {
        fprintf(stderr, "REQ %8s  ", request->name);
        fflush(stderr);

        /* Get rid of next request, and see if test still fails */
        do
        {
          next = request->next;
          xmlUnlinkNode(request);
          xmlFreeNode(request);
          request = next;
        } while(next && next->type != XML_ELEMENT_NODE);

        /* Rewrite initial */
        xmlFreeNodeList(initial->children);
        initial->children = NULL;

        sync_begin =
          xmlNewChild(initial, NULL, (const xmlChar*)"sync-begin", NULL);
        session_class->to_xml_sync(INF_SESSION(session), initial);
        xmlNewChild(initial, NULL, (const xmlChar*)"sync-end", NULL);
        /* this sets num-messages: */
        inf_test_reduce_replay_remove_sync_requests(initial);

        if(inf_test_reduce_replay_validate_test(doc, &error))
        {
          if(i % skip != 0)
          {
            /* Simply continue */
            fprintf(stderr, "SKIP\n");
          }
          else if(inf_test_reduce_replay_run_test(doc))
          {
            fprintf(stderr, "OK!\n");
            result = TRUE;
            break;
          }
          else
          {
            fprintf(stderr, "FAIL\n");
            xmlFreeDoc(last_fail);
            last_fail = xmlCopyDoc(doc, 1);
          }
        }
        else
        {
          /* Continue when test is invalid; we probably removed an undo's
           * associated request, so just wait until we remove the undo request
           * itself. */
          fprintf(stderr, "INVALID %s\n", error->message);
          g_error_free(error);
          error = NULL;
        }
      }
    }
    else
    {
      if(error)
      {
        fprintf(stderr, "Playing local replay failed: %s\n", error->message);
        g_error_free(error);
        result = FALSE;
        break;
      }
      else
      {
        fprintf(stderr, "Played all records and the error still occurs\n");
        result = FALSE;
        break;
      }
    }
  }
  g_object_unref(local_replay);

  if(result)
  {
    /* Also reduce from back */
    i = 0;

    back_doc = xmlCopyDoc(last_fail, 1);
    root = xmlDocGetRootElement(back_doc);
    initial = inf_test_reduce_replay_find_node(root, "initial");
    g_assert(initial);

    next = initial;
    do
    {
      request = next;
      next = inf_test_reduce_replay_next_node(request);
      ++i;
    } while(next != NULL);

    for(;;)
    {
      g_assert(i > 1);

      --i;
      fprintf(stderr, "%.6u... ", i);
      fflush(stderr);

      do
      {
        next = request;
        request = request->prev;
        xmlUnlinkNode(next);
        xmlFreeNode(next);
      } while(request && request->type != XML_ELEMENT_NODE);

      if(inf_test_reduce_replay_validate_test(back_doc, &error))
      {
        if(i % skip != 0)
        {
          /* Simply continue */
          fprintf(stderr, "SKIP\n");
        }
        else if(inf_test_reduce_replay_run_test(back_doc))
        {
          fprintf(stderr, "OK!\n");
          result = TRUE;
          break;
        }
        else
        {
          fprintf(stderr, "FAIL\n");
          xmlFreeDoc(last_fail);
          last_fail = xmlCopyDoc(back_doc, 1);
        }
      }
      else
      {
        fprintf(stderr, "INVALID %s\n", error->message);
        g_error_free(error);
        error = NULL;

        result = FALSE;
        break;
      }
    }

    xmlFreeDoc(back_doc);
  }

  /* Save last failing record in each case */
  xmlSaveFile("last_fail.record.xml", last_fail);
  printf("Last failing record in last_fail.record.xml\n");
  xmlFreeDoc(last_fail);
  return result;
}

int main(int argc, char* argv[])
{
  GError* error = NULL;
  xmlDocPtr doc;
  gboolean ret;
  guint skip;

  if(!inf_init(&error))
  {
    fprintf(stderr, "%s\n", error->message);
    return -1;
  }

  if(!g_file_test(REPLAY, G_FILE_TEST_IS_EXECUTABLE))
  {
    fprintf(stderr, "Replay tool not available. Run \"make\" first.");
    return -1;
  }

  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <record-file> [<skip>]\n", argv[0]);
    return -1;
  }

  doc = xmlReadFile(argv[1], "UTF-8", XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  if(!doc || !xmlDocGetRootElement(doc))
  {
    if(doc) xmlFreeDoc(doc);
    fprintf(stderr, "%s\n", xmlGetLastError()->message);
    return -1;
  }

  skip = 1;
  if(argc > 2) skip = strtol(argv[2], NULL, 10);

  ret = inf_test_reduce_replay_reduce(doc, argv[1], skip);

  xmlFreeDoc(doc);
  return ret ? 0 : -1;
}

/* vim:set et sw=2 ts=2: */
