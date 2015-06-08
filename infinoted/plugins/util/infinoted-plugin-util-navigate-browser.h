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

#ifndef __INFINOTED_PLUGIN_UTIL_NAVIGATE_BROWSER_H__
#define __INFINOTED_PLUGIN_UTIL_NAVIGATE_BROWSER_H__

#include <libinfinity/common/inf-browser.h>

#include <glib.h>

G_BEGIN_DECLS

typedef struct _InfinotedPluginUtilNavigateData
  InfinotedPluginUtilNavigateData;

typedef enum _InfinotedPluginUtilNavigateError
{
  INFINOTED_PLUGIN_UTIL_NAVIGATE_ERROR_PATH_NOT_ABSOLUTE,
  INFINOTED_PLUGIN_UTIL_NAVIGATE_ERROR_NOT_EXIST
} InfinotedPluginUtilNavigateError;

typedef void(*InfinotedPluginUtilNavigateCallback)(InfBrowser* browser,
                                                   const InfBrowserIter* iter,
                                                   const GError* error,
                                                   gpointer user_data);

GQuark
infinoted_plugin_util_navigate_error_quark(void);

InfinotedPluginUtilNavigateData*
infinoted_plugin_util_navigate_to(InfBrowser* browser,
                                  const gchar* path,
                                  gsize len,
                                  gboolean explore_last,
                                  InfinotedPluginUtilNavigateCallback cb,
                                  gpointer user_data);

void
infinoted_plugin_util_navigate_cancel(InfinotedPluginUtilNavigateData* data);

G_END_DECLS

#endif /* __INFINOTED_PLUGIN_UTIL_UNIX_NAVIGATE_BROWSER_H__ */

/* vim:set et sw=2 ts=2: */
