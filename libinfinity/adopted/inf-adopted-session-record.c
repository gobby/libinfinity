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

#include <libinfinity/adopted/inf-adopted-session-record.h>
#include <libinfinity/common/inf-xml-util.h>

#include <libxml/xmlwriter.h>
#include <errno.h>

/* TODO: Better error handling; we should have a proper InfErrnoError
 * (or InfSystemError or something), and we should check the fflush error
 * codes. */
/* TODO: Or just use GIOChannel here... */

/**
 * SECTION:inf-adopted-session-record
 * @title: InfAdoptedSessionRecord
 * @short_description: Create a record of a session
 * @include: libinfinity/adopted/inf-adopted-session-record.h
 * @see_also: #InfAdoptedSession
 * @stability: Unstable
 *
 * #InfAdoptedSessionRecord creates a record of a #InfAdoptedSession. It
 * records every modification made to the session from the beginning of its
 * own lifetime to the end of its lifetime.
 *
 * It does not record user status changes and thus the recorded session
 * cannot reliably be replayed with all user information. It's main purpose is
 * to make it easy to reproduce bugs in libinfinity. However, it might be
 * extended in the future.
 *
 * To replay a record, use the tool inf-test-text-replay in the infinote
 * test suite.
 */

/* TODO: Record user join/leave events, and update last send vectors on
 * rejoin. */

typedef struct _InfAdoptedSessionRecordPrivate InfAdoptedSessionRecordPrivate;
struct _InfAdoptedSessionRecordPrivate {
  InfAdoptedSession* session;
  xmlTextWriterPtr writer;
  FILE* file;
  gchar* filename;

  GHashTable* last_send_table;
};

enum {
  PROP_0,

  /* construct only */
  PROP_SESSION
};

#define INF_ADOPTED_SESSION_RECORD_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_SESSION_RECORD, InfAdoptedSessionRecordPrivate))

static GObjectClass* parent_class;
static GQuark libxml2_writer_error_quark;

static void
inf_adopted_session_record_handle_xml_error(InfAdoptedSessionRecord* record)
{
  InfAdoptedSessionRecordPrivate* priv;
  xmlErrorPtr xmlerror;

  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);
  xmlerror = xmlGetLastError();

  g_warning(
    "Error writing record '%s': %s",
    priv->filename,
    xmlerror->message
  );
}

static void
inf_adopted_session_record_write_node(InfAdoptedSessionRecord* record,
                                      xmlNodePtr xml)
{
  InfAdoptedSessionRecordPrivate* priv;
  xmlAttrPtr attr;
  xmlChar* value;
  xmlNodePtr child;
  int result;

  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);

  result = xmlTextWriterStartElement(priv->writer, xml->name);
  if(result < 0) inf_adopted_session_record_handle_xml_error(record);

  for(attr = xml->properties; attr != NULL; attr = attr->next)
  {
    value = xmlGetProp(xml, attr->name);
    result = xmlTextWriterWriteAttribute(priv->writer, attr->name, value);
    if(result < 0) inf_adopted_session_record_handle_xml_error(record);
    xmlFree(value);
  }

  for(child = xml->children; child != NULL; child = child->next)
  {
    if(child->type == XML_ELEMENT_NODE)
    {
      inf_adopted_session_record_write_node(record, child);
    }
    else if(child->type == XML_TEXT_NODE)
    {
      value = xmlNodeGetContent(child);
      result = xmlTextWriterWriteString(priv->writer, value);
      if(result < 0) inf_adopted_session_record_handle_xml_error(record);
      xmlFree(value);
    }
  }

  result = xmlTextWriterEndElement(priv->writer);
  if(result < 0) inf_adopted_session_record_handle_xml_error(record);
}

static void
inf_adopted_session_record_user_joined(InfAdoptedSessionRecord* record,
                                       InfAdoptedUser* user)
{
  InfAdoptedSessionRecordPrivate* priv;
  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);

  g_hash_table_insert(
    priv->last_send_table,
    user,
    inf_adopted_state_vector_copy(inf_adopted_user_get_vector(user))
  );
}

