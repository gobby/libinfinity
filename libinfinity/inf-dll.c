/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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

/* This code saves the DLL Handle of the libinfinity DLL, for use
 * with g_win32_get_package_installation_directory_of_module */

#include <glib.h>

#ifdef G_OS_WIN32

#include <windows.h>

HMODULE _inf_dll_handle = NULL;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
{
  switch(fdwReason)
  {
  case DLL_PROCESS_ATTACH:
    _inf_dll_handle = (HMODULE)hinstDLL;
    break;
  }

  return TRUE;
}

#endif /* G_OS_WIN32 */

/* vim:set et sw=2 ts=2: */
