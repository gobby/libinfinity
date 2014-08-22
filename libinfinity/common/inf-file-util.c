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

/**
 * SECTION:inf-file-util
 * @title: File and directory utility functions
 * @short_description: Helper functions to handle directories.
 * @include: libinfinity/common/inf-file-util.h
 * @stability: Unstable
 *
 * These functions are utility functions that can be used when dealing with
 * directories. It allows platform-independent creation, deletion and
 * traversal of directories, with proper error reporting.
 **/

#include <libinfinity/common/inf-file-util.h>

#include <glib/gstdio.h>
#include <string.h>

#include "config.h"

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#endif

#ifdef G_OS_WIN32

static void
inf_file_util_set_error_from_win32(GError** error,
                                   int code)
{
  GFileError gcode;
  gchar* message;

  switch(code)
  {
  case ERROR_FILE_NOT_FOUND: gcode = G_FILE_ERROR_NOENT; break;
  case ERROR_PATH_NOT_FOUND: gcode = G_FILE_ERROR_NOENT; break;
  case ERROR_TOO_MANY_OPEN_FILES: gcode = G_FILE_ERROR_MFILE; break;
  case ERROR_ACCESS_DENIED: gcode = G_FILE_ERROR_ACCES; break;
  case ERROR_ALREADY_EXISTS: gcode = G_FILE_ERROR_EXIST; break;
  case ERROR_OUTOFMEMORY: gcode = G_FILE_ERROR_NOMEM; break;
  case ERROR_INVALID_DRIVE: gcode = G_FILE_ERROR_NXIO; break;
  case ERROR_CURRENT_DIRECTORY: gcode = G_FILE_ERROR_PERM; break; /* EBUSY */
  case ERROR_NOT_SAME_DEVICE: gcode = G_FILE_ERROR_NODEV; break; /*EXDEV */
  case ERROR_NOT_READY: gcode = G_FILE_ERROR_FAILED; break; /* EBUSY */
  case ERROR_NOT_SUPPORTED: gcode = G_FILE_ERROR_NOSYS; break;
  case ERROR_FILE_EXISTS: gcode = G_FILE_ERROR_EXIST; break;
  case ERROR_DISK_FULL: gcode = G_FILE_ERROR_NOSPC; break;
  case ERROR_INVALID_NAME: gcode = G_FILE_ERROR_INVAL; break;
  case ERROR_DIR_NOT_EMPTY: gcode = G_FILE_ERROR_EXIST; break;
  case ERROR_PATH_BUSY: gcode = G_FILE_ERROR_FAILED; break; /* EBUSY */
  case ERROR_BAD_ARGUMENTS: gcode = G_FILE_ERROR_INVAL; break;
  case ERROR_BAD_PATHNAME: gcode = G_FILE_ERROR_INVAL; break;
  case ERROR_BUSY: gcode = G_FILE_ERROR_FAILED; break; /* EBUSY */
  case ERROR_FILENAME_EXCED_RANGE: gcode = G_FILE_ERROR_NAMETOOLONG; break;
  case ERROR_DIRECTORY: gcode = G_FILE_ERROR_ISDIR; break;
  case ERROR_DELETE_PENDING: gcode = G_FILE_ERROR_NOENT; break;
  case ERROR_INVALID_ADDRESS: gcode = G_FILE_ERROR_FAULT; break;
  default: gcode = G_FILE_ERROR_FAILED; break;
  }

  message = g_win32_error_message(code);

  g_set_error(
    error,
    G_FILE_ERROR,
    G_FILE_ERROR_FAILED,
    "%s",
    message
  );

  g_free(message);
}

#else

static void
inf_file_util_set_error_from_errno(GError** error,
                                   int code)
{
  g_set_error(
    error,
    G_FILE_ERROR,
    g_file_error_from_errno(code),
    "%s",
    g_strerror(code)
  );
}

#endif

