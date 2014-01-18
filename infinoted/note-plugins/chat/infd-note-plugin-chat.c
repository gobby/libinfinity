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

#include <libinfinity/server/infd-note-plugin.h>
#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-storage.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-chat-session.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/communication/inf-communication-manager.h>

#include <string.h>

/* TODO: Expose them to the client library? */
typedef enum InfdNotePluginChatError {
  INFD_NOTE_PLUGIN_CHAT_ERROR_NOT_A_CHAT_SESSION
} InfdNotePluginChatError;

static InfSession*
infd_note_plugin_chat_session_new(InfIo* io,
                                  InfCommunicationManager* manager,
                                  InfSessionStatus status,
                                  InfCommunicationHostedGroup* sync_group,
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
infd_note_plugin_chat_session_read_read_func(void* context,
                                             char* buffer,
                                             int len)
{
  int res;
  res = fread(buffer, 1, len, (FILE*)context);

  if(ferror((FILE*)context))
    return -1;

  return res;
}

static int
infd_note_plugin_chat_sesison_read_close_func(void* context)
{
  return fclose((FILE*)context);
}

static InfSession*
infd_note_plugin_chat_session_read(InfdStorage* storage,
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
    infd_note_plugin_chat_session_read_read_func,
    infd_note_plugin_chat_sesison_read_close_func,
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
        g_quark_from_static_string("INF_NOTE_PLUGIN_CHAT_ERROR"),
        INFD_NOTE_PLUGIN_CHAT_ERROR_NOT_A_CHAT_SESSION,
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
infd_note_plugin_chat_session_write(InfdStorage* storage,
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

  if(xmlDocFormatDump(stream, doc, 1) == -1)
  {
    xmlerror = xmlGetLastError();
    fclose(stream);
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

  fclose(stream);
  xmlFreeDoc(doc);
  return TRUE;
}

const InfdNotePlugin INFD_NOTE_PLUGIN = {
  NULL,
  "InfdFilesystemStorage",
  "InfChat",
  infd_note_plugin_chat_session_new,
  infd_note_plugin_chat_session_read,
  infd_note_plugin_chat_session_write
};

/* vim:set et sw=2 ts=2: */
