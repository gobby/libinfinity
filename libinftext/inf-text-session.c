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
#include <libinftext/inf-text-default-delete-operation.h>
#include <libinftext/inf-text-remote-delete-operation.h>
#include <libinftext/inf-text-insert-operation.h>
#include <libinftext/inf-text-delete-operation.h>
#include <libinftext/inf-text-move-operation.h>
#include <libinftext/inf-text-chunk.h>
#include <libinftext/inf-text-user.h>
#include <libinfinity/adopted/inf-adopted-no-operation.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>

#include <libxml/tree.h>
#include <string.h>

/* TODO: Optionally broadcast operations delayed to merge adjacent operations
 * and send as a single request. */

typedef struct _InfTextSessionPrivate InfTextSessionPrivate;
struct _InfTextSessionPrivate {
  gboolean dummy;
  /* TODO: Wah, fields? */
};

enum {
  PROP_0

  /* TODO: Wah, properties? */
};

typedef struct _InfTextSessionInsertForeachData
  InfTextSessionInsertForeachData;
typedef struct _InfTextSessionEraseForeachData
  InfTextSessionEraseForeachData;

struct _InfTextSessionInsertForeachData {
  guint position;
  InfTextChunk* chunk;
  InfUser* user;
};

struct _InfTextSessionEraseForeachData {
  guint position;
  guint length;
  InfUser* user;
};

#define INF_TEXT_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TEXT_TYPE_SESSION, InfTextSessionPrivate))

static InfAdoptedSessionClass* parent_class;
static GQuark inf_text_session_error_quark;

/*
 * Utility functions
 */

/* Converts at most *bytes bytes with cd and writes the result, which are
 * at most 1024 bytes, into xml, setting the given author. *bytes will be
 * set to the number of bytes not yet processed. */
static void
inf_text_session_segment_to_xml(GIConv* cd,
                                xmlNodePtr xml,
                                gconstpointer text,
                                gsize* bytes, /* in/out */
                                guint author)
{
  gchar utf8_text[1024];
  gsize result;

  gsize bytes_left;

  gchar* inbuf;
  gchar* outbuf;

  bytes_left = 1024;

  inbuf = (gchar*)text;
  outbuf = utf8_text;

  result = g_iconv(
    *cd,
    &inbuf,
    bytes,
    &outbuf,
    &bytes_left
  );

  /* Conversion into UTF-8 should always succeed */
  g_assert(result == 0);

  xmlNodeAddContentLen(xml, (const xmlChar*)utf8_text, 1024 - bytes_left);
  inf_xml_util_set_attribute_uint(xml, "author", author);
}

static gpointer
inf_text_session_segment_from_xml(GIConv* cd,
                                  xmlNodePtr xml,
                                  guint* length,
                                  gsize* bytes,
                                  guint* author,
                                  GError** error)
{
  gsize bytes_read;
  xmlChar* utf8_text;
  gpointer text;

  if(!inf_xml_util_get_attribute_uint_required(xml, "author", author, error))
    return NULL;

  utf8_text = xmlNodeGetContent(xml);
  *length = g_utf8_strlen((const gchar*)utf8_text, -1);

  text = g_convert_with_iconv(
    (const gchar*)utf8_text,
    -1,
    *cd,
    &bytes_read,
    bytes,
    error
  );

  xmlFree(utf8_text);
  if(text == NULL) return FALSE;

  return text;
}

/*
 * Signal handlers.
 */

static void
inf_text_session_buffer_insert_text_cb_before(InfTextBuffer* buffer,
                                              guint pos,
                                              InfTextChunk* chunk,
                                              InfUser* user,
                                              gpointer user_data)
{
  InfTextSession* session;
  InfAdoptedOperation* operation;
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedRequest* request;

  /* TODO: Always broadcast, but block signal handler when the insertion
   * happend in synchronization or during a remote request. */
  if(user != NULL && (inf_user_get_flags(user) & INF_USER_LOCAL) != 0)
  {
    g_assert(INF_TEXT_IS_USER(user));

    session = INF_TEXT_SESSION(user_data);

    operation = INF_ADOPTED_OPERATION(
      inf_text_default_insert_operation_new(pos, chunk)
    );

    algorithm = inf_adopted_session_get_algorithm(
      INF_ADOPTED_SESSION(session)
    );

    request = inf_adopted_algorithm_generate_request_noexec(
      algorithm,
      INF_ADOPTED_USER(user),
      operation
    );

    inf_adopted_session_broadcast_request(
      INF_ADOPTED_SESSION(session),
      request
    );

    g_object_unref(G_OBJECT(request));
  }
}