static gboolean
inf_file_util_delete_directory_list_func(const gchar* name,
                                         const gchar* path,
                                         InfFileType type,
                                         gpointer user_data,
                                         GError** error)
{
  switch(type)
  {
  case INF_FILE_TYPE_UNKNOWN:
  case INF_FILE_TYPE_REG:
  case INF_FILE_TYPE_LNK:
    if(inf_file_util_delete_file(path, error) == FALSE)
      return FALSE;
    return TRUE;
  case INF_FILE_TYPE_DIR:
    if(inf_file_util_delete_directory(path, error) == FALSE)
      return FALSE;
    return TRUE;
  default:
    g_assert_not_reached();
    return FALSE;
  }
}
/**
 * inf_file_util_create_single_directory:
 * @path: The directory to create.
 * @mode: Permissions to use for the newly created directory.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Attempts to create a directory at @path. The @mode parameter is only used
 * on Unix in which case it specifies the permissions to use for all newly
 * created directories in the same way as g_mkdir() would.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_file_util_create_single_directory(const gchar* path,
                                      int mode,
                                      GError** error)
{
#ifdef G_OS_WIN32
  gunichar2* path16;
  int code;

  path16 = g_utf8_to_utf16(path, -1, NULL, NULL, error);
  if(!path16) return FALSE;

  if(CreateDirectoryW(path16, NULL) == FALSE)
  {
    g_free(path16);
    code = GetLastError();
    inf_file_util_set_error_from_win32(error, code);
    return FALSE;
  }

  g_free(path16);
  return TRUE;
#else
  int code;

  if(g_mkdir(path, mode) == -1)
  {
    code = errno;
    inf_file_util_set_error_from_errno(error, code);
    return FALSE;
  }

  return TRUE;
#endif
}

/**
 * inf_file_util_create_directory:
 * @path: The directory to create.
 * @mode: Permissions to use for the newly created directory.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Attempts to create a directory at @path, creating intermediate directories
 * as necessary. The @mode parameter is only used on Unix in which case it
 * specifies the permissions to use for all newly created directories in the
 * same way as g_mkdir() would.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_file_util_create_directory(const gchar* path,
                               int mode,
                               GError** error)
{
  gchar* dirname;
  gboolean result;

  if(g_file_test(path, G_FILE_TEST_IS_DIR))
    return TRUE;

  dirname = g_path_get_dirname(path);

  result = inf_file_util_create_directory(dirname, mode, error);
  g_free(dirname);

  if(result == FALSE)
    return FALSE;

  return inf_file_util_create_single_directory(path, mode, error);
}

/**
 * inf_file_util_list_directory:
 * @path: The directory to explore.
 * @func: Callback function to be called for each child of the directory at
 * @path.
 * @user_data: Additional data to pass to @func.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Calls @func for each file within the given directory. It also passes the
 * type of the found file to the callback function. The callback function can
 * return %FALSE to stop the iteration. If it does this, then this function
 * still returns %TRUE. This can for example be used to find a file in a
 * directory. If, in addition, the callback function sets @error, then this
 * function returns %FALSE and propagates the error.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_file_util_list_directory(const gchar* path,
                             InfFileListFunc func,
                             gpointer user_data,
                             GError** error)
{
#if !defined(G_OS_WIN32) && !defined(__APPLE__)
  int dir_fd;
  DIR* dir;

  struct dirent* dir_entry;
  struct dirent* dir_result;
  struct stat stat_buf;

  int saved_errno;
  InfFileType filetype;
  gchar* file_path;
  gboolean result;
  GError* local_error;

  dir_fd = open(path, O_NOFOLLOW | O_RDONLY);
  if(dir_fd == -1 || (dir = fdopendir(dir_fd)) == NULL)
  {
    inf_file_util_set_error_from_errno(error, errno);
    if(dir_fd != -1) close(dir_fd);
    return FALSE;
  }

  result = TRUE;
  local_error = NULL;
  dir_entry = g_malloc(offsetof(struct dirent, d_name) +
                       fpathconf(dir_fd, _PC_NAME_MAX) + 1);

  for(saved_errno = readdir_r(dir, dir_entry, &dir_result);
      result == TRUE && saved_errno == 0 && dir_result != NULL;
      saved_errno = readdir_r(dir, dir_entry, &dir_result))
  {
    if(strcmp(dir_result->d_name, ".") == 0 ||
       strcmp(dir_result->d_name, "..") == 0)
    {
      continue;
    }

    filetype = INF_FILE_TYPE_UNKNOWN;
    file_path = g_build_filename(path, dir_result->d_name, NULL);

#ifdef HAVE_D_TYPE
    if(dir_result->d_type == DT_LNK)
      filetype = INF_FILE_TYPE_LNK;
    else if(dir_result->d_type == DT_DIR)
      filetype = INF_FILE_TYPE_DIR;
    else if(dir_result->d_type == DT_REG)
      filetype = INF_FILE_TYPE_REG;
    else if(dir_result->d_type == DT_UNKNOWN)
#endif
    {
      /* Some filesystems, such as reiserfs, don't support reporting the
       * entry's file type. In that case we do an additional lstat here.
       * Also lstat if d_type is not available on this platform. */
      if(lstat(file_path, &stat_buf) == 0)
      {
        if(S_ISDIR(stat_buf.st_mode))
          filetype = INF_FILE_TYPE_DIR;
        else if(S_ISREG(stat_buf.st_mode))
          filetype = INF_FILE_TYPE_REG;
        else if(S_ISLNK(stat_buf.st_mode))
          filetype = INF_FILE_TYPE_LNK;
      }
    }

    result = func(
      dir_result->d_name,
      file_path,
      filetype,
      user_data,
      &local_error
    );

    g_free(file_path);
  }

  g_free(dir_entry);
  if(closedir(dir) == -1)
    if(saved_errno != 0)
      saved_errno = errno;

  if(local_error == NULL)
  {
    if(saved_errno != 0)
    {
      inf_file_util_set_error_from_errno(error, saved_errno);
      return FALSE;
    }
  }
  else
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  return TRUE;
#else
  GDir* dir;
  const gchar* name;
  gchar* file_path;
  InfFileType filetype;

  gboolean result;
  GError* local_error;

  dir = g_dir_open(path, 0, error);

  if(dir == NULL)
    return FALSE;

  local_error = NULL;
  result = TRUE;
  for(name = g_dir_read_name(dir);
      name != NULL && result == TRUE;
      name = g_dir_read_name(dir))
  {
    file_path = g_build_filename(path, name, NULL);
    filetype = INF_FILE_TYPE_UNKNOWN;
    if(g_file_test(file_path, G_FILE_TEST_IS_DIR))
      filetype = INF_FILE_TYPE_DIR;
    else if(g_file_test(file_path, G_FILE_TEST_IS_REGULAR))
      filetype = INF_FILE_TYPE_REG;

    result = func(name, file_path, filetype, user_data, &local_error);
    g_free(file_path);
  }

  g_dir_close(dir);

  if(local_error != NULL)
  {
    g_propagate_error(error, local_error);
    return FALSE;
  }

  return TRUE;
