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
 * SECTION:inf-text-filesystem-format
 * @title: Storage of text sessions on the file system
 * @short_description: Utility functions to deal with storing
 * InfTextSessions in filesystem storage
 * @include: libinftext/inf-text-filesystem-storage.h
 * @see_also: #InfTextSession, #InfdFilesystemStorage
 * @stability: Unstable
 *
 * The functions in this section are utility functions that can be used when
 * implementing a #InfdNotePlugin to handle #InfTextSession<!-- -->s. These
 * functions implement reading and writing the content of an #InfTextSession
 * to an XML file in the storage.
 */

#include <libinftext/inf-text-filesystem-format.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/inf-i18n.h>

typedef struct _InfTextFilesystemFormatWriteData {
  xmlNodePtr root;
  GHashTable* encountered_authors;
} InfTextFilesystemFormatWriteData;

static GQuark
inf_text_filesystem_format_error_quark()
{
  return g_quark_from_static_string("INF_TEXT_FILESYSTEM_FORMAT_ERROR");
}

static int
inf_text_filesystem_format_read_read_func(void* context,
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
inf_text_filesystem_format_read_close_func(void* context)
{
  return infd_filesystem_storage_stream_close((FILE*)context);
}

static gboolean
inf_text_filesystem_format_read_user(InfUserTable* user_table,
                                     xmlNodePtr node,
                                     GError** error)
{
  guint id;
  gdouble hue;
  xmlChar* name;
  gboolean result;
  InfUser* user;

  if(!inf_xml_util_get_attribute_uint_required(node, "id", &id, error))
    return FALSE;

  if(!inf_xml_util_get_attribute_double_required(node, "hue", &hue, error))
    return FALSE;

  name = inf_xml_util_get_attribute_required(node, "name", error);
  if(name == NULL)
    return FALSE;

  if(inf_user_table_lookup_user_by_id(user_table, id) != NULL)
  {
    g_set_error(
      error,
      inf_text_filesystem_format_error_quark(),
      INF_TEXT_FILESYSTEM_FORMAT_ERROR_USER_EXISTS,
      _("User with ID %u exists already"),
      id
    );

    result = FALSE;
  }
  else
  {
    if(inf_user_table_lookup_user_by_name(user_table, (const gchar*)name))
    {
      g_set_error(
        error,
        inf_text_filesystem_format_error_quark(),
        INF_TEXT_FILESYSTEM_FORMAT_ERROR_USER_EXISTS,
        _("User with name \"%s\" exists already"),
        (const gchar*)name
      );

      result = FALSE;
    }
    else
    {
      user = INF_USER(
        g_object_new(
          INF_TEXT_TYPE_USER,
          "id", id,
          "name", name,
          "hue", hue,
          NULL
        )
      );

      inf_user_table_add_user(user_table, user);
      g_object_unref(user);
      result = TRUE;
    }
  }

  xmlFree(name);
  return result;
}

static gboolean
inf_text_filesystem_format_read_buffer(InfTextBuffer* buffer,
                                       InfUserTable* user_table,
                                       xmlNodePtr node,
                                       GError** error)
{
  xmlNodePtr child;
  guint author;
  gchar* content;
  gboolean result;
  gboolean res;
  InfUser* user;
  gsize bytes;
  guint chars;

  gboolean is_utf8;
  gchar* converted;
  gsize converted_bytes;

  g_assert(inf_text_buffer_get_length(buffer) == 0);

  is_utf8 = TRUE;
  if(strcmp(inf_text_buffer_get_encoding(buffer), "UTF-8") != 0)
    is_utf8 = FALSE;

  for(child = node->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE)
      continue;

    if(strcmp((const gchar*)child->name, "segment") == 0)
    {
      res = inf_xml_util_get_attribute_uint_required(
        child,
        "author",
        &author,
        error
      );

      if(res == FALSE)
        return FALSE;

      if(author != 0)
      {
        user = inf_user_table_lookup_user_by_id(user_table, author);

        if(user == NULL)
        {
          g_set_error(
            error,
            g_quark_from_static_string("INF_NOTE_PLUGIN_TEXT_ERROR"),
            INF_TEXT_FILESYSTEM_FORMAT_ERROR_NO_SUCH_USER,
            _("User with ID \"%u\" does not exist"),
            author
          );

          return FALSE;
        }
      }
      else
      {
        user = NULL;
      }

      content = inf_xml_util_get_child_text(child, &bytes, &chars, error);
      if(!content) return FALSE;

      if(*content != '\0')
      {
        if(is_utf8)
        {
          inf_text_buffer_insert_text(
            buffer,
            inf_text_buffer_get_length(buffer),
            content,
            bytes,
            chars,
            user
          );

          g_free(content);
        }
        else
        {
          /* Convert from UTF-8 to buffer encoding */
          converted = g_convert(
            content,
            bytes,
            inf_text_buffer_get_encoding(buffer),
            "UTF-8",
            NULL,
            &converted_bytes, error
          );

          g_free(content);

          if(converted == NULL)
            return FALSE;

          inf_text_buffer_insert_text(
            buffer,
            inf_text_buffer_get_length(buffer),
            converted,
            converted_bytes,
            chars,
            user
          );

          g_free(converted);
        }
      }
    }
  }

  return TRUE;
}

