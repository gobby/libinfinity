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

#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-chat-filesystem-format.h>
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
                                       const gchar* path,
                                       gpointer user_data)
{
  InfChatBuffer* buffer;
  InfChatSession* session;

  buffer = inf_chat_buffer_new(256);

  session = inf_chat_session_new(
    manager,
    buffer,
    status,
    INF_COMMUNICATION_GROUP(sync_group),
    sync_connection
  );

  g_object_unref(buffer);
  return INF_SESSION(session);
}

static InfSession*
infinoted_plugin_note_chat_session_read(InfdStorage* storage,
                                        InfIo* io,
                                        InfCommunicationManager* manager,
                                        const gchar* path,
                                        gpointer user_data,
                                        GError** error)
{
  InfChatBuffer* buffer;
  gboolean result;
  InfChatSession* session;

  g_assert(INFD_IS_FILESYSTEM_STORAGE(storage));

  buffer = inf_chat_buffer_new(256);

  result = infd_chat_filesystem_format_read(
    INFD_FILESYSTEM_STORAGE(storage),
    path,
    buffer,
    error
  );

  if(result == FALSE)
  {
    g_object_unref(buffer);
    return NULL;
  }

  session = inf_chat_session_new(
    manager,
    buffer,
    INF_SESSION_RUNNING,
    NULL,
    NULL
  );

  g_object_unref(buffer);
  return INF_SESSION(session);
}

static gboolean
infinoted_plugin_note_chat_session_write(InfdStorage* storage,
                                         InfSession* session,
                                         const gchar* path,
                                         gpointer user_data,
                                         GError** error)
{
  return infd_chat_filesystem_format_write(
    INFD_FILESYSTEM_STORAGE(storage),
    path,
    INF_CHAT_BUFFER(inf_session_get_buffer(session)),
    error
  );
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
