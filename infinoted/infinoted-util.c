/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <infinoted/infinoted-util.h>

#include <libinfinity/inf-i18n.h>

#include <glib.h>
#include <string.h>
#include <errno.h>

/**
 * infinoted_util_create_dirname:
 * @path: The filename to create a path to.
 * @error: Location to store error information, if any.
 *
 * Creates directories leading to the given path. Does not create a directory
 * for the last component of the path, assuming that it is a filename that
 * you are going to write into that directory later.
 *
 * Returns: %TRUE on success, or %FALSE on error in which case @error is set.
 */
gboolean
infinoted_util_create_dirname(const gchar* path,
                              GError** error)
{
  gchar* dirname;
  int save_errno;

  dirname = g_path_get_dirname(path);

  if(g_mkdir_with_parents(dirname, 0700) != 0)
  {
    save_errno = errno;

    g_set_error(
      error,
      g_quark_from_static_string("ERRNO_ERROR"),
      save_errno,
      _("Could not create directory \"%s\": %s"),
      dirname,
      strerror(save_errno)
    );
    
    g_free(dirname);
    return FALSE;
  }

  g_free(dirname);
  return TRUE;
}

/* vim:set et sw=2 ts=2: */
