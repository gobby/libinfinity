/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/adopted/inf-adopted-session-replay.h>
#include <libinfinity/common/inf-simulated-connection.h>
#include <libinfinity/common/inf-standalone-io.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/inf-i18n.h>

#include <libxml/xmlreader.h>

#include <string.h>

/**
 * SECTION:inf-adopted-session-replay
 * @title: InfAdoptedSessionReplay
 * @short_description: Replay a record of a session
 * @include: libinfinity/adopted/inf-adopted-session-replay.h
 * @see_also: #InfAdoptedSession, #InfAdoptedSessionRecord
 * @stability: Unstable
 *
 * #InfAdoptedSessionReplay can be used to replay a record created with
 * #InfAdoptedSessionRecord. <!-- TODO: Enable as soon as we have
 * InfAdoptedSessionTimline: It can be used together with
 * #InfAdoptedSessionTimeline to be allowed to also go backwards in time. -->
 *
 * Use inf_adopted_session_replay_set_record() to specify the recording to
 * replay, and then use inf_adopted_session_replay_get_session() to obtain
 * the replayed session.
 */

/* cf.
 * http://www.gnu.org/software/dotgnu/pnetlib-doc/System/Xml/XmlNodeType.html
 */
#define XML_READER_TYPE_NONE 0
#define XML_READER_TYPE_ELEMENT 1
#define XML_READER_TYPE_SIGNIFICANT_WHITESPACE 14
#define XML_READER_TYPE_END_ELEMENT 15

typedef struct _InfAdoptedSessionReplayPrivate InfAdoptedSessionReplayPrivate;
struct _InfAdoptedSessionReplayPrivate {
  gchar* filename;
  xmlTextReaderPtr reader;
  GError* error;

  InfCommunicationManager* publisher_manager;
  InfCommunicationHostedGroup* publisher_group;
  InfSimulatedConnection* publisher_conn;

  InfCommunicationManager* client_manager;
  InfCommunicationJoinedGroup* client_group;
  InfSimulatedConnection* client_conn;

  InfAdoptedSession* session;
};

enum {
  PROP_0,

  PROP_FILENAME,
  PROP_SESSION
};

#define INF_ADOPTED_SESSION_REPLAY_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_SESSION_REPLAY, InfAdoptedSessionReplayPrivate))

static GObjectClass* parent_class;
static GQuark session_replay_error_quark;

static xmlNodePtr
inf_adopted_session_replay_read_current(xmlTextReaderPtr reader,
                                        GError** error)
{
  xmlErrorPtr xml_error;
  xmlNodePtr cur;

  cur = xmlTextReaderExpand(reader);
  if(!cur)
  {
    xml_error = xmlGetLastError();

    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_XML,
      "%s",
      xml_error->message
    );

    return NULL;
  }

  return cur;
}

static gboolean
inf_adopted_session_replay_handle_advance_result(int result,
                                                 GError** error)
{
  xmlErrorPtr xml_error;

  switch(result)
  {
  case -1:
    xml_error = xmlGetLastError();

    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_XML,
      "%s",
      xml_error->message
    );

    return FALSE;
  case 0:
  case 1:
    return TRUE;
  default:
    g_assert_not_reached();
    return FALSE;
  }
}

