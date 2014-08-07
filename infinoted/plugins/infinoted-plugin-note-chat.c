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
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <infinoted/infinoted-plugin-manager.h>
#include <infinoted/infinoted-parameter.h>

#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/common/inf-chat-session.h>

#include <libinfinity/inf-i18n.h>

typedef struct _InfinotedPluginNoteChat InfinotedPluginNoteChat;
struct _InfinotedPluginNoteChat {
  InfinotedPluginManager* manager;
  const InfdNotePlugin* plugin;
};

typedef enum InfinotedPluginNoteChatError {
  INFINOTED_PLUGIN_NOTE_CHAT_ERROR_NOT_A_CHAT_SESSION,
  INFINOTED_PLUGIN_NOTE_CHAT_ERROR_TYPE_ALREADY_HANDLED
} InfinotedPluginNoteChatError;

/* Note plugin implementation */
static InfSession*
infinoted_plugin_note_chat_session_new(InfIo* io,
                                       InfCommunicationManager* manager,
                                       InfSessionStatus status,
                                       InfCommunicationGroup* sync_group,
                                       InfXmlConnection* sync_connection,
                                       gpointer user_data)
{
  InfChatSession* session;

  session = inf_chat_session_new(
    manager,
    256,
    status,
    INF_COMMUNICATION_GROUP(sync_group),
    sync_connection
  );

  return INF_SESSION(session);
}

static int
infinoted_plugin_note_chat_session_read_read_func(void* context,
                                                  char* buffer,
                                                  int len)
{
  gsize res;
  res = infd_filesystem_storage_stream_read((FILE*)context, buffer, len);

  if(ferror((FILE*)context))
    return -1;

  return (int)res;
}

static int
infinoted_plugin_note_chat_session_read_close_func(void* context)
{
  return infd_filesystem_storage_stream_close((FILE*)context);
}

static InfSession*
infinoted_plugin_note_chat_session_read(InfdStorage* storage,
                                        InfIo* io,
                                        InfCommunicationManager* manager,
                                        const gchar* path,
                                        gpointer user_data,
                                        GError** error)
{
  InfChatSession* session;

  FILE* stream;
  xmlDocPtr doc;
  xmlErrorPtr xmlerror;
  xmlNodePtr root;
  gboolean result;

  g_assert(INFD_IS_FILESYSTEM_STORAGE(storage));

  /* TODO: Use a SAX parser for better performance */
  stream = infd_filesystem_storage_open(
    INFD_FILESYSTEM_STORAGE(storage),
    "InfChat",
    path,
    "r",
    NULL,
    error
  );

  if(stream == NULL)
    return FALSE;

  doc = xmlReadIO(
    infinoted_plugin_note_chat_session_read_read_func,
    infinoted_plugin_note_chat_session_read_close_func,
    stream,
    path,
    "UTF-8",
    XML_PARSE_NOWARNING | XML_PARSE_NOERROR
  );

  result = TRUE;

  if(doc == NULL)
  {
    xmlerror = xmlGetLastError();

    g_set_error(
      error,
      g_quark_from_static_string("LIBXML2_PARSER_ERROR"),
      xmlerror->code,
      "Error parsing XML in file '%s': [%d]: %s",
      path,
      xmlerror->line,
      xmlerror->message
    );

    result = FALSE;
  }
  else
  {
    root = xmlDocGetRootElement(doc);
    if(strcmp((const char*)root->name, "inf-chat-session") != 0)
    {
      g_set_error(
        error,
        g_quark_from_static_string("INFINOTED_PLUGIN_NOTE_CHAT_ERROR"),
        INFINOTED_PLUGIN_NOTE_CHAT_ERROR_NOT_A_CHAT_SESSION,
        "Error processing file '%s': %s",
        path,
        "The document is not a chat session"
      );

      result = FALSE;
    }

    xmlFreeDoc(doc);
  }

  if(result == FALSE)
    return NULL;

  session = inf_chat_session_new(
    manager,
    256,
    INF_SESSION_RUNNING,
    NULL,
    NULL
  );

  return INF_SESSION(session);
}