#endif
}

/**
 * inf_file_util_delete_file:
 * @path: Path to the file to delete.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Removes the file at @path if it is empty. Fails if @path points to a
 * directory and not a regular file. If the function fails %FALSE is returned
 * and @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_file_util_delete_file(const gchar* path,
                          GError** error)
{
#ifdef G_OS_WIN32
  gunichar2* path16;

  path16 = g_utf8_to_utf16(path, -1, NULL, NULL, error);
  if(!path16) return FALSE;

  if(DeleteFileW(path16) == 0)
  {
    inf_file_util_set_error_from_win32(error, GetLastError());
    g_free(path16);
    return FALSE;
  }

  g_free(path16);
  return TRUE;
#else
  if(g_unlink(path) == -1)
  {
    inf_file_util_set_error_from_errno(error, errno);
    return FALSE;
  }

  return TRUE;
#endif
}

/**
 * inf_file_util_delete_single_directory:
 * @path: Path to the directory to delete.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Removes the directory at @path if it is empty, or fails otherwise. Fails
 * if @path points to a regular file and not a directory. If the function
 * fails %FALSE is returned and @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_file_util_delete_single_directory(const gchar* path,
                                      GError** error)
{
#ifdef G_OS_WIN32
  gunichar2* path16;

  path16 = g_utf8_to_utf16(path, -1, NULL, NULL, error);
  if(!path16) return FALSE;

  if(RemoveDirectoryW(path16) == 0)
  {
    inf_file_util_set_error_from_win32(error, GetLastError());
    g_free(path16);
    return FALSE;
  }

  g_free(path16);
  return TRUE;
#else
  if(g_rmdir(path) == -1)
  {
    inf_file_util_set_error_from_errno(error, errno);
    return FALSE;
  }

  return TRUE;
#endif
}

/**
 * inf_file_util_delete_directory:
 * @path: Path to the directory to delete.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Removes the directory at @path recursively. Fails if @path points to a
 * regular file and not a directory. If the function fails %FALSE is returned
 * and @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_file_util_delete_directory(const gchar* path,
                               GError** error)
{
  gboolean result;

  result = inf_file_util_list_directory(
    path,
    inf_file_util_delete_directory_list_func,
    NULL,
    error
  );

  if(result == FALSE)
    return FALSE;

  return inf_file_util_delete_single_directory(path, error);
}

/**
 * inf_file_util_delete:
 * @path: Path to the object to delete.
 * @error: Location to store error information, if any, or %NULL.
 *
 * Removes the file or directory at @path. If it is a directory the directory
 * is deleted recursively. If the function fails %FALSE is returned
 * and @error is set.
 *
 * Returns: %TRUE on success or %FALSE on error.
 */
gboolean
inf_file_util_delete(const gchar* path,
                     GError** error)
{
  if(g_file_test(path, G_FILE_TEST_IS_DIR))
    return inf_file_util_delete_directory(path, error);
  else
    return inf_file_util_delete_file(path, error);
}

/* vim:set et sw=2 ts=2: */