static void
inf_text_filesystem_format_write_foreach_user_func(InfUser* user,
                                                   gpointer user_data)
{
  InfTextFilesystemFormatWriteData* data;
  gpointer user_id;
  xmlNodePtr node;

  data = (InfTextFilesystemFormatWriteData*)user_data;
  user_id = GUINT_TO_POINTER(inf_user_get_id(user));

  /* TODO: Use g_hash_table_contains when we can use glib 2.32 */
  if(g_hash_table_lookup(data->encountered_authors, user_id) != NULL)
  {
    node = xmlNewChild(data->root, NULL, (const xmlChar*)"user", NULL);

    inf_xml_util_set_attribute_uint(node, "id", inf_user_get_id(user));
    inf_xml_util_set_attribute(node, "name", inf_user_get_name(user));
    inf_xml_util_set_attribute_double(
      node,
      "hue",
      inf_text_user_get_hue(INF_TEXT_USER(user))
    );
  }
}

/**
 * inf_text_filesystem_format_read:
 * @storage: A #InfdFilesystemStorage.
 * @path: Storage path to retrieve the session from.
 * @user_table: An empty #InfUserTable to use as the new session's user table.
 * @buffer: An empty #InfTextBuffer to use as the new session's buffer.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Reads a text session from @path in @storage. The file is expected to have
 * been saved with inf_text_filesystem_format_write() before. The @user_table
 * parameter should be an empty user table that will be used for the session,
 * and the @buffer parameter should be an empty #InfTextBuffer, and the
 * document will be written into this buffer. If the function succeeds, the
 * user table and buffer can be used to create an #InfTextSession with
 * inf_text_session_new_with_user_table(). If the function fails, %FALSE is
 * returned and @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_text_filesystem_format_read(InfdFilesystemStorage* storage,
                                const gchar* path,
                                InfUserTable* user_table,
                                InfTextBuffer* buffer,
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

  g_return_val_if_fail(INFD_IS_FILESYSTEM_STORAGE(storage), NULL);
  g_return_val_if_fail(path != NULL, NULL);
  g_return_val_if_fail(INF_TEXT_IS_BUFFER(buffer), NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);
  g_return_val_if_fail(inf_text_buffer_get_length(buffer) == 0, NULL);

  /* TODO: Use a SAX parser for better performance */
  full_path = NULL;
  stream = infd_filesystem_storage_open(
    INFD_FILESYSTEM_STORAGE(storage),
    "InfText",
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
    inf_text_filesystem_format_read_read_func,
    inf_text_filesystem_format_read_close_func,
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
    if(strcmp((const char*)root->name, "inf-text-session") != 0)
    {
      g_set_error(
        error,
        inf_text_filesystem_format_error_quark(),
        INF_TEXT_FILESYSTEM_FORMAT_ERROR_NOT_A_TEXT_SESSION,
        _("Error processing file \"%s\": %s"),
        path,
        _("The document is not a text session")
      );

      result = FALSE;
    }
    else
    {
      for(child = root->children; child != NULL; child = child->next)
      {
        if(child->type != XML_ELEMENT_NODE)
          continue;

        if(strcmp((const char*)child->name, "user") == 0)
        {
          if(!inf_text_filesystem_format_read_user(user_table, child, error))
          {
            g_prefix_error(error, _("Error processing file \"%s\": "), path);
            result = FALSE;
            break;
          }
        }
        else if(strcmp((const char*)child->name, "buffer") == 0)
        {
          if(!inf_text_filesystem_format_read_buffer(buffer, user_table,
                                                     child, error))
          {
            g_prefix_error(error, _("Error processing file \"%s\": "), path);
            result = FALSE;
            break;
          }
        }
      }

      if(child == NULL)
        result = TRUE;
    }

    xmlFreeDoc(doc);
  }

  return result;
}