static void
inf_text_session_buffer_erase_text_cb_before(InfTextBuffer* buffer,
                                             guint pos,
                                             guint len,
                                             InfUser* user,
                                             gpointer user_data)
{
  InfTextSession* session;
  InfAdoptedOperation* operation;
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedRequest* request;
  InfTextChunk* erased_chunk;

  if( (inf_user_get_flags(user) & INF_USER_LOCAL) != 0)
  {
    g_assert(INF_TEXT_IS_USER(user));
    session = INF_TEXT_SESSION(user_data);

    erased_chunk = inf_text_buffer_get_slice(buffer, pos, len);
    operation = INF_ADOPTED_OPERATION(
      inf_text_default_delete_operation_new(pos, erased_chunk)
    );

    inf_text_chunk_free(erased_chunk);

    algorithm = inf_adopted_session_get_algorithm(
      INF_ADOPTED_SESSION(session)
    );

    request = inf_adopted_algorithm_generate_request_noexec(
      algorithm,
      INF_ADOPTED_USER(user),
      operation
    );

    inf_adopted_session_broadcast_request(
      INF_ADOPTED_SESSION(session),
      request
    );

    g_object_unref(G_OBJECT(request));
  }
}

static void
inf_text_session_buffer_insert_text_cb_after_foreach_func(InfUser* user,
                                                          gpointer user_data)
{
  InfTextSessionInsertForeachData* data;
  guint position;
  gint length;

  data = (InfTextSessionInsertForeachData*)user_data;
  if(inf_user_get_status(user) != INF_USER_UNAVAILABLE && user != data->user)
  {
    position = inf_text_user_get_caret_position(INF_TEXT_USER(user));
    length = inf_text_user_get_selection_length(INF_TEXT_USER(user));

    inf_text_move_operation_transform_insert(
      data->position,
      inf_text_chunk_get_length(data->chunk),
      &position,
      &length
    );

    inf_text_user_set_selection(INF_TEXT_USER(user), position, length);
  }
}

static void
inf_text_session_buffer_erase_text_cb_after_foreach_func(InfUser* user,
                                                         gpointer user_data)
{
  InfTextSessionEraseForeachData* data;
  guint position;
  gint length;

  data = (InfTextSessionEraseForeachData*)user_data;
  if(inf_user_get_status(user) != INF_USER_UNAVAILABLE && user != data->user)
  {
    position = inf_text_user_get_caret_position(INF_TEXT_USER(user));
    length = inf_text_user_get_selection_length(INF_TEXT_USER(user));

    inf_text_move_operation_transform_delete(
      data->position,
      data->length,
      &position,
      &length
    );

    inf_text_user_set_selection(INF_TEXT_USER(user), position, length);
  }
}

static void
inf_text_session_buffer_insert_text_cb_after(InfTextBuffer* buffer,
                                             guint pos,
                                             InfTextChunk* chunk,
                                             InfUser* user,
                                             gpointer user_data)
{
  InfUserTable* user_table;
  InfTextSessionInsertForeachData data;

  user_table = inf_session_get_user_table(INF_SESSION(user_data));
  data.position = pos;
  data.chunk = chunk;
  data.user = user;

  inf_user_table_foreach_user(
    user_table,
    inf_text_session_buffer_insert_text_cb_after_foreach_func,
    &data
  );

  if(user != NULL)
  {
    inf_text_user_set_selection(
      INF_TEXT_USER(user),
      pos + inf_text_chunk_get_length(chunk),
      0
    );
  }
}

