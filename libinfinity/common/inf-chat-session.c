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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/**
 * SECTION:inf-chat-session
 * @title: InfChatSession
 * @short_description: Simple standalone chat
 * @include: libinfinity/common/inf-chat-session.h
 * @stability: Unstable
 *
 * #InfChatSession represents a chat session. Normally, there is one chat
 * session per server, and it can be enabled via infd_directory_enable_chat().
 * Clients can subscribe to the chat session via
 * infc_browser_subscribe_chat().
 **/

#include <libinfinity/common/inf-chat-session.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-signals.h>

#include <errno.h>
#include <string.h>

typedef struct _InfChatSessionLogUserlistForeachData
  InfChatSessionLogUserlistForeachData;
struct _InfChatSessionLogUserlistForeachData {
  FILE* log_file;
  gchar* time_str;
  guint users_total;
};

typedef struct _InfChatSessionPrivate InfChatSessionPrivate;
struct _InfChatSessionPrivate {
  gchar* log_filename;
  FILE* log_file;
};

enum {
  PROP_0,

  PROP_LOG_FILE
};

enum {
  RECEIVE_MESSAGE,
  SEND_MESSAGE,

  LAST_SIGNAL
};

#define INF_CHAT_SESSION_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_TYPE_CHAT_SESSION, InfChatSessionPrivate))

static InfSessionClass* parent_class;
static guint chat_session_signals[LAST_SIGNAL];
static GQuark inf_chat_session_error_quark;

/*
 * Error functions
 */

/* Currently unused, but can be used for a later translate_error vfunc
 * implementation. */
#if 0
static const gchar*
inf_chat_session_strerror(InfChatSessionError code)
{
  switch(code)
  {
  case INF_CHAT_SESSION_ERROR_TYPE_INVALID:
    return _("An invalid message type was sent");
  case INF_CHAT_SESSION_ERROR_NO_SUCH_USER:
    return _("A user with the requested ID does not exist");
  case INF_CHAT_SESSION_ERROR_FAILED:
    return _("An unknown chat session error has occured");
  default:
    return _("An error with unknown error code occured");
  }
}
#endif

/*
 * Message Type <-> string conversions
 */

static const gchar*
inf_chat_session_message_type_to_string(InfChatBufferMessageType type)
{
  switch(type)
  {
  case INF_CHAT_BUFFER_MESSAGE_NORMAL: return "normal";
  case INF_CHAT_BUFFER_MESSAGE_EMOTE: return "emote";
  case INF_CHAT_BUFFER_MESSAGE_USERJOIN: return "userjoin";
  case INF_CHAT_BUFFER_MESSAGE_USERPART: return "userpart";
  default: g_assert_not_reached(); return NULL;
  }
}

static gboolean
inf_chat_session_message_type_from_string(const gchar* string,
                                          InfChatBufferMessageType* type,
                                          GError** error)
{
  if(strcmp(string, "normal") == 0)
  {
    *type = INF_CHAT_BUFFER_MESSAGE_NORMAL;
    return TRUE;
  }
  else if(strcmp(string, "emote") == 0)
  {
    *type = INF_CHAT_BUFFER_MESSAGE_EMOTE;
    return TRUE;
  }
  else if(strcmp(string, "userjoin") == 0)
  {
    *type = INF_CHAT_BUFFER_MESSAGE_USERJOIN;
    return TRUE;
  }
  else if(strcmp(string, "userpart") == 0)
  {
    *type = INF_CHAT_BUFFER_MESSAGE_USERPART;
    return TRUE;
  }

  g_set_error(
    error,
    inf_chat_session_error_quark,
    INF_CHAT_SESSION_ERROR_TYPE_INVALID,
    "Invalid message type: \"%s\"",
    string
  );

  return FALSE;
}

/*
 * Message XML functions
 */

static xmlNodePtr
inf_chat_session_message_to_xml(InfChatSession* session,
                                const InfChatBufferMessage* message,
                                gboolean for_sync)
{
  xmlNodePtr xml;
  xml = xmlNewNode(NULL, (const xmlChar*)"message");

  if(message->type != INF_CHAT_BUFFER_MESSAGE_NORMAL)
  {
    inf_xml_util_set_attribute(
      xml,
      "type",
      inf_chat_session_message_type_to_string(message->type)
    );
  }

  if(for_sync)
    inf_xml_util_set_attribute_long(xml, "time", (long)message->time);

  inf_xml_util_set_attribute_uint(
    xml,
    "user",
    inf_user_get_id(message->user)
  );

  if(message->text != NULL)
    inf_xml_util_add_child_text(xml, message->text, message->length);

  return xml;
}

