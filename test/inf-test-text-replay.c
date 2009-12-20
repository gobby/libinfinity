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

/* TODO: Add some of the replay logic as public libinfinity API */

#include "util/inf-test-util.h"

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-undo-grouping.h>
#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-default-insert-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-buffer.h>

#include <libinfinity/communication/inf-communication-manager.h>
#include <libinfinity/common/inf-simulated-connection.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-io.h>

#include <libxml/xmlreader.h>

#include <string.h>

#define XML_READER_TYPE_ELEMENT 1
#define XML_READER_TYPE_SIGNIFICANT_WHITESPACE 14
#define XML_READER_TYPE_END_ELEMENT 15

/* TODO: Get rid of replay object, we don't need it as NULL target is
 * allowed for CommunicationGroup. */

static GQuark inf_test_text_replay_error_quark;

typedef enum _InfTestTextReplayError {
  INF_TEST_TEXT_REPLAY_UNEXPECTED_EOF,
  INF_TEST_TEXT_REPLAY_UNEXPECTED_NODE
} InfTestTextReplayError;

typedef struct _InfTestTextReplayPlayUserTableForeachFuncData
  InfTestTextReplayPlayUserTableForeachFuncData;
struct _InfTestTextReplayPlayUserTableForeachFuncData {
  InfAdoptedAlgorithm* algorithm;
  GSList* undo_groupings;
};

typedef struct _InfTestTextReplayObject InfTestTextReplayObject;
struct _InfTestTextReplayObject {
  GObject parent_instance;
};

typedef struct _InfTestTextReplayObjectClass InfTestTextReplayObjectClass;
struct _InfTestTextReplayObjectClass {
  GObjectClass parent_class;
};

#define INF_TEST_TYPE_TEXT_REPLAY_OBJECT \
  (inf_test_text_replay_object_get_type())

/* To make gcc silent */
GType
inf_test_text_replay_object_get_type(void) G_GNUC_CONST;

static void
inf_test_text_replay_object_init(InfTestTextReplayObject* self)
{
}

static void
inf_test_text_replay_object_class_init(InfTestTextReplayObjectClass* klass)
{
}

static void
inf_test_text_replay_object_communication_object_init(
  InfCommunicationObjectIface* iface)
{
}

/* Dummy type implementing InfNetObject for the sending side */
G_DEFINE_TYPE_WITH_CODE(
  InfTestTextReplayObject,
  inf_test_text_replay_object,
  G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE(
    INF_COMMUNICATION_TYPE_OBJECT,
    inf_test_text_replay_object_communication_object_init
  )
)