static gboolean
inf_adopted_session_replay_handle_advance_required_result(int result,
                                                          GError** error)
{
  xmlErrorPtr xml_error;

  switch(result)
  {
  case -1:
    xml_error = xmlGetLastError();

    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_XML,
      "%s",
      xml_error->message
    );

    return FALSE;
  case 0:
    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_UNEXPECTED_EOF,
      "%s",
      _("Unexpected end of recording")
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
inf_adopted_session_replay_advance(xmlTextReaderPtr reader,
                                   GError** error)
{
  int result;
  result = xmlTextReaderRead(reader);

  return inf_adopted_session_replay_handle_advance_result(result, error);
}

static gboolean
inf_adopted_session_replay_advance_required(xmlTextReaderPtr reader,
                                            GError** error)
{
  int result;
  result = xmlTextReaderRead(reader);

  return inf_adopted_session_replay_handle_advance_required_result(
    result,
    error
  );
}

static gboolean
inf_adopted_session_replay_advance_subtree_required(xmlTextReaderPtr reader,
                                                    GError** error)
{
  int result;
  result = xmlTextReaderNext(reader);

  return inf_adopted_session_replay_handle_advance_required_result(
    result,
    error
  );
}

static gboolean
inf_adopted_session_replay_skip_whitespace(xmlTextReaderPtr reader,
                                           GError** error)
{
  while(xmlTextReaderNodeType(reader) ==
        XML_READER_TYPE_SIGNIFICANT_WHITESPACE)
  {
    if(inf_adopted_session_replay_advance(reader, error) == FALSE)
      return FALSE;
  }

  return TRUE;
}

static gboolean
inf_adopted_session_replay_skip_whitespace_required(xmlTextReaderPtr reader,
                                                    GError** error)
{
  while(xmlTextReaderNodeType(reader) ==
        XML_READER_TYPE_SIGNIFICANT_WHITESPACE)
  {
    if(inf_adopted_session_replay_advance_required(reader, error) == FALSE)
      return FALSE;
  }

  return TRUE;
}

static void
inf_adopted_session_replay_clear(InfAdoptedSessionReplay* replay)
{
  InfAdoptedSessionReplayPrivate* priv;
  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay);

  g_object_freeze_notify(G_OBJECT(replay));

  if(priv->filename != NULL)
  {
    g_free(priv->filename);
    priv->filename = NULL;

    g_object_notify(G_OBJECT(replay), "filename");
  }

  if(priv->reader != NULL)
  {
    if(xmlTextReaderClose(priv->reader) == -1)
      g_warning("Failed to close XML reader: %s", xmlGetLastError()->message);
    xmlFreeTextReader(priv->reader);
    priv->reader = NULL;
  }

  g_assert(priv->error == NULL);

  if(priv->publisher_group != NULL)
  {
    g_object_unref(priv->publisher_group);
    priv->publisher_group = NULL;
  }

  if(priv->publisher_conn != NULL)
  {
    g_object_unref(priv->publisher_conn);
    priv->publisher_conn = NULL;
  }

  if(priv->publisher_manager != NULL)
  {
    g_object_unref(priv->publisher_manager);
    priv->publisher_manager = NULL;
  }

  if(priv->client_group != NULL)
  {
    g_object_unref(priv->client_group);
    priv->client_group = NULL;
  }

  if(priv->client_conn != NULL)
  {
    g_object_unref(priv->client_conn);
    priv->client_conn = NULL;
  }

  if(priv->client_manager != NULL)
  {
    g_object_unref(priv->client_manager);
    priv->client_manager = NULL;
  }

  if(priv->session != NULL)
  {
    g_object_unref(priv->session);
    priv->session = NULL;

    g_object_notify(G_OBJECT(replay), "session");
  }

  g_object_thaw_notify(G_OBJECT(replay));
}

static void
inf_adopted_session_replay_synchronization_failed_cb(InfSession* session,
                                                     InfXmlConnection* conn,
                                                     GError* error,
                                                     gpointer user_data)
{
  InfAdoptedSessionReplay* replay;
  InfAdoptedSessionReplayPrivate* priv;

  replay = INF_ADOPTED_SESSION_REPLAY(user_data);
  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay);

  g_assert(priv->error == NULL);
  priv->error = g_error_copy(error);
}