static gboolean
inf_chat_session_message_from_xml(InfChatSession* session,
                                  InfChatBufferMessage* message,
                                  xmlNodePtr xml,
                                  gboolean for_sync,
                                  GError** error)
{
  xmlChar* type;
  gboolean result;
  InfChatBufferMessageType message_type;
  InfChatBufferMessageFlags message_flags;
  long message_time;
  guint user_id;
  InfUserTable* user_table;
  InfUser* user;

  message_flags = 0;

  type = inf_xml_util_get_attribute(xml, "type");
  if(type == NULL)
  {
    message_type = INF_CHAT_BUFFER_MESSAGE_NORMAL;
  }
  else
  {
    result = inf_chat_session_message_type_from_string(
      (const char*)type,
      &message_type,
      error
    );

    xmlFree(type);
    if(result == FALSE) return FALSE;
  }

  if(for_sync)
  {
    result = inf_xml_util_get_attribute_long_required(
      xml,
      "time",
      &message_time,
      error
    );

    if(result == FALSE) return FALSE;
    message_flags = INF_CHAT_BUFFER_MESSAGE_BACKLOG;
  }
  else
  {
    if(message_type == INF_CHAT_BUFFER_MESSAGE_USERJOIN ||
       message_type == INF_CHAT_BUFFER_MESSAGE_USERPART)
    {
      g_set_error(
        error,
        inf_chat_session_error_quark,
        INF_CHAT_SESSION_ERROR_TYPE_INVALID,
        "Non-backlog message type cannot be \"%s\"",
        inf_chat_session_message_type_to_string(message_type)
      );

      return FALSE;
    }

    message_time = time(NULL);
  }

  if(!inf_xml_util_get_attribute_uint_required(xml, "user", &user_id, error))
    return FALSE;

  user_table = inf_session_get_user_table(INF_SESSION(session));
  user = inf_user_table_lookup_user_by_id(user_table, user_id);

  if(user == NULL)
  {
    g_set_error(
      error,
      inf_chat_session_error_quark,
      INF_CHAT_SESSION_ERROR_NO_SUCH_USER,
      _("No such user with ID \"%u\""),
      user_id
    );

    return FALSE;
  }

  if(message_type != INF_CHAT_BUFFER_MESSAGE_USERJOIN &&
     message_type != INF_CHAT_BUFFER_MESSAGE_USERPART)
  {
    message->text =
      inf_xml_util_get_child_text(xml, &message->length, NULL, error);
    if(!message->text)
      return FALSE;
  }
  else
  {
    message->text = NULL;
    message->length = 0;
  }

  message->type = message_type;
  message->user = user;
  message->time = message_time;
  message->flags = message_flags;

  return TRUE;
}

/*
 * Logging functions
 */

static gchar*
inf_chat_session_strdup_strftime(const char* format,
                                 const struct tm* tm,
                                 gsize* len)
{
  gsize alloc;
  gchar* str;
  size_t result;

  alloc = 64;
  str = g_malloc(alloc * sizeof(gchar));
  result = strftime(str, alloc, format, tm);

  while(result == 0 && alloc < 1024)
  {
    alloc *= 2;
    str = g_realloc(str, alloc * sizeof(gchar));
    result = strftime(str, alloc, format, tm);
  }

  if(result == 0)
  {
    g_free(str);
    return NULL;
  }

  if(len) *len = result;
  return str;
}

static void
inf_chat_session_log_message(InfChatSession* session,
                             const InfChatBufferMessage* message)
{
  InfChatSessionPrivate* priv;
  struct tm* tm;
  gchar* time_str;
  const gchar* name;
  const gchar* text;

  priv = INF_CHAT_SESSION_PRIVATE(session);

  if(priv->log_file != NULL)
  {
    tm = localtime(&message->time);
    time_str = inf_chat_session_strdup_strftime("%c", tm, NULL);
    name = inf_user_get_name(message->user);
    text = message->text;

    switch(message->type)
    {
    case INF_CHAT_BUFFER_MESSAGE_NORMAL:
      fprintf(priv->log_file, "%s <%s> %s\n", time_str, name, message->text);
      break;
    case INF_CHAT_BUFFER_MESSAGE_EMOTE:
      fprintf(priv->log_file, "%s * %s %s\n", time_str, name, message->text);
      break;
    case INF_CHAT_BUFFER_MESSAGE_USERJOIN:
      fprintf(priv->log_file, _("%s --- %s has joined\n"), time_str, name);
      break;
    case INF_CHAT_BUFFER_MESSAGE_USERPART:
      fprintf(priv->log_file, _("%s --- %s has left\n"), time_str, name);
      break;
    default:
      g_assert_not_reached();
      break;
    }

    g_free(time_str);
    fflush(priv->log_file);
  }
}