static void
inf_text_session_buffer_erase_text_cb_after(InfTextBuffer* buffer,
                                            guint pos,
                                            guint length,
                                            InfUser* user,
                                            gpointer user_data)
{
  InfUserTable* user_table;
  InfTextSessionEraseForeachData data;

  user_table = inf_session_get_user_table(INF_SESSION(user_data));
  data.position = pos;
  data.length = length;
  data.user = user;

  inf_user_table_foreach_user(
    user_table,
    inf_text_session_buffer_erase_text_cb_after_foreach_func,
    &data
  );

  if(user != NULL)
    inf_text_user_set_selection(INF_TEXT_USER(user), pos, 0);
}

/*
 * GObject overrides.
 */

static void
inf_text_session_init(GTypeInstance* instance,
                      gpointer g_class)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;

  session = INF_TEXT_SESSION(instance);
  priv = INF_TEXT_SESSION_PRIVATE(session);
}

static GObject*
inf_text_session_constructor(GType type,
                             guint n_construct_properties,
                             GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfTextSession* session;
  InfTextSessionPrivate* priv;
  InfTextBuffer* buffer;
  InfSessionStatus status;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));
  g_object_get(G_OBJECT(session), "status", &status, NULL);

  g_assert(
    status != INF_SESSION_SYNCHRONIZING ||
    inf_text_buffer_get_length(buffer) == 0
  );

  g_signal_connect(
    G_OBJECT(buffer),
    "insert-text",
    G_CALLBACK(inf_text_session_buffer_insert_text_cb_before),
    session
  );

  g_signal_connect(
    G_OBJECT(buffer),
    "erase-text",
    G_CALLBACK(inf_text_session_buffer_erase_text_cb_before),
    session
  );

  g_signal_connect(
    G_OBJECT(buffer),
    "insert-text",
    G_CALLBACK(inf_text_session_buffer_insert_text_cb_after),
    session
  );

  g_signal_connect(
    G_OBJECT(buffer),
    "erase-text",
    G_CALLBACK(inf_text_session_buffer_erase_text_cb_after),
    session
  );

  return object;
}

static void
inf_text_session_dispose(GObject* object)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;
  InfTextBuffer* buffer;

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(inf_text_session_buffer_insert_text_cb_before),
    session
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(inf_text_session_buffer_erase_text_cb_before),
    session
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(inf_text_session_buffer_insert_text_cb_after),
    session
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(buffer),
    G_CALLBACK(inf_text_session_buffer_erase_text_cb_after),
    session
  );

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_text_session_finalize(GObject* object)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_text_session_set_property(GObject* object,
                              guint prop_id,
                              const GValue* value,
                              GParamSpec* pspec)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_text_session_get_property(GObject* object,
                              guint prop_id,
                              GValue* value,
                              GParamSpec* pspec)
{
  InfTextSession* session;
  InfTextSessionPrivate* priv;

  session = INF_TEXT_SESSION(object);
  priv = INF_TEXT_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * VFunc implementations.
 */

static void
inf_text_session_to_xml_sync(InfSession* session,
                             xmlNodePtr parent)
{
  InfTextBuffer* buffer;
  InfTextBufferIter* iter;
  xmlNodePtr xml;
  gboolean result;

  gchar* text;
  gsize total_bytes;
  gsize bytes_left;
  GIConv cd;

  g_assert(INF_SESSION_CLASS(parent_class)->to_xml_sync != NULL);
  INF_SESSION_CLASS(parent_class)->to_xml_sync(session, parent);

  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(session));
  cd = g_iconv_open("UTF-8", inf_text_buffer_get_encoding(buffer));

  iter = inf_text_buffer_create_iter(buffer);
  if(iter != NULL)
  {
    result = TRUE;
    while(result == TRUE)
    {
      /* Write segment in 1024 byte chunks */
      text = inf_text_buffer_iter_get_text(buffer, iter);
      total_bytes = inf_text_buffer_iter_get_bytes(buffer, iter);
      bytes_left = total_bytes;

      while(bytes_left > 0)
      {
        xml = xmlNewChild(parent, NULL, (const xmlChar*)"sync-segment", NULL);
        inf_text_session_segment_to_xml(
          &cd,
          xml,
          text + total_bytes - bytes_left,
          &bytes_left,
          inf_text_buffer_iter_get_author(buffer, iter)
        );
      }

      g_free(text);
      result = inf_text_buffer_iter_next(buffer, iter);
    }

    inf_text_buffer_destroy_iter(buffer, iter);
  }

  g_iconv_close(cd);
}

