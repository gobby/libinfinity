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

#include <libinftext/inf-text-session.h>
#include <libinftext/inf-text-default-buffer.h>
#include <libinftext/inf-text-buffer.h>
#include <libinftext/inf-text-filesystem-format.h>

#include <libinfinity/server/infd-note-plugin.h>
#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-storage.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-xml-connection.h>
#include <libinfinity/communication/inf-communication-manager.h>

static InfSession*
infd_note_plugin_text_session_new(InfIo* io,
                                  InfCommunicationManager* manager,
                                  InfSessionStatus status,
                                  InfCommunicationHostedGroup* sync_group,
                                  InfXmlConnection* sync_connection,
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
    INF_COMMUNICATION_GROUP(sync_group),
    sync_connection
  );

  g_object_unref(buffer);
  return INF_SESSION(session);
}

static InfSession*
infd_note_plugin_text_session_read(InfdStorage* storage,
                                   InfIo* io,
                                   InfCommunicationManager* manager,
                                   const gchar* path,
                                   gpointer user_data,
                                   GError** error)
{
  InfTextBuffer* buffer;
  InfTextSession* session;

  g_assert(INFD_IS_FILESYSTEM_STORAGE(storage));

  buffer = INF_TEXT_BUFFER(inf_text_default_buffer_new("UTF-8"));

  session = inf_text_filesystem_format_read(
    INFD_FILESYSTEM_STORAGE(storage),
    io,
    manager,
    path,
    buffer,
    error
  );

  g_object_unref(buffer);
  return INF_SESSION(session);
}

static gboolean
infd_note_plugin_text_session_write(InfdStorage* storage,
                                    InfSession* session,
                                    const gchar* path,
                                    gpointer user_data,
                                    GError** error)
{
  return inf_text_filesystem_format_write(
    INFD_FILESYSTEM_STORAGE(storage),
    INF_TEXT_SESSION(session),
    path,
    error
  );
}

const InfdNotePlugin INFD_NOTE_PLUGIN = {
  NULL,
  "InfdFilesystemStorage",
  "InfText",
  infd_note_plugin_text_session_new,
  infd_note_plugin_text_session_read,
  infd_note_plugin_text_session_write
};

/* vim:set et sw=2 ts=2: */
