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

#ifndef __INF_TEXT_FILESYSTEM_FORMAT_H__
#define __INF_TEXT_FILESYSTEM_FORMAT_H__

#include <libinftext/inf-text-session.h>
#include <libinfinity/server/infd-filesystem-storage.h>

#include <glib.h>

G_BEGIN_DECLS

/**
 * InfTextFilesystemFormatError:
 * @INF_TEXT_FILESYSTEM_FORMAT_ERROR_NOT_A_TEXT_SESSION: The file to be read
 * is not a serialized text session.
 * @INF_TEXT_FILESYSTEM_FORMAT_ERROR_USER_EXISTS: The user table of the
 * session contains users with duplicate ID or duplicate name.
 * @INF_TEXT_FILESYSTEM_FORMAT_ERROR_NO_SUCH_USER: A segment of the text
 * document is written by a user which does not exist.
 *
 * Errors that can occur when reading a #InfTextSession from a
 * #InfdFilesystemStorage.
 */
typedef enum _InfTextFilesystemFormatError {
  INF_TEXT_FILESYSTEM_FORMAT_ERROR_NOT_A_TEXT_SESSION,
  INF_TEXT_FILESYSTEM_FORMAT_ERROR_USER_EXISTS,
  INF_TEXT_FILESYSTEM_FORMAT_ERROR_NO_SUCH_USER
} InfTextFilesystemFormatError;

gboolean
inf_text_filesystem_format_read(InfdFilesystemStorage* storage,
                                const gchar* path,
                                InfUserTable* user_table,
                                InfTextBuffer* buffer,
                                GError** error);

gboolean
inf_text_filesystem_format_write(InfdFilesystemStorage* storage,
                                 const gchar* path,
                                 InfUserTable* user_table,
                                 InfTextBuffer* buffer,
                                 GError** error);

G_END_DECLS

#endif /* __INF_TEXT_FILESYSTEM_FORMAT_H__ */

/* vim:set et sw=2 ts=2: */
