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

#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-storage.h>

#include <glib/gstdio.h>

#include <string.h>
#include <errno.h>

typedef struct _InfdFilesystemStoragePrivate InfdFilesystemStoragePrivate;
struct _InfdFilesystemStoragePrivate {
  gchar* root_directory;
};

enum {
  PROP_0,

  PROP_ROOT_DIRECTORY
};

#define INFD_FILESYSTEM_STORAGE_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFD_TYPE_FILESYSTEM_STORAGE, InfdFilesystemStoragePrivate))

static GObjectClass* parent_class;
static GQuark infd_filesystem_storage_error_quark;
static GQuark infd_filesystem_storage_system_error_quark;

/* Checks whether path is valid, and sets error if not */
static gboolean
infd_filesystem_storage_verify_path(const gchar* path,
                                    GError** error)
{
  gchar** components;
  gchar** component;

  components = g_strsplit(path, "/", 0);
  for(component = components; *component != NULL; ++ component)
  {
    if(strcmp(*component, ".") == 0 || strcmp(*component, "..") == 0)
    {
      g_set_error(
        error,
        infd_filesystem_storage_error_quark,
        INFD_FILESYSTEM_STORAGE_ERROR_INVALID_PATH,
        "The path contains invalid components"
      );

      g_strfreev(components);
      return FALSE;
    }
  }

  g_strfreev(components);
  return TRUE;
}

static void
infd_filesystem_storage_set_root_directory(InfdFilesystemStorage* storage,
                                           const gchar* root_directory)
{
  InfdFilesystemStoragePrivate* priv;
  gchar* converted;
  GError* error;
  int ret;

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  error = NULL;
  converted = g_filename_from_utf8(root_directory, -1, NULL, NULL, &error);

  /* TODO: We should somehow report at least this error further upwards */
  if(converted == NULL)
  {
    g_warning(
      "Failed to convert root directory to filename encoding: %s",
      error->message
    );

    g_error_free(error);
  }
  else
  {
    ret = g_mkdir_with_parents(converted, 0755);
    if(ret == -1)
    {
      g_warning(
        "Failed to create root directory: %s\n"
        "Subsequent storage operations will most likely fail\n",
        strerror(errno)
      );
    }

    g_free(priv->root_directory);
    priv->root_directory = converted;
  }
}

static void
infd_filesystem_storage_system_error(int code,
                                     GError** error)
{
  /* TODO: Use FormatMessage or something on Win32 */
  g_set_error(
    error,
    infd_filesystem_storage_system_error_quark,
    code,
    "%s",
    strerror(code)
  );
}

static gboolean
infd_filesystem_storage_remove_rec(const gchar* path,
                                   GError** error)
{
  GDir* dir;
  const gchar* name;
  gchar* child;
  int ret;

  ret = g_unlink(path);
  if(ret == -1)
  {
    if(errno == EISDIR)
    {
      dir = g_dir_open(path, 0, error);
      if(dir == NULL) return FALSE;

      for(name = g_dir_read_name(dir);
          name != NULL;
          name = g_dir_read_name(dir))
      {
        child = g_build_filename(path, name, NULL);
        if(infd_filesystem_storage_remove_rec(child, error) == FALSE)
        {
          g_free(child);
          return FALSE;
        }

        g_free(child);
      }

      g_dir_close(dir);

      ret = g_rmdir(path);
      if(ret == -1)
      {
        infd_filesystem_storage_system_error(errno, error);
        return FALSE;
      }
    }
    else
    {
      infd_filesystem_storage_system_error(errno, error);
      return FALSE;
    }
  }

  return TRUE;
}

static void
infd_filesystem_storage_init(GTypeInstance* instance,
                             gpointer g_class)
{
  InfdFilesystemStorage* storage;
  InfdFilesystemStoragePrivate* priv;

  storage = INFD_FILESYSTEM_STORAGE(instance);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  priv->root_directory = NULL;
}