static void
inf_chat_session_log_userlist_foreach_func(InfUser* user,
                                           gpointer user_data)
{
  InfChatSessionLogUserlistForeachData* data;
  data = (InfChatSessionLogUserlistForeachData*)user_data;

  if(inf_user_get_status(user) != INF_USER_UNAVAILABLE)
  {
    fprintf(
      data->log_file,
      "%s --- [%s]\n",
      data->time_str,
      inf_user_get_name(user)
    );

    ++ data->users_total;
  }
}

static void
inf_chat_session_log_userlist(InfChatSession* session)
{
  InfChatSessionPrivate* priv;
  InfChatSessionLogUserlistForeachData data;
  time_t cur_time;
  struct tm* tm;

  priv = INF_CHAT_SESSION_PRIVATE(session);
  if(priv->log_file != NULL)
  {
    cur_time = time(NULL);
    tm = localtime(&cur_time);

    data.time_str = inf_chat_session_strdup_strftime("%c", tm, NULL);
    data.log_file = priv->log_file;
    data.users_total = 0;

    inf_user_table_foreach_user(
      inf_session_get_user_table(INF_SESSION(session)),
      inf_chat_session_log_userlist_foreach_func,
      &data
    );

    fprintf(
      data.log_file,
      _("%s --- %u users total\n"),
      data.time_str,
      data.users_total
    );

    g_free(data.time_str);
    fflush(data.log_file);
  }
}

/*
 * Message reception
 */

static gboolean
inf_chat_session_receive_message(InfChatSession* session,
                                 InfXmlConnection* connection,
                                 xmlNodePtr xml,
                                 GError** error)
{
  InfChatSessionPrivate* priv;
  InfChatBufferMessage message;
  gboolean sync;

  priv = INF_CHAT_SESSION_PRIVATE(session);

  if(inf_session_get_status(INF_SESSION(session)) ==
     INF_SESSION_SYNCHRONIZING)
  {
    sync = TRUE;
  }
  else
  {
    sync = FALSE;
  }

  if(!inf_chat_session_message_from_xml(session, &message, xml, sync, error))
    return FALSE;

  if(!sync &&
     (inf_user_get_status(message.user) == INF_USER_UNAVAILABLE ||
      inf_user_get_connection(message.user) != connection))
  {
    g_set_error(
      error,
      inf_user_error_quark(),
      INF_USER_ERROR_NOT_JOINED,
      "%s",
      _("User did not join from this connection")
    );

    g_free(message.text);
    return FALSE;
  }

  g_signal_emit(
    session,
    chat_session_signals[RECEIVE_MESSAGE],
    0,
    &message
  );

  g_free(message.text);
  return TRUE;
}

static void
inf_chat_session_user_join(InfChatSession* session,
                           InfUser* user)
{
  InfChatBufferMessage message;

  message.type = INF_CHAT_BUFFER_MESSAGE_USERJOIN;
  message.user = user;
  message.text = NULL;
  message.length = 0;
  message.time = time(NULL);
  message.flags = 0;

  g_signal_emit(session, chat_session_signals[RECEIVE_MESSAGE], 0, &message);
}

static void
inf_chat_session_user_part(InfChatSession* session,
                           InfUser* user)
{
  InfChatBufferMessage message;

  message.type = INF_CHAT_BUFFER_MESSAGE_USERPART;
  message.user = user;
  message.text = NULL;
  message.length = 0;
  message.time = time(NULL);
  message.flags = 0;

  g_signal_emit(session, chat_session_signals[RECEIVE_MESSAGE], 0, &message);
}

/*
 * Signal handlers
 */

