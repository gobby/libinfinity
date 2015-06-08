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

#ifndef __INFD_CHAT_FILESYSTEM_FORMAT_H__
#define __INFD_CHAT_FILESYSTEM_FORMAT_H__

#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/common/inf-chat-buffer.h>

#include <glib.h>

G_BEGIN_DECLS

/**
 * InfdChatFilesystemFormatError:
 * @INFD_CHAT_FILESYSTEM_FORMAT_ERROR_NOT_A_CHAT_SESSION: The file to be read
 * is not a serialized chat session.
 *
 * Errors that can occur when reading a #InfChatSession from a
 * #InfdFilesystemStorage.
 */
typedef enum _InfdTextFilesystemFormatError {
  INFD_CHAT_FILESYSTEM_FORMAT_ERROR_NOT_A_CHAT_SESSION
} InfdChatFilesystemFormatError;

gboolean
infd_chat_filesystem_format_read(InfdFilesystemStorage* storage,
                                 const gchar* path,
                                 InfChatBuffer* buffer,
                                 GError** error);

gboolean
infd_chat_filesystem_format_write(InfdFilesystemStorage* storage,
                                  const gchar* path,
                                  InfChatBuffer* buffer,
                                  GError** error);

G_END_DECLS

#endif /* __INFD_CHAT_FILESYSTEM_FORMAT_H__ */

/* vim:set et sw=2 ts=2: */
