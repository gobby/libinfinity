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

#ifndef __INFD_NOTE_PLUGIN_H__
#define __INFD_NOTE_PLUGIN_H__

#include <libinfinity/server/infd-storage.h>
#include <libinfinity/common/inf-session.h>

#include <glib-object.h>

G_BEGIN_DECLS

/* TODO: GTypeModule stuff? */

typedef struct _InfdNotePlugin InfdNotePlugin;
struct _InfdNotePlugin {
  /* The typename of the storage backend this plugin can be used with, such
   * as InfdFilesystemStorage. */
  const gchar* storage_type;

  /* The note type this plugin handles, such as InfText */
  const gchar* note_type;

  InfSession*(*session_new)(InfConnectionManager* manager,
                            InfConnectionManagerGroup* group,
                            InfXmlConnection* sync_connection);

  gboolean(*session_read)(InfdStorage* storage,
                          InfSession* session,
                          const gchar* path,
                          GError** error);

  gboolean(*session_write)(InfdStorage* storage,
                           InfSession* session,
                           const gchar* path,
                           GError** error);
};

G_END_DECLS

#endif /* __INFD_NOTE_PLUGIN_H__ */

/* vim:set et sw=2 ts=2: */