static void
inf_chat_session_set_status_cb(InfUser* user,
                               InfUserStatus new_status,
                               gpointer user_data)
{
  InfSession* session;
  session = INF_SESSION(user_data);

  if(inf_session_get_status(session) == INF_SESSION_RUNNING)
  {
    if(inf_user_get_status(user) != INF_USER_UNAVAILABLE &&
       new_status == INF_USER_UNAVAILABLE)
    {
      inf_chat_session_user_part(INF_CHAT_SESSION(session), user);
    }
    else if(inf_user_get_status(user) == INF_USER_UNAVAILABLE &&
            new_status != INF_USER_UNAVAILABLE)
    {
      inf_chat_session_user_join(INF_CHAT_SESSION(session), user);
    }
  }
}

static void
inf_chat_session_add_user_cb(InfUserTable* user_table,
                             InfUser* user,
                             gpointer user_data)
{
  g_signal_connect(
    user,
    "set-status",
    G_CALLBACK(inf_chat_session_set_status_cb),
    user_data
  );

  if(inf_session_get_status(INF_SESSION(user_data)) == INF_SESSION_RUNNING)
    if(inf_user_get_status(user) != INF_USER_UNAVAILABLE)
      inf_chat_session_user_join(INF_CHAT_SESSION(user_data), user);
}

static void
inf_chat_session_remove_user_cb(InfUserTable* user_table,
                                InfUser* user,
                                gpointer user_data)
{
  if(inf_session_get_status(INF_SESSION(user_data)) == INF_SESSION_RUNNING)
    if(inf_user_get_status(user) != INF_USER_UNAVAILABLE)
      inf_chat_session_user_part(INF_CHAT_SESSION(user_data), user);

  inf_signal_handlers_disconnect_by_func(
    user,
    G_CALLBACK(inf_chat_session_set_status_cb),
    user_data
  );
}

static void
inf_chat_session_add_message_cb(InfChatBuffer* buffer,
                                const InfChatBufferMessage* message,
                                gpointer user_data)
{
  /* Ignore these messages, we cannot send them */
  if(message->type != INF_CHAT_BUFFER_MESSAGE_USERJOIN &&
     message->type != INF_CHAT_BUFFER_MESSAGE_USERPART)
  {
    /* A message has been added to the buffer, so send it */
    g_signal_emit(
      user_data,
      chat_session_signals[SEND_MESSAGE],
      0,
      message
    );
  }
}

/*
 * GObject overrides
 */

static void
inf_chat_session_init(GTypeInstance* instance,
                      gpointer g_class)
{
  InfChatSession* session;
  InfChatSessionPrivate* priv;

  session = INF_CHAT_SESSION(instance);
  priv = INF_CHAT_SESSION_PRIVATE(session);

  priv->log_filename = NULL;
  priv->log_file = NULL;
}

static void
inf_chat_session_constructor_foreach_user_func(InfUser* user,
                                               gpointer user_data)
{
  g_signal_connect(
    user,
    "set-status",
    G_CALLBACK(inf_chat_session_set_status_cb),
    user_data
  );
}

static GObject*
inf_chat_session_constructor(GType type,
                             guint n_construct_properties,
                             GObjectConstructParam* construct_properties)
{
  GObject* object;
  InfUserTable* user_table;
  InfChatBuffer* buffer;

  object = G_OBJECT_CLASS(parent_class)->constructor(
    type,
    n_construct_properties,
    construct_properties
  );

  user_table = inf_session_get_user_table(INF_SESSION(object));

  g_signal_connect_after(
    user_table,
    "add-user",
    G_CALLBACK(inf_chat_session_add_user_cb),
    object
  );

  g_signal_connect_after(
    user_table,
    "remove-user",
    G_CALLBACK(inf_chat_session_remove_user_cb),
    object
  );

  inf_user_table_foreach_user(
    INF_USER_TABLE(user_table),
    inf_chat_session_constructor_foreach_user_func,
    object
  );

  buffer = INF_CHAT_BUFFER(inf_session_get_buffer(INF_SESSION(object)));
  g_assert(INF_IS_CHAT_BUFFER(buffer));

  g_signal_connect_after(
    buffer,
    "add-message",
    G_CALLBACK(inf_chat_session_add_message_cb),
    object
  );

  return object;
}

static void
inf_chat_session_dispose_foreach_user_func(InfUser* user,
                                           gpointer user_data)
{
  inf_signal_handlers_disconnect_by_func(
    user,
    G_CALLBACK(inf_chat_session_set_status_cb),
    user_data
  );
}