static gboolean
inf_text_session_process_xml_sync(InfSession* session,
                                  InfXmlConnection* connection,
                                  const xmlNodePtr xml,
                                  GError** error)
{
  InfTextBuffer* buffer;
  GIConv cd;

  gpointer text;
  gsize bytes;
  guint length;
  guint author;
  InfUser* user;

  if(strcmp((const char*)xml->name, "sync-segment") == 0)
  {
    buffer = INF_TEXT_BUFFER(inf_session_get_buffer(session));
    cd = g_iconv_open(inf_text_buffer_get_encoding(buffer), "UTF-8");

    text = inf_text_session_segment_from_xml(
      &cd,
      xml,
      &length,
      &bytes,
      &author,
      error
    );

    if(text == NULL) return FALSE;

    if(author != 0)
    {
      /* TODO: inf_user_table_lookup_user_by_id_required, with error. */
      user = inf_user_table_lookup_user_by_id(
        inf_session_get_user_table(session),
        author
      );

      if(user == NULL)
      {
        g_free(text);

        g_set_error(
          error,
          inf_text_session_error_quark,
          INF_TEXT_SESSION_ERROR_NO_SUCH_USER,
          "No such user with ID '%u'",
          author
        );

        return FALSE;
      }
    }
    else
    {
      user = NULL;
    }

    inf_text_buffer_insert_text(
      buffer,
      inf_text_buffer_get_length(buffer),
      text,
      bytes,
      length,
      user
    );

    return TRUE;
  }
  else
  {
    g_assert(INF_SESSION_CLASS(parent_class)->process_xml_sync != NULL);

    return INF_SESSION_CLASS(parent_class)->process_xml_sync(
      session,
      connection,
      xml,
      error
    );
  }
}

static gboolean
inf_text_session_process_xml_run(InfSession* session,
                                 InfXmlConnection* connection,
                                 const xmlNodePtr xml,
                                 GError** error)
{
  g_assert(INF_SESSION_CLASS(parent_class)->process_xml_run != NULL);

  return INF_SESSION_CLASS(parent_class)->process_xml_run(
    session,
    connection,
    xml,
    error
  );
}

static GArray*
inf_text_session_get_xml_user_props(InfSession* session,
                                    InfXmlConnection* connection,
                                    const xmlNodePtr xml)
{
  GArray* array;
  GParameter* parameter;
  guint caret;
  gint selection;

  g_assert(INF_SESSION_CLASS(parent_class)->get_xml_user_props != NULL);

  array = INF_SESSION_CLASS(parent_class)->get_xml_user_props(
    session,
    connection,
    xml
  );

  /* TODO: Error reporting for get_xml_user_props */
  if(inf_xml_util_get_attribute_uint(xml, "caret", &caret, NULL))
  {
    parameter = inf_session_get_user_property(array, "caret-position");
    g_value_init(&parameter->value, G_TYPE_UINT);
    g_value_set_uint(&parameter->value, caret);
  }

  if(inf_xml_util_get_attribute_int(xml, "selection", &selection, NULL))
  {
    parameter = inf_session_get_user_property(array, "selection-length");
    g_value_init(&parameter->value, G_TYPE_INT);
    g_value_set_int(&parameter->value, selection);
  }

  return array;
}

static void
inf_text_session_set_xml_user_props(InfSession* session,
                                    const GParameter* params,
                                    guint n_params,
                                    xmlNodePtr xml)
{
  const GParameter* param;

  INF_SESSION_CLASS(parent_class)->set_xml_user_props(
    session,
    params,
    n_params,
    xml
  );

  param = inf_session_lookup_user_property(
    params,
    n_params,
    "caret-position"
  );

  if(param != NULL)
  {
    inf_xml_util_set_attribute_uint(
      xml,
      "caret",
      g_value_get_uint(&param->value)
    );
  }

  param = inf_session_lookup_user_property(
    params,
    n_params,
    "selection-length"
  );

  if(param != NULL)
  {
    inf_xml_util_set_attribute_int(
      xml,
      "selection",
      g_value_get_int(&param->value)
    );
  }
}

