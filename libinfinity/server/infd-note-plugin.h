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

#ifndef __INFD_NOTE_PLUGIN_H__
#define __INFD_NOTE_PLUGIN_H__

#include <libinfinity/server/infd-storage.h>
#include <libinfinity/communication/inf-communication-manager.h>
#include <libinfinity/communication/inf-communication-hosted-group.h>
#include <libinfinity/common/inf-session.h>
#include <libinfinity/common/inf-io.h>

#include <glib-object.h>

G_BEGIN_DECLS

/* TODO: GTypeModule stuff? */

typedef InfSession*(*InfdNotePluginSessionNew)(InfIo*,
                                               InfCommunicationManager*,
                                               InfSessionStatus,
                                               InfCommunicationHostedGroup*,
                                               InfXmlConnection*,
                                               gpointer);

typedef InfSession*(*InfdNotePluginSessionRead)(InfdStorage*,
                                                InfIo*,
                                                InfCommunicationManager*,
                                                const gchar*,
                                                gpointer,
                                                GError**);

typedef gboolean(*InfdNotePluginSessionWrite)(InfdStorage*,
                                              InfSession*,
                                              const gchar*,
                                              gpointer,
                                              GError**);

typedef struct _InfdNotePlugin InfdNotePlugin;
struct _InfdNotePlugin {
  gpointer user_data;

  /* The typename of the storage backend this plugin can be used with, such
   * as InfdFilesystemStorage. */
  const gchar* storage_type;

  /* The note type this plugin handles, such as InfText */
  const gchar* note_type;

  InfdNotePluginSessionNew session_new;
  InfdNotePluginSessionRead session_read;
  InfdNotePluginSessionWrite session_write;
};

G_END_DECLS

#endif /* __INFD_NOTE_PLUGIN_H__ */

/* vim:set et sw=2 ts=2: */