static void
inf_chat_session_dispose(GObject* object)
{
  InfChatSession* session;
  InfChatBuffer* buffer;
  InfUserTable* user_table;

  session = INF_CHAT_SESSION(object);
  user_table = inf_session_get_user_table(INF_SESSION(session));
  buffer = INF_CHAT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));

  inf_user_table_foreach_user(
    INF_USER_TABLE(user_table),
    inf_chat_session_dispose_foreach_user_func,
    session
  );

  inf_signal_handlers_disconnect_by_func(
    user_table,
    G_CALLBACK(inf_chat_session_add_user_cb),
    session
  );

  inf_signal_handlers_disconnect_by_func(
    user_table,
    G_CALLBACK(inf_chat_session_remove_user_cb),
    session
  );

  inf_signal_handlers_disconnect_by_func(
    buffer,
    G_CALLBACK(inf_chat_session_add_message_cb),
    session
  );

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_chat_session_finalize(GObject* object)
{
  InfChatSession* session;
  InfChatSessionPrivate* priv;

  session = INF_CHAT_SESSION(object);
  priv = INF_CHAT_SESSION_PRIVATE(session);

  inf_chat_session_set_log_file(session, NULL, NULL);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_chat_session_set_property(GObject* object,
                              guint prop_id,
                              const GValue* value,
                              GParamSpec* pspec)
{
  InfChatSession* session;
  InfChatSessionPrivate* priv;
  const gchar* log_file;
  GError* error;

  session = INF_CHAT_SESSION(object);
  priv = INF_CHAT_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_LOG_FILE:
    error = NULL;
    log_file = g_value_get_string(value);

    if(!inf_chat_session_set_log_file(session, log_file, &error))
    {
      g_warning("Failed to set log file: %s\n", error->message);
      g_error_free(error);
    }

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_chat_session_get_property(GObject* object,
                              guint prop_id,
                              GValue* value,
                              GParamSpec* pspec)
{
  InfChatSession* session;
  InfChatSessionPrivate* priv;

  session = INF_CHAT_SESSION(object);
  priv = INF_CHAT_SESSION_PRIVATE(session);

  switch(prop_id)
  {
  case PROP_LOG_FILE:
    g_value_set_string(value, priv->log_filename);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

/*
 * InfSession virtual functions and default signal handlers
 */

static void
inf_chat_session_to_xml_sync(InfSession* session,
                             xmlNodePtr parent)
{
  InfChatBuffer* buffer;
  const InfChatBufferMessage* message;
  xmlNodePtr child;
  guint i;

  buffer = INF_CHAT_BUFFER(inf_session_get_buffer(session));

  g_assert(parent_class->to_xml_sync != NULL);
  parent_class->to_xml_sync(session, parent);

  for(i = 0; i < inf_chat_buffer_get_n_messages(buffer); ++i)
  {
    message = inf_chat_buffer_get_message(buffer, i);

    child = inf_chat_session_message_to_xml(
      INF_CHAT_SESSION(session),
      message,
      TRUE
    );

    xmlAddChild(parent, child);
  }
}

static gboolean
inf_chat_session_process_xml_sync(InfSession* session,
                                  InfXmlConnection* connection,
                                  xmlNodePtr xml,
                                  GError** error)
{
  if(strcmp((const char*)xml->name, "message") == 0)
  {
    return inf_chat_session_receive_message(
      INF_CHAT_SESSION(session),
      connection,
      xml,
      error
    );
  }
  else
  {
    g_assert(parent_class->process_xml_sync != NULL);
    return parent_class->process_xml_sync(session, connection, xml, error);
  }
}

static InfCommunicationScope
inf_chat_session_process_xml_run(InfSession* session,
                                 InfXmlConnection* connection,
                                 xmlNodePtr xml,
                                 GError** error)
{
  gboolean result;

  if(strcmp((const char*)xml->name, "message") == 0)
  {
    result = inf_chat_session_receive_message(
      INF_CHAT_SESSION(session),
      connection,
      xml,
      error
    );

    if(!result)
      return INF_COMMUNICATION_SCOPE_PTP;
    else
      return INF_COMMUNICATION_SCOPE_GROUP;
  }
  else
  {
    g_assert(parent_class->process_xml_run != NULL);
    return parent_class->process_xml_run(session, connection, xml, error);
  }
}

static void
inf_chat_session_synchronization_complete(InfSession* session,
                                          InfXmlConnection* connection)
{
  if(inf_session_get_status(session) == INF_SESSION_SYNCHRONIZING)
    inf_chat_session_log_userlist(INF_CHAT_SESSION(session));

  g_assert(parent_class->synchronization_complete != NULL);
  parent_class->synchronization_complete(session, connection);
}

static void
inf_chat_session_synchronization_failed(InfSession* session,
                                        InfXmlConnection* connection,
                                        const GError* error)
{
  InfChatSessionPrivate* priv;
  time_t cur_time;
  struct tm* tm;
  gchar* time_str;

  if(inf_session_get_status(session) == INF_SESSION_SYNCHRONIZING)
  {
    priv = INF_CHAT_SESSION_PRIVATE(session);
    if(priv->log_file != NULL)
    {
      cur_time = time(NULL);
      tm = localtime(&cur_time);
      time_str = inf_chat_session_strdup_strftime("%c", tm, NULL);

      fprintf(
        priv->log_file,
        "%s --- Synchronization failed: %s\n",
        time_str,
        error->message
      );

      g_free(time_str);
    }
  }

  g_assert(parent_class->synchronization_failed != NULL);
  parent_class->synchronization_failed(session, connection, error);
}

static InfUser*
inf_chat_session_user_new(InfSession* session,
                          GParameter* params,
                          guint n_params)
{
  return g_object_newv(INF_TYPE_USER, n_params, params);
}

static void
inf_chat_session_receive_message_handler(InfChatSession* session,
                                         const InfChatBufferMessage* message)
{
  InfChatBuffer* buffer;
  buffer = INF_CHAT_BUFFER(inf_session_get_buffer(INF_SESSION(session)));

  /* The add_message signal handler would try to send the message, so prevent
   * this. */
  inf_signal_handlers_block_by_func(
    buffer,
    G_CALLBACK(inf_chat_session_add_message_cb),
    session
  );

  switch(message->type)
  {
  case INF_CHAT_BUFFER_MESSAGE_NORMAL:
    inf_chat_buffer_add_message(
      buffer,
      message->user,
      message->text,
      message->length,
      message->time,
      message->flags
    );
    break;
  case INF_CHAT_BUFFER_MESSAGE_EMOTE:
    inf_chat_buffer_add_emote_message(
      buffer,
      message->user,
      message->text,
      message->length,
      message->time,
      message->flags
    );
    break;
  case INF_CHAT_BUFFER_MESSAGE_USERJOIN:
    inf_chat_buffer_add_userjoin_message(
      buffer,
      message->user,
      message->time,
      message->flags
    );
    break;
  case INF_CHAT_BUFFER_MESSAGE_USERPART:
    inf_chat_buffer_add_userpart_message(
      buffer,
      message->user,
      message->time,
      message->flags
    );
    break;
  default:
    g_assert_not_reached();
    break;
  }

  inf_signal_handlers_unblock_by_func(
    buffer,
    G_CALLBACK(inf_chat_session_add_message_cb),
    session
  );

  /* Backlog messages (received during synchronization) are not yet logged.
   * We will need to parse the last messages in the log first and check
   * whether they have already been logged. */
  if(inf_session_get_status(INF_SESSION(session)) == INF_SESSION_RUNNING)
    inf_chat_session_log_message(session, message);
}

static void
inf_chat_session_send_message_handler(InfChatSession* session,
                                      const InfChatBufferMessage* message)
{
  xmlNodePtr xml;

  /* Actually send the message over the network */
  xml = inf_chat_session_message_to_xml(session, message, FALSE);
  inf_session_send_to_subscriptions(INF_SESSION(session), xml);

  inf_chat_session_log_message(session, message);
}

/*
 * GType registration
 */

static void
inf_chat_session_class_init(gpointer g_class,
                            gpointer class_data)
{
  GObjectClass* object_class;
  InfSessionClass* session_class;
  InfChatSessionClass* chat_session_class;

  object_class = G_OBJECT_CLASS(g_class);
  session_class = INF_SESSION_CLASS(g_class);
  chat_session_class = INF_CHAT_SESSION_CLASS(g_class);

  parent_class = INF_SESSION_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfChatSessionPrivate));

  object_class->constructor = inf_chat_session_constructor;
  object_class->dispose = inf_chat_session_dispose;
  object_class->finalize = inf_chat_session_finalize;
  object_class->set_property = inf_chat_session_set_property;
  object_class->get_property = inf_chat_session_get_property;

  session_class->to_xml_sync = inf_chat_session_to_xml_sync;
  session_class->process_xml_sync = inf_chat_session_process_xml_sync;
  session_class->process_xml_run = inf_chat_session_process_xml_run;
  session_class->synchronization_complete =
    inf_chat_session_synchronization_complete;
  session_class->synchronization_failed =
    inf_chat_session_synchronization_failed;

  session_class->user_new = inf_chat_session_user_new;

  chat_session_class->receive_message =
    inf_chat_session_receive_message_handler;
  chat_session_class->send_message =
    inf_chat_session_send_message_handler;

  inf_chat_session_error_quark =
    g_quark_from_static_string("INF_CHAT_SESSION_ERROR");

  g_object_class_install_property(
    object_class,
    PROP_LOG_FILE,
    g_param_spec_string(
      "log-file",
      "Log file",
      "The file into which to store all received messages",
      NULL,
      G_PARAM_READWRITE
    )
  );

  /**
   * InfChatSession::receive-message:
   * @session: The #InfChatSession that is receiving a message.
   * @message: The #InfChatBufferMessage that was received.
   *
   * This signal is emitted whenever a message has been received. If the
   * session is in %INF_SESSION_SYNCHRONIZING state the received message was
   * a backlog message.
   */
  chat_session_signals[RECEIVE_MESSAGE] = g_signal_new(
    "receive-message",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfChatSessionClass, receive_message),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INF_TYPE_CHAT_BUFFER_MESSAGE | G_SIGNAL_TYPE_STATIC_SCOPE
  );

  /**
   * InfChatSession::send-message:
   * @session: The #InfChatSession that is sending a message.
   * @message: The #InfChatBufferMessage that is sent.
   *
   * This signal is emitted whenever a message is sent. Messages can be sent
   * by calling inf_chat_buffer_add_message() or
   * inf_chat_buffer_add_emote_message() on the session's #InfChatBuffer.
   * Messages of type %INF_CHAT_BUFFER_MESSAGE_USERJOIN or
   * %INF_CHAT_BUFFER_MESSAGE_USERPART can not be sent explicitely, so this
   * signal will never be emitted for such messages.
   */
  chat_session_signals[SEND_MESSAGE] = g_signal_new(
    "send-message",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfChatSessionClass, send_message),
    NULL, NULL,
    inf_marshal_VOID__BOXED,
    G_TYPE_NONE,
    1,
    INF_TYPE_CHAT_BUFFER_MESSAGE | G_SIGNAL_TYPE_STATIC_SCOPE
  );
}