static gboolean
inf_text_session_validate_user_props(InfSession* session,
                                     const GParameter* params,
                                     guint n_params,
                                     InfUser* exclude,
                                     GError** error)
{
  const GParameter* caret;
  gboolean result;

  result = INF_SESSION_CLASS(parent_class)->validate_user_props(
    session,
    params,
    n_params,
    exclude,
    error
  );

  if(result == FALSE) return FALSE;

  caret = inf_session_lookup_user_property(
    params,
    n_params,
    "caret-position"
  );

  if(caret == NULL)
  {
    g_set_error(
      error,
      inf_text_session_error_quark,
      INF_REQUEST_ERROR_NO_SUCH_ATTRIBUTE,
      "'caret' attribute in user message is missing"
    );
  }

  /* Selection is optional and 0 if not given */

  return result;
}

static InfUser*
inf_text_session_user_new(InfSession* session,
                          const GParameter* params,
                          guint n_params)
{
  GObject* object;
  object = g_object_newv(INF_TEXT_TYPE_USER, n_params, (GParameter*)params);
  return INF_USER(object);
}

static xmlNodePtr
inf_text_session_operation_to_xml(InfAdoptedSession* session,
                                  InfAdoptedOperation* operation,
                                  gboolean for_sync)
{
  InfTextChunk* chunk;
  InfTextChunkIter iter;
  gboolean result;
  xmlNodePtr xml;

  gchar* utf8_text;
  gsize bytes_read;
  gsize bytes_written;

  GIConv cd;
  xmlNodePtr child;
  const gchar* text;
  gsize total_bytes;
  gsize bytes_left;

  if(INF_TEXT_IS_INSERT_OPERATION(operation))
  {
    xml = xmlNewNode(NULL, (const xmlChar*)"insert-caret");

    inf_xml_util_set_attribute_uint(
      xml,
      "pos",
      inf_text_insert_operation_get_position(
        INF_TEXT_INSERT_OPERATION(operation)
      )
    );

    /* Must be default insert operation so we get the inserted text */
    g_assert(INF_TEXT_IS_DEFAULT_INSERT_OPERATION(operation));

    chunk = inf_text_default_insert_operation_get_chunk(
      INF_TEXT_DEFAULT_INSERT_OPERATION(operation)
    );

    result = inf_text_chunk_iter_init(chunk, &iter);
    g_assert(result == TRUE);

    utf8_text = g_convert(
      inf_text_chunk_iter_get_text(&iter),
      inf_text_chunk_iter_get_bytes(&iter),
      "UTF-8",
      inf_text_chunk_get_encoding(chunk),
      &bytes_read,
      &bytes_written,
      NULL
    );

    /* Conversion to UTF-8 should always succeed */
    g_assert(utf8_text != NULL);
    g_assert(bytes_read == inf_text_chunk_iter_get_bytes(&iter));

    xmlNodeAddContentLen(xml, (const xmlChar*)utf8_text, bytes_written);
    g_free(utf8_text);

    /* We only allow a single segment because the whole inserted text must
     * be written by a single user. */
    g_assert(inf_text_chunk_iter_next(&iter) == FALSE);
  }
  else if(INF_TEXT_IS_DELETE_OPERATION(operation))
  {
    xml = xmlNewNode(NULL, (const xmlChar*)"delete-caret");

    inf_xml_util_set_attribute_uint(
      xml,
      "pos",
      inf_text_delete_operation_get_position(
        INF_TEXT_DELETE_OPERATION(operation)
      )
    );

    if(for_sync == TRUE)
    {
      /* Must be default delete operation so we get chunk */
      g_assert(INF_TEXT_IS_DEFAULT_DELETE_OPERATION(operation));

      chunk = inf_text_default_delete_operation_get_chunk(
        INF_TEXT_DEFAULT_DELETE_OPERATION(operation)
      );

      /* Need to transmit all deleted data */
      cd = g_iconv_open("UTF-8", inf_text_chunk_get_encoding(chunk));
      result = inf_text_chunk_iter_init(chunk, &iter);

      /* Should delete something, otherwise we could also use noop */
      g_assert(result == TRUE);
      while(result == TRUE)
      {
        text = inf_text_chunk_iter_get_text(&iter);
        total_bytes = inf_text_chunk_iter_get_bytes(&iter);
        bytes_left = total_bytes;
        child = xmlNewChild(xml, NULL, (const xmlChar*)"segment", NULL);

        while(bytes_left > 0)
        {
          inf_text_session_segment_to_xml(
            &cd,
            child,
            text + total_bytes - bytes_left,
            &bytes_left,
            inf_text_chunk_iter_get_author(&iter)
          );
        }

        result = inf_text_chunk_iter_next(&iter);
      }

      g_iconv_close(cd);
    }
    else
    {
      chunk = inf_text_default_delete_operation_get_chunk(
        INF_TEXT_DEFAULT_DELETE_OPERATION(operation)
      );

      /* Just transmit position and length, the other site generates a
       * InfTextRemoteDeleteOperation from that and is able to restore the
       * deleted text for potential Undo. */
      inf_xml_util_set_attribute_uint(
        xml,
        "len",
        inf_text_chunk_get_length(chunk)
      );
    }
  }
  else if(for_sync == FALSE && INF_TEXT_IS_MOVE_OPERATION(operation))
  {
    xml = xmlNewNode(NULL, (const xmlChar*)"move");

    inf_xml_util_set_attribute_uint(
      xml,
      "caret",
      inf_text_move_operation_get_position(INF_TEXT_MOVE_OPERATION(operation))
    );

    inf_xml_util_set_attribute_int(
      xml,
      "selection",
      inf_text_move_operation_get_length(INF_TEXT_MOVE_OPERATION(operation))
    );
  }
  else if(for_sync == FALSE && INF_ADOPTED_IS_NO_OPERATION(operation))
  {
    xml = xmlNewNode(NULL, (const xmlChar*)"no-op");
  }
  else
  {
    g_assert_not_reached();
  }

  return xml;
}