static gboolean
inf_adopted_session_replay_play_initial(InfAdoptedSessionReplay* replay,
                                        const InfcNotePlugin* plugin,
                                        GError** error)
{
  InfAdoptedSessionReplayPrivate* priv;
  xmlTextReaderPtr reader;
  xmlNodePtr cur;
  const xmlChar* name;
  xmlChar* value;
  gulong handler;

  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay);
  reader = priv->reader;

  /* Advance to root node */
  if(xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT)
    if(!inf_adopted_session_replay_advance_required(reader, error))
      return FALSE;

  name = xmlTextReaderConstName(reader);
  if(strcmp((const char*)name, "infinote-adopted-session-record") != 0)
  {
    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_DOCUMENT,
      "%s",
      _("Document is not a session recording")
    );

    return FALSE;
  }

  value = xmlTextReaderGetAttribute(reader, (const xmlChar*)"session-type");
  if(value && strcmp((const char*)name, plugin->note_type) != 0)
  {
    xmlFree(value);

    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_SESSION_TYPE,
      "%s",
      _("Session type of the recording does not match")
    );

    return FALSE;
  }

  if(value) xmlFree(value);

  if(!inf_adopted_session_replay_advance_required(reader, error))
    return FALSE;
  if(!inf_adopted_session_replay_skip_whitespace_required(reader, error))
    return FALSE;

  name = xmlTextReaderConstName(reader);
  if(strcmp((const char*)name, "initial") != 0)
  {
    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FORMAT,
      "%s",
      _("Initial session state missing in recording")
    );

    return FALSE;
  }

  if(!inf_adopted_session_replay_advance_required(reader, error))
    return FALSE;
  if(!inf_adopted_session_replay_skip_whitespace_required(reader, error))
    return FALSE;

  handler = g_signal_connect(
    priv->session,
    "synchronization-failed",
    G_CALLBACK(inf_adopted_session_replay_synchronization_failed_cb),
    replay
  );

  while(xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT)
  {
    switch(inf_session_get_status(INF_SESSION(priv->session)))
    {
    case INF_SESSION_CLOSED:
      g_assert_not_reached();
      g_signal_handler_disconnect(priv->session, handler);
      return FALSE;
    case INF_SESSION_SYNCHRONIZING:
      cur = inf_adopted_session_replay_read_current(reader, error);
      if(!cur)
      {
        g_signal_handler_disconnect(priv->session, handler);
        return FALSE;
      }

      inf_communication_group_send_message(
        INF_COMMUNICATION_GROUP(priv->publisher_group),
        INF_XML_CONNECTION(priv->publisher_conn),
        xmlCopyNode(cur, 1)
      );

      /* TODO: Check whether this caused an error. Maybe there should be an
       * error signal for InfCommunicationGroup, delegating
       * inf_net_object_received's error. */
      inf_simulated_connection_flush(priv->publisher_conn);

      /* error can be set if the synchronization failed */
      if(priv->error != NULL)
      {
        g_signal_handler_disconnect(priv->session, handler);
        g_propagate_error(error, priv->error);
        priv->error = NULL;
        return FALSE;
      }

      if(!inf_adopted_session_replay_advance_subtree_required(reader, error))
      {
        g_signal_handler_disconnect(priv->session, handler);
        return FALSE;
      }

      if(!inf_adopted_session_replay_skip_whitespace_required(reader, error))
      {
        g_signal_handler_disconnect(priv->session, handler);
        return FALSE;
      }

      break;
    case INF_SESSION_RUNNING:
      g_signal_handler_disconnect(priv->session, handler);

      g_set_error(
        error,
        session_replay_error_quark,
        INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FORMAT,
        "%s",
        _("Session switched to running without having finished playing "
          "the initial")
      );

      return FALSE;
    case INF_SESSION_PRESYNC:
    default:
      g_assert_not_reached();
      break;
    }
  }

  g_signal_handler_disconnect(priv->session, handler);

  if(xmlTextReaderNodeType(reader) != XML_READER_TYPE_END_ELEMENT)
  {
    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FORMAT,
      "%s",
      _("Superfluous XML in initial session section")
    );

    return FALSE;
  }

  if(inf_session_get_status(INF_SESSION(priv->session)) ==
     INF_SESSION_SYNCHRONIZING)
  {
    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FORMAT,
      "%s",
      _("Session is still in synchronizing state after having "
        "played the initial")
    );

    return FALSE;
  }

  /* Jump over end element */
  if(!inf_adopted_session_replay_advance_required(reader, error))
    return FALSE;

  /* Not "_required"; recording might end right after initial */
  if(!inf_adopted_session_replay_skip_whitespace(reader, error))
    return FALSE;

  return TRUE;
}

