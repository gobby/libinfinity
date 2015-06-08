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

#ifndef __INF_FILE_UTIL_H__
#define __INF_FILE_UTIL_H__

#include <glib.h>

G_BEGIN_DECLS

/**
 * InfFileType:
 * @INF_FILE_TYPE_UNKNOWN: Unknown file type.
 * @INF_FILE_TYPE_REG: File is a regular file.
 * @INF_FILE_TYPE_DIR: File is a directory.
 * @INF_FILE_TYPE_LNK: File is a symbolic link.
 *
 * This type represents the possible file types that
 * inf_file_util_list_directory() can report.
 */
typedef enum _InfFileType {
  INF_FILE_TYPE_UNKNOWN,
  INF_FILE_TYPE_REG,
  INF_FILE_TYPE_DIR,
  INF_FILE_TYPE_LNK
} InfFileType;

/**
 * InfFileListFunc:
 * @name: The name of the current file.
 * @path: The full path to the current file.
 * @type: The type of the current file.
 * @user_data: User data specified at the time of the call.
 * @error: Location to store error information, if any, or %NULL.
 *
 * This is the prototype of the callback function passed to
 * inf_file_util_list_directory(). If the function returns %FALSE then
 * directory traversal is stopped immediately. In addition @error can be set
 * and it is propagated to the caller of inf_file_util_list_directory().
 *
 * Returns: %TRUE if the iteration should be continued or %FALSE otherwise.
 */
typedef gboolean(*InfFileListFunc)(const gchar* name,
                                   const gchar* path,
                                   InfFileType type,
                                   gpointer user_data,
                                   GError** error);

gboolean
inf_file_util_create_single_directory(const gchar* path,
                                      int mode,
                                      GError** error);

gboolean
inf_file_util_create_directory(const gchar* path,
                               int mode,
                               GError** error);

gboolean
inf_file_util_list_directory(const gchar* path,
                             InfFileListFunc func,
                             gpointer user_data,
                             GError** error);

gboolean
inf_file_util_delete_file(const gchar* path,
                          GError** error);

gboolean
inf_file_util_delete_single_directory(const gchar* path,
                                      GError** error);

gboolean
inf_file_util_delete_directory(const gchar* path,
                               GError** error);

gboolean
inf_file_util_delete(const gchar* path,
                     GError** error);

G_END_DECLS

#endif /* __INF_FILE_UTIL_H__ */

/* vim:set et sw=2 ts=2: */