static InfAdoptedOperation*
inf_text_session_xml_to_operation(InfAdoptedSession* session,
                                  InfAdoptedUser* user,
                                  xmlNodePtr xml,
                                  gboolean for_sync,
                                  GError** error)
{
  InfTextBuffer* buffer;
  InfAdoptedOperation* result;

  guint pos;
  gchar* text;
  gsize bytes;
  guint length;
  InfTextChunk* chunk;

  xmlChar* utf8_text;

  xmlNodePtr child;
  GIConv cd;
  guint author;
  gboolean cmp;

  gint selection;

  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));

  if(strcmp((const char*)xml->name, "insert") == 0 ||
     strcmp((const char*)xml->name, "insert-caret") == 0)
  {
    if(!inf_xml_util_get_attribute_uint_required(xml, "pos", &pos, error))
      return NULL;

    /* TODO: Use XML_GET_CONTENT to avoid copy? */
    /* TODO: Can we find out character and byte count in one pass?
     * g_convert() calls strlen. */
    utf8_text = xmlNodeGetContent(xml);
    length = g_utf8_strlen((const gchar*)utf8_text, -1);

    text = g_convert(
      (const gchar*)utf8_text,
      -1,
      inf_text_buffer_get_encoding(buffer),
      "UTF-8",
      NULL,
      &bytes,
      error
    );

    xmlFree(utf8_text);
    if(text == NULL) return NULL;

    chunk = inf_text_chunk_new(inf_text_buffer_get_encoding(buffer));

    inf_text_chunk_insert_text(
      chunk,
      0,
      text,
      bytes,
      length,
      (user == NULL) ? 0 : inf_user_get_id(INF_USER(user))
    );

    g_free(text);

    result = INF_ADOPTED_OPERATION(
      inf_text_default_insert_operation_new(pos, chunk)
    );

    inf_text_chunk_free(chunk);
    return result;
  }
  else if(strcmp((const char*)xml->name, "delete") == 0 ||
          strcmp((const char*)xml->name, "delete-caret") == 0)
  {
    if(!inf_xml_util_get_attribute_uint_required(xml, "pos", &pos, error))
      return NULL;

    if(for_sync == TRUE)
    {
      chunk = inf_text_chunk_new(inf_text_buffer_get_encoding(buffer));
      cd = g_iconv_open(inf_text_buffer_get_encoding(buffer), "UTF-8");
      g_assert(cd != (GIConv)(-1));

      for(child = xml->children; child != NULL; child = child->next)
      {
        if(strcmp((const char*)child->name, "segment") == 0)
        {
          text = inf_text_session_segment_from_xml(
            &cd,
            child,
            &length,
            &bytes,
            &author,
            error
          );

          if(text == NULL)
          {
            inf_text_chunk_free(chunk);
            g_iconv_close(cd);
            return NULL;
          }
          else
          {
            inf_text_chunk_insert_text(
              chunk,
              inf_text_chunk_get_length(chunk),
              text,
              bytes,
              length,
              author
            );

            g_free(text);
          }
        }
        else
        {
          /* TODO: Error */
        }
      }

      g_iconv_close(cd);

      result = INF_ADOPTED_OPERATION(
        inf_text_default_delete_operation_new(pos, chunk)
      );

      inf_text_chunk_free(chunk);
      return result;
    }
    else
    {
      cmp = inf_xml_util_get_attribute_uint_required(
        xml,
        "len",
        &length,
        error
      );

      if(cmp == FALSE) return NULL;

      return INF_ADOPTED_OPERATION(
        inf_text_remote_delete_operation_new(pos, length)
      );
    }
  }
  else if(strcmp((const char*)xml->name, "move") == 0)
  {
    cmp = inf_xml_util_get_attribute_uint_required(xml, "caret", &pos, error);
    if(cmp == FALSE) return NULL;

    cmp = inf_xml_util_get_attribute_int_required(
      xml,
      "selection",
      &selection,
      error
    );

    if(cmp == FALSE) return NULL;

    return INF_ADOPTED_OPERATION(inf_text_move_operation_new(pos, selection));
  }
  else if(strcmp((const char*)xml->name, "no-op") == 0)
  {
    return INF_ADOPTED_OPERATION(inf_adopted_no_operation_new());
  }
  else
  {
    /* TODO: Error */
    return NULL;
  }
}