static gboolean
infinoted_plugin_note_chat_session_write(InfdStorage* storage,
                                         InfSession* session,
                                         const gchar* path,
                                         gpointer user_data,
                                         GError** error)
{
  xmlNodePtr root;

  FILE* stream;
  xmlDocPtr doc;
  xmlErrorPtr xmlerror;

  g_assert(INFD_IS_FILESYSTEM_STORAGE(storage));
  g_assert(INF_IS_CHAT_SESSION(session));

  /* Open stream before exporting buffer to XML so possible errors are
   * catched earlier. */
  stream = infd_filesystem_storage_open(
    INFD_FILESYSTEM_STORAGE(storage),
    "InfChat",
    path,
    "w",
    NULL,
    error
  );

  if(stream == NULL)
    return FALSE;

  root = xmlNewNode(NULL, (const xmlChar*)"inf-chat-session");

  doc = xmlNewDoc((const xmlChar*)"1.0");
  xmlDocSetRootElement(doc, root);

  /* TODO: At this point, we should tell libxml2 to use
   * infd_filesystem_storage_stream_write() instead of fwrite(),
   * to prevent C runtime mixups. */
  if(xmlDocFormatDump(stream, doc, 1) == -1)
  {
    xmlerror = xmlGetLastError();
    infd_filesystem_storage_stream_close(stream);
    xmlFreeDoc(doc);

    g_set_error(
      error,
      g_quark_from_static_string("LIBXML2_OUTPUT_ERROR"),
      xmlerror->code,
      "%s",
      xmlerror->message
    );

    return FALSE;
  }

  infd_filesystem_storage_stream_close(stream);
  xmlFreeDoc(doc);
  return TRUE;
}

const InfdNotePlugin INFINOTED_PLUGIN_NOTE_CHAT_PLUGIN = {
  NULL,
  "InfdFilesystemStorage",
  "InfChat",
  infinoted_plugin_note_chat_session_new,
  infinoted_plugin_note_chat_session_read,
  infinoted_plugin_note_chat_session_write
};

/* Infinoted plugin glue */
static void
infinoted_plugin_note_chat_info_initialize(gpointer plugin_info)
{
  InfinotedPluginNoteChat* plugin;
  plugin = (InfinotedPluginNoteChat*)plugin_info;

  plugin->manager = NULL;
  plugin->plugin = NULL;
}

static gboolean
infinoted_plugin_note_chat_initialize(InfinotedPluginManager* manager,
                                      gpointer plugin_info,
                                      GError** error)
{
  InfinotedPluginNoteChat* plugin;
  gboolean result;

  plugin = (InfinotedPluginNoteChat*)plugin_info;

  plugin->manager = manager;

  result = infd_directory_add_plugin(
    infinoted_plugin_manager_get_directory(manager),
    &INFINOTED_PLUGIN_NOTE_CHAT_PLUGIN
  );

  if(result != TRUE)
  {
    g_set_error(
      error,
      g_quark_from_static_string("INFINOTED_PLUGIN_NOTE_CHAT_ERROR"),
      INFINOTED_PLUGIN_NOTE_CHAT_ERROR_TYPE_ALREADY_HANDLED,
      _("There is a already a plugin which handles sessions of type \"%s\""),
      INFINOTED_PLUGIN_NOTE_CHAT_PLUGIN.note_type
    );

    return FALSE;
  }

  plugin->plugin = &INFINOTED_PLUGIN_NOTE_CHAT_PLUGIN;
  return TRUE;
}

static void
infinoted_plugin_note_chat_deinitialize(gpointer plugin_info)
{
  InfinotedPluginNoteChat* plugin;
  plugin = (InfinotedPluginNoteChat*)plugin_info;

  /* Note that this kills all sessions with that particular type. This is
   * typically not wanted when reloading a plugin in which case a plugin is
   * deinitialized and then re-initialized. */
  /* TODO: To fix this, we should add a plugin API to reload its parameters
   * without unloading and reloading the whole plugin. */
  if(plugin->plugin != NULL)
  {
    infd_directory_remove_plugin(
      infinoted_plugin_manager_get_directory(plugin->manager),
      plugin->plugin
    );

    plugin->plugin = NULL;
  }
}

static const InfinotedParameterInfo INFINOTED_PLUGIN_NOTE_CHAT_OPTIONS[] = {
  {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "note-chat",
  N_("Adds support to handle chat documents."),
  INFINOTED_PLUGIN_NOTE_CHAT_OPTIONS,
  sizeof(InfinotedPluginNoteChat),
  0,
  0,
  NULL,
  infinoted_plugin_note_chat_info_initialize,
  infinoted_plugin_note_chat_initialize,
  infinoted_plugin_note_chat_deinitialize,
  NULL,
  NULL,
  NULL,
  NULL
};

/* vim:set et sw=2 ts=2: */
