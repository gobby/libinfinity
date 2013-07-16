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

#include <libinfinity/inf-i18n.h>
#include <libinfinity/inf-dll.h>

#include <glib.h>

#include "config.h"

static gchar*
_inf_gettext_get_locale_dir(void)
{
#ifdef G_OS_WIN32
  gchar* root;
  gchar* temp;
  gchar* result;

  root =
    g_win32_get_package_installation_directory_of_module(_inf_dll_handle);
  temp = g_build_filename(root, "share", "locale", NULL);
  g_free(root);

  result = g_win32_locale_filename_from_utf8(temp);
  g_free(temp);
  return result;
#else
  return g_strdup(INF_LOCALEDIR);
#endif
}

void
_inf_gettext_init(void)
{
  gchar* localedir;
  localedir = _inf_gettext_get_locale_dir();
  bindtextdomain(GETTEXT_PACKAGE, localedir);
  g_free(localedir);

  bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
}

const char*
_inf_gettext(const char* msgid)
{
  return dgettext(GETTEXT_PACKAGE, msgid);
}

/* vim:set et sw=2 ts=2: */
