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

#include <libinfinity/server/infd-note-plugin.h>
#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-storage.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-connection-manager.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/common/inf-xml-util.h>

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-buffer.h>
#include <libinftext/inf-text-user.h>

#include <string.h>

/* TODO: Expose them to the client library? */
typedef enum InfdNotePluginTextError {
  INFD_NOTE_PLUGIN_TEXT_ERROR_NOT_A_TEXT_SESSION,
  INFD_NOTE_PLUGIN_TEXT_ERROR_USER_EXISTS,
  INFD_NOTE_PLUGIN_TEXT_ERROR_NO_SUCH_USER,
  INFD_NOTE_PLUGIN_TEXT_ERROR_UNEXPECTED_NODE
} InfdNotePluginTextError;

static InfSession*
infd_note_plugin_text_session_new(InfIo* io,
                                  InfConnectionManager* manager,
                                  InfConnectionManagerGroup* sync_group,
                                  InfXmlConnection* sync_connection)
{
  InfTextSession* session;

  session = inf_text_session_new(
    manager,
    INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8")),
    io,
    sync_group,
    sync_connection
  );

  return INF_SESSION(session);
}

static int
infd_note_plugin_text_session_read_read_func(void* context,
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
infd_note_plugin_text_sesison_read_close_func(void* context)
{
  return fclose((FILE*)context);
}

static gboolean
infd_note_plugin_text_session_unexpected_node(xmlNodePtr node,
                                              GError** error)
{
  g_set_error(
    error,
    g_quark_from_static_string("INF_NOTE_PLUGIN_TEXT_ERROR"),
    INFD_NOTE_PLUGIN_TEXT_ERROR_UNEXPECTED_NODE,
    "Node `%s' unexpected",
    (const gchar*)node->name
  );

  return FALSE;
}

static gboolean
infd_note_plugin_text_read_user(InfUserTable* user_table,
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
      g_quark_from_static_string("INF_NOTE_PLUGIN_TEXT_ERROR"),
      INFD_NOTE_PLUGIN_TEXT_ERROR_USER_EXISTS,
      "User with ID %u exists already",
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
        g_quark_from_static_string("INF_NOTE_PLUGIN_TEXT_ERROR"),
        INFD_NOTE_PLUGIN_TEXT_ERROR_USER_EXISTS,
        "User with name `%s' exists already",
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
infd_note_plugin_text_read_buffer(InfTextBuffer* buffer,
                                  InfUserTable* user_table,
                                  xmlNodePtr node,
                                  GError** error)
{
  xmlNodePtr child;
  guint author;
  xmlChar* content;
  gboolean result;
  gboolean res;
  InfUser* user;

  g_assert(inf_text_buffer_get_length(buffer) == 0);

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
      {
        result = FALSE;
        break;
      }

      if(author != 0)
      {
        user = inf_user_table_lookup_user_by_id(user_table, author);

        if(user == NULL)
        {
          g_set_error(
            error,
            g_quark_from_static_string("INF_NOTE_PLUGIN_TEXT_ERROR"),
            INFD_NOTE_PLUGIN_TEXT_ERROR_NO_SUCH_USER,
            "User with ID %u does not exist",
            author
          );

          result = FALSE;
          break;
        }
      }
      else
      {
        user = NULL;
      }

      content = xmlNodeGetContent(child);
      if(content != NULL)
      {
        if(*content != '\0')
        {
          /* TODO: Use inf_text_buffer_append when we have it */
          inf_text_buffer_insert_text(
            buffer,
            inf_text_buffer_get_length(buffer),
            content,
            strlen((const char*)content),
            g_utf8_strlen((const gchar*)content, -1),
            user
          );
        }

        xmlFree(content);
      }
    }
    else
    {
      infd_note_plugin_text_session_unexpected_node(child, error);
      result = FALSE;
      break;
    }
  }

  if(child == NULL)
    result = TRUE;

  return result;
}