/*
 * GObject overrides.
 */

static void
inf_adopted_session_replay_init(GTypeInstance* instance,
                                gpointer g_class)
{
  InfAdoptedSessionReplay* record;
  InfAdoptedSessionReplayPrivate* priv;

  record = INF_ADOPTED_SESSION_REPLAY(instance);
  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(record);

  priv->filename = NULL;
  priv->reader = NULL;
  priv->error = NULL;

  priv->publisher_manager = NULL;
  priv->publisher_group = NULL;
  priv->publisher_conn = NULL;

  priv->client_manager = NULL;
  priv->client_group = NULL;
  priv->client_conn = NULL;

  priv->session = NULL;
}

static void
inf_adopted_session_replay_dispose(GObject* object)
{
  InfAdoptedSessionReplay* replay;
  InfAdoptedSessionReplayPrivate* priv;

  replay = INF_ADOPTED_SESSION_REPLAY(object);
  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay);

  inf_adopted_session_replay_clear(replay);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_adopted_session_replay_finalize(GObject* object)
{
  InfAdoptedSessionReplay* replay;
  InfAdoptedSessionReplayPrivate* priv;

  replay = INF_ADOPTED_SESSION_REPLAY(object);
  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay);

  g_assert(priv->filename == NULL);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_adopted_session_replay_set_property(GObject* object,
                                        guint prop_id,
                                        const GValue* value,
                                        GParamSpec* pspec)
{
  InfAdoptedSessionReplay* replay;
  InfAdoptedSessionReplayPrivate* priv;

  replay = INF_ADOPTED_SESSION_REPLAY(object);
  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay);

  switch(prop_id)
  {
  case PROP_FILENAME:
  case PROP_SESSION:
    /* read only */
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_session_replay_get_property(GObject* object,
                                        guint prop_id,
                                        GValue* value,
                                        GParamSpec* pspec)
{
  InfAdoptedSessionReplay* replay;
  InfAdoptedSessionReplayPrivate* priv;

  replay = INF_ADOPTED_SESSION_REPLAY(object);
  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay);

  switch(prop_id)
  {
  case PROP_FILENAME:
    g_value_set_string(value, priv->filename);
    break;
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
inf_adopted_session_replay_class_init(gpointer g_class,
                                      gpointer class_data)
{
  GObjectClass* object_class;

  object_class = G_OBJECT_CLASS(g_class);
  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAdoptedSessionReplayPrivate));

  object_class->dispose = inf_adopted_session_replay_dispose;
  object_class->finalize = inf_adopted_session_replay_finalize;
  object_class->set_property = inf_adopted_session_replay_set_property;
  object_class->get_property = inf_adopted_session_replay_get_property;

  session_replay_error_quark =
    g_quark_from_static_string("INF_ADOPTED_SESSION_REPLAY_ERROR");

  g_object_class_install_property(
    object_class,
    PROP_FILENAME,
    g_param_spec_string(
      "filename",
      "Filename",
      "The filename of the record to play",
      NULL,
      G_PARAM_READABLE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_SESSION,
    g_param_spec_object(
      "session",
      "Session",
      "The replayed session",
      INF_ADOPTED_TYPE_SESSION,
      G_PARAM_READABLE
    )
  );
}

GType
inf_adopted_session_replay_get_type(void)
{
  static GType session_replay_type = 0;

  if(!session_replay_type)
  {
    static const GTypeInfo session_replay_type_info = {
      sizeof(InfAdoptedSessionReplayClass),   /* class_size */
      NULL,                                   /* base_init */
      NULL,                                   /* base_finalize */
      inf_adopted_session_replay_class_init,  /* class_init */
      NULL,                                   /* class_finalize */
      NULL,                                   /* class_data */
      sizeof(InfAdoptedSessionReplay),        /* instance_size */
      0,                                      /* n_preallocs */
      inf_adopted_session_replay_init,        /* instance_init */
      NULL                                    /* value_table */
    };

    session_replay_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfAdoptedSessionReplay",
      &session_replay_type_info,
      0
    );
  }

  return session_replay_type;
}