/* The next three functions assume that buffer and chunks contain UTF-8 */
static GString*
inf_test_text_replay_load_buffer(InfTextBuffer* buffer)
{
  InfTextBufferIter* iter;
  GString* result;
  gchar* text;
  gsize bytes;

  result = g_string_sized_new(inf_text_buffer_get_length(buffer));

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

  if(INF_TEXT_IS_INSERT_OPERATION(operation))
  {
    g_assert(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(operation));

    chunk = inf_text_default_insert_operation_get_chunk(
      INF_TEXT_DEFAULT_INSERT_OPERATION(operation)
    );

    position = inf_text_insert_operation_get_position(
      INF_TEXT_INSERT_OPERATION(operation)
    );

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
}

static xmlNodePtr
inf_test_text_replay_read_current(xmlTextReaderPtr reader,
                                  GError** error)
{
  xmlError* xml_error;
  xmlNodePtr cur;

  cur = xmlTextReaderExpand(reader);
  if(cur == NULL)
  {
    xml_error = xmlGetLastError();

    g_set_error(
      error,
      g_quark_from_static_string("LIBXML2_READER_ERROR"),
      xml_error->code,
      "%s",
      xml_error->message
    );

    return NULL;
  }

  return cur;
}

static gboolean
inf_test_text_replay_handle_advance_required_result(xmlTextReaderPtr reader,
                                                    int result,
                                                    GError** error)
{
  xmlError* xml_error;

  switch(result)
  {
  case -1:
    xml_error = xmlGetLastError();

    g_set_error(
      error,
      g_quark_from_static_string("LIBXML2_READER_ERROR"),
      xml_error->code,
      "%s",
      xml_error->message
    );

    return FALSE;
  case 0:
    g_set_error(
      error,
      inf_test_text_replay_error_quark,
      INF_TEST_TEXT_REPLAY_UNEXPECTED_EOF,
      "Unexpected end of document"
    );

    return FALSE;
  case 1:
    return TRUE;
  default:
    g_assert_not_reached();
    return FALSE;
  }
}

static gboolean
inf_test_text_replay_advance_required(xmlTextReaderPtr reader,
                                      GError** error)
{
  int result;
  result = xmlTextReaderRead(reader);

  return inf_test_text_replay_handle_advance_required_result(
    reader,
    result,
    error
  );
}

static gboolean
inf_test_text_replay_advance_subtree_required(xmlTextReaderPtr reader,
                                              GError** error)
{
  int result;
  result = xmlTextReaderNext(reader);

  return inf_test_text_replay_handle_advance_required_result(
    reader,
    result,
    error
  );
}

static gboolean
inf_test_text_replay_skip_whitespace_required(xmlTextReaderPtr reader,
                                              GError** error)
{
  while(xmlTextReaderNodeType(reader) ==
        XML_READER_TYPE_SIGNIFICANT_WHITESPACE)
  {
    if(inf_test_text_replay_advance_required(reader, error) == FALSE)
      return FALSE;
  }

  return TRUE;
}

static gboolean
inf_test_text_replay_advance_skip_whitespace_required(xmlTextReaderPtr reader,
                                                      GError** error)
{
  if(!inf_test_text_replay_advance_required(reader, error))
    return FALSE;
  if(!inf_test_text_replay_skip_whitespace_required(reader, error))
    return FALSE;
  return TRUE;
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

static gboolean
inf_test_text_replay_play_initial(xmlTextReaderPtr reader,
                                  InfCommunicationGroup* publisher_group,
                                  InfXmlConnection* publisher,
                                  GError** error)
{
  xmlNodePtr cur;

  if(!inf_test_text_replay_advance_skip_whitespace_required(reader, error))
    return FALSE;

  while(xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT)
  {
    cur = inf_test_text_replay_read_current(reader, error);
    if(cur == NULL) return FALSE;

    inf_communication_group_send_message(
      publisher_group,
      publisher,
      xmlCopyNode(cur, 1)
    );

    /* TODO: Check whether that caused an error */
    inf_simulated_connection_flush(INF_SIMULATED_CONNECTION(publisher));

    if(!inf_test_text_replay_advance_subtree_required(reader, error))
      return FALSE;

    if(!inf_test_text_replay_skip_whitespace_required(reader, error))
      return FALSE;
  }

  return TRUE;
}

static gboolean
inf_test_text_replay_play_requests(xmlTextReaderPtr reader,
                                   InfSession* session,
                                   GSList** undo_groupings,
                                   InfCommunicationGroup* publisher_group,
                                   InfXmlConnection* publisher,
                                   InfXmlConnection* client,
                                   GError** error)
{
  InfSessionClass* session_class;
  GArray* user_props;
  xmlNodePtr cur;
  InfUser* user;
  guint i;
  InfTextUndoGrouping* grouping;
  GParameter* param;
  guint user_id;

  session_class = INF_SESSION_GET_CLASS(session);

  while(xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT)
  {
    cur = inf_test_text_replay_read_current(reader, error);
    if(cur == NULL) return FALSE;

    if(strcmp((const char*)cur->name, "request") == 0)
    {
      /* TODO: Add user join/leaves to record. */
      /* Until that is done, make users available when they issue a request */
      if(!inf_xml_util_get_attribute_uint_required(cur, "user", &user_id, error))
        return FALSE;

      user = inf_user_table_lookup_user_by_id(
        inf_session_get_user_table(session),
        user_id
      );
      g_assert(user);

      if(inf_user_get_status(user) == INF_USER_UNAVAILABLE)
      {
        g_object_set(
          G_OBJECT(user),
          "status", INF_USER_ACTIVE,
          "connection", client,
          NULL
        );
      }

      inf_communication_group_send_group_message(
        publisher_group,
        xmlCopyNode(cur, 1)
      );

      /* TODO: Check whether that caused an error */
      inf_simulated_connection_flush(INF_SIMULATED_CONNECTION(publisher));
    }
    else if(strcmp((const char*)cur->name, "user") == 0)
    {
      /* User join */
      user_props = session_class->get_xml_user_props(session, publisher, cur);
      param = inf_session_get_user_property(user_props, "connection");
      if(!G_IS_VALUE(&param->value))
      {
        g_value_init(&param->value, INF_TYPE_XML_CONNECTION);
        g_value_set_object(&param->value, G_OBJECT(client));
      }

      user = inf_session_add_user(
        session,
        (const GParameter*)user_props->data,
        user_props->len,
        error
      );

      for(i = 0; i < user_props->len; ++ i)
        g_value_unset(&g_array_index(user_props, GParameter, i).value);

      grouping = inf_text_undo_grouping_new();
      inf_adopted_undo_grouping_set_algorithm(
        INF_ADOPTED_UNDO_GROUPING(grouping),
        inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session)),
        INF_ADOPTED_USER(user)
      );

      *undo_groupings = g_slist_prepend(*undo_groupings, grouping);

      g_array_free(user_props, TRUE);
      if(user == NULL) return FALSE;
    }
    else
    {
      g_set_error(
        error,
        inf_test_text_replay_error_quark,
        INF_TEST_TEXT_REPLAY_UNEXPECTED_NODE,
        "Unexpected node: '%s'",
        cur->name
      );

      return FALSE;
    }

    if(!inf_test_text_replay_advance_subtree_required(reader, error))
      return FALSE;

    if(!inf_test_text_replay_skip_whitespace_required(reader, error))
      return FALSE;
  }

  return TRUE;
}

