/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2015 Armin Burgmeier <armin@arbur.net>
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

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-filesystem-format.h>

#include <libinfinity/inf-i18n.h>

typedef struct _InfinotedPluginNoteText InfinotedPluginNoteText;
struct _InfinotedPluginNoteText {
  InfinotedPluginManager* manager;
  const InfdNotePlugin* plugin;
};

/* Note plugin implementation */
static InfSession*
infinoted_plugin_note_text_session_new(InfIo* io,
                                       InfCommunicationManager* manager,
                                       InfSessionStatus status,
                                       InfCommunicationGroup* sync_group,
                                       InfXmlConnection* sync_connection,
                                       const gchar* path,
                                       gpointer user_data)
{
  InfTextSession* session;
  InfTextBuffer* buffer;

  buffer = INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8"));

  session = inf_text_session_new(
    manager,
    buffer,
    io,
    status,
    sync_group,
    sync_connection
  );

  g_object_unref(buffer);

  return INF_SESSION(session);
}

static InfSession*
infinoted_plugin_note_text_session_read(InfdStorage* storage,
                                        InfIo* io,
                                        InfCommunicationManager* manager,
                                        const gchar* path,
                                        gpointer user_data,
                                        GError** error)
{
  InfUserTable* user_table;
  InfTextBuffer* buffer;
  gboolean result;
  InfTextSession* session;

  g_assert(INFD_IS_FILESYSTEM_STORAGE(storage));

  user_table = inf_user_table_new();
  buffer = INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8"));

  result = inf_text_filesystem_format_read(
    INFD_FILESYSTEM_STORAGE(storage),
    path,
    user_table,
    buffer,
    error
  );

  if(result == FALSE)
  {
    g_object_unref(user_table);
    g_object_unref(buffer);
    return NULL;
  }

  session = inf_text_session_new_with_user_table(
    manager,
    buffer,
    io,
    user_table,
    INF_SESSION_RUNNING,
    NULL,
    NULL
  );

  g_object_unref(user_table);
  g_object_unref(buffer);

  return INF_SESSION(session);
}

static gboolean
infinoted_plugin_note_text_session_write(InfdStorage* storage,
                                         InfSession* session,
                                         const gchar* path,
                                         gpointer user_data,
                                         GError** error)
{
  return inf_text_filesystem_format_write(
    INFD_FILESYSTEM_STORAGE(storage),
    path,
    inf_session_get_user_table(session),
    INF_TEXT_BUFFER(inf_session_get_buffer(session)),
    error
  );
}

const InfdNotePlugin INFINOTED_PLUGIN_NOTE_TEXT_PLUGIN = {
  NULL,
  "InfdFilesystemStorage",
  "InfText",
  infinoted_plugin_note_text_session_new,
  infinoted_plugin_note_text_session_read,
  infinoted_plugin_note_text_session_write
};

/* Infinoted plugin glue */
static void
infinoted_plugin_note_text_info_initialize(gpointer plugin_info)
{
  InfinotedPluginNoteText* plugin;
  plugin = (InfinotedPluginNoteText*)plugin_info;

  plugin->manager = NULL;
  plugin->plugin = NULL;
}

static gboolean
infinoted_plugin_note_text_initialize(InfinotedPluginManager* manager,
                                      gpointer plugin_info,
                                      GError** error)
{
  InfinotedPluginNoteText* plugin;
  gboolean result;

  plugin = (InfinotedPluginNoteText*)plugin_info;

  plugin->manager = manager;

  result = infd_directory_add_plugin(
    infinoted_plugin_manager_get_directory(manager),
    &INFINOTED_PLUGIN_NOTE_TEXT_PLUGIN
  );

  if(result != TRUE)
  {
    g_set_error(
      error,
      g_quark_from_static_string("INFINOTED_PLUGIN_NOTE_TEXT_ERROR"),
      0,
      _("There is a already a plugin which handles sessions of type \"%s\""),
      INFINOTED_PLUGIN_NOTE_TEXT_PLUGIN.note_type
    );

    return FALSE;
  }

  plugin->plugin = &INFINOTED_PLUGIN_NOTE_TEXT_PLUGIN;
  return TRUE;
}

static void
infinoted_plugin_note_text_deinitialize(gpointer plugin_info)
{
  InfinotedPluginNoteText* plugin;
  plugin = (InfinotedPluginNoteText*)plugin_info;

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

static const InfinotedParameterInfo INFINOTED_PLUGIN_NOTE_TEXT_OPTIONS[] = {
  {
    NULL,
    0,
    0,
    0,
    NULL
  }
};

const InfinotedPlugin INFINOTED_PLUGIN = {
  "note-text",
  N_("Adds support to handle plain text documents."),
  INFINOTED_PLUGIN_NOTE_TEXT_OPTIONS,
  sizeof(InfinotedPluginNoteText),
  0,
  0,
  NULL,
  infinoted_plugin_note_text_info_initialize,
  infinoted_plugin_note_text_initialize,
  infinoted_plugin_note_text_deinitialize,
  NULL,
  NULL,
  NULL,
  NULL
};

/* vim:set et sw=2 ts=2: */