/*
 * Gype registration.
 */

static void
inf_text_session_class_init(gpointer g_class,
                            gpointer class_data)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;
  InfAdoptedSessionClass* adopted_session_class;

  object_class = G_OBJECT_CLASS(g_class);
  session_class = INF_SESSION_CLASS(g_class);
  adopted_session_class = INF_ADOPTED_SESSION_CLASS(g_class);

  parent_class = INF_ADOPTED_SESSION_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfTextSessionPrivate));

  object_class->constructor = inf_text_session_constructor;
  object_class->dispose = inf_text_session_dispose;
  object_class->finalize = inf_text_session_finalize;
  object_class->set_property = inf_text_session_set_property;
  object_class->get_property = inf_text_session_get_property;

  session_class->to_xml_sync = inf_text_session_to_xml_sync;
  session_class->process_xml_sync = inf_text_session_process_xml_sync;
  session_class->process_xml_run = inf_text_session_process_xml_run;
  session_class->get_xml_user_props = inf_text_session_get_xml_user_props;
  session_class->set_xml_user_props = inf_text_session_set_xml_user_props;
  session_class->validate_user_props = inf_text_session_validate_user_props;
  session_class->user_new = inf_text_session_user_new;

  adopted_session_class->xml_to_operation = inf_text_session_xml_to_operation;
  adopted_session_class->operation_to_xml = inf_text_session_operation_to_xml;

  inf_text_session_error_quark = g_quark_from_static_string(
    "INF_TEXT_SESSION_ERROR"
  );
}

GType
inf_text_session_get_type(void)
{
  static GType session_type = 0;

  if(!session_type)
  {
    static const GTypeInfo session_type_info = {
      sizeof(InfTextSessionClass),   /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      inf_text_session_class_init,   /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfTextSession),        /* instance_size */
      0,                             /* n_preallocs */
      inf_text_session_init,         /* instance_init */
      NULL                           /* value_table */
    };

    session_type = g_type_register_static(
      INF_ADOPTED_TYPE_SESSION,
      "InfTextSession",
      &session_type_info,
      0
    );
  }

  return session_type;
}