static void
inf_test_text_replay_play_user_table_foreach_func(InfUser* user,
                                                  gpointer user_data)
{
  InfTextUndoGrouping* grouping;
  InfTestTextReplayPlayUserTableForeachFuncData* data;

  grouping = inf_text_undo_grouping_new();
  data = (InfTestTextReplayPlayUserTableForeachFuncData*)user_data;

  inf_adopted_undo_grouping_set_algorithm(INF_ADOPTED_UNDO_GROUPING(grouping),
                                          data->algorithm,
                                          INF_ADOPTED_USER(user));
  data->undo_groupings = g_slist_prepend(data->undo_groupings, grouping);
}

static gboolean
inf_test_text_replay_play(xmlTextReaderPtr reader,
                          InfSession* session,
                          InfCommunicationGroup* publisher_group,
                          InfXmlConnection* publisher,
                          InfXmlConnection* client,
                          GError** error)
{
  gboolean result;

  /* Used to find InfTextChunk errors */
  GString* content;

  InfUserTable* user_table;
  InfTestTextReplayPlayUserTableForeachFuncData data;

  /* Advance to root node */
  if(xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT)
    if(inf_test_text_replay_advance_required(reader, error) == FALSE)
      return FALSE;

  if(strcmp((const char*)xmlTextReaderConstName(reader),
            "infinote-adopted-session-record") != 0)
  {
    g_set_error(
      error,
      inf_test_text_replay_error_quark,
      INF_TEST_TEXT_REPLAY_UNEXPECTED_NODE,
      "Document is not an infinote session record"
    );

    return FALSE;
  }

  if(!inf_test_text_replay_advance_skip_whitespace_required(reader, error))
    return FALSE;

  if(strcmp((const char*)xmlTextReaderConstName(reader), "initial") != 0)
  {
    g_set_error(
      error,
      inf_test_text_replay_error_quark,
      INF_TEST_TEXT_REPLAY_UNEXPECTED_NODE,
      "Initial document state missing in record"
    );

    return FALSE;
  }

  result = inf_test_text_replay_play_initial(
    reader,
    publisher_group,
    publisher,
    error
  );

  if(!result) return FALSE;

  if(!inf_test_text_replay_skip_whitespace_required(reader, error))
    return FALSE;

  if(xmlTextReaderNodeType(reader) != XML_READER_TYPE_END_ELEMENT)
  {
    g_set_error(
      error,
      inf_test_text_replay_error_quark,
      INF_TEST_TEXT_REPLAY_UNEXPECTED_NODE,
      "Expected ending of initial content"
    );

    return FALSE;
  }

  if(!inf_test_text_replay_advance_skip_whitespace_required(reader, error))
    return FALSE;

  content = inf_test_text_replay_load_buffer(
    INF_TEXT_BUFFER(inf_session_get_buffer(session))
  );

  g_signal_connect(
    G_OBJECT(inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session))),
    "apply-request",
    G_CALLBACK(inf_test_text_replay_apply_request_cb_before),
    content
  );

  g_signal_connect_after(
    G_OBJECT(inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session))),
    "apply-request",
    G_CALLBACK(inf_test_text_replay_apply_request_cb_after),
    content
  );

  /* Let an undo grouper group stuff, just as a consistency check
   * that it does not crash or behave badly. */
  user_table = inf_session_get_user_table(session);
  data.algorithm =
    inf_adopted_session_get_algorithm(INF_ADOPTED_SESSION(session));
  data.undo_groupings = NULL;
  inf_user_table_foreach_user(
    user_table,
    inf_test_text_replay_play_user_table_foreach_func,
    &data
  );

  result = inf_test_text_replay_play_requests(
    reader,
    session,
    &data.undo_groupings,
    publisher_group,
    publisher,
    client,
    error
  );

  while(data.undo_groupings != NULL)
  {
    g_object_unref(data.undo_groupings->data);
    data.undo_groupings =
      g_slist_remove(data.undo_groupings, data.undo_groupings->data);
  }

  g_string_free(content, TRUE);

  if(!result) return FALSE;

  if(!inf_test_text_replay_skip_whitespace_required(reader, error))
    return FALSE;

  /* TODO: Allow EOF here in case the record was suddenly interrupted, for
   * example when infinote crashed. */
  if(xmlTextReaderNodeType(reader) != XML_READER_TYPE_END_ELEMENT)
  {
    g_set_error(
      error,
      inf_test_text_replay_error_quark,
      INF_TEST_TEXT_REPLAY_UNEXPECTED_NODE,
      "Expected end of record"
    );

    return FALSE;
  }

  return TRUE;
}