GType
inf_chat_session_get_type(void)
{
  static GType chat_session_type = 0;

  if(!chat_session_type)
  {
    static const GTypeInfo chat_session_type_info = {
      sizeof(InfChatSessionClass),  /* class_size */
      NULL,                         /* base_init */
      NULL,                         /* base_finalize */
      inf_chat_session_class_init,  /* class_init */
      NULL,                         /* class_finalize */
      NULL,                         /* class_data */
      sizeof(InfChatSession),       /* instance_size */
      0,                            /* n_preallocs */
      inf_chat_session_init,        /* instance_init */
      NULL                          /* value_table */
    };

    chat_session_type = g_type_register_static(
      INF_TYPE_SESSION,
      "InfChatSession",
      &chat_session_type_info,
      0
    );
  }

  return chat_session_type;
}

/*
 * Public API
 */

/**
 * inf_chat_session_new:
 * @manager: A #InfCommunicationManager.
 * @backlog_size: The number of messages to store.
 * @status: Initial status of the session. If this is
 * %INF_SESSION_SYNCHRONIZING or %INF_SESSION_PRESYNC, then @sync_group and
 * @sync_connection need to be set.
 * @sync_group: A group in which the session is synchronized. Ignored if
 * @status is %INF_SESSION_RUNNING.
 * @sync_connection: A connection to synchronize the session from. Ignored if
 * @status is %INF_SESSION_RUNNING.
 *
 * Creates a new #InfChatSession with no initial messages. The communication
 * manager is used to send and receive requests from subscription and
 * synchronization.
 *
 * @backlog_size specifies how much messages to save before dropping old
 * messages. This also limits how many old messages are transferred when
 * synchronizing the session.
 *
 * If @status is %INF_SESSION_PRESYNC or %INF_SESSION_SYNCHRONIZING, then the
 * session will initially be synchronized, meaning an initial backlog is
 * retrieved from @sync_connection (which must not be %NULL in this case). If
 * you are subscribed to the session, set the subscription group via
 * inf_session_set_subscription_group().
 *
 * Returns: A new #InfChatSession.
 */