/*
 * Public API.
 */

/** inf_text_session_new:
 *
 * @manager: A #InfConnectionManager.
 * @buffer: An initial #InfTextBuffer.
 * @io: A #InfIo object.
 * @sync_group: A group in which the session is synchronized, or %NULL.
 * @sync_connection: A connection to synchronize the session from. Ignored if
 * @sync_group is %NULL.
 *
 * Creates a new #InfTextSession. The connection manager is used to send and
 * receive requests from subscription and synchronization. @buffer will be
 * set to be initially empty if the session is initially synchronized
 * (see below). @io is required to trigger timeouts.
 *
 * If @sync_group is not %NULL, the session will initially be sychronized,
 * meaning the initial content is retrieved from @sync_connection. If you are
 * subscribed to the session, set the subscription group via
 * inf_session_set_subscription_group().
 *
 * Return Value: A new #InfTextSession.
 **/
InfTextSession*
inf_text_session_new(InfConnectionManager* manager,
                     InfTextBuffer* buffer,
                     InfIo* io,
                     InfConnectionManagerGroup* sync_group,
                     InfXmlConnection* sync_connection)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(INF_TEXT_IS_BUFFER(buffer), NULL);
  g_return_val_if_fail(INF_IS_IO(io), NULL);

  g_return_val_if_fail(
    sync_group == NULL || INF_IS_XML_CONNECTION(sync_connection),
    NULL
  );

  object = g_object_new(
    INF_TEXT_TYPE_SESSION,
    "connection-manager", manager,
    "buffer", buffer,
    "sync-group", sync_group,
    "sync-connection", sync_connection,
    "io", io,
    NULL
  );

  return INF_TEXT_SESSION(object);
}

/** inf_text_session_new_with_user_table:
 *
 * @manager: A #InfConnectionManager.
 * @buffer: An initial #InfTextBuffer.
 * @io: A #InfIo object.
 * @user_table: A #InfUserTable.
 * @sync_group: A group in which the session is synchronized, or %NULL.
 * @sync_connection: A connection to synchronize the session from. Ignored if
 * @sync_group is %NULL.
 *
 * Creates a new #InfTextSession. The connection manager is used to send and
 * receive requests from subscription and synchronization. @buffer will be
 * set to be initially empty if the session is initially synchronized
 * (see below). @io is required to trigger timeouts.
 *
 * If @sync_group is not %NULL, the session will initially be sychronized,
 * meaning the initial content is retrieved from @sync_connection. If you are
 * subscribed to the session, set the subscription group via
 * inf_session_set_subscription_group().
 *
 * @user_table is used as an initial user table. The user table should only
 * contain unavailable users, if any, that may rejoin during the session. If
 * there was an available user in the user table, it would probably belong
 * to another session, but different sessions cannot share the same user
 * object.
 *
 * Return Value: A new #InfTextSession.
 **/
InfTextSession*
inf_text_session_new_with_user_table(InfConnectionManager* manager,
                                     InfTextBuffer* buffer,
                                     InfIo* io,
                                     InfUserTable* user_table,
                                     InfConnectionManagerGroup* sync_group,
                                     InfXmlConnection* sync_connection)
{
  GObject* object;

  g_return_val_if_fail(INF_IS_CONNECTION_MANAGER(manager), NULL);
  g_return_val_if_fail(INF_TEXT_IS_BUFFER(buffer), NULL);
  g_return_val_if_fail(INF_IS_IO(io), NULL);
  g_return_val_if_fail(INF_IS_USER_TABLE(user_table), NULL);

  g_return_val_if_fail(
    sync_group == NULL || INF_IS_XML_CONNECTION(sync_connection),
    NULL
  );

  object = g_object_new(
    INF_TEXT_TYPE_SESSION,
    "connection-manager", manager,
    "buffer", buffer,
    "user-table", user_table,
    "sync-group", sync_group,
    "sync-connection", sync_connection,
    "io", io,
    NULL
  );

  return INF_TEXT_SESSION(object);
}

/* vim:set et sw=2 ts=2: */