static void
inf_adopted_session_record_execute_request_cb(InfAdoptedAlgorithm* algorithm,
                                              InfAdoptedUser* user,
                                              InfAdoptedRequest* request,
                                              gboolean apply,
                                              gpointer user_data)
{
  InfAdoptedSessionRecord* record;
  InfAdoptedSessionRecordPrivate* priv;
  InfAdoptedSessionClass* session_class;
  InfAdoptedStateVector* previous;
  xmlNodePtr xml;
  int result;

  record = INF_ADOPTED_SESSION_RECORD(user_data);
  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);
  session_class = INF_ADOPTED_SESSION_GET_CLASS(priv->session);

  xml = xmlNewNode(NULL, (const xmlChar*)"request");
  previous = g_hash_table_lookup(priv->last_send_table, user);
  g_assert(previous != NULL);

  session_class->request_to_xml(priv->session, xml, request, previous, FALSE);
  inf_adopted_session_record_write_node(record, xml);
  xmlFreeNode(xml);

  result = xmlTextWriterFlush(priv->writer);
  if(result < 0) inf_adopted_session_record_handle_xml_error(record);
  fflush(priv->file);

  /* Update last send entry */
  previous =
    inf_adopted_state_vector_copy(inf_adopted_request_get_vector(request));
  if(inf_adopted_request_affects_buffer(request))
    inf_adopted_state_vector_add(previous, inf_user_get_id(INF_USER(user)), 1);
  g_hash_table_insert(priv->last_send_table, user, previous);
}

static void
inf_adopted_session_record_add_user_cb(InfUserTable* user_table,
                                       InfUser* user,
                                       gpointer user_data)
{
  InfAdoptedSessionRecord* record;
  InfAdoptedSessionRecordPrivate* priv;
  xmlNodePtr xml;
  int result;

  record = INF_ADOPTED_SESSION_RECORD(user_data);
  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);

  inf_adopted_session_record_user_joined(record, INF_ADOPTED_USER(user));

  result = xmlTextWriterWriteString(priv->writer, (const xmlChar*)"\n  ");
  if(result < 0) inf_adopted_session_record_handle_xml_error(record);

  xml = xmlNewNode(NULL, (const xmlChar*)"user");
  inf_session_user_to_xml(INF_SESSION(priv->session), user, xml);
  inf_adopted_session_record_write_node(record, xml);
  xmlFreeNode(xml);

  result = xmlTextWriterFlush(priv->writer);
  if(result < 0) inf_adopted_session_record_handle_xml_error(record);
  fflush(priv->file);
}

static void
inf_adopted_session_record_start_foreach_user_func(InfUser* user,
                                                   gpointer user_data)
{
  inf_adopted_session_record_user_joined(
    INF_ADOPTED_SESSION_RECORD(user_data),
    INF_ADOPTED_USER(user)
  );
}

static void
inf_adopted_session_record_real_start(InfAdoptedSessionRecord* record)
{
  InfAdoptedSessionRecordPrivate* priv;
  InfAdoptedAlgorithm* algorithm;
  InfUserTable* user_table;
  xmlNodePtr xml;
  xmlNodePtr child;
  xmlNodePtr cur;
  int result;
  guint total;

  InfSessionClass* session_class;

  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);
  algorithm = inf_adopted_session_get_algorithm(priv->session);
  user_table = inf_session_get_user_table(INF_SESSION(priv->session));
  session_class = INF_SESSION_GET_CLASS(priv->session);

  g_signal_connect(
    G_OBJECT(algorithm),
    "execute-request",
    G_CALLBACK(inf_adopted_session_record_execute_request_cb),
    record
  );

  g_signal_connect(
    G_OBJECT(user_table),
    "add-user",
    G_CALLBACK(inf_adopted_session_record_add_user_cb),
    record
  );

  priv->last_send_table = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)inf_adopted_state_vector_free
  );

  inf_user_table_foreach_user(
    inf_session_get_user_table(INF_SESSION(priv->session)),
    inf_adopted_session_record_start_foreach_user_func,
    record
  );

  result = xmlTextWriterStartDocument(priv->writer, NULL, "UTF-8", NULL);
  if(result < 0) inf_adopted_session_record_handle_xml_error(record);

  result = xmlTextWriterStartElement(
    priv->writer,
    (const xmlChar*)"infinote-adopted-session-record"
  );
  if(result < 0) inf_adopted_session_record_handle_xml_error(record);

  /* TODO: Have someone else inserting sync-begin and sync-end... that's quite
   * hacky here. */
  xml = xmlNewNode(NULL, (const xmlChar*)"initial");
  child = xmlNewChild(xml, NULL, (const xmlChar*)"sync-begin", NULL);
  session_class->to_xml_sync(INF_SESSION(priv->session), xml);
  xmlNewChild(xml, NULL, (const xmlChar*)"sync-end", NULL);

  total = 0;
  for(cur = child; cur != NULL; cur = cur->next)
    ++ total;
  inf_xml_util_set_attribute_uint(child, "num-messages", total - 2);

  inf_adopted_session_record_write_node(record, xml);
  xmlFreeNode(xml);

  result = xmlTextWriterFlush(priv->writer);
  if(result < 0) inf_adopted_session_record_handle_xml_error(record);
  fflush(priv->file);
}

