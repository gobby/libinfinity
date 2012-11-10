/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#ifndef __INFINOTED_NOTE_PLUGIN_H__
#define __INFINOTED_NOTE_PLUGIN_H__

#include <infinoted/infinoted-log.h>

#include <libinfinity/server/infd-directory.h>

G_BEGIN_DECLS

typedef enum _InfinotedNotePluginError {
  INFINOTED_NOTE_PLUGIN_ERROR_OPEN_FAILED,
  INFINOTED_NOTE_PLUGIN_ERROR_NO_ENTRY_POINT,
  INFINOTED_NOTE_PLUGIN_ERROR_STORAGE_MISMATCH,
  INFINOTED_NOTE_PLUGIN_ERROR_DUPLICATE_NOTE_TYPE
} InfinotedNotePluginError;

const InfdNotePlugin*
infinoted_note_plugin_load(const gchar* plugin_path,
                           GError** error);

gboolean
infinoted_note_plugin_load_directory(const gchar* path,
                                     InfdDirectory* directory,
                                     InfinotedLog* log);

G_END_DECLS

#endif /* __INFINOTED_NOTE_PLUGINS_H__ */

/* vim:set et sw=2 ts=2: */
