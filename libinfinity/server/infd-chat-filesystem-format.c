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

/**
 * SECTION:infd-chat-filesystem-format
 * @title: Storage of chat sessions on the file system
 * @short_description: Utility functions to deal with storing
 * #InfChatSession<!-- -->s in filesystem storage
 * @include: libinfinity/server/infd-chat-filesystem-storage.h
 * @see_also: #InfChatSession, #InfdFilesystemStorage
 * @stability: Unstable
 *
 * The functions in this section are utility functions that can be used when
 * implementing a #InfdNotePlugin to handle #InfChatSession<!-- -->s. These
 * functions implement reading and writing the content of an #InfChatSession
 * to an XML file in the storage.
 */

#include <libinfinity/server/infd-chat-filesystem-format.h>
#include <libinfinity/inf-i18n.h>

#include <string.h>

static GQuark
infd_chat_filesystem_format_error_quark()
{
  return g_quark_from_static_string("INFD_CHAT_FILESYSTEM_FORMAT_ERROR");
}

static int
infd_chat_filesystem_format_read_read_func(void* context,
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
infd_chat_filesystem_format_read_close_func(void* context)
{
  return infd_filesystem_storage_stream_close((FILE*)context);
}

/**
 * infd_chat_filesystem_format_read:
 * @storage: A #InfdFilesystemStorage.
 * @path: Storage path to retrieve the session from.
 * @buffer: An empty #InfTextBuffer to use as the new session's buffer.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Reads a chat session from @path in @storage. The file is expected to have
 * been saved with infd_chat_filesystem_format_write() before. The @buffer
 * parameter should be an empty #InfChatBuffer, and the document will be
 * written into this buffer. If the function succeeds, the buffer can be used
 * to create an #InfChatSession with inf_chat_session_new(). If the function 
 * fails, %FALSE is returned and @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
infd_chat_filesystem_format_read(InfdFilesystemStorage* storage,
                                 const gchar* path,
                                 InfChatBuffer* buffer,
                                 GError** error)
{
  FILE* stream;
  gchar* full_path;
  gchar* uri;

  xmlDocPtr doc;
  xmlErrorPtr xmlerror;
  xmlNodePtr root;
  xmlNodePtr child;
  gboolean result;

  g_return_val_if_fail(INFD_IS_FILESYSTEM_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(INF_IS_CHAT_BUFFER(buffer), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  /* TODO: Use a SAX parser for better performance */
  full_path = NULL;
  stream = infd_filesystem_storage_open(
    INFD_FILESYSTEM_STORAGE(storage),
    "InfChat",
    path,
    "r",
    &full_path,
    error
  );

  if(stream == NULL)
  {
    g_free(full_path);
    return FALSE;
  }

  uri = g_filename_to_uri(full_path, NULL, error);
  g_free(full_path);

  if(uri == NULL)
    return FALSE;

  doc = xmlReadIO(
    infd_chat_filesystem_format_read_read_func,
    infd_chat_filesystem_format_read_close_func,
    stream,
    uri,
    "UTF-8",
    XML_PARSE_NOWARNING | XML_PARSE_NOERROR
  );

  g_free(uri);

  if(doc == NULL)
  {
    xmlerror = xmlGetLastError();

    g_set_error(
      error,
      g_quark_from_static_string("LIBXML2_PARSER_ERROR"),
      xmlerror->code,
      _("Error parsing XML in file \"%s\": [%d]: %s"),
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
        infd_chat_filesystem_format_error_quark(),
        INFD_CHAT_FILESYSTEM_FORMAT_ERROR_NOT_A_CHAT_SESSION,
        _("Error processing file \"%s\": %s"),
        path,
        _("The document is not a chat session")
      );

      result = FALSE;
    }
    else
    {
      result = TRUE;
    }

    xmlFreeDoc(doc);
  }

  if(result == FALSE)
    return FALSE;

  return TRUE;
}

/**
 * infd_chat_filesystem_format_write:
 * @storage: A #InfdFilesystemStorage.
 * @path: Storage path where to write the session to.
 * @buffer: The #InfChatBuffer to write.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Writes the given buffer into the filesystem storage at @path. If
 * successful, the session can then be read back with
 * infd_chat_filesystem_format_read(). If the function fails, %FALSE is
 * returned and @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
infd_chat_filesystem_format_write(InfdFilesystemStorage* storage,
                                  const gchar* path,
                                  InfChatBuffer* buffer,
                                  GError** error)
{
  FILE* stream;
  xmlDocPtr doc;
  xmlNodePtr root;
  xmlErrorPtr xmlerror;

  g_return_val_if_fail(INFD_IS_FILESYSTEM_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(INF_IS_CHAT_BUFFER(buffer), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

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

/* vim:set et sw=2 ts=2: */