/**
 * inf_text_filesystem_format_write:
 * @storage: A #InfdFilesystemStorage.
 * @path: Storage path where to write the session to.
 * @user_table: The #InfUserTable to write.
 * @buffer: The #InfTextBuffer to write.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Writes the given user table and buffer into the filesystem storage at
 * @path. If successful, the session can then be read back with
 * inf_text_filesystem_format_read(). If the function fails, %FALSE is
 * returned and @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_text_filesystem_format_write(InfdFilesystemStorage* storage,
                                 const gchar* path,
                                 InfUserTable* user_table,
                                 InfTextBuffer* buffer,
                                 GError** error)
{
  InfTextBufferIter* iter;
  xmlNodePtr buffer_node;
  xmlNodePtr segment_node;

  guint author;
  gchar* content;
  gsize bytes;
  gchar* converted;
  gsize converted_bytes;

  FILE* stream;
  xmlDocPtr doc;
  xmlErrorPtr xmlerror;
  gboolean is_utf8;

  InfTextFilesystemFormatWriteData data;

  g_return_val_if_fail(INFD_IS_FILESYSTEM_STORAGE(storage), FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(INF_IS_USER_TABLE(user_table), FALSE);
  g_return_val_if_fail(INF_TEXT_IS_BUFFER(buffer), FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  is_utf8 = TRUE;
  if(strcmp(inf_text_buffer_get_encoding(buffer), "UTF-8") != 0)
    is_utf8 = FALSE;

  /* Open stream before exporting buffer to XML so possible errors are
   * catched earlier. */
  stream = infd_filesystem_storage_open(
    INFD_FILESYSTEM_STORAGE(storage),
    "InfText",
    path,
    "w",
    NULL,
    error
  );

  if(stream == NULL)
    return FALSE;

  data.root = xmlNewNode(NULL, (const xmlChar*)"inf-text-session");
  data.encountered_authors = g_hash_table_new(NULL, NULL);

  buffer_node = xmlNewNode(NULL, (const xmlChar*)"buffer");
  iter = inf_text_buffer_create_begin_iter(buffer);
  if(iter != NULL)
  {
    do
    {
      author = inf_text_buffer_iter_get_author(buffer, iter);
      content = inf_text_buffer_iter_get_text(buffer, iter);
      bytes = inf_text_buffer_iter_get_bytes(buffer, iter);

      /* TODO: Use g_hash_table_add with glib 2.32 */
      g_hash_table_insert(
        data.encountered_authors,
        GUINT_TO_POINTER(author),
        GUINT_TO_POINTER(author)
      );

      segment_node = xmlNewChild(
        buffer_node,
        NULL,
        (const xmlChar*)"segment",
        NULL
      );

      inf_xml_util_set_attribute_uint(segment_node, "author", author);

      if(is_utf8)
      {
        /* Buffer is UTF-8, no conversion necessary */
        inf_xml_util_add_child_text(segment_node, content, bytes);
        g_free(content);
      }
      else
      {
        /* Convert from buffer encoding to UTF-8 for storage */
        converted = g_convert(
          content,
          bytes,
          "UTF-8",
          inf_text_buffer_get_encoding(buffer),
          NULL,
          &converted_bytes,
          error
        );

        g_free(content);

        if(converted == NULL)
        {
          xmlFreeNode(buffer_node);
          xmlFreeNode(data.root);
          g_hash_table_destroy(data.encountered_authors);
          infd_filesystem_storage_stream_close(stream);
          return FALSE;
        }

        inf_xml_util_add_child_text(segment_node, converted, converted_bytes);
        g_free(converted);
      }
    } while(inf_text_buffer_iter_next(buffer, iter));

    inf_text_buffer_destroy_iter(buffer, iter);
  }

  /* After we wrote the buffer, now write the user table, but only for those
   * users that have contributed to the document. The others we drop, to
   * avoid cluttering the user table too much. */
  inf_user_table_foreach_user(
    user_table,
    inf_text_filesystem_format_write_foreach_user_func,
    &data
  );

  g_hash_table_destroy(data.encountered_authors);

  /* Write the buffer after the users */
  xmlAddChild(data.root, buffer_node);

  doc = xmlNewDoc((const xmlChar*)"1.0");
  xmlDocSetRootElement(doc, data.root);

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