InfChatSession*
inf_chat_session_new(InfCommunicationManager* manager,
                     guint backlog_size,
                     InfSessionStatus status,
                     InfCommunicationGroup* sync_group,
                     InfXmlConnection* sync_connection)
{
  InfChatBuffer* buffer;
  InfChatSession* session;

  g_return_val_if_fail(INF_COMMUNICATION_IS_MANAGER(manager), NULL);

  g_return_val_if_fail(
    (status == INF_SESSION_RUNNING &&
     sync_group == NULL && sync_connection == NULL) ||
    (status != INF_SESSION_RUNNING &&
     INF_COMMUNICATION_IS_GROUP(sync_group) &&
     INF_IS_XML_CONNECTION(sync_connection)),
    NULL
  );

  /* This actually does more than just g_object_new, but I think language
   * bindings can just copy this. */
  buffer = inf_chat_buffer_new(backlog_size);

  session = INF_CHAT_SESSION(
    g_object_new(
      INF_TYPE_CHAT_SESSION,
      "communication-manager", manager,
      "buffer", buffer,
      "status", status,
      "sync-group", sync_group,
      "sync-connection", sync_connection,
      NULL
    )
  );

  g_object_unref(buffer);
  return session;
}

/**
 * inf_chat_session_set_log_file:
 * @session: A #InfChatSession.
 * @log_file: A filename to store all received messages into.
 * @error: Location to store error information, if any.
 *
 * Sets a file onto which all received messages are appended. The file is
 * created if it does not exist. If a previous log file was set, then it is
 * closed before opening the new file.
 *
 * Backlog messages received upon synchronization are not logged.
 *
 * Returns: %TRUE if the log file could be opened, %FALSE otherwise (in which
 * case @error is set).
 */
