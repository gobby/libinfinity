/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2013 Armin Burgmeier <armin@arbur.net>
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
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/inf-i18n.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#include <glib/gstdio.h>

#include <string.h>
#include <errno.h>

#ifdef G_OS_WIN32
# include <windows.h>
#else
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
    if(*component == '\0' ||
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
  int ret;

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  error = NULL;
  converted = g_filename_from_utf8(root_directory, -1, NULL, NULL, &error);

  /* TODO: We should somehow report at least this error further upwards */
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
    ret = g_mkdir_with_parents(converted, 0755);
    if(ret == -1)
    {
      g_warning(
        _("Failed to create root directory: %s\n"
          "Subsequent storage operations will most likely fail\n"),
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
  /* TODO_Win32: Use FormatMessage or something on Win32,
   * or probably better g_win32_error_message(). */
  /* TODO: Actually we should get away from including system error codes
   * in these errors, as they get eventually sent over the net */
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
#ifdef G_OS_WIN32
  SHFILEOPSTRUCTW op;
  gunichar2* from;
  glong len;
  gboolean result;
  int error_code;

  from = g_utf8_to_utf16(path, -1, NULL, &len, error);
  if(!from) return FALSE;

  from = g_realloc(from, (len+2)*sizeof(gunichar2));
  from[len+1] = L'\0';

  op.hwnd = NULL;
  op.wFunc = FO_DELETE;
  op.pFrom = from;
  op.pTo = NULL;
  op.fFlags = FOF_SILENT | FOF_NOERRORUI | FOF_NOCONFIRMATION;
  op.fAnyOperationsAborted = 0;
  op.hNameMappings = NULL;
  op.lpszProgressTitle = NULL;

  result = TRUE;
  error_code = SHFileOperationW(&op);
  if(error_code != 0 || op.fAnyOperationsAborted != 0)
  {
    g_set_error(
      error,
      infd_filesystem_storage_error_quark,
      INFD_FILESYSTEM_STORAGE_ERROR_REMOVE_FILES,
      "Failed to remove files from disk"
    );

    result = FALSE;
  }

  g_free(from);
  return result;
#else
  /* TODO: Use the REMOVE_FILES error code when something fails below */
  GDir* dir;
  const gchar* name;
  gchar* child;
  int ret;

  ret = g_unlink(path);
  if(ret == -1)
  {
    if(errno == EISDIR || errno == EPERM)
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
#endif
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

/* TODO: Unify these a bit: Make these functions available in the public
 * API, use in infinoted-plugin-text. Also, share code between this and
 * infd_filesystem_storage_open. */
static xmlDocPtr
infd_filesystem_storage_read_xml_stream(InfdFilesystemStorage* storage,
                                        FILE* stream,
                                        const char* filename,
                                        const char* toplevel_tag,
                                        GError** error)
{
  xmlDocPtr doc;
  xmlNodePtr root;
  xmlErrorPtr xmlerror;

  doc = xmlReadIO(
    infd_filesystem_storage_read_xml_stream_read_func,
    infd_filesystem_storage_read_xml_stream_close_func,
    stream,
    filename,
    "UTF-8",
    XML_PARSE_NOWARNING | XML_PARSE_NOERROR
  );

  if(doc == NULL)
  {
    xmlerror = xmlGetLastError();

    g_set_error(
      error,
      g_quark_from_static_string("LIBXML2_PARSER_ERROR"),
      xmlerror->code,
      _("Error parsing XML in file \"%s\": [%d]: %s"),
      filename,
      xmlerror->line,
      xmlerror->message
    );
  }
  else
  {
    root = xmlDocGetRootElement(doc);
    if(strcmp((const char*)root->name, toplevel_tag) != 0)
    {
      g_set_error(
        error,
        infd_filesystem_storage_error_quark,
        INFD_FILESYSTEM_STORAGE_ERROR_INVALID_FORMAT,
        _("Error processing file \"%s\": Toplevel tag is not \"%s\""),
        filename,
        toplevel_tag
      );

      xmlFreeDoc(doc);
      doc = NULL;
    }
  }

  return doc;
}

static xmlDocPtr
infd_filesystem_storage_read_xml_file(InfdFilesystemStorage* storage,
                                      const char* filename,
                                      const char* toplevel_tag,
                                      GError** error)
{
  InfdFilesystemStoragePrivate* priv;
  gchar* full_name;
  FILE* stream;
  int save_errno;
  xmlDocPtr doc;

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  full_name = g_build_filename(priv->root_directory, filename, NULL);
  stream = fopen(full_name, "r");
  save_errno = errno;
  
  if(stream == NULL)
  {
    infd_filesystem_storage_system_error(errno, error);
    return NULL;
  }

  doc = infd_filesystem_storage_read_xml_stream(
    storage,
    stream,
    full_name,
    toplevel_tag,
    error
  );

  g_free(full_name);
  /* TODO: stream is already closed by xmlReadIO(?). Check, both in
   * success and in error case. */
  return doc;
}

static gboolean
infd_filesystem_storage_write_xml_file(InfdFilesystemStorage* storage,
                                       const char* filename,
                                       xmlDocPtr doc,
                                       GError** error)
{
  InfdFilesystemStoragePrivate* priv;
  gchar* full_name;
  FILE* stream;
  int save_errno;
  xmlErrorPtr xmlerror;

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);

  full_name = g_build_filename(priv->root_directory, filename, NULL);
  stream = fopen(full_name, "w");
  save_errno = errno;
  g_free(full_name);

  if(stream == NULL)
  {
    infd_filesystem_storage_system_error(save_errno, error);
    return FALSE;
  }

  if(xmlDocFormatDump(stream, doc, 1) == -1)
  {
    xmlerror = xmlGetLastError();
    fclose(stream);
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

  if(fclose(stream) != 0)
  {
    infd_filesystem_storage_system_error(save_errno, error);
    return FALSE;
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
#if !defined(G_OS_WIN32) && !defined(__APPLE__)
  int dir_fd;
  DIR* dir;
  struct dirent* dir_entry;
  struct dirent* dir_result;
  struct stat stat_buf;
  int saved_errno;
  enum { F_UNKNOWN, F_DIR, F_REG, F_LNK } filetype;
#else
  GDir* dir;
  const gchar* name;
#endif
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

  list = NULL;

#if !defined(G_OS_WIN32) && !defined(__APPLE__)
  dir_fd = open(full_name, O_NOFOLLOW | O_RDONLY);
  if(dir_fd == -1 || (dir = fdopendir(dir_fd)) == NULL)
  {
    infd_filesystem_storage_system_error(errno, error);
    if(dir_fd != -1) close(dir_fd);
    g_free(full_name);
    return NULL;
  }

  dir_entry = g_malloc(offsetof(struct dirent, d_name) +
                       fpathconf(dir_fd, _PC_NAME_MAX) + 1);

  for(saved_errno = readdir_r(dir, dir_entry, &dir_result);
      saved_errno == 0 && dir_result != NULL;
      saved_errno = readdir_r(dir, dir_entry, &dir_result))
  {
    converted_name = g_filename_to_utf8(dir_result->d_name,
                                        -1,
                                        NULL,
                                        &name_len,
                                        NULL);
    if(converted_name != NULL && strcmp(converted_name, ".") != 0
                              && strcmp(converted_name, "..") != 0)
    {
      filetype = F_UNKNOWN;

#ifdef HAVE_D_TYPE
      if(dir_result->d_type == DT_LNK)
        filetype = F_LNK;
      else if(dir_result->d_type == DT_DIR)
        filetype = F_DIR;
      else if(dir_result->d_type == DT_REG)
        filetype = F_REG;
      else if(dir_result->d_type == DT_UNKNOWN)
#endif
      {
        /* Some filesystems, such as reiserfs, don't support reporting the
         * entry's file type. In that case we do an additional lstat here.
         * Also lstat if d_type is not available on this platform. */
        file_path = g_build_filename(full_name, dir_result->d_name, NULL);
        if(lstat(file_path, &stat_buf) == 0)
        {
          if(S_ISDIR(stat_buf.st_mode))
            filetype = F_DIR;
          else if(S_ISREG(stat_buf.st_mode))
            filetype = F_REG;
          else if(S_ISLNK(stat_buf.st_mode))
            filetype = F_LNK;
        }
        g_free(file_path);
      }

      if(filetype != F_LNK && filetype != F_UNKNOWN)
      {
        if(filetype == F_DIR)
        {
          list = g_slist_prepend(
            list,
            infd_storage_node_new_subdirectory(converted_name)
          );
        }
        else if(filetype == F_REG)
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
      }
    }

    g_free(converted_name);
  }

  g_free(dir_entry);
  if(closedir(dir) == -1)
  {
    infd_filesystem_storage_system_error(errno, error);
    infd_storage_node_list_free(list);
    g_free(full_name);
    return NULL;
  }

  if(saved_errno != 0)
  {
    infd_filesystem_storage_system_error(saved_errno, error);
    infd_storage_node_list_free(list);
    g_free(full_name);
    return NULL;
  }
#else
  dir = g_dir_open(full_name, 0, error);

  if(dir == NULL)
    return NULL;

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
#endif
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
                                            const gchar* identifier,
                                            const gchar* path,
                                            GError** error)
{
  InfdFilesystemStorage* fs_storage;
  InfdFilesystemStoragePrivate* priv;
  gchar* converted_name;
  gchar* disk_name;
  gchar* full_name;
#ifdef G_OS_WIN32
  gchar* sep;
#endif
  gboolean ret;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(fs_storage);

  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return FALSE;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return FALSE;

#ifdef G_OS_WIN32
  /* This is required for SHFileOperation. Maybe we should do it at a central
   * place also for the other file operations... */
  for(sep = converted_name; *sep != '\0'; ++sep)
    if(*sep == '/')
      *sep = '\\';
#endif

  if(identifier != NULL)
  {
    disk_name = g_strconcat(converted_name, ".", identifier, NULL);
    g_free(converted_name);
  }
  else
  {
    disk_name = converted_name;
  }

  full_name = g_build_filename(priv->root_directory, disk_name, NULL);
  g_free(disk_name);

  ret = infd_filesystem_storage_remove_rec(full_name, error);
  g_free(full_name);

  return ret;
}

static InfAclUser**
infd_filesystem_storage_storage_read_user_list(InfdStorage* storage,
                                               guint* n_users,
                                               GError** error)
{
  InfdFilesystemStorage* fs_storage;
  InfdFilesystemStoragePrivate* priv;
  gchar* full_name;
  FILE* stream;
  int save_errno;

  xmlDocPtr doc;
  xmlNodePtr root;
  xmlNodePtr child;
  GPtrArray* array;
  InfAclUser* user;
  guint i;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);
  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(fs_storage);

  full_name = g_build_filename(priv->root_directory, "users.xml", NULL);

  stream = fopen(full_name, "r");
  save_errno = errno;

  if(stream == NULL)
  {
    g_free(full_name);
    if(save_errno == ENOENT)
    {
      /* The ACL file does not exist. This is not an error, but just means
       * the ACL is empty. */
      *n_users = 0;
      return NULL;
    }
    else
    {
      infd_filesystem_storage_system_error(save_errno, error);
      return NULL;
    }
  }

  doc = infd_filesystem_storage_read_xml_stream(
    INFD_FILESYSTEM_STORAGE(storage),
    stream,
    full_name,
    "inf-acl-user-list",
    error
  );

  g_free(full_name);
  if(doc == NULL)
    return NULL;

  array = g_ptr_array_sized_new(128);

  root = xmlDocGetRootElement(doc);
  for(child = root->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;

    if(strcmp((const char*)child->name, "user") == 0)
    {
      user = inf_acl_user_from_xml(child, error);
      if(user == NULL)
      {
        for(i = 0; i < array->len; ++i)
          inf_acl_user_free((InfAclUser*)g_ptr_array_index(array, i));
        g_ptr_array_free(array, TRUE);
        xmlFreeDoc(doc);
        return NULL;
      }

      g_ptr_array_add(array, user);
    }
  }

  xmlFreeDoc(doc);
  *n_users = array->len;
  return (InfAclUser**)g_ptr_array_free(array, FALSE);
}

static gboolean
infd_filesystem_storage_storage_write_user_list(InfdStorage* storage,
                                                const InfAclUser** users,
                                                guint n_users,
                                                GError** error)
{
  InfdFilesystemStorage* fs_storage;
  xmlNodePtr root;
  xmlNodePtr child;
  guint i;
  xmlDocPtr doc;
  gboolean result;

  fs_storage = INFD_FILESYSTEM_STORAGE(storage);

  root = xmlNewNode(NULL, (const xmlChar*)"inf-acl-user-list");
  for(i = 0; i < n_users; ++i)
  {
    child = xmlNewChild(root, NULL, (const xmlChar*)"user", NULL);
    inf_acl_user_to_xml(users[i], child, TRUE);
  }

  doc = xmlNewDoc((const xmlChar*)"1.0");
  xmlDocSetRootElement(doc, root);

  result = infd_filesystem_storage_write_xml_file(
    fs_storage,
    "users.xml",
    doc,
    error
  );

  xmlFreeDoc(doc);
  return result;
}

static GSList*
infd_filesystem_storage_storage_read_acl(InfdStorage* storage,
                                         const gchar* path,
                                         GError** error)
{
  InfdFilesystemStoragePrivate* priv;
  gchar* converted_name;
  gchar* disk_name;
  gchar* full_name;
  FILE* stream;
  int save_errno;
  xmlDocPtr doc;
  xmlNodePtr root;
  xmlNodePtr child;
  GSList* list;
  InfdStorageAcl* acl;
  xmlChar* user_name;

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);
  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return NULL;

  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return NULL;

  if(strcmp(converted_name, "/") != 0)
  {
    disk_name = g_strconcat(converted_name, ".xml.acl", NULL);
    g_free(converted_name);

    full_name = g_build_filename(priv->root_directory, disk_name, NULL);
    g_free(disk_name);
  }
  else
  {
    full_name = g_build_filename(
      priv->root_directory,
      "global-acl.xml",
      NULL
    );

    g_free(converted_name);
  }

  stream = fopen(full_name, "r");
  save_errno = errno;

  if(stream == NULL)
  {
    g_free(full_name);
    if(save_errno == ENOENT)
    {
      /* The ACL file does not exist. This is not an error, but just means
       * the ACL is empty. */
      return NULL;
    }
    else
    {
      infd_filesystem_storage_system_error(save_errno, error);
      return NULL;
    }
  }

  doc = infd_filesystem_storage_read_xml_stream(
    INFD_FILESYSTEM_STORAGE(storage),
    stream,
    full_name,
    "inf-acl",
    error
  );

  /* TODO: stream is already closed by xmlReadIO(?). Check, both in
   * success and in error case. */

  g_free(full_name);

  if(doc == NULL)
    return NULL;

  root = xmlDocGetRootElement(doc);
  list = NULL;

  for(child = root->children; child != NULL; child = child->next)
  {
    if(child->type != XML_ELEMENT_NODE) continue;

    if(strcmp((const char*)child->name, "sheet") == 0)
    {
      user_name = inf_xml_util_get_attribute_required(child, "user", error);
      if(user_name == NULL)
      {
        infd_storage_acl_list_free(list);
        xmlFreeDoc(doc);
        return NULL;
      }

      acl = g_slice_new(InfdStorageAcl);
      acl->user_id = g_strdup((const gchar*)user_name);
      xmlFree(user_name);
      
      if(!inf_acl_sheet_perms_from_xml(child, &acl->mask, &acl->perms, error))
      {
        g_free(acl->user_id);
        g_slice_free(InfdStorageAcl, acl);
        infd_storage_acl_list_free(list);
        xmlFreeDoc(doc);
        return NULL;
      }

      if(acl->mask != 0)
      {
        list = g_slist_prepend(list, acl);
      }
      else
      {
        g_free(acl->user_id);
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
  InfdFilesystemStoragePrivate* priv;
  gchar* converted_name;
  gchar* disk_name;
  gchar* full_name;
  xmlNodePtr root;
  xmlNodePtr child;
  guint i;
  xmlDocPtr doc;
  gboolean result;
  int save_errno;

  priv = INFD_FILESYSTEM_STORAGE_PRIVATE(storage);
  if(infd_filesystem_storage_verify_path(path, error) == FALSE)
    return FALSE;

  /* TODO: A function which gives us the file name, to share in read_acl
   * and write_acl. */
  converted_name = g_filename_from_utf8(path, -1, NULL, NULL, error);
  if(converted_name == NULL)
    return FALSE;

  if(strcmp(converted_name, "/") != 0)
  {
    disk_name = g_strconcat(converted_name, ".xml.acl", NULL);
    g_free(converted_name);
  }
  else
  {
    disk_name = g_strdup("global-acl.xml");
    g_free(converted_name);
  }

  root = NULL;
  if(sheet_set != NULL)
  {
    root = xmlNewNode(NULL, (const xmlChar*)"inf-acl");
    for(i = 0; i < sheet_set->n_sheets; ++i)
    {
      if(sheet_set->sheets[i].mask != 0)
      {
        child = xmlNewChild(root, NULL, (const xmlChar*)"sheet", NULL);

        inf_xml_util_set_attribute(
          child,
          "user",
          sheet_set->sheets[i].user->user_id
        );

        inf_acl_sheet_perms_to_xml(
          sheet_set->sheets[i].mask,
          sheet_set->sheets[i].perms,
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
    full_name = g_build_filename(priv->root_directory, disk_name, NULL);
    if(g_unlink(full_name) == -1)
    {
      save_errno = errno;
      if(save_errno != ENOENT)
      {
        g_free(full_name);
        infd_filesystem_storage_system_error(save_errno, error);
        return FALSE;
      }
    }

    g_free(full_name);
  }
  else
  {
    doc = xmlNewDoc((const xmlChar*)"1.0");
    xmlDocSetRootElement(doc, root);

    result = infd_filesystem_storage_write_xml_file(
      INFD_FILESYSTEM_STORAGE(storage),
      disk_name,
      doc,
      error
    );

    xmlFreeDoc(doc);

    if(result == FALSE)
    {
      g_free(full_name);
      return FALSE;
    }
  }

  g_free(disk_name);
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

  iface->read_user_list =
    infd_filesystem_storage_storage_read_user_list;
  iface->write_user_list =
    infd_filesystem_storage_storage_write_user_list;
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

/**
 * infd_filesystem_storage_open:
 * @storage: A #InfdFilesystemStorage.
 * @identifier: The type of node to open.
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
                             const gchar* identifier,
                             const gchar* path,
                             const gchar* mode,
                             GError** error)
{
  InfdFilesystemStoragePrivate* priv;
  gchar* converted_name;
  gchar* disk_name;
  gchar* full_name;
  FILE* res;
  int save_errno;
#ifndef G_OS_WIN32
  int fd;
  int open_mode;
#endif

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

#ifdef G_OS_WIN32
  res = g_fopen(full_name, mode);
#else
  if(strcmp(mode, "r") == 0) open_mode = O_RDONLY;
  else if(strcmp(mode, "w") == 0) open_mode = O_CREAT | O_WRONLY | O_TRUNC;
  else g_assert_not_reached();
  fd = open(full_name, O_NOFOLLOW | open_mode, 0600);
  if(fd == -1)
    res = NULL;
  else
    res = fdopen(fd, mode);
#endif
  save_errno = errno;
  g_free(full_name);

  if(res == NULL)
  {
    infd_filesystem_storage_system_error(save_errno, error);
    return NULL;
  }

  return res;
}

/* vim:set et sw=2 ts=2: */
