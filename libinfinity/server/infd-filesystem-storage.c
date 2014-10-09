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

#include "config.h"

#include <libinfinity/server/infd-filesystem-storage.h>
#include <libinfinity/server/infd-storage.h>
#include <libinfinity/common/inf-file-util.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/inf-i18n.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#include <glib/gstdio.h>

#include <string.h>
#include <errno.h>

#ifndef G_OS_WIN32
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <dirent.h>
# include <unistd.h>
#endif

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

/* Checks whether path is valid, and sets error if not */
static gboolean
infd_filesystem_storage_verify_path(const gchar* path,
                                    GError** error)
{
  gchar** components;
  gchar** component;

  if(path[0] != '/')
  {
    g_set_error_literal(
      error,
      infd_filesystem_storage_error_quark,
      INFD_FILESYSTEM_STORAGE_ERROR_INVALID_PATH,
      _("The path does not start with \"/\"")
    );
  }

  components = g_strsplit(path + 1, "/", 0);
  for(component = components; *component != NULL; ++ component)
  {
    if(**component == '\0' ||
       strcmp(*component, ".") == 0 || strcmp(*component, "..") == 0)
    {
      g_set_error(
        error,
        infd_filesystem_storage_error_quark,
        INFD_FILESYSTEM_STORAGE_ERROR_INVALID_PATH,
        "%s",
        _("The path contains invalid components")
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

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  error = NULL;
  converted = g_filename_from_utf8(root_directory, -1, NULL, NULL, &error);

  /* TODO: We should somehow report at least this error further upwards, and
   * let the user decide what to do. */
  if(converted == NULL)
  {
    g_warning(
      _("Failed to convert root directory to filename encoding: %s"),
      error->message
    );

    g_error_free(error);
  }
  else
  {
    if(!inf_file_util_create_directory(converted, 0755, &error))
    {
      g_warning(
        _("Failed to create root directory: %s\n"
          "Subsequent storage operations will most likely fail\n"),
        error->message
      );

      g_error_free(error);
    }

    g_free(priv->root_directory);
    priv->root_directory = converted;
  }
}

static void
infd_filesystem_storage_system_error(int code,
                                     GError** error)
{
  g_set_error(
    error,
    G_FILE_ERROR,
    g_file_error_from_errno(code),
    "%s",
    g_strerror(code)
  );
}

static int
infd_filesystem_storage_read_xml_stream_read_func(void* context,
                                                  char* buffer,
                                                  int len)
{
  int res;
  res = fread(buffer, 1, len, (FILE*)context);

  if(ferror((FILE*)context))
    return -1;

  return res;
}

static int
infd_filesystem_storage_read_xml_stream_close_func(void* context)
{
  return fclose((FILE*)context);
}

/* The following functions don't check the given path, and should only be used
 * when it has been checked before with infd_filesystem_storage_verify_path().
 * The public functions do check the path before calling any of these. */
static FILE*
infd_filesystem_storage_open_impl(InfdFilesystemStorage* storage,
                                  const gchar* path,
                                  const gchar* mode,
                                  GError** error)
{
  FILE* res;
  int save_errno;
#ifndef G_OS_WIN32
  int fd;
  int open_mode;
#endif

#ifdef G_OS_WIN32
  res = g_fopen(path, mode);
#else
  if(strcmp(mode, "r") == 0) open_mode = O_RDONLY;
  else if(strcmp(mode, "w") == 0) open_mode = O_CREAT | O_WRONLY | O_TRUNC;
  else g_assert_not_reached();
  fd = open(path, O_NOFOLLOW | open_mode, 0644);
  if(fd == -1)
    res = NULL;
  else
    res = fdopen(fd, mode);
#endif
  save_errno = errno;

  if(res == NULL)
  {
    infd_filesystem_storage_system_error(save_errno, error);
    return NULL;
  }

  return res;
}

xmlDocPtr
infd_filesystem_storage_read_xml_file_impl(InfdFilesystemStorage* storage,
                                           const gchar* path,
                                           const gchar* toplevel_tag,
                                           GError** error)
{
  FILE* file;
  gchar* uri;

  xmlDocPtr doc;
  xmlNodePtr root;
  xmlErrorPtr xmlerror;

  file = infd_filesystem_storage_open_impl(storage, path, "r", error);
  if(file == NULL)
    return NULL;

  uri = g_filename_to_uri(path, NULL, error);

  if(uri == NULL)
  {
    fclose(file);
    return NULL;
  }

  doc = xmlReadIO(
    infd_filesystem_storage_read_xml_stream_read_func,
    infd_filesystem_storage_read_xml_stream_close_func,
    file,
    uri,
    "UTF-8",
    XML_PARSE_NOWARNING | XML_PARSE_NOERROR
  );

  g_free(uri);

  if(doc == NULL)
  {
    xmlerror = xmlGetLastError();

    g_set_error(
      error,
      g_quark_from_static_string("LIBXML2_PARSER_ERROR"),
      xmlerror->code,
      _("Error parsing XML in file \"%s\": [%d]: %s"),
      xmlerror->file,
      xmlerror->line,
      xmlerror->message
    );
  }
  else if(toplevel_tag != NULL)
  {
    root = xmlDocGetRootElement(doc);
    if(root == NULL || strcmp((const char*)root->name, toplevel_tag) != 0)
    {
      g_set_error(
        error,
        infd_filesystem_storage_error_quark,
        INFD_FILESYSTEM_STORAGE_ERROR_INVALID_FORMAT,
        _("Error processing file \"%s\": Toplevel tag is not \"%s\""),
        doc->name,
        toplevel_tag
      );

      xmlFreeDoc(doc);
      doc = NULL;
    }
  }

  return doc;
}

gboolean
infd_filesystem_storage_write_xml_file_impl(InfdFilesystemStorage* storage,
                                            const gchar* path,
                                            xmlDocPtr doc,
                                            GError** error)
{
  InfdFilesystemStoragePrivate* priv;
  FILE* file;

  int save_errno;
  xmlErrorPtr xmlerror;

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  file = infd_filesystem_storage_open_impl(storage, path, "w", error);
  if(file == NULL)
    return FALSE;

  if(xmlDocFormatDump(file, doc, 1) == -1)
  {
    xmlerror = xmlGetLastError();
    fclose(file);
    /* TODO: unlink? */

    g_set_error(
      error,
      g_quark_from_static_string("LIBXML2_OUTPUT_ERROR"),
      xmlerror->code,
      "%s",
      xmlerror->message
    );

    return FALSE;
  }

  if(fclose(file) != 0)
  {
    save_errno = errno;
    infd_filesystem_storage_system_error(save_errno, error);
    return FALSE;
  }

  return TRUE;
}

static gchar*
infd_filesystem_storage_get_acl_path(InfdFilesystemStorage* storage,
                                     const gchar* path,
                                     GError** error)
{
  gchar* full_path;

  if(strcmp(path, "/") != 0)
  {
    full_path =
      infd_filesystem_storage_get_path(storage, "xml.acl", path, error);
  }
  else
  {
    full_path =
      infd_filesystem_storage_get_path(storage, "xml", "/global-acl", error);
  }

  return full_path;
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

static gboolean
infd_filesystem_storage_storage_read_subdirectory_list_func(const gchar* name,
                                                            const gchar* path,
                                                            InfFileType type,
                                                            gpointer data,
                                                            GError** error)
{
  GSList** list;
  gchar* converted_name;
  gsize name_len;
  gchar* separator;

  list = (GSList**)data;
  converted_name = g_filename_to_utf8(name, -1, NULL, &name_len, error);
  if(converted_name == NULL) return FALSE;

  if(type == INF_FILE_TYPE_DIR)
  {
    *list = g_slist_prepend(
      *list,
      infd_storage_node_new_subdirectory(converted_name)
    );
  }
  else if(type == INF_FILE_TYPE_REG)
  {
    /* The note type identifier is behind the last '.'. Only note
     * types starting with "Inf" are recognized, other files are
     * auxiliary files in the directory. */
    separator = g_strrstr_len(converted_name, name_len, ".");
    if(separator != NULL && strncmp(separator + 1, "Inf", 3) == 0)
    {
      *separator = '\0';
      *list = g_slist_prepend(
        *list,
        infd_storage_node_new_note(converted_name, separator + 1)
      );
    }
  }

  g_free(converted_name);
  return TRUE;
}

static GSList*
infd_filesystem_storage_storage_read_subdirectory(InfdStorage* storage,
                                                  const gchar* path,
                                                  GError** error)
{
  InfdFilesystemStorage* fs_storage;
  InfdFilesystemStoragePrivate* priv;
  GSList* list;
  gchar* converted_name;
  gchar* full_name;
  gboolean result;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(fs_storage);

  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return NULL;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return NULL;

  full_name = g_build_filename(priv->root_directory, converted_name, NULL);
  g_free(converted_name);

  list = NULL;

  result = inf_file_util_list_directory(
    full_name,
    infd_filesystem_storage_storage_read_subdirectory_list_func,
    &list,
    error
  );

  g_free(full_name);

  if(result == FALSE)
  {
    infd_storage_node_list_free(list);
    return NULL;
  }

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
  gboolean result;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(fs_storage);

  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return FALSE;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return FALSE;

  full_name = g_build_filename(priv->root_directory, converted_name, NULL);
  g_free(converted_name);

  result = inf_file_util_create_single_directory(full_name, 0755, error);
  g_free(full_name);

  return result;
}

static gboolean
infd_filesystem_storage_storage_remove_node(InfdStorage* storage,
                                            const gchar* identifier,
                                            const gchar* path,
                                            GError** error)
{
  InfdFilesystemStorage* fs_storage;
  InfdFilesystemStoragePrivate* priv;
  gchar* converted_name;
  gchar* disk_name;
  gchar* full_name;
  gboolean result;
  int save_errno;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(fs_storage);

  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return FALSE;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return FALSE;

  if(identifier != NULL)
  {
    disk_name = g_strconcat(converted_name, ".", identifier, NULL);
  }
  else
  {
    disk_name = converted_name;
  }

  full_name = g_build_filename(priv->root_directory, disk_name, NULL);
  if(disk_name != converted_name) g_free(disk_name);

  result = inf_file_util_delete(full_name, error);
  g_free(full_name);

  if(result == TRUE)
  {
    disk_name = g_strconcat(converted_name, ".xml.acl", NULL);
    full_name = g_build_filename(priv->root_directory, disk_name, NULL);
    g_free(disk_name);

    if(g_unlink(full_name) == -1)
    {
      save_errno = errno;
      if(save_errno != ENOENT)
      {
        infd_filesystem_storage_system_error(save_errno, error);
        result = FALSE;
      }
    }

    g_free(full_name);
  }

  g_free(converted_name);
  return result;
}

static GSList*
infd_filesystem_storage_storage_read_acl(InfdStorage* storage,
                                         const gchar* path,
                                         GError** error)
{
  InfdFilesystemStorage* fs_storage;
  InfdFilesystemStoragePrivate* priv;
  gchar* full_path;
  GError* local_error;

  xmlDocPtr doc;
  xmlNodePtr root;
  xmlNodePtr child;
  GSList* list;
  InfdStorageAcl* acl;
  xmlChar* account_id;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  full_path = infd_filesystem_storage_get_acl_path(fs_storage, path, error);
  if(full_path == NULL) return NULL;

  local_error = NULL;
  doc = infd_filesystem_storage_read_xml_file_impl(
    fs_storage,
    full_path,
    "inf-acl",
    &local_error
  );

  g_free(full_path);

  if(local_error != NULL)
  {
    if(local_error->domain == G_FILE_ERROR &&
       local_error->code == G_FILE_ERROR_NOENT)
    {
      /* The ACL file does not exist. This is not an error, but just means
       * the ACL is empty. */
      g_error_free(local_error);
      return NULL;
    }

    g_propagate_error(error, local_error);
    return NULL;
  }

  root = xmlDocGetRootElement(doc);
  list = NULL;

  for(child = root->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;

    if(strcmp((const char*)child->name, "sheet") == 0)
    {
      account_id =
        inf_xml_util_get_attribute_required(child, "account", error);
      if(account_id == NULL)
      {
        infd_storage_acl_list_free(list);
        xmlFreeDoc(doc);
        return NULL;
      }

      acl = g_slice_new(InfdStorageAcl);
      acl->account_id = g_strdup((const gchar*)account_id);
      xmlFree(account_id);
      
      if(!inf_acl_sheet_perms_from_xml(child, &acl->mask, &acl->perms, error))
      {
        g_free(acl->account_id);
        g_slice_free(InfdStorageAcl, acl);
        infd_storage_acl_list_free(list);
        xmlFreeDoc(doc);
        return NULL;
      }

      if(!inf_acl_mask_empty(&acl->mask))
      {
        list = g_slist_prepend(list, acl);
      }
      else
      {
        g_free(acl->account_id);
        g_slice_free(InfdStorageAcl, acl);
      }
    }
  }

  xmlFreeDoc(doc);
  return list;
}

static gboolean
infd_filesystem_storage_storage_write_acl(InfdStorage* storage,
                                          const gchar* path,
                                          const InfAclSheetSet* sheet_set,
                                          GError** error)
{
  InfdFilesystemStorage* fs_storage;
  InfdFilesystemStoragePrivate* priv;
  gchar* full_path;

  xmlNodePtr root;
  xmlNodePtr child;
  guint i;
  xmlDocPtr doc;
  gboolean result;
  int save_errno;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  full_path = infd_filesystem_storage_get_acl_path(fs_storage, path, error);
  if(full_path == NULL) return FALSE;

  root = NULL;
  if(sheet_set != NULL)
  {
    root = xmlNewNode(NULL, (const xmlChar*)"inf-acl");
    for(i = 0; i < sheet_set->n_sheets; ++i)
    {
      if(!inf_acl_mask_empty(&sheet_set->sheets[i].mask))
      {
        child = xmlNewChild(root, NULL, (const xmlChar*)"sheet", NULL);

        inf_xml_util_set_attribute(
          child,
          "account",
          inf_acl_account_id_to_string(sheet_set->sheets[i].account)
        );

        inf_acl_sheet_perms_to_xml(
          &sheet_set->sheets[i].mask,
          &sheet_set->sheets[i].perms,
          child
        );
      }
    }

    if(root->children == NULL)
    {
      xmlFreeNode(root);
      root = NULL;
    }
  }

  if(root == NULL)
  {
    if(g_unlink(full_path) == -1)
    {
      save_errno = errno;
      if(save_errno != ENOENT)
      {
        g_free(full_path);
        infd_filesystem_storage_system_error(save_errno, error);
        return FALSE;
      }
    }
  }
  else
  {
    doc = xmlNewDoc((const xmlChar*)"1.0");
    xmlDocSetRootElement(doc, root);

    result = infd_filesystem_storage_write_xml_file_impl(
      INFD_FILESYSTEM_STORAGE(storage),
      full_path,
      doc,
      error
    );

    xmlFreeDoc(doc);

    if(result == FALSE)
    {
      g_free(full_path);
      return FALSE;
    }
  }

  g_free(full_path);
  return TRUE;
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
  iface->read_acl =
    infd_filesystem_storage_storage_read_acl;
  iface->write_acl =
    infd_filesystem_storage_storage_write_acl;
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

/**
 * infd_filesystem_storage_new:
 * @root_directory: A directory name in UTF-8.
 *
 * Creates a new #InfdFilesystemStorage that stores its nodes in the
 * given directory on the file system. The directory is created if it does
 * not exist.
 *
 * Returns: A new #InfdFilesystemStorage.
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

/**
 * infd_filesystem_storage_get_path:
 * @storage: A #InfdFilesystemStorage.
 * @identifier: The type of node to open.
 * @path: The path to open, in UTF-8.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Returns the full file name to the given path within the storage's root
 * directory. The function might fail if @path contains invalid characters.
 * If the function fails, %NULL is returned and @error is set.
 *
 * Only if @identifier starts with &quot;Inf&quot;, the file will show up in
 * the directory listing of infd_storage_read_subdirectory(). Other
 * identifiers can be used to store custom data in the filesystem, linked to
 * this #InfdFilesystemStorage object.
 *
 * Returns: An absolute filename path to be freed with g_free(), or %NULL.
 */
gchar*
infd_filesystem_storage_get_path(InfdFilesystemStorage* storage,
                                 const gchar* identifier,
                                 const gchar* path,
                                 GError** error)
{
  InfdFilesystemStoragePrivate* priv;
  gchar* converted_name;
  gchar* disk_name;
  gchar* full_name;

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);
  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return NULL;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return NULL;

  disk_name = g_strconcat(converted_name, ".", identifier, NULL);
  g_free(converted_name);

  full_name = g_build_filename(priv->root_directory, disk_name, NULL);
  g_free(disk_name);

  return full_name;
}

/**
 * infd_filesystem_storage_open:
 * @storage: A #InfdFilesystemStorage.
 * @identifier: The type of node to open.
 * @path: The path to open, in UTF-8.
 * @mode: Either "r" for reading or "w" for writing.
 * @full_path: Return location of the full filename, or %NULL.
 * @error: Location to store error information, if any.
 *
 * Opens a file in the given path within the storage's root directory. If
 * the file exists already, and @mode is set to "w", the file is overwritten.
 *
 * If @full_path is not %NULL, then it will be set to a newly allocated
 * string which contains the full name of the opened file, in the Glib file
 * name encoding. Note that @full_path will also be set if the function fails.
 *
 * Only if @identifier starts with &quot;Inf&quot;, the file will show up in
 * the directory listing of infd_storage_read_subdirectory(). Other
 * identifiers can be used to store custom data in the filesystem, linked to
 * this #InfdFilesystemStorage object.
 *
 * Returns: A stream for the open file. Close with
 * infd_filesystem_storage_stream_close().
 **/
FILE*
infd_filesystem_storage_open(InfdFilesystemStorage* storage,
                             const gchar* identifier,
                             const gchar* path,
                             const gchar* mode,
                             gchar** full_path,
                             GError** error)
{
  gchar* full_name;
  FILE* res;

  g_return_val_if_fail(INFD_IS_FILESYSTEM_STORAGE(storage), NULL);
  g_return_val_if_fail(identifier != NULL, NULL);
  g_return_val_if_fail(path != NULL, NULL);
  g_return_val_if_fail(mode != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);

  full_name = infd_filesystem_storage_get_path(
    storage,
    identifier,
    path,
    error
  );

  if(full_name == NULL)
    return NULL;

  res = infd_filesystem_storage_open_impl(
    storage,
    full_name,
    mode,
    error
  );

  if(full_path != NULL)
    *full_path = full_name;
  else
    g_free(full_name);

  return res;
}

/**
 * infd_filesystem_storage_read_xml_file:
 * @storage: A #InfdFilesystemStorage.
 * @identifier: The type of node to open.
 * @path: The path to open, in UTF-8.
 * @toplevel_tag: The expected toplevel XML tag name, or %NULL.
 * @error: Location to store error information, if any.
 *
 * Opens a file in the given path, and parses its XML content. See
 * infd_filesystem_storage_open() for how @identifier and @path should be
 * interpreted.
 *
 * If @toplevel_tag is non-%NULL, then this function generates an error if
 * the XML document read has a toplevel tag with a different name.
 *
 * Returns: A new XML document, or %NULL on error. Free with xmlDocFree().
 **/
xmlDocPtr
infd_filesystem_storage_read_xml_file(InfdFilesystemStorage* storage,
                                      const gchar* identifier,
                                      const gchar* path,
                                      const gchar* toplevel_tag,
                                      GError** error)
{
  gchar* full_name;
  xmlDocPtr res;

  g_return_val_if_fail(INFD_IS_FILESYSTEM_STORAGE(storage), NULL);
  g_return_val_if_fail(identifier != NULL, NULL);
  g_return_val_if_fail(path != NULL, NULL);
  g_return_val_if_fail(error == NULL || *error == NULL, NULL);


  full_name = infd_filesystem_storage_get_path(
    storage,
    identifier,
    path,
    error
  );
  
  if(full_name == NULL)
    return NULL;

  res = infd_filesystem_storage_read_xml_file_impl(
    storage,
    full_name,
    toplevel_tag,
    error
  );

  g_free(full_name);
  return res;
}

/**
 * infd_filesystem_storage_write_xml_file:
 * @storage: A #InfdFilesystemStorage.
 * @identifier: The type of node to write.
 * @path: The path to write to, in UTF-8.
 * @doc: The XML document to write.
 * @error: Location to store error information, if any.
 *
 * Writes the XML doument in @doc into a file in the filesystem indicated
 * by @identifier and @path. See infd_filesystem_storage_open() for how
 * @identifier and @path should be interpreted.
 *
 * Returns: %TRUE on success or %FALSE on error.
 **/
gboolean
infd_filesystem_storage_write_xml_file(InfdFilesystemStorage* storage,
                                       const gchar* identifier,
                                       const gchar* path,
                                       xmlDocPtr doc,
                                       GError** error)
{
  gchar* full_name;
  gboolean result;

  g_return_val_if_fail(INFD_IS_FILESYSTEM_STORAGE(storage), FALSE);
  g_return_val_if_fail(identifier != NULL, FALSE);
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(doc != NULL, FALSE);
  g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

  full_name = infd_filesystem_storage_get_path(
    storage,
    identifier,
    path,
    error
  );
  
  if(full_name == NULL)
    return FALSE;

  result = infd_filesystem_storage_write_xml_file_impl(
    storage,
    full_name,
    doc,
    error
  );

  g_free(full_name);
  return result;
}

/**
 * infd_filesystem_storage_stream_close:
 * @file: A #FILE opened with infd_filesystem_storage_open().
 *
 * This is a thin wrapper around fclose(). Use this function instead of
 * fclose() if you have opened the file with infd_filesystem_storage_open(),
 * to make sure that the same C runtime is closing the file that has opened
 * it.
 *
 * Returns: The return value of fclose().
 */
int
infd_filesystem_storage_stream_close(FILE* file)
{
  return fclose(file);
}

/**
 * infd_filesystem_storage_stream_read:
 * @file: A #FILE opened with infd_filesystem_storage_open().
 * @buffer: A buffer into which to read data.
 * @len: Maximum number of bytes to read.
 *
 * This is a thin wrapper around fread(). Use this function instead of
 * fread() if you have opened the file with infd_filesystem_storage_open(),
 * to make sure that the same C runtime is closing the file that has opened
 * it.
 *
 * Returns: The return value of fread().
 */
gsize
infd_filesystem_storage_stream_read(FILE* file,
                                    gpointer buffer,
                                    gsize len)
{
  return fread(buffer, 1, len, file);
}

/**
 * infd_filesystem_storage_stream_write:
 * @file: A #FILE opened with infd_filesystem_storage_open().
 * @buffer: The data to write.
 * @len: Maximum number of bytes to write.
 *
 * This is a thin wrapper around fwrite(). Use this function instead of
 * fwrite() if you have opened the file with infd_filesystem_storage_open(),
 * to make sure that the same C runtime is closing the file that has opened
 * it.
 *
 * Returns: The return value of fwrite().
 */
gsize
infd_filesystem_storage_stream_write(FILE* file,
                                     gconstpointer buffer,
                                     gsize len)
{
  return fwrite(buffer, 1, len, file);
}

/* vim:set et sw=2 ts=2: */