gboolean
inf_chat_session_set_log_file(InfChatSession* session,
                              const gchar* log_file,
                              GError** error)
{
  InfChatSessionPrivate* priv;
  FILE* new_file;
  int save_errno;
  long offset;
  time_t cur_time;
  struct tm* tm;
  gchar* time_str;
  guint len;

  g_return_val_if_fail(INF_IS_CHAT_SESSION(session), FALSE);
  priv = INF_CHAT_SESSION_PRIVATE(session);

  /* Open the new log file before doing anything else, so that we keep
   * the current log file if this fails. */
  if(log_file != NULL)
  {
    new_file = fopen(log_file, "a");
    if(new_file == NULL)
    {
      save_errno = errno;
    }
    else
    {
      offset = ftell(new_file);
      if(offset == -1)
      {
        save_errno = errno;
        fclose(new_file);
        new_file = NULL;
      }
    }

    if(new_file == NULL)
    {
      g_set_error(
        error,
        G_FILE_ERROR,
        g_file_error_from_errno(save_errno),
        "%s",
        strerror(save_errno)
      );

      return FALSE;
    }
  }

  cur_time = time(NULL);
  tm = localtime(&cur_time);
  time_str = inf_chat_session_strdup_strftime("%c", tm, NULL);

  if(priv->log_file != NULL)
  {
    fprintf(priv->log_file, _("%s --- Log closed\n"), time_str);
    fclose(priv->log_file);
  }

  if(log_file != NULL)
  {
    len = strlen(log_file);
    priv->log_filename =
      g_realloc(priv->log_filename, (len + 1) * sizeof(gchar));
    memcpy(priv->log_filename, log_file, len);
    priv->log_filename[len] = '\0';
    priv->log_file = new_file;

    if(offset > 0) fprintf(priv->log_file, "\n");
    fprintf(priv->log_file, _("%s --- Log opened\n"), time_str);

    if(inf_session_get_status(INF_SESSION(session)) == INF_SESSION_RUNNING)
      inf_chat_session_log_userlist(session);
    else
      fflush(priv->log_file);
  }
  else
  {
    g_free(priv->log_filename);
    priv->log_filename = NULL;
    priv->log_file = NULL;
  }

  g_free(time_str);
  return TRUE;
}

/* vim:set et sw=2 ts=2: */