static InfSession*
infd_note_plugin_text_session_read(InfdStorage* storage,
                                   InfIo* io,
                                   InfConnectionManager* manager,
                                   const gchar* path,
                                   GError** error)
{
  InfUserTable* user_table;
  InfTextBuffer* buffer;
  InfTextSession* session;

  FILE* stream;
  xmlDocPtr doc;
  xmlErrorPtr xmlerror;
  xmlNodePtr root;
  xmlNodePtr child;
  gboolean result;

  g_assert(INFD_IS_FILESYSTEM_STORAGE(storage));

  user_table = inf_user_table_new();
  buffer = INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8"));

  /* TODO: Use a SAX parser for better performance */
  stream = infd_filesystem_storage_open(
    INFD_FILESYSTEM_STORAGE(storage),
    "InfText",
    path,
    "r",
    error
  );

  if(stream == NULL) return FALSE;

  doc = xmlReadIO(
    infd_note_plugin_text_session_read_read_func,
    infd_note_plugin_text_sesison_read_close_func,
    stream,
    path, /* TODO: Get some "infinote-filesystem-storage://" URL? */
    "UTF-8",
    XML_PARSE_NOWARNING | XML_PARSE_NOERROR
  );

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
    if(strcmp((const char*)root->name, "inf-text-session") != 0)
    {
      g_set_error(
        error,
        g_quark_from_static_string("INF_NOTE_PLUGIN_TEXT_ERROR"),
        INFD_NOTE_PLUGIN_TEXT_ERROR_NOT_A_TEXT_SESSION,
        "Error processing file '%s': %s",
        path,
        "The document is not a text session"
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
          if(!infd_note_plugin_text_read_user(user_table, child, error))
          {
            g_prefix_error(error, "Error processing file '%s': ", path);
            result = FALSE;
            break;
          }
        }
        else if(strcmp((const char*)child->name, "buffer") == 0)
        {
          if(!infd_note_plugin_text_read_buffer(buffer, user_table,
                                                child, error))
          {
            g_prefix_error(error, "Error processing file '%s': ", path);
            result = FALSE;
            break;
          }
        }
        else
        {
          infd_note_plugin_text_session_unexpected_node(child, error);
          g_prefix_error(error, "Error processing file '%s': ", path);
          result = FALSE;
          break;
        }
      }

      if(child == NULL)
        result = TRUE;
    }

    xmlFreeDoc(doc);
  }

  if(result == FALSE)
    return NULL;

  session = inf_text_session_new_with_user_table(
    manager,
    buffer,
    io,
    user_table,
    NULL,
    NULL
  );

  return INF_SESSION(session);
}

static void
infd_note_plugin_text_session_write_foreach_user_func(InfUser* user,
                                                      gpointer user_data)
{
  xmlNodePtr parent;
  xmlNodePtr node;

  parent = (xmlNodePtr)user_data;
  node = xmlNewChild(parent, NULL, (const xmlChar*)"user", NULL);

  inf_xml_util_set_attribute_uint(node, "id", inf_user_get_id(user));
  inf_xml_util_set_attribute(node, "name", inf_user_get_name(user));
  inf_xml_util_set_attribute_double(
    node,
    "hue",
    inf_text_user_get_hue(INF_TEXT_USER(user))
  );
}

static gboolean
infd_note_plugin_text_session_write(InfdStorage* storage,
                                    InfSession* session,
                                    const gchar* path,
                                    GError** error)
{
  InfUserTable* table;
  InfTextBuffer* buffer;
  InfTextBufferIter* iter;
  xmlNodePtr root;
  xmlNodePtr buffer_node;
  xmlNodePtr segment_node;

  guint author;
  gchar* content;
  gsize bytes;

  FILE* stream;
  xmlDocPtr doc;
  xmlErrorPtr xmlerror;

  g_assert(INFD_IS_FILESYSTEM_STORAGE(storage));
  g_assert(INF_TEXT_IS_SESSION(session));

  /* Open stream before exporting buffer to XML so possible errors are
   * catched earlier. */
  stream = infd_filesystem_storage_open(
    INFD_FILESYSTEM_STORAGE(storage),
    "InfText",
    path,
    "w",
    error
  );

  if(stream == NULL)
    return FALSE;

  root = xmlNewNode(NULL, (const xmlChar*)"inf-text-session");
  buffer = INF_TEXT_BUFFER(inf_session_get_buffer(session));
  table = inf_session_get_user_table(session);

  inf_user_table_foreach_user(
    table,
    infd_note_plugin_text_session_write_foreach_user_func,
    root
  );

  buffer_node = xmlNewChild(root, NULL, (const xmlChar*)"buffer", NULL);
  iter = inf_text_buffer_create_iter(buffer);
  if(iter != NULL)
  {
    do
    {
      author = inf_text_buffer_iter_get_author(buffer, iter);
      content = inf_text_buffer_iter_get_text(buffer, iter);
      bytes = inf_text_buffer_iter_get_bytes(buffer, iter);

      segment_node = xmlNewChild(
        buffer_node,
        NULL,
        (const xmlChar*)"segment",
        NULL
      );

      inf_xml_util_set_attribute_uint(segment_node, "author", author);
      xmlNodeAddContentLen(segment_node, (const xmlChar*)content, (int)bytes);
      g_free(content);
    } while(inf_text_buffer_iter_next(buffer, iter));

    inf_text_buffer_destroy_iter(buffer, iter);
  }

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
  "InfdFilesystemStorage",
  "InfText",
  infd_note_plugin_text_session_new,
  infd_note_plugin_text_session_read,
  infd_note_plugin_text_session_write
};

/* vim:set et sw=2 ts=2: */
