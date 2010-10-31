/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007-2010 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/common/inf-init.h>
#include <libinfinity/inf-i18n.h>

#include <unistd.h> /* Get ssize_t on MSVC, required by gnutls.h */
#include <gnutls/gnutls.h>
#include <glib-object.h>

#ifdef G_OS_WIN32
# include <winsock2.h>
#endif

static guint inf_init_counter = 0;

extern gboolean INF_XMPP_CONNECTION_PRINT_TRAFFIC;

/**
 * inf_init:
 * @error: Location to store error information, if any.
 *
 * This function initializes the libinfinity library and should be called
 * before any other functions of the library. The function does nothing if
 * it has already been called before.
 *
 * Returns: Whether the initialization was successful or not.
 */
gboolean
inf_init(GError** error)
{
#ifdef G_OS_WIN32
  WSADATA data;
  int result;
  gchar* error_message;
#endif

  if(!g_thread_supported())
    g_thread_init(NULL);

  if(inf_init_counter == 0)
  {
#ifdef G_OS_WIN32
    result = WSAStartup(MAKEWORD(2, 2), &data);
    if(result != 0)
    {
      error_message = g_win32_error_message(result);
      g_set_error(
        error,
        g_quark_from_static_string("INF_INIT_ERROR"),
        0,
        "%s",
        error_message
      );

      g_free(error_message);

      return FALSE;
    }
#endif
    g_type_init();
    gnutls_global_init();
    _inf_gettext_init();
  }

  /* Initialize traffic debug */
  if(g_getenv("LIBINFINITY_DEBUG_PRINT_TRAFFIC"))
    INF_XMPP_CONNECTION_PRINT_TRAFFIC = TRUE;
  else
    INF_XMPP_CONNECTION_PRINT_TRAFFIC = FALSE;

  ++inf_init_counter;
  return TRUE;
}

/**
 * inf_deinit:
 *
 * This functions deinitializes the libinfinity library. Make sure that all
 * objects the library provides have been freed before calling this function.
 * If inf_init() has been called multiple times, then inf_deinit() needs to be
 * called the same number of times to actually deinitialize the library.
 */
void
inf_deinit(void)
{
  if(--inf_init_counter == 0)
  {
    gnutls_global_deinit();
#ifdef G_OS_WIN32
    WSACleanup();
#endif
  }
}

/* vim:set et sw=2 ts=2: */