/*
 * Public API.
 */

/**
 * inf_adopted_session_replay_new:
 *
 * Creates a new #InfAdoptedSessionReplay. Use
 * inf_adopted_session_replay_set_record() to start the recording, and
 * inf_adopted_session_replay_play_next() or
 * inf_adopted_session_replay_play_to_end() to play it.
 *
 * Return Value: A new #InfAdoptedSessionReplay. Free with g_object_unref()
 * when no longer in use.
 **/
InfAdoptedSessionReplay*
inf_adopted_session_replay_new(void)
{
  GObject* object;
  object = g_object_new(INF_ADOPTED_TYPE_SESSION_REPLAY, NULL);
  return INF_ADOPTED_SESSION_REPLAY(object);
}

/**
 * inf_adopted_session_replay_set_record:
 * @replay: A #InfAdoptedSessionReplay.
 * @filename: Path to the record file to play.
 * @plugin: A #InfcNotePlugin for the note type of the recorded session.
 * @error: Location to store error information, if any.
 *
 * Set the record file for @replay to play. It should have been created with
 * #InfAdoptedSessionRecord. @plugin should match the type of the recorded
 * session. If an error occurs, the function returns %FALSE and @error is set.
 *
 * Returns: %TRUE on success, or %FALSE if the record file could not be set.
 */
gboolean
inf_adopted_session_replay_set_record(InfAdoptedSessionReplay* replay,
                                      const gchar* filename,
                                      const InfcNotePlugin* plugin,
                                      GError** error)
{
  InfAdoptedSessionReplayPrivate* priv;
  xmlTextReaderPtr reader;
  InfIo* io;
  gboolean result;
  xmlErrorPtr xml_error;

  g_return_val_if_fail(INF_ADOPTED_IS_SESSION_REPLAY(replay), FALSE);
  g_return_val_if_fail(filename != NULL, FALSE);
  g_return_val_if_fail(plugin != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay);

  reader = xmlReaderForFile(
    filename,
    NULL,
    XML_PARSE_NOERROR | XML_PARSE_NOWARNING
  );

  if(!reader)
  {
    xml_error = xmlGetLastError();

    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FILE,
      "%s",
      xml_error->message
    );

    return FALSE;
  }

  /* TODO: Keep current staet if playing the initial fails */

  g_object_freeze_notify(G_OBJECT(replay));

  inf_adopted_session_replay_clear(replay);

  priv->filename = g_strdup(filename);
  priv->reader = reader;

  priv->publisher_conn = inf_simulated_connection_new();
  priv->client_conn = inf_simulated_connection_new();
  inf_simulated_connection_connect(priv->publisher_conn, priv->client_conn);

  inf_simulated_connection_set_mode(
    priv->publisher_conn,
    INF_SIMULATED_CONNECTION_DELAYED
  );

  inf_simulated_connection_set_mode(
    priv->client_conn,
    INF_SIMULATED_CONNECTION_DELAYED
  );

  priv->publisher_manager = inf_communication_manager_new();
  priv->publisher_group = inf_communication_manager_open_group(
    priv->publisher_manager,
    "InfAdoptedSessionReplay",
    NULL
  );
  inf_communication_hosted_group_add_member(
    priv->publisher_group,
    INF_XML_CONNECTION(priv->publisher_conn)
  );

  priv->client_manager = inf_communication_manager_new();
  priv->client_group = inf_communication_manager_join_group(
    priv->client_manager,
    "InfAdoptedSessionReplay",
    INF_XML_CONNECTION(priv->client_conn),
    "central"
  );

  /* This is not used anyway, but it needs to be present: */
  io = INF_IO(inf_standalone_io_new());

  priv->session = INF_ADOPTED_SESSION(
    plugin->session_new(
      io,
      priv->client_manager,
      INF_SESSION_SYNCHRONIZING,
      priv->client_group,
      INF_XML_CONNECTION(priv->client_conn),
      plugin->user_data
    )
  );

  g_object_unref(io);

  inf_communication_group_set_target(
    INF_COMMUNICATION_GROUP(priv->client_group),
    INF_COMMUNICATION_OBJECT(priv->session)
  );

  inf_simulated_connection_flush(priv->publisher_conn);
  inf_simulated_connection_flush(priv->client_conn);

  if(!inf_adopted_session_replay_play_initial(replay, plugin, error))
  {
    inf_adopted_session_replay_clear(replay);
    result = FALSE;
  }
  else
  {
    g_object_notify(G_OBJECT(replay), "filename");
    g_object_notify(G_OBJECT(replay), "session");
    result = TRUE;
  }

  g_object_thaw_notify(G_OBJECT(replay));

  return result;
}

