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

#ifndef __INFD_FILESYSTEM_ACCOUNT_STORAGE_H__
#define __INFD_FILESYSTEM_ACCOUNT_STORAGE_H__

#include <libinfinity/server/infd-filesystem-storage.h>

#include <glib-object.h>

G_BEGIN_DECLS

#define INFD_TYPE_FILESYSTEM_ACCOUNT_STORAGE                 (infd_filesystem_account_storage_get_type())
#define INFD_FILESYSTEM_ACCOUNT_STORAGE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST((obj), INFD_TYPE_FILESYSTEM_ACCOUNT_STORAGE, InfdFilesystemAccountStorage))
#define INFD_FILESYSTEM_ACCOUNT_STORAGE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST((klass), INFD_TYPE_FILESYSTEM_ACCOUNT_STORAGE, InfdFilesystemAccountStorageClass))
#define INFD_IS_FILESYSTEM_ACCOUNT_STORAGE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE((obj), INFD_TYPE_FILESYSTEM_ACCOUNT_STORAGE))
#define INFD_IS_FILESYSTEM_ACCOUNT_STORAGE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE((klass), INFD_TYPE_FILESYSTEM_ACCOUNT_STORAGE))
#define INFD_FILESYSTEM_ACCOUNT_STORAGE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS((obj), INFD_TYPE_FILESYSTEM_ACCOUNT_STORAGE, InfdFilesystemAccountStorageClass))

typedef struct _InfdFilesystemAccountStorage InfdFilesystemAccountStorage;
typedef struct _InfdFilesystemAccountStorageClass
  InfdFilesystemAccountStorageClass;

/**
 * InfdFilesystemAccountStorageError:
 * @INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_INVALID_FORMAT: An on-disk XML file
 * is not formatted correctly.
 * @INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_NAME: An account name is
 * already in use.
 * @INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_CERTIFICATE: An account
 * which uses the same certificate to login exists already.
 * @INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_IDS_EXHAUSTED: Could not
 * obtain a unique account ID.
 * @INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_NO_SUCH_ACCOUNT: The account with
 * the given ID does not exist.
 *
 * Specifies the possible error codes in the
 * <literal>INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR</literal> error domain.
 * Such errors can occur when reading the accounts file from disk.
 */
typedef enum _InfdFilesystemAccountStorageError {
  INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_INVALID_FORMAT,
  INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_NAME,
  INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_DUPLICATE_CERTIFICATE,
  INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_IDS_EXHAUSTED,
  INFD_FILESYSTEM_ACCOUNT_STORAGE_ERROR_NO_SUCH_ACCOUNT
} InfdFilesystemAccountStorageError;

/**
 * InfdFilesystemAccountStorageClass:
 *
 * This structure does not contain any public fields.
 */
struct _InfdFilesystemAccountStorageClass {
  /*< private >*/
  GObjectClass parent_class;
};

/**
 * InfdFilesystemAccountStorage:
 *
 * #InfdFilesystemAccountStorage is an opaque data type. You should only
 * access it via the public API functions.
 */
struct _InfdFilesystemAccountStorage {
  /*< private >*/
  GObject parent;
};

GType
infd_filesystem_account_storage_get_type(void) G_GNUC_CONST;

InfdFilesystemAccountStorage*
infd_filesystem_account_storage_new(void);

gboolean
infd_filesystem_account_storage_set_filesystem(InfdFilesystemAccountStorage* s,
                                               InfdFilesystemStorage* fs,
                                               GError** error);

G_END_DECLS

#endif /* __INFD_FILESYSTEM_ACCOUNT_STORAGE_H__ */

/* vim:set et sw=2 ts=2: */
