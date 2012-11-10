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

#ifndef __INFINOTED_DIRECTORY_SYNC_H__
#define __INFINOTED_DIRECTORY_SYNC_H__

#include <infinoted/infinoted-log.h>

#include <libinfinity/server/infd-directory.h>

#include <glib.h>

G_BEGIN_DECLS

/* TODO: This is (currently) the only part of infinoted which requires
 * libinftext, apart from the note plugin. I'd therefore prefer to move this
 * code to the note plugin, to keep infinoted generic. */

typedef struct _InfinotedDirectorySync InfinotedDirectorySync;
struct _InfinotedDirectorySync {
  InfdDirectory* directory;
  InfinotedLog* log;
  gchar* sync_directory;
  unsigned int sync_interval;
  gchar* sync_hook;
  GSList* sessions;
};

typedef enum _InfinotedDirectorySyncError {
  INFINOTED_DIRECTORY_SYNC_ERROR_INVALID_PATH
} InfinotedDirectorySyncError;

InfinotedDirectorySync*
infinoted_directory_sync_new(InfdDirectory* directory,
                             InfinotedLog* log,
                             const gchar* sync_directory,
                             unsigned int sync_interval,
                             const gchar* sync_hook);

void
infinoted_directory_sync_free(InfinotedDirectorySync* autosave);

G_END_DECLS

#endif /* __INFINOTED_DIRECTORY_SYNC_H__ */

/* vim:set et sw=2 ts=2: */