static void
infd_filesystem_storage_finalize(GObject* object)
{
  InfdFilesystemStorage* storage;
  InfdFilesystemStoragePrivate* priv;

  storage = INFD_FILESYSTEM_STORAGE(object);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  g_free(priv->root_directory);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infd_filesystem_storage_set_property(GObject* object,
                                     guint prop_id,
                                     const GValue* value,
                                     GParamSpec* pspec)
{
  InfdFilesystemStorage* storage;
  InfdFilesystemStoragePrivate* priv;

  storage = INFD_FILESYSTEM_STORAGE(object);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  switch(prop_id)
  {
  case PROP_ROOT_DIRECTORY:
    infd_filesystem_storage_set_root_directory(
      storage,
      g_value_get_string(value)
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infd_filesystem_storage_get_property(GObject* object,
                                     guint prop_id,
                                     GValue* value,
                                     GParamSpec* pspec)
{
  InfdFilesystemStorage* storage;
  InfdFilesystemStoragePrivate* priv;

  storage = INFD_FILESYSTEM_STORAGE(object);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  switch(prop_id)
  {
  case PROP_ROOT_DIRECTORY:
    g_value_set_string(value, priv->root_directory);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static GSList*
infd_filesystem_storage_storage_read_subdirectory(InfdStorage* storage,
                                                  const gchar* path,
                                                  GError** error)
{
  InfdFilesystemStorage* fs_storage;
  InfdFilesystemStoragePrivate* priv;
  GSList* list;
  GDir* dir;
  const gchar* name;
  gchar* converted_name;
  gchar* full_name;
  gchar* file_path;
  gchar* separator;
  gsize name_len;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(fs_storage);

  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return NULL;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return NULL;

  full_name = g_build_filename(priv->root_directory, converted_name, NULL);
  g_free(converted_name);

  dir = g_dir_open(full_name, 0, error);

  if(dir == NULL)
    return NULL;

  list = NULL;
  for(name = g_dir_read_name(dir); name != NULL; name = g_dir_read_name(dir))
  {
    converted_name = g_filename_to_utf8(name, -1, NULL, &name_len, NULL);
    if(converted_name != NULL)
    {
      file_path = g_build_filename(full_name, name, NULL);
      if(g_file_test(file_path, G_FILE_TEST_IS_DIR))
      {
        list = g_slist_prepend(
          list,
          infd_storage_node_new_subdirectory(converted_name)
        );
      }
      else if(g_file_test(file_path, G_FILE_TEST_IS_REGULAR))
      {
        /* The note type identifier is behind the last '.' */
        separator = g_strrstr_len(converted_name, name_len, ".");
        if(separator != NULL)
        {
          *separator = '\0';
          list = g_slist_prepend(
            list,
            infd_storage_node_new_note(converted_name, separator + 1)
          );
        }
      }

      g_free(converted_name);
      g_free(file_path);
    }
  }

  g_dir_close(dir);
  g_free(full_name);
  return list;
}

static gboolean
infd_filesystem_storage_storage_create_subdirectory(InfdStorage* storage,
                                                    const gchar* path,
                                                    GError** error)
{
  InfdFilesystemStorage* fs_storage;
  InfdFilesystemStoragePrivate* priv;
  gchar* converted_name;
  gchar* full_name;
  int ret;
  int save_errno;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(fs_storage);

  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return FALSE;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return FALSE;

  full_name = g_build_filename(priv->root_directory, converted_name, NULL);
  g_free(converted_name);

  ret = g_mkdir(full_name, 0755);
  save_errno = errno;

  g_free(full_name);
  if(ret == -1)
  {
    infd_filesystem_storage_system_error(save_errno, error);
    return FALSE;
  }


  return TRUE;
}

static gboolean
infd_filesystem_storage_storage_remove_node(InfdStorage* storage,
                                            const gchar* path,
                                            GError** error)
{
  InfdFilesystemStorage* fs_storage;
  InfdFilesystemStoragePrivate* priv;
  gchar* converted_name;
  gchar* full_name;
  gboolean ret;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(fs_storage);

  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return FALSE;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return FALSE;

  full_name = g_build_filename(priv->root_directory, converted_name, NULL);
  g_free(converted_name);

  ret = infd_filesystem_storage_remove_rec(full_name, error);
  g_free(full_name);

  return ret;
}

static void
infd_filesystem_storage_class_init(gpointer g_class,
                                   gpointer class_data)
{
  GObjectClass* object_class;
  InfdFilesystemStorageClass* filesystem_storage_class;

  object_class = G_OBJECT_CLASS(g_class);
  filesystem_storage_class = INFD_FILESYSTEM_STORAGE_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfdFilesystemStoragePrivate));

  object_class->finalize = infd_filesystem_storage_finalize;
  object_class->set_property = infd_filesystem_storage_set_property;
  object_class->get_property = infd_filesystem_storage_get_property;

  infd_filesystem_storage_error_quark = g_quark_from_static_string(
    "INFD_FILESYSTEM_STORAGE_ERROR"
  );

  infd_filesystem_storage_system_error_quark = g_quark_from_static_string(
    "INFD_FILESYSTEM_STORAGE_SYSTEM_ERROR"
  );

  g_object_class_install_property(
    object_class,
    PROP_ROOT_DIRECTORY,
    g_param_spec_string(
      "root-directory",
      "Root directory",
      "The directory in which the storage stores its content",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );
}

static void
infd_filesystem_storage_storage_init(gpointer g_iface,
                                     gpointer iface_data)
{
  InfdStorageIface* iface;
  iface = (InfdStorageIface*)g_iface;

  iface->read_subdirectory =
    infd_filesystem_storage_storage_read_subdirectory;
  iface->create_subdirectory =
    infd_filesystem_storage_storage_create_subdirectory;
  iface->remove_node =
    infd_filesystem_storage_storage_remove_node;
}

GType
infd_filesystem_storage_get_type(void)
{
  static GType filesystem_storage_type = 0;

  if(!filesystem_storage_type)
  {
    static const GTypeInfo filesystem_storage_type_info = {
      sizeof(InfdFilesystemStorageClass),  /* class_size */
      NULL,                                /* base_init */
      NULL,                                /* base_finalize */
      infd_filesystem_storage_class_init,  /* class_init */
      NULL,                                /* class_finalize */
      NULL,                                /* class_data */
      sizeof(InfdFilesystemStorage),       /* instance_size */
      0,                                   /* n_preallocs */
      infd_filesystem_storage_init,        /* instance_init */
      NULL                                 /* value_table */
    };

    static const GInterfaceInfo storage_info = {
      infd_filesystem_storage_storage_init,
      NULL,
      NULL
    };

    filesystem_storage_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfdFilesystemStorage",
      &filesystem_storage_type_info,
      0
    );

    g_type_add_interface_static(
      filesystem_storage_type,
      INFD_TYPE_STORAGE,
      &storage_info
    );
  }

  return filesystem_storage_type;
}

/** infd_filesystem_storage_new:
 *
 * @root_directory: A directory name in UTF-8.
 *
 * Creates a new #InfdFilesystemStorage that stores its nodes in the
 * given directory on the file system. The directory is created if it does
 * not exist.
 *
 * Return Value: A new #InfdFilesystemStorage.
 **/
InfdFilesystemStorage*
infd_filesystem_storage_new(const gchar* root_directory)
{
  GObject* object;

  object = g_object_new(
    INFD_TYPE_FILESYSTEM_STORAGE,
    "root-directory", root_directory,
    NULL
  );

  return INFD_FILESYSTEM_STORAGE(object);
}

/** infd_filesystem_storage_open:
 *
 * @storage:  A #InfdFilesystemStorage.
 * @path: Tha path to open.
 * @mode: Either "r" for reading or "w" for writing.
 * @error: Location to store error information, if any.
 *
 * Opens a file in the given path within the storage's root directory. If
 * the file exists already, and @mode is set to "w", the file is overwritten.
 *
 * Return Value: A stream for the open file. Close with fclose().
 **/
FILE*
infd_filesystem_storage_open(InfdFilesystemStorage* storage,
                             const gchar* path,
                             const gchar* mode,
                             GError** error)
{
  InfdFilesystemStoragePrivate* priv;
  gchar* converted_name;
  gchar* full_name;
  FILE* res;
  int save_errno;

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);
  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return FALSE;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return FALSE;

  full_name = g_build_filename(priv->root_directory, converted_name, NULL);
  g_free(converted_name);

  res = g_fopen(full_name, mode);
  save_errno = errno;
  g_free(full_name);

  if(res == NULL)
  {
    infd_filesystem_storage_system_error(save_errno, error);
    return FALSE;
  }

  return res;
}

/* vim:set et sw=2 ts=2: */