/**
 * inf_adopted_session_replay_get_session:
 * @replay: A #InfAdoptedSessionReplay.
 *
 * Returns the played back session, or %NULL if
 * inf_adopted_session_replay_set_record() was not yet called.
 *
 * Returns: A #InfAdoptedSessionReplay, or %NULL.
 */
InfAdoptedSession*
inf_adopted_session_replay_get_session(InfAdoptedSessionReplay* replay)
{
  g_return_val_if_fail(INF_ADOPTED_IS_SESSION_REPLAY(replay), NULL);
  return INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay)->session;
}

/**
 * inf_adopted_session_replay_play_next:
 * @replay: A #InfAdoptedSessionReplay.
 * @error: Location to store error information, if any.
 *
 * Reads the next request from the record and passes it to the session. Note
 * that this might do nothing if that request is not yet causally ready,
 * meaning that it depends on another request that has not yet been played. In
 * that case it will be executed as soon as it is ready, that is after some
 * future inf_adopted_session_replay_play_next() call. Therefore, it is also
 * possible that this function executes more than one request.
 *
 * If an error occurs, then this function returns %FALSE and @error is set.
 * If the end of the recording is reached, then it also returns %FALSE, but
 * @error is left untouched. If the next request has been read, then it
 * returns %TRUE.
 *
 * Returns: %TRUE if a request was read, otherwise %FALSE.
 */