static void
inf_adopted_session_record_synchronization_complete_cb(InfSession* session,
                                                       InfXmlConnection* conn,
                                                       gpointer user_data)
{
  InfAdoptedSessionRecord* record;
  record = INF_ADOPTED_SESSION_RECORD(user_data);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(session),
    G_CALLBACK(inf_adopted_session_record_synchronization_complete_cb),
    record
  );

  inf_adopted_session_record_real_start(record);
}

/*
 * GObject overrides.
 */

static void
inf_adopted_session_record_init(GTypeInstance* instance,
                                gpointer g_class)
{
  InfAdoptedSessionRecord* record;
  InfAdoptedSessionRecordPrivate* priv;

  record = INF_ADOPTED_SESSION_RECORD(instance);
  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);

  priv->session = NULL;
  priv->writer = NULL;
  priv->file = NULL;
  priv->filename = NULL;
  priv->last_send_table = NULL;
}

static void
inf_adopted_session_record_dispose(GObject* object)
{
  InfAdoptedSessionRecord* record;
  InfAdoptedSessionRecordPrivate* priv;
  GError* error;

  record = INF_ADOPTED_SESSION_RECORD(object);
  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);

  if(priv->writer != NULL)
  {
    error = NULL;
    inf_adopted_session_record_stop_recording(record, &error);
    if(error != NULL)
    {
      g_assert(priv->filename != NULL);

      g_warning(
        "Error while finishing record '%s': %s",
        priv->filename,
        error->message
      );

      g_error_free(error);
    }
  }

  if(priv->session != NULL)
  {
    g_object_unref(priv->session);
    priv->session = NULL;
  }

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_adopted_session_record_finalize(GObject* object)
{
  InfAdoptedSessionRecord* record;
  InfAdoptedSessionRecordPrivate* priv;

  record = INF_ADOPTED_SESSION_RECORD(object);
  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);

  g_assert(priv->filename == NULL);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_adopted_session_record_set_property(GObject* object,
                                        guint prop_id,
                                        const GValue* value,
                                        GParamSpec* pspec)
{
  InfAdoptedSessionRecord* record;
  InfAdoptedSessionRecordPrivate* priv;

  record = INF_ADOPTED_SESSION_RECORD(object);
  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);

  switch(prop_id)
  {
  case PROP_SESSION:
    g_assert(priv->session == NULL); /* construct only */
    priv->session = INF_ADOPTED_SESSION(g_value_dup_object(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(value, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_session_record_get_property(GObject* object,
                                        guint prop_id,
                                        GValue* value,
                                        GParamSpec* pspec)
{
  InfAdoptedSessionRecord* record;
  InfAdoptedSessionRecordPrivate* priv;

  record = INF_ADOPTED_SESSION_RECORD(object);
  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);

  switch(prop_id)
  {
  case PROP_SESSION:
    g_value_set_object(value, G_OBJECT(priv->session));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * Gype registration.
 */

static void
inf_adopted_session_record_class_init(gpointer g_class,
                                      gpointer class_data)
{
  GObjectClass* object_class;

  object_class = G_OBJECT_CLASS(g_class);
  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAdoptedSessionRecordPrivate));

  object_class->dispose = inf_adopted_session_record_dispose;
  object_class->finalize = inf_adopted_session_record_finalize;
  object_class->set_property = inf_adopted_session_record_set_property;
  object_class->get_property = inf_adopted_session_record_get_property;

  libxml2_writer_error_quark =
    g_quark_from_static_string("LIBXML2_WRITER_ERROR");

  g_object_class_install_property(
    object_class,
    PROP_SESSION,
    g_param_spec_object(
      "session",
      "Session",
      "The session to record",
      INF_ADOPTED_TYPE_SESSION,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

GType
inf_adopted_session_record_get_type(void)
{
  static GType session_record_type = 0;

  if(!session_record_type)
  {
    static const GTypeInfo session_record_type_info = {
      sizeof(InfAdoptedSessionRecordClass),   /* class_size */
      NULL,                                   /* base_init */
      NULL,                                   /* base_finalize */
      inf_adopted_session_record_class_init,  /* class_init */
      NULL,                                   /* class_finalize */
      NULL,                                   /* class_data */
      sizeof(InfAdoptedSessionRecord),        /* instance_size */
      0,                                      /* n_preallocs */
      inf_adopted_session_record_init,        /* instance_init */
      NULL                                    /* value_table */
    };

    session_record_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfAdoptedSessionRecord",
      &session_record_type_info,
      0
    );
  }

  return session_record_type;
}

/*
 * Public API.
 */

/**
 * inf_adopted_session_record_new:
 * @session: A #InfAdoptedSession.
 *
 * Creates a new #InfAdoptedSessionRecord, recording @session. To start
 * recording, call inf_adopted_session_record_start_recording().
 *
 * Return Value: A new #InfAdoptedSessionRecord.
 **/
InfAdoptedSessionRecord*
inf_adopted_session_record_new(InfAdoptedSession* session)
{
  GObject* object;

  g_return_val_if_fail(INF_ADOPTED_IS_SESSION(session), NULL);

  object = g_object_new(
    INF_ADOPTED_TYPE_SESSION_RECORD,
    "session", session,
    NULL
  );

  return INF_ADOPTED_SESSION_RECORD(object);
}

/**
 * inf_adopted_session_record_start_recording:
 * @record: A #InfAdoptedSessionRecord.
 * @filename: The file in which to store the record.
 * @error: Location to store error information, if any.
 *
 * Starts to record the session. Make sure the session is not already closed
 * before calling this function. If an error occurs, such as if @filename
 * could not be opened, then the function returns %FALSE and @error is set.
 *
 * Return Value: %TRUE if the session is started to be recorded, %FALSE on
 * error.
 **/
gboolean
inf_adopted_session_record_start_recording(InfAdoptedSessionRecord* record,
                                           const gchar* filename,
                                           GError** error)
{
  InfAdoptedSessionRecordPrivate* priv;
  InfSessionStatus status;
  xmlOutputBufferPtr buffer;
  xmlErrorPtr xmlerror;
  gchar* uri;
  int errcode;

  g_return_val_if_fail(INF_ADOPTED_IS_SESSION_RECORD(record), FALSE);
  g_return_val_if_fail(filename != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);
  status = inf_session_get_status(INF_SESSION(priv->session));

  g_return_val_if_fail(priv->writer == NULL, FALSE);
  g_return_val_if_fail(status != INF_SESSION_CLOSED, FALSE);

  priv->file = fopen(filename, "w");
  if(priv->file == NULL)
  {
    errcode = errno;

    g_set_error(
      error,
      g_quark_from_static_string("ERRNO_ERROR"),
      errcode,
      "%s",
      strerror(errcode)
    );

    return FALSE;
  }

  buffer = xmlOutputBufferCreateFile(priv->file, NULL);
  if(buffer == NULL)
  {
    fclose(priv->file);
    priv->file = NULL;

    xmlerror = xmlGetLastError();

    g_set_error(
      error,
      libxml2_writer_error_quark,
      xmlerror->code,
      "%s",
      xmlerror->message
    );

    return FALSE;
  }

  priv->writer = xmlNewTextWriter(buffer);
  if(priv->writer == NULL)
  {
    /* TODO: Does this also fclose our file? */
    xmlOutputBufferClose(buffer);
    priv->file = NULL;

    xmlerror = xmlGetLastError();

    g_set_error(
      error,
      libxml2_writer_error_quark,
      xmlerror->code,
      "%s",
      xmlerror->message
    );

    return FALSE;
  }

  xmlTextWriterSetIndent(priv->writer, 1);

  switch(status)
  {
  case INF_SESSION_SYNCHRONIZING:
    g_signal_connect_after(
      G_OBJECT(priv->session),
      "synchronization-complete",
      G_CALLBACK(inf_adopted_session_record_synchronization_complete_cb),
      record
    );

    break;
  case INF_SESSION_RUNNING:
    inf_adopted_session_record_real_start(record);
    break;
  default:
    g_assert_not_reached();
    break;
  }

  g_assert(priv->filename == NULL);
  priv->filename = g_strdup(filename);
  return TRUE;
}

/**
 * inf_adopted_session_record_stop_recording:
 * @record: A #InfAdoptedSessionRecord.
 * @error: Location to store error information, if any.
 *
 * Stops the recording of the current session, which must have been started
 * previously via inf_adopted_session_record_start_recording(). If an error
 * occurs, then the function returns %FALSE and @error is set. Note that even
 * if an error occurs, then the recording is stopped as well. However, the
 * file might not have been completely written to disk, so you should still
 * show any errors during this function to the user.
 *
 * Return Value: %TRUE if the recording has been stored successfully, %FALSE
 * otherwise.
 */
gboolean
inf_adopted_session_record_stop_recording(InfAdoptedSessionRecord* record,
                                          GError** error)
{
  InfAdoptedSessionRecordPrivate* priv;
  InfAdoptedAlgorithm* algorithm;
  InfUserTable* user_table;
  xmlErrorPtr xmlerror;
  int result;

  g_return_val_if_fail(INF_ADOPTED_IS_SESSION_RECORD(record), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INF_ADOPTED_SESSION_RECORD_PRIVATE(record);
  algorithm = inf_adopted_session_get_algorithm(priv->session);
  user_table = inf_session_get_user_table(INF_SESSION(priv->session));

  g_return_val_if_fail(priv->writer != NULL, FALSE);

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(priv->session),
    G_CALLBACK(inf_adopted_session_record_synchronization_complete_cb),
    record
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(algorithm),
    G_CALLBACK(inf_adopted_session_record_execute_request_cb),
    record
  );

  g_signal_handlers_disconnect_by_func(
    G_OBJECT(algorithm),
    G_CALLBACK(inf_adopted_session_record_add_user_cb),
    record
  );

  result = xmlTextWriterWriteString(priv->writer, (const xmlChar*)"\n");
  if(result < 0) inf_adopted_session_record_handle_xml_error(record);

  result = xmlTextWriterEndDocument(priv->writer);
  if(result < 0)
  {
    xmlerror = xmlGetLastError();

    g_set_error(
      error,
      libxml2_writer_error_quark,
      xmlerror->code,
      "%s",
      xmlerror->message
    );

    return FALSE;
  }

  /* TODO: Does this fclose our file? */
  xmlFreeTextWriter(priv->writer);
  priv->writer = NULL;
  priv->file = NULL;

  g_free(priv->filename);
  priv->filename = NULL;

  g_hash_table_unref(priv->last_send_table);
  priv->last_send_table = NULL;

  return result >= 0;
}

/**
 * inf_adopted_session_record_is_recording:
 * @record: A #InfAdoptedSessionRecord.
 *
 * Returns whether @record is currently recording a session.
 *
 * Returns: Whether @record currently records the session.
 */
gboolean
inf_adopted_session_record_is_recording(InfAdoptedSessionRecord* record)
{
  g_return_val_if_fail(INF_ADOPTED_IS_SESSION_RECORD(record), FALSE);
  return INF_ADOPTED_SESSION_RECORD_PRIVATE(record)->writer != NULL;
}

/* vim:set et sw=2 ts=2: */