static void
inf_test_text_replay_process(xmlTextReaderPtr reader)
{
  InfCommunicationManager* publisher_manager;
  InfCommunicationManager* client_manager;

  InfCommunicationHostedGroup* publisher_group;
  InfCommunicationJoinedGroup* client_group;

  InfSimulatedConnection* publisher;
  InfSimulatedConnection* client;

  InfTextBuffer* buffer;
  InfIo* io;
  InfTextSession* session;
  InfTestTextReplayObject* replay_object;

  GError* error;

  publisher = inf_simulated_connection_new();
  client = inf_simulated_connection_new();
  inf_simulated_connection_connect(publisher, client);

  inf_simulated_connection_set_mode(
    publisher,
    INF_SIMULATED_CONNECTION_DELAYED
  );

  inf_simulated_connection_set_mode(
    client,
    INF_SIMULATED_CONNECTION_DELAYED
  );

  publisher_manager = inf_communication_manager_new();

  publisher_group = inf_communication_manager_open_group(
    publisher_manager,
    "InfAdoptedSessionReplay",
    NULL
  );

  inf_communication_hosted_group_add_member(
    publisher_group,
    INF_XML_CONNECTION(publisher)
  );

  client_manager = inf_communication_manager_new();

  client_group = inf_communication_manager_join_group(
    client_manager,
    "InfAdoptedSessionReplay",
    INF_XML_CONNECTION(client),
    "central"
  );

  buffer = INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8"));
  io = INF_IO(inf_standalone_io_new());

  session = inf_text_session_new(
    client_manager,
    buffer,
    io,
    INF_SESSION_SYNCHRONIZING,
    INF_COMMUNICATION_GROUP(client_group),
    INF_XML_CONNECTION(client)
  );

  g_object_unref(io);

  inf_communication_group_set_target(
    INF_COMMUNICATION_GROUP(client_group),
    INF_COMMUNICATION_OBJECT(session)
  );

  replay_object = g_object_new(INF_TEST_TYPE_TEXT_REPLAY_OBJECT, NULL);
  inf_communication_group_set_target(
    INF_COMMUNICATION_GROUP(publisher_group),
    INF_COMMUNICATION_OBJECT(replay_object)
  );

  inf_simulated_connection_flush(publisher);
  inf_simulated_connection_flush(client);

  error = NULL;
  inf_test_text_replay_play(
    reader,
    INF_SESSION(session),
    INF_COMMUNICATION_GROUP(publisher_group),
    INF_XML_CONNECTION(publisher),
    INF_XML_CONNECTION(client),
    &error
  );

  g_object_unref(client_group);
  g_object_unref(publisher_group);

  g_object_unref(replay_object);
  g_object_unref(session);

  g_object_unref(publisher);
  g_object_unref(client);

  g_object_unref(publisher_manager);
  g_object_unref(client_manager);

  if(error != NULL)
  {
    fprintf(
      stderr,
      "Line %d: %s\n",
      xmlTextReaderGetParserLineNumber(reader),
      error->message
    );

    g_error_free(error);
  }
  else
  {
    /*inf_test_util_print_buffer(buffer);*/
    fprintf(stderr, "Replayed record successfully\n");
  }

  g_object_unref(buffer);
}

int main(int argc, char* argv[])
{
  xmlTextReaderPtr reader;
  int i;

  g_type_init();
  inf_test_text_replay_error_quark =
    g_quark_from_static_string("INF_TEST_TEXT_REPLAY_ERROR");

  if(argc < 2)
  {
    fprintf(stderr, "Usage: %s <record-file1> <record-file2> ...\n", argv[0]);
    return -1;
  }

  for(i = 1; i < argc; ++ i)
  {
    fprintf(stderr, "%s...", argv[i]);
    fflush(stderr);

    reader = xmlReaderForFile(
      argv[i],
      NULL,
      XML_PARSE_NOERROR | XML_PARSE_NOWARNING
    );

    if(!reader)
    {
      fprintf(stderr, "%s\n", xmlGetLastError()->message);
      return -1;
    }

    inf_test_text_replay_process(reader);

    if(xmlTextReaderClose(reader) == -1)
    {
      fprintf(stderr, "%s\n", xmlGetLastError()->message);
      return -1;
    }

    xmlFreeTextReader(reader);
  }

  return 0;
}
/* vim:set et sw=2 ts=2: */