gboolean
inf_adopted_session_replay_play_next(InfAdoptedSessionReplay* replay,
                                     GError** error)
{
  InfAdoptedSessionReplayPrivate* priv;
  xmlTextReaderPtr reader;
  int type;
  xmlNodePtr cur;

  guint id;
  InfUser* user;

  InfSessionClass* session_class;
  GArray* user_props;
  GParameter* param;
  gboolean result;
  guint i;

  g_return_val_if_fail(INF_ADOPTED_IS_SESSION_REPLAY(replay), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  priv = INF_ADOPTED_SESSION_REPLAY_PRIVATE(replay);
  reader = priv->reader;

  type = xmlTextReaderNodeType(reader);
  /* EOF, maybe the writer crashed and could not finish the record properly */
  if(type == XML_READER_TYPE_NONE) return FALSE;
  /* </inf-adopted-session-record> */
  if(type == XML_READER_TYPE_END_ELEMENT) return FALSE;

  if(type != XML_READER_TYPE_ELEMENT)
  {
    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FORMAT,
      "%s",
      _("Superfluous XML in requests section")
    );

    return FALSE;
  }

  cur = inf_adopted_session_replay_read_current(reader, error);
  if(cur == NULL) return FALSE;

  if(strcmp((const char*)cur->name, "request") == 0)
  {
    /* TODO: Add user join/leaves to record.
     * Until that is done, make users available when they issue a request. */
    if(!inf_xml_util_get_attribute_uint_required(cur, "user", &id, error))
      return FALSE;

    user = inf_user_table_lookup_user_by_id(
      inf_session_get_user_table(INF_SESSION(priv->session)),
      id
    );

    if(!user)
    {
      g_set_error(
        error,
        session_replay_error_quark,
        INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FORMAT,
        _("No such user with ID \"%u\""),
        id
      );

      return FALSE;
    }

    if(inf_user_get_status(user) == INF_USER_UNAVAILABLE)
    {
      g_object_set(
        G_OBJECT(user),
        "status", INF_USER_ACTIVE,
        "connection", priv->client_conn,
        NULL
      );
    }

    inf_communication_group_send_group_message(
      INF_COMMUNICATION_GROUP(priv->publisher_group),
      xmlCopyNode(cur, 1)
    );

    /* TODO: Check whether this caused an error. Maybe there should be an
     * error signal for InfCommunicationGroup, delegating
     * inf_net_object_received's error. */
    inf_simulated_connection_flush(priv->publisher_conn);
  }
  else if(strcmp((const char*)cur->name, "user") == 0)
  {
    /* User join */
    session_class = INF_SESSION_GET_CLASS(priv->session);
    user_props = session_class->get_xml_user_props(
      INF_SESSION(priv->session),
      INF_XML_CONNECTION(priv->publisher_conn),
      cur
    );

    param = inf_session_get_user_property(user_props, "connection");
    if(!G_IS_VALUE(&param->value))
    {
      g_value_init(&param->value, INF_TYPE_XML_CONNECTION);
      g_value_set_object(&param->value, G_OBJECT(priv->client_conn));
    }

    result = session_class->validate_user_props(
      INF_SESSION(priv->session),
      (const GParameter*)user_props->data,
      user_props->len,
      NULL,
      error
    );

    if(result == TRUE)
    {
      user = inf_session_add_user(
        INF_SESSION(priv->session),
        (const GParameter*)user_props->data,
        user_props->len
      );
    }

    for(i = 0; i < user_props->len; ++i)
      g_value_unset(&g_array_index(user_props, GParameter, i).value);
    g_array_free(user_props, TRUE);

    if(user == NULL) return FALSE;
  }
  else
  {
    g_set_error(
      error,
      session_replay_error_quark,
      INF_ADOPTED_SESSION_REPLAY_ERROR_BAD_FORMAT,
      _("Unexpected node \"%s\" in requests section"),
      id
    );

    return FALSE;
  }

  if(!inf_adopted_session_replay_advance_subtree_required(reader, error))
    return FALSE;
  if(!inf_adopted_session_replay_skip_whitespace(reader, error))
    return FALSE;

  return TRUE;
}

/**
 * inf_adopted_session_replay_play_to_end:
 * @replay: A #InfAdoptedSessionReplay.
 * @error: Location to store error information, if any.
 *
 * Plays all requests that are contained in the recording, so that the
 * replay's session has the same state as the recorded session when the
 * recording was stopped.
 *
 * Note that, depending on the size of the record, this function may take
 * some time to finish.
 *
 * If an error occurs during replay, then the function returns %FALSE and
 * @error is set. Otherwise it returns %TRUE.
 *
 * Returns: %TRUE on success, or %FALSE if an error occurs.
 */
gboolean
inf_adopted_session_replay_play_to_end(InfAdoptedSessionReplay* replay,
                                       GError** error)
{
  GError* local_error;
  gboolean result;

  g_return_val_if_fail(INF_ADOPTED_IS_SESSION_REPLAY(replay), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  local_error = NULL;

  do
  {
    result = inf_adopted_session_replay_play_next(replay, &local_error);
  } while(result);

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  return TRUE;
}

/* vim:set et sw=2 ts=2: */
